#ifndef RX_MATCH_H
#define RX_MATCH_H

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "string_view.h"

typedef struct MatchStageCtx MatchStageCtx;

// Accept vanilla void pointers to completely decouple compilation units
MatchStageCtx* match_ctx_create(const char *pattern, const char *format_str, void *pipe, void *agg);
void match_ctx_free(MatchStageCtx *ctx);
bool stage_match(StringView *sv, void *context);

#endif
