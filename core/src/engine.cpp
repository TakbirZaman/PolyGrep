#include "threadforge/engine.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>

#include "threadforge/thread_pool.hpp"

namespace fs = std::filesystem;

namespace tf {
namespace {

using Clock = std::chrono::steady_clock;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string utf8(const fs::path& p) {
    auto s = p.u8string();  // std::string (C++17) or std::u8string (C++20)
    return std::string(s.begin(), s.end());
}

using Matcher = std::function<bool(const std::string&)>;

// Boyer-Moore-Horspool needs the needle to outlive the searcher -> keep them together.
struct LiteralMatcher {
    using Searcher = std::boyer_moore_horspool_searcher<std::string::const_iterator>;
    LiteralMatcher(std::string n, bool cs) : needle(std::move(n)), case_sensitive(cs), searcher(needle.begin(), needle.end()) {}
    bool operator()(const std::string& line) const {
        if (case_sensitive) return std::search(line.begin(), line.end(), searcher) != line.end();
        const std::string l = to_lower(line);
        return std::search(l.begin(), l.end(), searcher) != l.end();
    }
    std::string needle;
    bool case_sensitive;
    Searcher searcher;
};

Matcher make_matcher(const Options& o) {
    if (o.pattern.empty()) throw std::invalid_argument("pattern is empty");
    if (o.use_regex) {
        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (!o.case_sensitive) flags |= std::regex::icase;
        auto re = std::make_shared<const std::regex>(o.pattern, flags);  // throws std::regex_error
        return [re](const std::string& line) { return std::regex_search(line, *re); };
    }
    auto m = std::make_shared<LiteralMatcher>(o.case_sensitive ? o.pattern : to_lower(o.pattern), o.case_sensitive);
    return [m](const std::string& line) { return (*m)(line); };
}

std::vector<std::string> normalise_exts(const std::vector<std::string>& in) {
    std::vector<std::string> out;
    for (auto e : in) {
        e = to_lower(e);
        while (!e.empty() && (e.front() == '.' || e.front() == '*')) e.erase(e.begin());
        if (!e.empty()) out.push_back(std::move(e));
    }
    return out;
}

bool ext_allowed(const fs::path& p, const std::vector<std::string>& exts) {
    if (exts.empty()) return true;
    std::string e = to_lower(utf8(p.extension()));
    if (!e.empty() && e.front() == '.') e.erase(e.begin());
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

bool looks_binary(std::ifstream& f) {
    char buf[512];
    f.read(buf, sizeof buf);
    const auto n = static_cast<std::size_t>(f.gcount());
    f.clear();
    f.seekg(0);
    return std::memchr(buf, '\0', n) != nullptr;
}

void tokenize(const std::string& line, std::unordered_map<std::string, std::uint64_t>& out) {
    std::string w;
    auto flush = [&] {
        if (w.size() >= 3) ++out[w];
        w.clear();
    };
    for (unsigned char c : line) {
        if (std::isalnum(c) || c == '_') w.push_back(static_cast<char>(std::tolower(c)));
        else flush();
    }
    flush();
}

constexpr std::size_t kMaxLineText = 500;

}  // namespace

void Engine::finish() noexcept {
    final_elapsed_ms_.store(now_ms() - start_ms_.load());
    running_.store(false);
}

Stats Engine::stats() const {
    Stats s;
    s.running = running_.load();
    s.files_scanned = files_scanned_.load();
    s.files_skipped = files_skipped_.load();
    s.bytes_read = bytes_read_.load();
    s.matches = matches_.load();
    s.elapsed_ms = static_cast<std::uint64_t>(s.running ? now_ms() - start_ms_.load() : final_elapsed_ms_.load());
    return s;
}

std::vector<std::pair<std::string, std::uint64_t>> Engine::top_words(std::size_t n) const {
    std::vector<std::pair<std::string, std::uint64_t>> v;
    {
        std::lock_guard<std::mutex> lk(words_m_);
        v.assign(words_.begin(), words_.end());
    }
    n = std::min(n, v.size());
    std::partial_sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n), v.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    v.resize(n);
    return v;
}

void Engine::run(const Options& opt, MatchCallback on_match) {
    if (running_.exchange(true)) throw std::runtime_error("engine is already running");
    struct Finish {
        Engine& e;
        ~Finish() { e.finish(); }
    } finish_guard{*this};

    start_ms_.store(now_ms());
    cancel_.store(false);
    files_scanned_ = files_skipped_ = bytes_read_ = matches_ = 0;
    {
        std::lock_guard<std::mutex> lk(words_m_);
        words_.clear();
    }

    const fs::path root = fs::u8path(opt.root);
    std::error_code ec;
    if (!fs::exists(root, ec)) throw std::runtime_error("path does not exist: " + opt.root);

    const Matcher matcher = make_matcher(opt);
    const std::vector<std::string> exts = normalise_exts(opt.extensions);
    const unsigned n_threads = opt.threads ? opt.threads : std::max(1u, std::thread::hardware_concurrency());
    const bool count_words = opt.count_words;

    std::mutex emit_m;  // serialises callback + match counting

    auto scan_file = [&](const fs::path& path) {
        try {
            std::ifstream f(path, std::ios::binary);
            if (!f || looks_binary(f)) {
                ++files_skipped_;
                return;
            }
            const std::string name = utf8(path);
            std::unordered_map<std::string, std::uint64_t> local_words;
            std::string line;
            std::uint64_t line_no = 0, bytes = 0;
            while (!cancel_.load(std::memory_order_relaxed) && std::getline(f, line)) {
                ++line_no;
                bytes += line.size() + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (matcher(line)) {
                    std::lock_guard<std::mutex> lk(emit_m);
                    if (opt.max_matches && matches_.load() >= opt.max_matches) {
                        cancel_.store(true);
                        break;
                    }
                    ++matches_;
                    if (on_match) on_match(name, line_no, line.size() > kMaxLineText ? line.substr(0, kMaxLineText) + "..." : line);
                }
                if (count_words) tokenize(line, local_words);
            }
            bytes_read_ += bytes;
            ++files_scanned_;
            if (count_words && !local_words.empty()) {
                std::lock_guard<std::mutex> lk(words_m_);
                for (auto& [w, c] : local_words) words_[w] += c;
            }
        } catch (...) {
            ++files_skipped_;
        }
    };

    {
        ThreadPool pool(n_threads, static_cast<std::size_t>(n_threads) * 32);  // declared last -> joined first
        if (fs::is_regular_file(root, ec)) {
            pool.submit([&, root] { scan_file(root); });
        } else {
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end && !cancel_.load(std::memory_order_relaxed)) {
                std::error_code ec2;
                if (it->is_regular_file(ec2) && ext_allowed(it->path(), exts)) {
                    pool.submit([&, p = it->path()] { scan_file(p); });
                }
                it.increment(ec);
            }
        }
        pool.wait_idle();
    }
}

}  // namespace tf
