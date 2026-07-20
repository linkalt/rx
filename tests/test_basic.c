#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "string_view.h"
#include "pipeline.h"
#include "replace.h"
#include "aggregate.h"
#include "match.h"

static int test_string_view_mutation(void) {
    StringView sv;
    char buf[] = "hello";
    sv_init(&sv, buf, 5);
    bool ok = sv_ensure_mutable(&sv);
    assert(ok);
    assert(sv.buf != NULL);
    memcpy(sv.buf, "world", 5);
    sv_update(&sv, sv.buf, 5);
    assert(sv.length == 5);
    sv_free(&sv);
    return 0;
}

static int test_replace_stage(void) {
    ReplaceStageCtx *ctx = replace_ctx_create("ll", "XX");
    assert(ctx != NULL);
    StringView sv;
    char buf[] = "hello";
    sv_init(&sv, buf, 5);
    assert(sv_ensure_mutable(&sv));
    assert(stage_replace(&sv, ctx) == PIPE_OK);
    assert(strcmp(sv.ptr, "heXXo") == 0);
    sv_free(&sv);
    replace_ctx_free(ctx);
    return 0;
}

static int test_aggregate_version(void) {
    Aggregator agg;
    agg_init(&agg, AGG_MAX_VERSION);
    StringView v1, v2;
    char a[] = "1.2";
    char b[] = "1.10";
    sv_init(&v1, a, 3);
    sv_init(&v2, b, 4);
    agg_process(&agg, &v1);
    agg_process(&agg, &v2);
    agg_flush(&agg);
    agg_free(&agg);
    sv_free(&v1);
    sv_free(&v2);
    return 0;
}

static int test_literal_match_context(void) {
    Pipeline pipeline;
    pipeline_init(&pipeline);
    Aggregator aggregator;
    agg_init(&aggregator, AGG_NONE);

    MatchStageCtx *ctx = match_ctx_create("DELETE", NULL, &pipeline, &aggregator);
    assert(ctx != NULL);
    match_ctx_free(ctx);
    pipeline_free(&pipeline);
    agg_free(&aggregator);
    return 0;
}

int main(void) {
    test_string_view_mutation();
    test_replace_stage();
    test_aggregate_version();
    test_literal_match_context();
    puts("tests passed");
    return 0;
}
