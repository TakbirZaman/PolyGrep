// Dependency-free test runner (no gtest needed).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "threadforge/capi.h"
#include "threadforge/engine.hpp"
#include "threadforge/thread_pool.hpp"

namespace fs = std::filesystem;
static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; } } while (0)

struct TempTree {
    fs::path root;
    TempTree() {
        root = fs::temp_directory_path() / ("tf_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "sub" / "deep");
    }
    ~TempTree() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& rel, const std::string& content) {
        std::ofstream(root / rel, std::ios::binary) << content;
    }
};

using Hits = std::vector<std::string>;
static Hits run_hits(tf::Engine& e, const tf::Options& o) {
    Hits h;
    e.run(o, [&](const std::string& f, std::uint64_t l, const std::string& t) {
        h.push_back(fs::path(f).filename().string() + ":" + std::to_string(l) + ":" + t);
    });
    std::sort(h.begin(), h.end());
    return h;
}

static void test_thread_pool() {
    tf::ThreadPool pool(4, 8);  // bounded queue exercises back-pressure
    std::atomic<int> sum{0};
    std::vector<std::future<int>> futs;
    for (int i = 1; i <= 1000; ++i) futs.push_back(pool.submit([i, &sum] { sum += i; return i * 2; }));
    long total = 0;
    for (auto& f : futs) total += f.get();
    pool.wait_idle();
    CHECK(sum == 500500);
    CHECK(total == 1001000);
    auto bad = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    bool threw = false;
    try { bad.get(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);  // exceptions propagate through the future
}

static void test_engine() {
    TempTree t;
    int expected = 0;
    for (int f = 0; f < 40; ++f) {
        std::string body;
        for (int l = 0; l < 100; ++l) {
            if (l % 10 == 0) { body += (l % 20 == 0 ? "found the Needle here\n" : "another needle line\r\n"); ++expected; }
            else body += "plain filler text number " + std::to_string(l) + "\n";
        }
        t.write((f % 2 ? fs::path("sub") : fs::path("sub") / "deep") / ("f" + std::to_string(f) + (f % 4 == 0 ? ".txt" : ".cpp")), body);
    }
    t.write("bin.dat", std::string("needle\0needle", 13));

    tf::Options o;
    o.root = t.root.string();
    o.pattern = "needle";

    tf::Engine e;
    o.threads = 1;
    auto single = run_hits(e, o);
    CHECK(static_cast<int>(single.size()) == expected);
    CHECK(e.stats().files_skipped == 1);  // binary file

    o.threads = 8;
    auto multi = run_hits(e, o);
    CHECK(single == multi);  // parallel result == sequential result

    o.case_sensitive = true;
    CHECK(static_cast<int>(run_hits(e, o).size()) == expected / 2);  // only lower-case "needle" lines
    o.case_sensitive = false;

    o.use_regex = true;
    o.pattern = "^found .* here$";
    CHECK(static_cast<int>(run_hits(e, o).size()) == expected / 2);

    bool threw = false;
    o.pattern = "([unclosed";
    try { run_hits(e, o); } catch (const std::regex_error&) { threw = true; }
    CHECK(threw);
    o.use_regex = false;

    o.pattern = "needle";
    o.extensions = {".TXT"};  // normalised: case-insensitive, dot optional
    auto txt = run_hits(e, o);
    CHECK(txt.size() == 100u);  // 10 .txt files x 10 matches
    o.extensions.clear();

    o.max_matches = 5;
    std::size_t cb = 0;
    e.run(o, [&](auto&&, auto, auto&&) { ++cb; });
    CHECK(cb == 5 && e.stats().matches == 5);
    o.max_matches = 0;

    o.count_words = true;
    o.pattern = "filler";
    e.run(o, [](auto&&, auto, auto&&) {});
    auto top = e.top_words(3);
    CHECK(!top.empty());
    bool found_filler = false;
    for (auto& [w, c] : e.top_words(5)) found_filler |= (w == "filler" && c == 40u * 90);
    CHECK(found_filler);
}

static void test_cancel() {
    TempTree t;
    for (int i = 0; i < 300; ++i) t.write("sub/f" + std::to_string(i) + ".txt", std::string(200000, 'a') + "\nneedle\n");
    tf::Engine e;
    tf::Options o; o.root = t.root.string(); o.pattern = "needle"; o.threads = 2;
    std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); e.cancel(); });
    e.run(o, [](auto&&, auto, auto&&) {});
    canceller.join();
    CHECK(!e.stats().running);
    CHECK(e.stats().files_scanned < 300);  // stopped early
}

static void test_capi() {
    TempTree t;
    t.write("a.txt", "hello world\nsecond line\n");
    tf_handle h = tf_create();
    tf_options o{};
    const std::string root = t.root.string();  // must outlive tf_run
    o.root = root.c_str();
    o.pattern = "WORLD";
    o.extensions = "txt,md";
    int hits = 0;
    int rc = tf_run(h, &o, [](const char*, uint64_t line, const char* text, void* u) {
        ++*static_cast<int*>(u);
        CHECK(line == 1 && std::string(text) == "hello world");
    }, &hits);
    CHECK(rc == 0 && hits == 1);
    tf_stats s{};
    tf_get_stats(h, &s);
    CHECK(s.matches == 1 && s.files_scanned == 1 && !s.running);
    o.root = "/definitely/not/here";
    CHECK(tf_run(h, &o, nullptr, nullptr) != 0);
    CHECK(std::string(tf_last_error(h)).find("does not exist") != std::string::npos);
    tf_destroy(h);
}

int main() {
    test_thread_pool();
    test_engine();
    test_cancel();
    test_capi();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
