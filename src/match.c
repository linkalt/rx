#define _GNU_SOURCE
#include "match.h"
#include "pipeline.h"
#include "aggregate.h"
#include "simd.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static bool append_bytes(char **dest, size_t *length, size_t *capacity,
                         const char *src, size_t src_len) {
    if (src_len > SIZE_MAX - *length - 1) return false;
    size_t required = *length + src_len + 1;
    if (required > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 64;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }
        char *next = realloc(*dest, new_capacity);
        if (!next) return false;
        *dest = next;
        *capacity = new_capacity;
    }
    memcpy(*dest + *length, src, src_len);
    *length += src_len;
    (*dest)[*length] = '\0';
    return true;
}

struct MatchStageCtx {
    pcre2_code *code;
    pcre2_match_data *match_data;
    pcre2_match_context *match_context;
    pcre2_jit_stack *jit_stack;
    bool jit_ready;
    bool has_first_code_unit;
    uint32_t first_code_unit;
    PCRE2_SIZE minimum_length;
    bool literal_mode;
    const char *literal_pattern;
    size_t literal_length;
    const char *format_str;
    Pipeline *parent_pipeline;
    Aggregator *aggregator;
    int format_group_only;
    char *format_buf;
    size_t format_len;
    size_t format_capacity;
};

MatchStageCtx* match_ctx_create(const char *pattern, const char *format_str, void *pipe, void *agg) {
    int errorcode;
    PCRE2_SIZE erroroffset;

    MatchStageCtx *ctx = calloc(1, sizeof(MatchStageCtx));
    if (!ctx) return NULL;

    ctx->literal_mode = false;
    ctx->literal_pattern = NULL;
    ctx->literal_length = 0;
    if (pattern && pattern[0] != '\0') {
        bool has_regex_syntax = false;
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
                    has_regex_syntax = true;
                    break;
                default:
                    break;
            }
            if (has_regex_syntax)
                break;
        }
        if (!has_regex_syntax) {
            ctx->literal_mode = true;
            ctx->literal_pattern = strdup(pattern);
            ctx->literal_length = strlen(pattern);
            if (!ctx->literal_pattern) {
                free(ctx);
                return NULL;
            }
        }
    }

    if (!ctx->literal_mode) {
        ctx->code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errorcode, &erroroffset, NULL);
        if (!ctx->code) {
            free(ctx);
            return NULL;
        }

        uint32_t first_code_type = 0;
        if (pcre2_pattern_info(ctx->code, PCRE2_INFO_FIRSTCODETYPE,
                               &first_code_type) == 0 &&
            first_code_type == 1 &&
            pcre2_pattern_info(ctx->code, PCRE2_INFO_FIRSTCODEUNIT,
                               &ctx->first_code_unit) == 0) {
            ctx->has_first_code_unit = true;
        }
        (void)pcre2_pattern_info(ctx->code, PCRE2_INFO_MINLENGTH,
                                 &ctx->minimum_length);

        ctx->jit_ready = pcre2_jit_compile(ctx->code, PCRE2_JIT_COMPLETE) == 0;
        ctx->match_data = pcre2_match_data_create_from_pattern(ctx->code, NULL);
        ctx->match_context = pcre2_match_context_create(NULL);
        if (ctx->jit_ready)
            ctx->jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
        if (!ctx->match_data || !ctx->match_context ||
            (ctx->jit_ready && !ctx->jit_stack)) {
            match_ctx_free(ctx);
            return NULL;
        }
        if (ctx->jit_ready)
            pcre2_jit_stack_assign(ctx->match_context, NULL, ctx->jit_stack);
    }

    ctx->format_str = format_str;
    ctx->parent_pipeline = (Pipeline *)pipe;
    ctx->aggregator = (Aggregator *)agg;
    ctx->format_group_only = -1;
    if (format_str && format_str[0] == '\\' && format_str[1] >= '0' && format_str[1] <= '9' && format_str[2] == '\0') {
        ctx->format_group_only = format_str[1] - '0';
    }
    ctx->format_buf = NULL;
    ctx->format_len = 0;
    ctx->format_capacity = 0;
    return ctx;
}

