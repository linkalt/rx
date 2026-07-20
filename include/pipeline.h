#ifndef RX_PIPELINE_H
#define RX_PIPELINE_H

#include "string_view.h"

typedef enum {
    PIPE_OK = 0,
    PIPE_CONSUME,
    PIPE_ERR
} PipelineResult;

typedef PipelineResult (*StageFunc)(StringView *sv, void *context);

typedef struct {
    StageFunc run;
    void *context;
    /* Optional destructor for the context */
    void (*destroy)(void *context);
} PipelineStage;

typedef struct {
    PipelineStage *stages;
    size_t count;
    size_t capacity;
} Pipeline;

void pipeline_init(Pipeline *p);
bool pipeline_add(Pipeline *p, StageFunc run, void *context, void (*destroy)(void *));
bool pipeline_execute(Pipeline *p, StringView *sv);
void pipeline_free(Pipeline *p);

#endif
