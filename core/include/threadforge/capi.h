/* Stable C ABI over tf::Engine, for C# (P/Invoke), MFC and any other host. All strings are UTF-8. */
#ifndef THREADFORGE_CAPI_H
#define THREADFORGE_CAPI_H

#include <stdint.h>

#if defined(_WIN32)
#  ifdef TF_BUILD_DLL
#    define TF_API __declspec(dllexport)
#  else
#    define TF_API __declspec(dllimport)
#  endif
#else
#  define TF_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tf_handle_s* tf_handle;

typedef struct tf_options {
    const char* root;
    const char* pattern;
    const char* extensions;      /* comma separated, e.g. "cpp,h,cs"; NULL or "" = all */
    uint64_t    max_matches;     /* 0 = unlimited */
    uint32_t    threads;         /* 0 = auto */
    int32_t     use_regex;
    int32_t     case_sensitive;
    int32_t     count_words;
} tf_options;

typedef struct tf_stats {
    uint64_t files_scanned;
    uint64_t files_skipped;
    uint64_t bytes_read;
    uint64_t matches;
    uint64_t elapsed_ms;
    int32_t  running;
} tf_stats;

/* Invoked serially from engine worker threads. Pointers are valid only during the call. */
typedef void (*tf_match_cb)(const char* file, uint64_t line, const char* text, void* user);

TF_API tf_handle   tf_create(void);
TF_API void        tf_destroy(tf_handle h);
/* Blocking. Returns 0 on success (also when cancelled), non-zero on error (see tf_last_error). */
TF_API int32_t     tf_run(tf_handle h, const tf_options* opts, tf_match_cb cb, void* user);
TF_API void        tf_cancel(tf_handle h);                       /* thread-safe */
TF_API void        tf_get_stats(tf_handle h, tf_stats* out);     /* thread-safe */
TF_API const char* tf_last_error(tf_handle h);

#ifdef __cplusplus
}
#endif
#endif
