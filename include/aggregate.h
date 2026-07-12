#ifndef RX_AGGREGATE_H
#define RX_AGGREGATE_H

#include "string_view.h"

typedef enum {
    AGG_NONE,
    AGG_MAX_VERSION,
    AGG_FIRST          // <-- Added
} AggMode;

typedef struct {
    AggMode mode;
    char *best_raw;
    size_t best_len;
    bool has_fired;    // <-- Added to track if we already captured the first item
} Aggregator;

void agg_init(Aggregator *agg, AggMode mode);
void agg_process(Aggregator *agg, const StringView *sv);
void agg_flush(Aggregator *agg);
void agg_free(Aggregator *agg);

#endif
