#ifndef RX_REPLACE_H
#define RX_REPLACE_H

#include "string_view.h"
#include "pipeline.h"

typedef struct ReplaceStageCtx ReplaceStageCtx;

ReplaceStageCtx* replace_ctx_create(const char *target, const char *replacement);
void replace_ctx_free(ReplaceStageCtx *ctx);
PipelineResult stage_replace(StringView *sv, void *context);

#endif