void match_ctx_free(MatchStageCtx *ctx) {
    if (ctx) {
        free(ctx->format_buf);
        free((void *)ctx->literal_pattern);
        if (ctx->jit_stack) pcre2_jit_stack_free(ctx->jit_stack);
        if (ctx->match_context) pcre2_match_context_free(ctx->match_context);
        if (ctx->match_data) pcre2_match_data_free(ctx->match_data);
        if (ctx->code) pcre2_code_free(ctx->code);
        free(ctx);
    }
}

PipelineResult stage_match(StringView *sv, void *context) {
    MatchStageCtx *ctx = (MatchStageCtx*)context;
    size_t start_offset = 0;

    if (sv->length == 0) return PIPE_CONSUME;

    if (ctx->literal_mode) {
        size_t pattern_len = ctx->literal_length;
        if (pattern_len == 0)
            return PIPE_CONSUME;

        size_t search_len = sv->length;
        while (start_offset <= search_len - pattern_len) {
            const char *found = simd_memmem(sv->ptr + start_offset,
                                           search_len - start_offset,
                                           ctx->literal_pattern,
                                           pattern_len);
            if (!found)
                break;

            size_t match_start = (size_t)(found - sv->ptr);
            size_t match_end = match_start + pattern_len;
            StringView match_sv;
            sv_init(&match_sv, sv->ptr + match_start, pattern_len);
            bool passed = true;
            if (ctx->parent_pipeline) {
                for (size_t i = 1; i < ctx->parent_pipeline->count; i++) {
                    PipelineResult res = ctx->parent_pipeline->stages[i].run(&match_sv, ctx->parent_pipeline->stages[i].context);
                    if (res == PIPE_ERR) {
                        sv_free(&match_sv);
                        return PIPE_ERR;
                    }
                    if (res == PIPE_CONSUME) {
                        passed = true;
                        break;
                    }
                }
            }
            if (passed) {
                if (ctx->aggregator && ctx->aggregator->mode != AGG_NONE) {
                    if (!agg_process(ctx->aggregator, &match_sv)) {
                        sv_free(&match_sv);
                        return PIPE_ERR;
                    }
                } else {
                    sv_print(&match_sv);
                    /* --first: stop after first match */
                    if (ctx->aggregator && ctx->aggregator->mode == AGG_FIRST)
                        ctx->aggregator->has_fired = true;
                }
            }
            sv_free(&match_sv);
            if (ctx->aggregator && ctx->aggregator->has_fired)
                return PIPE_CONSUME;
            start_offset = match_end;
        }
        return PIPE_CONSUME;
    }

    /* PCRE2 proves these constraints at compile time. Avoid entering the
       matcher at all for lines that cannot possibly match. */
    if (sv->length < ctx->minimum_length)
        return PIPE_CONSUME;
    if (ctx->has_first_code_unit &&
        !memchr(sv->ptr, (unsigned char)ctx->first_code_unit, sv->length))
        return PIPE_CONSUME;

    while (start_offset < sv->length) {
        size_t loop_safety_check = start_offset;

        int rc;
        if (ctx->jit_ready) {
            rc = pcre2_jit_match(ctx->code, (PCRE2_SPTR)sv->ptr, sv->length,
                                 start_offset, 0, ctx->match_data,
                                 ctx->match_context);
        } else {
            rc = pcre2_match(ctx->code, (PCRE2_SPTR)sv->ptr, sv->length,
                             start_offset, 0, ctx->match_data,
                             ctx->match_context);
        }

        if (rc == PCRE2_ERROR_NOMATCH)
            break;
        if (rc < 0)
            return PIPE_ERR;

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(ctx->match_data);
        if (ovector[0] == PCRE2_UNSET || ovector[1] == PCRE2_UNSET) {
            break;
        }

        StringView match_sv;
        if (ctx->format_group_only >= 0) {
            uint32_t group_idx = (uint32_t)ctx->format_group_only;
            if ((size_t)group_idx < (size_t)rc) {
                size_t g_start = ovector[group_idx * 2];
                size_t g_end = ovector[group_idx * 2 + 1];
                if (g_start != PCRE2_UNSET && g_end != PCRE2_UNSET) {
                    sv_init(&match_sv, sv->ptr + g_start, g_end - g_start);
                } else {
                    sv_init(&match_sv, "", 0);
                }
            } else {
                sv_init(&match_sv, "", 0);
            }
        } else if (!ctx->format_str) {
            sv_init(&match_sv, sv->ptr + ovector[0], ovector[1] - ovector[0]);
        } else {
            char *dest = ctx->format_buf;
            size_t new_len = 0;
            size_t capacity = ctx->format_capacity;
            const char *f = ctx->format_str;

            ctx->format_len = 0;
            while (*f != '\0') {
                if (*f == '\\' && *(f + 1) >= '0' && *(f + 1) <= '9') {
                    uint32_t group_idx = *(f + 1) - '0';
                    f += 2;
                    if (group_idx < (uint32_t)rc) {
                        size_t g_start = ovector[group_idx * 2];
                        size_t g_end = ovector[group_idx * 2 + 1];
                        if (g_start != PCRE2_UNSET && g_end != PCRE2_UNSET) {
                            bool appended = append_bytes(&dest, &new_len, &capacity,
                                                         sv->ptr + g_start,
                                                         g_end - g_start);
                            ctx->format_buf = dest;
                            ctx->format_capacity = capacity;
                            if (!appended)
                                return PIPE_ERR;
                        }
                    }
                } else {
                    bool appended = append_bytes(&dest, &new_len, &capacity, f++, 1);
                    ctx->format_buf = dest;
                    ctx->format_capacity = capacity;
                    if (!appended) {
                        return PIPE_ERR;
                    }
                }
            }
            ctx->format_buf = dest;
            ctx->format_len = new_len;
            ctx->format_capacity = capacity;
            match_sv.ptr = ctx->format_buf;
            match_sv.length = ctx->format_len;
            match_sv.buf = NULL;
            match_sv.capacity = 0;
        }

        bool passed = true;
        if (ctx->parent_pipeline) {
            for (size_t i = 1; i < ctx->parent_pipeline->count; i++) {
                PipelineResult res = ctx->parent_pipeline->stages[i].run(&match_sv, ctx->parent_pipeline->stages[i].context);
                if (res == PIPE_ERR) {
                    sv_free(&match_sv);
                    return PIPE_ERR;
                }
                if (res == PIPE_CONSUME) {
                    passed = true;
                    break;
                }
            }
        }

        if (passed) {
            if (ctx->aggregator && ctx->aggregator->mode != AGG_NONE) {
                if (!agg_process(ctx->aggregator, &match_sv)) {
                    sv_free(&match_sv);
                    return PIPE_ERR;
                }
            } else {
                sv_print(&match_sv);
                /* --first: stop after first match */
                if (ctx->aggregator && ctx->aggregator->mode == AGG_FIRST)
                    ctx->aggregator->has_fired = true;
            }
        }

        /* This is a no-op for non-owning views, and frees a buffer created
           by a downstream replacement stage. */
        sv_free(&match_sv);

        if (ctx->aggregator && ctx->aggregator->has_fired)
            return PIPE_CONSUME;

        // HARD PROTECTION LAYER:
        // Force start_offset past the end of the match.
        // If it fails to advance past our last position check, strictly step forward by 1 byte.
        if (ovector[1] > loop_safety_check) {
            start_offset = ovector[1];
        } else {
            start_offset = loop_safety_check + 1;
        }
    }

    return PIPE_CONSUME;
}

bool match_ctx_is_literal(const MatchStageCtx *ctx, const char **pattern, size_t *len) {
    if (ctx && ctx->literal_mode) {
        *pattern = ctx->literal_pattern;
        *len = ctx->literal_length;
        return true;
    }
    return false;
}
