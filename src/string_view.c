#include "string_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdalign.h>

#define SV_MIN_CAPACITY 64
#define SV_ALIGNMENT 32  /* 32-byte alignment for AVX2 vector loads/stores */

static size_t
sv_initial_capacity(size_t len)
{
    /* Guard against overflow when computing len + 1. */
    if (len >= SIZE_MAX - 1)
        return SIZE_MAX;

    size_t cap = len + 1;

    if (cap < SV_MIN_CAPACITY)
        cap = SV_MIN_CAPACITY;

    /* Round up to alignment boundary */
    if (cap % SV_ALIGNMENT != 0)
        cap = ((cap + SV_ALIGNMENT - 1) / SV_ALIGNMENT) * SV_ALIGNMENT;

    return cap;
}

void
sv_init(StringView *sv, const char *str, size_t len)
{
    if (!sv)
        return;

    sv->ptr = str;
    sv->length = len;
    sv->buf = NULL;
    sv->capacity = 0;
}

void
sv_free(StringView *sv)
{
    if (!sv)
        return;

    free(sv->buf);

    sv->buf = NULL;
    sv->ptr = NULL;
    sv->length = 0;
    sv->capacity = 0;
}

bool
sv_ensure_mutable(StringView *sv)
{
    if (!sv)
        return false;

    if (sv->buf)
        return true;

    sv->capacity = sv_initial_capacity(sv->length);

    /* Use aligned allocation for AVX2-friendly memory access */
    sv->buf = aligned_alloc(SV_ALIGNMENT, sv->capacity);
    if (!sv->buf) {
        sv->capacity = 0;
        return false;
    }

    memcpy(sv->buf, sv->ptr, sv->length);
    sv->buf[sv->length] = '\0';

    sv->ptr = sv->buf;

    return true;
}

void
sv_update(StringView *sv, const char *new_ptr, size_t new_len)
{
    if (!sv)
        return;

    if (sv->buf && new_ptr == sv->buf) {
        sv->length = new_len;
        sv->buf[new_len] = '\0';
        return;
    }

    free(sv->buf);

    sv->buf = NULL;
    sv->capacity = 0;
    sv->ptr = new_ptr;
    sv->length = new_len;
}

void
sv_print(const StringView *sv)
{
    if (!sv)
        return;

    fwrite(sv->ptr, 1, sv->length, stdout);
    putchar('\n');
}
