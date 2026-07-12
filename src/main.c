#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "string_view.h"
#include "match.h"
#include "replace.h"
#include "pipeline.h"
#include "aggregate.h"

#define BUFFER_SIZE (128 * 1024)

static void free_replace_stages(Pipeline *pipeline) {
    for (size_t i = 1; i < pipeline->count; i++)
        replace_ctx_free(pipeline->stages[i].context);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s --match <regex> [--format <layout>] [--replace <from> <to> ...] [--max-version] [--first]\n", argv[0]);
        return 1;
    }

    Pipeline pipeline;
    pipeline_init(&pipeline);

    const char *pattern = NULL;
    const char *format_str = NULL;
    bool process_max_version = false;
    bool process_first = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--match") == 0 && i + 1 < argc) {
            pattern = argv[++i];
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format_str = argv[++i];
        } else if (strcmp(argv[i], "--max-version") == 0) {
            process_max_version = true;
        } else if (strcmp(argv[i], "--first") == 0) {
            process_first = true;
        }
    }

    if (!pattern) {
        fprintf(stderr, "Error: Missing required parameter --match <regex>\n");
        pipeline_free(&pipeline);
        return 1;
    }

    Aggregator aggregator;
    AggMode mode = AGG_NONE;
    if (process_max_version) mode = AGG_MAX_VERSION;
    else if (process_first) mode = AGG_FIRST;
    agg_init(&aggregator, mode);

    // Instantiate Matcher with tracking structures passed cleanly inside the context bounds
    MatchStageCtx *match_ctx = match_ctx_create(pattern, format_str, &pipeline, &aggregator);
    if (!match_ctx) {
        fprintf(stderr, "Error: Failed compiling regex pattern.\n");
        pipeline_free(&pipeline);
        return 1;
    }
    if (!pipeline_add(&pipeline, stage_match, match_ctx)) {
        fprintf(stderr, "rx: failed to allocate pipeline stage\n");
        match_ctx_free(match_ctx);
        pipeline_free(&pipeline);
        return EXIT_FAILURE;
    }
    // Append standard character mutations down the layout array track
    // Inside src/main.c Pass 2 loop:
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replace") == 0 && i + 2 < argc) {
            const char *target = argv[++i];    // Pass the entire word arg string pointer
            const char *dest = argv[++i];      // Pass the destination word arg string pointer

            ReplaceStageCtx *replace_ctx = replace_ctx_create(target, dest);
            if (!replace_ctx || !pipeline_add(&pipeline, stage_replace, replace_ctx)) {
                fprintf(stderr, "rx: invalid replacement or failed to allocate pipeline stage\n");
                replace_ctx_free(replace_ctx);
                free_replace_stages(&pipeline);
                match_ctx_free(match_ctx);
                pipeline_free(&pipeline);
                return EXIT_FAILURE;
            }
        }
    }

    size_t buffer_capacity = BUFFER_SIZE;
    char *buf = malloc(buffer_capacity);
    if (!buf) {
        free_replace_stages(&pipeline);
        match_ctx_free(match_ctx);
        pipeline_free(&pipeline);
        return 1;
    }

    size_t bytes_left = 0;
    ssize_t bytes_read;
    bool stop = false;
    int status = 0;

    while (!stop) {
        if (bytes_left == buffer_capacity) {
            if (buffer_capacity > SIZE_MAX / 2) {
                fprintf(stderr, "rx: input line is too long\n");
                status = 1;
                break;
            }
            char *next = realloc(buf, buffer_capacity * 2);
            if (!next) {
                fprintf(stderr, "rx: failed to grow input buffer\n");
                status = 1;
                break;
            }
            buf = next;
            buffer_capacity *= 2;
        }

        bytes_read = read(STDIN_FILENO, buf + bytes_left, buffer_capacity - bytes_left);
        if (bytes_read < 0) {
            perror("rx: read");
            status = 1;
            break;
        }
        if (bytes_read == 0)
            break;

        size_t total_buf_len = bytes_left + bytes_read;
        char *ptr = buf;
        char *end = buf + total_buf_len;
        char *newline;

        while ((newline = memchr(ptr, '\n', end - ptr)) != NULL) {
            size_t line_len = newline - ptr;
            size_t target_len = line_len;
            if (target_len > 0 && ptr[target_len - 1] == '\r') {
                target_len--;
            }

            StringView sv;
            sv_init(&sv, ptr, target_len);
            pipeline_execute(&pipeline, &sv);
            sv_free(&sv);
            ptr = newline + 1;
            if (aggregator.has_fired) {
                stop = true;
                break;
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
        pipeline_execute(&pipeline, &sv);
        sv_free(&sv);
    }

    if (status == 0 && aggregator.mode != AGG_NONE) agg_flush(&aggregator);

    free(buf);
    agg_free(&aggregator);
    match_ctx_free(match_ctx);

    free_replace_stages(&pipeline);
    pipeline_free(&pipeline);

    return status;
}
