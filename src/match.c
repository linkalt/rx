#include "match.h"
#include "pipeline.h"
#include "aggregate.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct MatchStageCtx {
    pcre2_code *code;
    pcre2_match_data *match_data;
    pcre2_match_context *match_context;
    pcre2_jit_stack *jit_stack;
    const char *format_str;
    Pipeline *parent_pipeline;
    Aggregator *aggregator;
};

MatchStageCtx* match_ctx_create(const char *pattern, const char *format_str, void *pipe, void *agg) {
    int errorcode;
    PCRE2_SIZE erroroffset;

    MatchStageCtx *ctx = malloc(sizeof(MatchStageCtx));
    if (!ctx) return NULL;

    ctx->code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errorcode, &erroroffset, NULL);
    if (!ctx->code) {
        free(ctx);
        return NULL;
    }

    pcre2_jit_compile(ctx->code, PCRE2_JIT_COMPLETE);
    ctx->match_data = pcre2_match_data_create_from_pattern(ctx->code, NULL);
    ctx->match_context = pcre2_match_context_create(NULL);
    ctx->jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
    pcre2_jit_stack_assign(ctx->match_context, NULL, ctx->jit_stack);

    ctx->format_str = format_str;
    ctx->parent_pipeline = (Pipeline *)pipe;
    ctx->aggregator = (Aggregator *)agg;
    return ctx;
}

void match_ctx_free(MatchStageCtx *ctx) {
    if (ctx) {
        if (ctx->jit_stack) pcre2_jit_stack_free(ctx->jit_stack);
        if (ctx->match_context) pcre2_match_context_free(ctx->match_context);
        if (ctx->match_data) pcre2_match_data_free(ctx->match_data);
        if (ctx->code) pcre2_code_free(ctx->code);
        free(ctx);
    }
}

bool stage_match(StringView *sv, void *context) {
    MatchStageCtx *ctx = (MatchStageCtx*)context;
    size_t start_offset = 0;

    if (sv->length == 0) return false;

    while (start_offset < sv->length) {
        size_t loop_safety_check = start_offset;

        int rc = pcre2_match(
            ctx->code, (PCRE2_SPTR)sv->ptr, sv->length,
                             start_offset, 0, ctx->match_data, ctx->match_context
        );

        if (rc < 0) {
            break; // Standard exit when no more matches are present
        }

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(ctx->match_data);
        if (ovector[0] == PCRE2_UNSET || ovector[1] == PCRE2_UNSET) {
            break;
        }

        StringView match_sv;
        if (!ctx->format_str) {
            sv_init(&match_sv, sv->ptr + ovector[0], ovector[1] - ovector[0]);
        } else {
            char *dest = malloc(sv->length + 1);
            size_t new_len = 0;
            const char *f = ctx->format_str;

            while (*f != '\0') {
                if (*f == '\\' && *(f + 1) >= '0' && *(f + 1) <= '9') {
                    uint32_t group_idx = *(f + 1) - '0';
                    f += 2;
                    if (group_idx < (uint32_t)rc) {
                        size_t g_start = ovector[group_idx * 2];
                        size_t g_end = ovector[group_idx * 2 + 1];
                        size_t len = g_end - g_start;
                        memcpy(dest + new_len, sv->ptr + g_start, len);
                        new_len += len;
                    }
                } else {
                    dest[new_len++] = *f++;
                }
            }
            match_sv.ptr = dest;
            match_sv.length = new_len;
            match_sv.buf = dest;
            match_sv.capacity = new_len + 1;
        }

        bool passed = true;
        if (ctx->parent_pipeline) {
            for (size_t i = 1; i < ctx->parent_pipeline->count; i++) {
                if (!ctx->parent_pipeline->stages[i].run(&match_sv, ctx->parent_pipeline->stages[i].context)) {
                    passed = false;
                    break;
                }
            }
        }

        if (passed) {
            if (ctx->aggregator && ctx->aggregator->mode != AGG_NONE) {
                agg_process(ctx->aggregator, &match_sv);
            } else {
                sv_print(&match_sv);
            }
        }

        sv_free(&match_sv);

        // HARD PROTECTION LAYER:
        // Force start_offset past the end of the match.
        // If it fails to advance past our last position check, strictly step forward by 1 byte.
        if (ovector[1] > loop_safety_check) {
            start_offset = ovector[1];
        } else {
            start_offset = loop_safety_check + 1;
        }
    }

    return false;
}
