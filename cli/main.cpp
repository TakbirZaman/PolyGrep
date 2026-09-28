// tf_cli <root> <pattern> [--regex] [--case] [--ext cpp,h] [--threads N] [--max N] [--words N]
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "threadforge/engine.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: tf_cli <root> <pattern> [--regex] [--case] [--ext a,b] [--threads N] [--max N] [--words N]\n";
        return 2;
    }
    tf::Options o;
    o.root = argv[1];
    o.pattern = argv[2];
    std::size_t words = 0;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--regex") o.use_regex = true;
        else if (a == "--case") o.case_sensitive = true;
        else if (a == "--ext") {
            std::stringstream ss(next());
            for (std::string e; std::getline(ss, e, ',');) o.extensions.push_back(e);
        } else if (a == "--threads") o.threads = static_cast<unsigned>(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--max") o.max_matches = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--words") { words = std::strtoull(next().c_str(), nullptr, 10); o.count_words = words > 0; }
    }

    tf::Engine engine;
    try {
        engine.run(o, [](const std::string& f, std::uint64_t line, const std::string& t) {
            std::cout << f << ':' << line << ": " << t << '\n';
        });
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    const auto s = engine.stats();
    std::cerr << "\n" << s.matches << " matches | " << s.files_scanned << " files (" << s.files_skipped << " skipped) | "
              << s.bytes_read / 1024 << " KiB | " << s.elapsed_ms << " ms\n";
    if (words) {
        std::cerr << "\ntop words:\n";
        for (auto& [w, c] : engine.top_words(words)) std::cerr << "  " << c << "\t" << w << '\n';
    }
    return 0;
}
