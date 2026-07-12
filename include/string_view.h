#ifndef RX_STRING_VIEW_H
#define RX_STRING_VIEW_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *ptr;
    size_t length;
    char *buf;       // Dynamically allocated only if modified
    size_t capacity; // Allocated capacity tracker
} StringView;

// Lifecycle
void sv_init(StringView *sv, const char *str, size_t len);
void sv_free(StringView *sv);

// Mutations (Triggers copy-on-write/modify)
bool sv_ensure_mutable(StringView *sv);
void sv_update(StringView *sv, const char *new_ptr, size_t new_len);

// Utilities
void sv_print(const StringView *sv);

#endif
