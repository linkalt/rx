#ifndef RX_AGGREGATE_H
#define RX_AGGREGATE_H

#include "string_view.h"
#include "replace.h"

typedef enum {
    AGG_NONE,
    AGG_MAX_VERSION,
    AGG_FIRST
} AggMode;

typedef struct ReplacePattern {
    ReplaceStageCtx *ctx;
    struct ReplacePattern *next;
} ReplacePattern;

typedef struct {
    AggMode mode;
    char *best_raw;
    size_t best_len;
    bool has_fired;
    ReplacePattern *replace_patterns;  // Replace patterns for aggregator modes
} Aggregator;

void agg_init(Aggregator *agg, AggMode mode);
bool agg_process(Aggregator *agg, const StringView *sv);
void agg_flush(Aggregator *agg);
void agg_free(Aggregator *agg);
bool agg_add_replace(Aggregator *agg, const char *target, const char *replacement);

// Version comparison function (for external use in fast paths)
int compare_versions(const char *v1, size_t l1, const char *v2, size_t l2);

#endif
