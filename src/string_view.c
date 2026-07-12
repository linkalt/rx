#include "string_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_MIN_CAPACITY 64

static size_t
sv_initial_capacity(size_t len)
{
    size_t cap = len + 1;

    if (cap < SV_MIN_CAPACITY)
        cap = SV_MIN_CAPACITY;

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

    sv->buf = malloc(sv->capacity);
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
