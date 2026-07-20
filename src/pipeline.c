#include "pipeline.h"

#include <stdint.h>
#include <stdlib.h>

#define PIPELINE_INITIAL_CAPACITY 4

void
pipeline_init(Pipeline *p)
{
    if (!p)
        return;

    p->stages = NULL;
    p->count = 0;
    p->capacity = 0;
}

bool
pipeline_add(Pipeline *p, StageFunc run, void *context, void (*destroy)(void *))
{
    if (!p)
        return false;

    if (p->count == p->capacity) {
        if (p->capacity > SIZE_MAX / 2)
            return false;
        size_t new_capacity =
            (p->capacity == 0)
                ? PIPELINE_INITIAL_CAPACITY
                : p->capacity * 2;

        if (new_capacity > SIZE_MAX / sizeof(*p->stages))
            return false;

        PipelineStage *new_stages =
            realloc(p->stages, new_capacity * sizeof(*new_stages));

        if (!new_stages)
            return false;

        p->stages = new_stages;
        p->capacity = new_capacity;
    }

    p->stages[p->count].run = run;
    p->stages[p->count].context = context;
    p->stages[p->count].destroy = destroy;
    p->count++;

    return true;
}

bool
pipeline_execute(Pipeline *p, StringView *sv)
{
    if (!p)
        return false;

    PipelineStage *stage = p->stages;
    PipelineStage *end = stage + p->count;

    while (stage != end) {
        PipelineResult res = stage->run(sv, stage->context);
        if (res == PIPE_ERR)
            return false;
        if (res == PIPE_CONSUME)
            break;

        ++stage;
    }

    return true;
}

void
pipeline_free(Pipeline *p)
{
    if (!p)
        return;

    if (p->stages) {
        for (size_t i = 0; i < p->count; ++i) {
            if (p->stages[i].destroy && p->stages[i].context) {
                p->stages[i].destroy(p->stages[i].context);
            }
        }
        free(p->stages);
    }

    p->stages = NULL;
    p->count = 0;
    p->capacity = 0;
}
