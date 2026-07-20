#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdalign.h>

#include "string_view.h"
#include "match.h"
#include "replace.h"
#include "pipeline.h"
#include "aggregate.h"
#include "simd.h"

#define BUFFER_SIZE (128 * 1024)
#define OUT_BUF_SIZE (1024 * 1024)
#define OUT_BUF_ALIGN 32  /* 32-byte alignment for AVX2 */

static void flush_out(char **out_ptr, char *out_buf) {
    size_t n = *out_ptr - out_buf;
    if (n > 0) {
        fwrite(out_buf, 1, n, stdout);
        *out_ptr = out_buf;
    }
}

static void destroy_match_context(void *context) {
    match_ctx_free(context);
}

static void destroy_replace_context(void *context) {
    replace_ctx_free(context);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s --match <regex> [--format <layout>] [--replace <from> <to> [--first|--max-version] ...] [--max-version] [--first]\n", argv[0]);
        return 1;
    }

    Pipeline pipeline;
    pipeline_init(&pipeline);

    const char *pattern = NULL;
    const char *format_str = NULL;
    bool process_max_version = false;
    bool process_first = false;

    // Track replace patterns for aggregator modes
    struct {
        const char *target;
        const char *replacement;
        bool for_first;
        bool for_max_version;
    } replace_patterns[32];
    int replace_count = 0;

    // First pass: collect global flags (--first, --max-version) that may appear anywhere
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--first") == 0) {
            process_first = true;
        } else if (strcmp(argv[i], "--max-version") == 0) {
            process_max_version = true;
        }
    }

    // Second pass: parse all arguments including --replace with optional per-replace flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--match") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "rx: --match requires a regex\n");
                return EXIT_FAILURE;
            }
            pattern = argv[++i];
        } else if (strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "rx: --format requires a layout\n");
                return EXIT_FAILURE;
            }
            format_str = argv[++i];
        } else if (strcmp(argv[i], "--replace") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "rx: --replace requires <from> <to>\n");
                return EXIT_FAILURE;
            }
            const char *target = argv[++i];
            const char *replacement = argv[++i];
            
            // Check for optional --first or --max-version flags after the replacement
            bool for_first = false;
            bool for_max_version = false;
            if (i + 1 < argc && strcmp(argv[i + 1], "--first") == 0) {
                for_first = true;
                i++;
            } else if (i + 1 < argc && strcmp(argv[i + 1], "--max-version") == 0) {
                for_max_version = true;
                i++;
            }
            
            if (replace_count < 32) {
                replace_patterns[replace_count].target = target;
                replace_patterns[replace_count].replacement = replacement;
                replace_patterns[replace_count].for_first = for_first;
                replace_patterns[replace_count].for_max_version = for_max_version;
                replace_count++;
            }
        } else if (strcmp(argv[i], "--max-version") == 0 || strcmp(argv[i], "--first") == 0) {
            // Already handled in first pass, skip
        } else {
            fprintf(stderr, "rx: unknown option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (!pattern) {
        fprintf(stderr, "Error: Missing required parameter --match <regex>\n");
        return 1;
    }

    /* ── Detect literal-only pattern ─────────────────────────────── */
    bool is_literal = pattern[0] != '\0';
    for (const unsigned char *p = (const unsigned char *)pattern; *p != '\0'; ++p) {
        switch (*p) {
            case '.':
            case '^':
            case '$':
            case '*':
            case '+':
            case '?':
            case '(':
            case ')':
            case '[':
            case '{':
            case '|':
                is_literal = false;
                break;
            default:
                break;
        }
        if (!is_literal) break;
    }

    /* ── Check if we need pipeline or aggregation ────────────────── */
    bool need_pipeline = (format_str != NULL) || process_max_version || process_first;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replace") == 0) {
            need_pipeline = true;
            break;
        }
    }

    /* ── Ultra-fast path: simple literal match, no pipeline ──────── */
    if (is_literal && !need_pipeline) {
        const char *lit_pat = pattern;
        size_t lit_len = strlen(lit_pat);

        /* Try mmap first */
        struct stat st;
        if (fstat(STDIN_FILENO, &st) == 0 && S_ISREG(st.st_mode)) {
            char *data = mmap(NULL, (size_t)st.st_size, PROT_READ,
                              MAP_PRIVATE, STDIN_FILENO, 0);
            if (data != MAP_FAILED) {
                char *ptr = data;
                char *end = data + (size_t)st.st_size;
                alignas(OUT_BUF_ALIGN) char out_buf[OUT_BUF_SIZE];
                char *out_ptr = out_buf;

                while (ptr < end) {
                    __builtin_prefetch(ptr + 64, 0, 1);
                    ptr = (char*)simd_memmem(ptr, end - ptr, lit_pat, lit_len);
                    if (!ptr) break;
                    if (end - ptr >= (ptrdiff_t)lit_len &&
                        memcmp(ptr, lit_pat, lit_len) == 0) {
                        if (out_ptr - out_buf + lit_len + 1 > OUT_BUF_SIZE) {
                            flush_out(&out_ptr, out_buf);
                        }
                        memcpy(out_ptr, ptr, lit_len);
                        out_ptr += lit_len;
                        *out_ptr++ = '\n';
                        ptr += lit_len;
                    } else {
                        ptr++;
                    }
                }
                flush_out(&out_ptr, out_buf);
                munmap(data, (size_t)st.st_size);
                return 0;
            }
        }

        /* Fallback read() path */
        char *buf = malloc(BUFFER_SIZE);
        if (!buf) return 1;

        size_t bytes_left = 0;
        ssize_t bytes_read;
        while ((bytes_read = read(STDIN_FILENO, buf + bytes_left, BUFFER_SIZE - bytes_left)) > 0) {
            size_t total = bytes_left + (size_t)bytes_read;
            char *ptr = buf;
            char *end = buf + total;
            alignas(OUT_BUF_ALIGN) char out_buf[OUT_BUF_SIZE];
            char *out_ptr = out_buf;

            while (ptr < end) {
                __builtin_prefetch(ptr + 64, 0, 1);
                ptr = (char*)simd_memmem(ptr, end - ptr, lit_pat, lit_len);
                if (!ptr) {
                    ptr = end;
                    break;
                }
                if (end - ptr >= (ptrdiff_t)lit_len &&
                    memcmp(ptr, lit_pat, lit_len) == 0) {
                    if (out_ptr - out_buf + lit_len + 1 > OUT_BUF_SIZE) {
                        flush_out(&out_ptr, out_buf);
                    }
                    memcpy(out_ptr, ptr, lit_len);
                    out_ptr += lit_len;
                    *out_ptr++ = '\n';
                    ptr += lit_len;
                } else {
                    ptr++;
                }
            }
            flush_out(&out_ptr, out_buf);

            /* Carry forward unprocessed data for the next read.
               memchr search is not line-oriented, but we still need
               to handle patterns split across buffer boundaries. */
            bytes_left = (size_t)(end - ptr);
            if (bytes_left > 0) {
                memmove(buf, ptr, bytes_left);
            }
        }
        free(buf);
        if (bytes_read < 0) {
            perror("rx: read");
            return 1;
        }
        return 0;
    }

    /* ── Pipeline path ────────────────────────────────────────────── */
    Aggregator aggregator;
    AggMode mode = AGG_NONE;
    if (process_max_version) mode = AGG_MAX_VERSION;
    else if (process_first) mode = AGG_FIRST;
    agg_init(&aggregator, mode);

    MatchStageCtx *match_ctx = match_ctx_create(pattern, format_str, &pipeline, &aggregator);
    if (!match_ctx) {
        fprintf(stderr, "Error: Failed compiling regex pattern.\n");
        pipeline_free(&pipeline);
        return 1;
    }
    if (!pipeline_add(&pipeline, stage_match, match_ctx, destroy_match_context)) {
        fprintf(stderr, "rx: failed to allocate pipeline stage\n");
        match_ctx_free(match_ctx);
        pipeline_free(&pipeline);
        return EXIT_FAILURE;
    }
    
    // Add replace patterns to pipeline based on aggregator mode
    for (int i = 0; i < replace_count; i++) {
        bool should_add_to_pipeline = false;
        bool should_add_to_aggregator = false;
        
        if (mode == AGG_FIRST && replace_patterns[i].for_first) {
            should_add_to_aggregator = true;
        } else if (mode == AGG_MAX_VERSION && replace_patterns[i].for_max_version) {
            should_add_to_aggregator = true;
        } else {
            // Default: replace all matches (pipeline) when no specific aggregator flag after --replace
            should_add_to_pipeline = true;
        }
        
        if (should_add_to_pipeline) {
            ReplaceStageCtx *replace_ctx = replace_ctx_create(replace_patterns[i].target, replace_patterns[i].replacement);
            if (!replace_ctx || !pipeline_add(&pipeline, stage_replace, replace_ctx, destroy_replace_context)) {
                fprintf(stderr, "rx: invalid replacement or failed to allocate pipeline stage\n");
                replace_ctx_free(replace_ctx);
                pipeline_free(&pipeline);
                return EXIT_FAILURE;
            }
        } else if (should_add_to_aggregator) {
            if (!agg_add_replace(&aggregator, replace_patterns[i].target, replace_patterns[i].replacement)) {
                fprintf(stderr, "rx: invalid replacement or failed to allocate aggregator replacement\n");
                pipeline_free(&pipeline);
                return EXIT_FAILURE;
            }
        }
    }

    const char *lit_pat = NULL;
    size_t lit_len = 0;
    bool use_fast_path = false;
    bool simple_literal = false;
    bool max_version_literal = false;
    if (pipeline.count > 0 && pipeline.stages[0].run == stage_match) {
        use_fast_path = match_ctx_is_literal(pipeline.stages[0].context, &lit_pat, &lit_len);
        simple_literal = use_fast_path && pipeline.count == 1 && mode == AGG_NONE && format_str == NULL;
        // Fast path for --max-version with literal pattern (no pipeline stages, no format)
        max_version_literal = use_fast_path && pipeline.count == 1 && mode == AGG_MAX_VERSION && format_str == NULL && aggregator.replace_patterns == NULL;
    }

    char *buf = malloc(BUFFER_SIZE);
    if (!buf) {
        pipeline_free(&pipeline);
        return 1;
    }

    int status = 0;
    size_t bytes_left = 0;
    ssize_t bytes_read;
    bool stop = false;

    while (!stop) {
        bytes_read = read(STDIN_FILENO, buf + bytes_left, BUFFER_SIZE - bytes_left);
        if (bytes_read < 0) {
            perror("rx: read");
            status = 1;
            break;
        }
        if (bytes_read == 0)
            break;

        size_t total_buf_len = bytes_left + (size_t)bytes_read;
        char *ptr = buf;
        char *end = buf + total_buf_len;

        if (simple_literal) {
            /* ── Ultra-fast path ─────────────────────────────────────
               simd_memmem search, batched output.  No pipeline, no
               StringView, no double-scan. */
            char out_buf[OUT_BUF_SIZE];
            char *out_ptr = out_buf;
            while (ptr < end) {
                __builtin_prefetch(ptr + 64, 0, 1);
                char *match = (char *)simd_memmem(ptr, end - ptr, lit_pat, lit_len);
                if (!match)
                    break;

                if (out_ptr - out_buf + lit_len + 1 > OUT_BUF_SIZE) {
                    flush_out(&out_ptr, out_buf);
                }
                memcpy(out_ptr, match, lit_len);
                out_ptr += lit_len;
                *out_ptr++ = '\n';
                ptr = match + lit_len;
            }
            flush_out(&out_ptr, out_buf);
        } else if (max_version_literal) {
            /* ── Fast path for --max-version with literal pattern ─────
               simd_memmem search, track best version without pipeline overhead. */
            while (ptr < end) {
                __builtin_prefetch(ptr + 64, 0, 1);
                char *match = (char *)simd_memmem(ptr, end - ptr, lit_pat, lit_len);
                if (!match) {
                    char *last_newline = memrchr(ptr, '\n', end - ptr);
                    if (last_newline) {
                        ptr = last_newline + 1;
                    }
                    break;
                }

                char *line_start = memrchr(ptr, '\n', match - ptr);
                line_start = line_start ? line_start + 1 : ptr;

                char *line_end = memchr(match, '\n', end - match);
                if (!line_end) {
                    ptr = line_start;
                    break;
                }

                size_t line_len = line_end - line_start;
                size_t target_len = line_len;
                if (target_len > 0 && line_start[target_len - 1] == '\r') {
                    target_len--;
                }

                // Extract the matched portion for version comparison
                // The match is within the line, so we need to find the match within the line
                char *match_in_line = (char *)simd_memmem(line_start, target_len, lit_pat, lit_len);
                if (match_in_line) {
                    // For --max-version, we compare the matched portion (the version string)
                    // The matched portion starts at match_in_line and has length lit_len
                    if (!aggregator.best_raw || compare_versions(match_in_line, lit_len, aggregator.best_raw, aggregator.best_len) > 0) {
                        char *next = realloc(aggregator.best_raw, lit_len + 1);
                        if (!next) {
                            status = 1;
                            stop = true;
                            break;
                        }
                        aggregator.best_raw = next;
                        aggregator.best_len = lit_len;
                        memcpy(aggregator.best_raw, match_in_line, lit_len);
                        aggregator.best_raw[lit_len] = '\0';
                    }
                }

                ptr = line_end + 1;
            }
        } else if (use_fast_path) {
            while (ptr < end) {
                __builtin_prefetch(ptr + 64, 0, 1);
                char *match = (char *)simd_memmem(ptr, end - ptr, lit_pat, lit_len);
                if (!match) {
                    char *last_newline = memrchr(ptr, '\n', end - ptr);
                    if (last_newline) {
                        ptr = last_newline + 1;
                    }
                    break;
                }

                char *line_start = memrchr(ptr, '\n', match - ptr);
                line_start = line_start ? line_start + 1 : ptr;

                char *line_end = memchr(match, '\n', end - match);
                if (!line_end) {
                    ptr = line_start;
                    break;
                }

                size_t line_len = line_end - line_start;
                size_t target_len = line_len;
                if (target_len > 0 && line_start[target_len - 1] == '\r') {
                    target_len--;
                }

                StringView sv;
                sv_init(&sv, line_start, target_len);
                bool executed = pipeline_execute(&pipeline, &sv);
                sv_free(&sv);
                if (!executed) {
                    fprintf(stderr, "rx: pipeline stage failed\n");
                    status = 1;
                    stop = true;
                    break;
                }

                ptr = line_end + 1;
                if (aggregator.has_fired) {
                    stop = true;
                    break;
                }
            }
        } else {
            char *newline;
            while ((newline = memchr(ptr, '\n', end - ptr)) != NULL) {
                /* Prefetch next cache line for streaming access */
                __builtin_prefetch(ptr + 64, 0, 1);
                
                size_t line_len = newline - ptr;
                size_t target_len = line_len;
                if (target_len > 0 && ptr[target_len - 1] == '\r') {
                    target_len--;
                }

                StringView sv;
                sv_init(&sv, ptr, target_len);
                bool executed = pipeline_execute(&pipeline, &sv);
                sv_free(&sv);
                if (!executed) {
                    fprintf(stderr, "rx: pipeline stage failed\n");
                    status = 1;
                    stop = true;
                    break;
                }
                ptr = newline + 1;
                if (aggregator.has_fired) {
                    stop = true;
                    break;
                }
            }
        }

        bytes_left = end - ptr;
        if (bytes_left > 0) {
            memmove(buf, ptr, bytes_left);
        }
    }

    if (!stop && status == 0 && bytes_left > 0) {
        if (buf[bytes_left - 1] == '\r')
            bytes_left--;
        StringView sv;
        sv_init(&sv, buf, bytes_left);
        if (!pipeline_execute(&pipeline, &sv)) {
            fprintf(stderr, "rx: pipeline stage failed\n");
            status = 1;
        }
        sv_free(&sv);
    }

    if (status == 0 && aggregator.mode != AGG_NONE) agg_flush(&aggregator);

    free(buf);
    agg_free(&aggregator);

    pipeline_free(&pipeline);

    return status;
}
