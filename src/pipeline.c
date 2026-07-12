#include "pipeline.h"
#include <stdlib.h>

void pipeline_init(Pipeline *p) {
    p->stages = NULL;
    p->count = 0;
    p->capacity = 0;
}

void pipeline_add(Pipeline *p, StageFunc run, void *context) {
    if (p->count >= p->capacity) {
        p->capacity = p->capacity == 0 ? 4 : p->capacity * 2;
        p->stages = realloc(p->stages, p->capacity * sizeof(PipelineStage));
    }
    p->stages[p->count++] = (PipelineStage){.run = run, .context = context};
}

bool pipeline_execute(Pipeline *p, StringView *sv) {
    for (size_t i = 0; i < p->count; i++) {
        if (!p->stages[i].run(sv, p->stages[i].context)) {
            return false; // Dropped by a pipeline stage
        }
    }
    return true;
}

void pipeline_free(Pipeline *p) {
    if (p->stages) {
        free(p->stages);
        p->stages = NULL;
    }
    p->count = 0;
    p->capacity = 0;
}
