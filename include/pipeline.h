#ifndef RX_PIPELINE_H
#define RX_PIPELINE_H

#include "string_view.h"

typedef bool (*StageFunc)(StringView *sv, void *context);

typedef struct {
    StageFunc run;
    void *context;
} PipelineStage;

typedef struct {
    PipelineStage *stages;
    size_t count;
    size_t capacity;
} Pipeline;

void pipeline_init(Pipeline *p);
bool pipeline_add(Pipeline *p, StageFunc run, void *context);
bool pipeline_execute(Pipeline *p, StringView *sv);
void pipeline_free(Pipeline *p);

#endif
