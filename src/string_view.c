#include "string_view.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void sv_init(StringView *sv, const char *str, size_t len) {
    sv->ptr = str;
    sv->length = len;
    sv->buf = NULL;
    sv->capacity = 0;
}

void sv_free(StringView *sv) {
    if (sv->buf) {
        free(sv->buf);
    }
    sv->ptr = NULL;
    sv->length = 0;
    sv->capacity = 0;
}

bool sv_ensure_mutable(StringView *sv) {
    if (sv->buf) return true; // Already mutable

    sv->buf = malloc(sv->length + 1);
    if (!sv->buf) return false;

    memcpy(sv->buf, sv->ptr, sv->length);
    sv->buf[sv->length] = '\0';
    sv->ptr = sv->buf;
    sv->capacity = sv->length + 1;
    return true;
}

void sv_update(StringView *sv, const char *new_ptr, size_t new_len) {
    if (sv->buf && new_ptr == sv->buf) {
        sv->length = new_len;
        sv->buf[new_len] = '\0';
    } else {
        // If it points elsewhere, clean up and reset to standard view
        if (sv->buf) {
            free(sv->buf);
            sv->buf = NULL;
            sv->capacity = 0;
        }
        sv->ptr = new_ptr;
        sv->length = new_len;
    }
}

void sv_print(const StringView *sv) {
    fwrite(sv->ptr, 1, sv->length, stdout);
    putchar('\n');
}
