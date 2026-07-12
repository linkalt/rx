#define _GNU_SOURCE
#include "replace.h"
#include <stdlib.h>
#include <string.h>

struct ReplaceStageCtx {
    char *target;
    char *replacement;
    size_t target_len;
    size_t replace_len;
};

ReplaceStageCtx* replace_ctx_create(const char *target, const char *replacement) {
    ReplaceStageCtx *ctx = malloc(sizeof(ReplaceStageCtx));
    if (!ctx) return NULL;
    ctx->target = strdup(target);
    ctx->replacement = strdup(replacement);
    ctx->target_len = strlen(target);
    ctx->replace_len = strlen(replacement);
    return ctx;
}

bool stage_replace(StringView *sv, void *context) {
    ReplaceStageCtx *ctx = (ReplaceStageCtx*)context;

    // Quick escape check: if target string isn't inside our view, skip out early
    if (sv->length < ctx->target_len) return true;

    // Count occurrences to calculate exact memory payload layout
    size_t matches = 0;
    for (size_t i = 0; i <= sv->length - ctx->target_len; ) {
        if (memcmp(sv->ptr + i, ctx->target, ctx->target_len) == 0) {
            matches++;
            i += ctx->target_len;
        } else {
            i++;
        }
    }

    if (matches == 0) return true; // Nothing to change

    // Compute size adjustments and claim mutable memory block
    size_t new_len = sv->length + (matches * ctx->replace_len) - (matches * ctx->target_len);

    // Allocate a scratchpad for the string transformation
    char *scratch = malloc(new_len + 1);
    if (!scratch) return false;

    size_t src_idx = 0;
    size_t dst_idx = 0;

    while (src_idx < sv->length) {
        if (src_idx <= sv->length - ctx->target_len &&
            memcmp(sv->ptr + src_idx, ctx->target, ctx->target_len) == 0) {
            memcpy(scratch + dst_idx, ctx->replacement, ctx->replace_len);
        dst_idx += ctx->replace_len;
        src_idx += ctx->target_len;
            } else {
                scratch[dst_idx++] = sv->ptr[src_idx++];
            }
    }

    // Force updates directly into our structural layout footprint
    if (sv->buf && sv->capacity > new_len) {
        memcpy(sv->buf, scratch, new_len);
        free(scratch);
        sv_update(sv, sv->buf, new_len);
    } else {
        sv_free(sv);
        sv->ptr = scratch;
        sv->length = new_len;
        sv->buf = scratch;
        sv->capacity = new_len + 1;
    }

    return true;
}
