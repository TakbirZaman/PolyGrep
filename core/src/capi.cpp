#include "threadforge/capi.h"

#include <new>
#include <sstream>
#include <string>

#include "threadforge/engine.hpp"

struct tf_handle_s {
    tf::Engine engine;
    std::string error;
};

extern "C" {

tf_handle tf_create(void) { return new (std::nothrow) tf_handle_s(); }
void tf_destroy(tf_handle h) { delete h; }

int32_t tf_run(tf_handle h, const tf_options* o, tf_match_cb cb, void* user) {
    if (!h || !o) return 1;
    try {
        h->error.clear();
        tf::Options opt;
        opt.root = o->root ? o->root : "";
        opt.pattern = o->pattern ? o->pattern : "";
        opt.use_regex = o->use_regex != 0;
        opt.case_sensitive = o->case_sensitive != 0;
        opt.count_words = o->count_words != 0;
        opt.threads = o->threads;
        opt.max_matches = o->max_matches;
        if (o->extensions) {
            std::stringstream ss(o->extensions);
            for (std::string e; std::getline(ss, e, ',');) opt.extensions.push_back(e);
        }
        h->engine.run(opt, [cb, user](const std::string& f, std::uint64_t line, const std::string& text) {
            if (cb) cb(f.c_str(), line, text.c_str(), user);
        });
        return 0;
    } catch (const std::exception& e) {
        h->error = e.what();
    } catch (...) {
        h->error = "unknown error";
    }
    return 1;
}

void tf_cancel(tf_handle h) {
    if (h) h->engine.cancel();
}

void tf_get_stats(tf_handle h, tf_stats* out) {
    if (!h || !out) return;
    const auto s = h->engine.stats();
    out->files_scanned = s.files_scanned;
    out->files_skipped = s.files_skipped;
    out->bytes_read = s.bytes_read;
    out->matches = s.matches;
    out->elapsed_ms = s.elapsed_ms;
    out->running = s.running ? 1 : 0;
}

const char* tf_last_error(tf_handle h) { return h ? h->error.c_str() : ""; }

}  // extern "C"
