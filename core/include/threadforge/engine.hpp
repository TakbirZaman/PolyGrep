#pragma once
// PolyGrep engine: parallel recursive text search + word-frequency index.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tf {

struct Options {
    std::string root;                    // directory (or single file), UTF-8
    std::string pattern;                 // literal text or ECMAScript regex, UTF-8
    std::vector<std::string> extensions; // e.g. {"cpp","h"}; empty = every file
    bool use_regex = false;
    bool case_sensitive = false;
    bool count_words = false;            // build word-frequency table while scanning
    unsigned threads = 0;                // 0 = hardware_concurrency
    std::uint64_t max_matches = 0;       // 0 = unlimited; stops the scan when reached
};

struct Stats {
    std::uint64_t files_scanned = 0;
    std::uint64_t files_skipped = 0;     // binary / unreadable
    std::uint64_t bytes_read = 0;
    std::uint64_t matches = 0;
    std::uint64_t elapsed_ms = 0;
    bool running = false;
};

// Called for every match. The engine serialises calls (never concurrent), from worker threads.
using MatchCallback = std::function<void(const std::string& file, std::uint64_t line, const std::string& text)>;

class Engine {
public:
    // Blocking. Throws std::invalid_argument / std::runtime_error / std::regex_error on bad input.
    void run(const Options& options, MatchCallback on_match);

    // Thread-safe; may be called from any thread while run() is active.
    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    // Thread-safe live snapshot.
    Stats stats() const;

    // Most frequent words of the last run (requires Options::count_words).
    std::vector<std::pair<std::string, std::uint64_t>> top_words(std::size_t n) const;

private:
    void finish() noexcept;

    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> files_scanned_{0}, files_skipped_{0}, bytes_read_{0}, matches_{0};
    std::atomic<std::int64_t> start_ms_{0}, final_elapsed_ms_{0};

    mutable std::mutex words_m_;
    std::unordered_map<std::string, std::uint64_t> words_;
};

}  // namespace tf
