#define _GNU_SOURCE
#include "replace.h"
#include "pipeline.h"
#include "simd.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ReplaceStageCtx {
    char *target;
    char *replacement;
    size_t target_len;
    size_t replace_len;
};

static size_t find_matches(const ReplaceStageCtx *ctx, const char *src, size_t len,
                            size_t *positions, size_t max_matches) {
    size_t count = 0;
    const char *cursor = src;
    size_t remaining = len;

    while (remaining >= ctx->target_len && count < max_matches) {
        const char *match = simd_memmem(cursor, remaining, ctx->target, ctx->target_len);
        if (!match)
            break;
        positions[count++] = (size_t)(match - src);
        cursor = match + ctx->target_len;
        remaining = len - (size_t)(cursor - src);
    }

    return count;
}

ReplaceStageCtx* replace_ctx_create(const char *target, const char *replacement) {
    if (!target || !replacement || target[0] == '\0') return NULL;

    ReplaceStageCtx *ctx = malloc(sizeof(ReplaceStageCtx));
    if (!ctx) return NULL;
    ctx->target = strdup(target);
    ctx->replacement = strdup(replacement);
    if (!ctx->target || !ctx->replacement) {
        free(ctx->target);
        free(ctx->replacement);
        free(ctx);
        return NULL;
    }
    ctx->target_len = strlen(target);
    ctx->replace_len = strlen(replacement);
    return ctx;
}

void replace_ctx_free(ReplaceStageCtx *ctx) {
    if (!ctx) return;
    free(ctx->target);
    free(ctx->replacement);
    free(ctx);
}

PipelineResult stage_replace(StringView *sv, void *context) {
    ReplaceStageCtx *ctx = (ReplaceStageCtx*)context;
    if (!sv || !ctx) return PIPE_ERR;

    if (sv->length < ctx->target_len) return PIPE_OK;

    size_t max_matches = (sv->length / ctx->target_len) + 1;
    size_t *positions = malloc(max_matches * sizeof(*positions));
    if (!positions) return PIPE_ERR;

    size_t matches = find_matches(ctx, sv->ptr, sv->length, positions, max_matches);
    if (matches == 0) {
        free(positions);
        return PIPE_OK;
    }

    size_t new_len;
    if (ctx->replace_len >= ctx->target_len) {
        size_t growth = ctx->replace_len - ctx->target_len;
        if (growth > 0 && matches > (SIZE_MAX - sv->length) / growth) {
            free(positions);
            return PIPE_ERR;
        }
        new_len = sv->length + matches * growth;
    } else {
        new_len = sv->length - matches * (ctx->target_len - ctx->replace_len);
    }
    if (new_len == SIZE_MAX) {
        free(positions);
        return PIPE_ERR;
    }

    if (sv->buf && sv->capacity >= new_len + 1) {
        char *dst = sv->buf;
        size_t src_idx = 0;
        size_t dst_idx = 0;
        size_t pidx = 0;

        while (src_idx < sv->length) {
            if (pidx < matches && positions[pidx] == src_idx) {
                memcpy(dst + dst_idx, ctx->replacement, ctx->replace_len);
                dst_idx += ctx->replace_len;
                src_idx += ctx->target_len;
                ++pidx;
            } else {
                dst[dst_idx++] = sv->ptr[src_idx++];
            }
        }

        free(positions);
        sv_update(sv, sv->buf, new_len);
        return PIPE_OK;
    }

    char *scratch = malloc(new_len + 1);
    if (!scratch) {
        free(positions);
        return PIPE_ERR;
    }

    size_t src_idx = 0;
    size_t dst_idx = 0;
    size_t pidx = 0;

    while (src_idx < sv->length) {
        if (pidx < matches && positions[pidx] == src_idx) {
            memcpy(scratch + dst_idx, ctx->replacement, ctx->replace_len);
            dst_idx += ctx->replace_len;
            src_idx += ctx->target_len;
            ++pidx;
        } else {
            scratch[dst_idx++] = sv->ptr[src_idx++];
        }
    }

    free(positions);
    sv_free(sv);
    sv->ptr = scratch;
    sv->length = new_len;
    sv->buf = scratch;
    sv->capacity = new_len + 1;

    return PIPE_OK;
}
