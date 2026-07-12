#include "aggregate.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void agg_init(Aggregator *agg, AggMode mode) {
    agg->mode = mode;
    agg->best_raw = NULL;
    agg->best_len = 0;
    agg->has_fired = false;
}

// Low-overhead natural version comparison algorithm (e.g., 1.10 > 1.9)
static int compare_versions(const char *v1, size_t l1, const char *v2, size_t l2) {
    size_t i = 0, j = 0;

    while (i < l1 || j < l2) {
        // 1. Skip all separators/delimiters for version 1
        while (i < l1 && (v1[i] == '.' || v1[i] == '_' || v1[i] == '-' || v1[i] == 'v')) {
            i++;
        }
        // Skip all separators/delimiters for version 2
        while (j < l2 && (v2[j] == '.' || v2[j] == '_' || v2[j] == '-' || v2[j] == 'v')) {
            j++;
        }

        // If both strings reached the end at the same time, they are equal
        if (i >= l1 && j >= l2) {
            break;
        }

        // 2. Extract the numeric block for this segment
        unsigned long long num1 = 0;
        bool has_num1 = false;
        while (i < l1 && v1[i] >= '0' && v1[i] <= '9') {
            num1 = num1 * 10 + (v1[i] - '0');
            i++;
            has_num1 = true;
        }

        unsigned long long num2 = 0;
        bool has_num2 = false;
        while (j < l2 && v2[j] >= '0' && v2[j] <= '9') {
            num2 = num2 * 10 + (v2[j] - '0');
            j++;
            has_num2 = true;
        }

        // 3. Compare the numeric segments
        if (num1 > num2) return 1;
        if (num1 < num2) return -1;

        // 4. Fallback check if one has a number segment and the other has alphabetical tags (e.g. 1.0 vs 1.0-beta)
        if (has_num1 != has_num2) {
            return has_num1 ? 1 : -1;
        }

        // 5. Handle non-numeric text tags if present (e.g., 'alpha' vs 'beta')
        if (i < l1 && j < l2 && !(v1[i] >= '0' && v1[i] <= '9') && !(v2[j] >= '0' && v2[j] <= '9')) {
            while (i < l1 && j < l2 && v1[i] != '.' && v2[j] != '.') {
                if (v1[i] != v2[j]) {
                    return (v1[i] > v2[j]) ? 1 : -1;
                }
                i++; j++;
            }
        }
    }

    return 0;
}

void agg_process(Aggregator *agg, const StringView *sv) {
    if (agg->mode == AGG_MAX_VERSION) {
        if (!agg->best_raw || compare_versions(sv->ptr, sv->length, agg->best_raw, agg->best_len) > 0) {
            char *next = realloc(agg->best_raw, sv->length + 1);
            if (next) {
                agg->best_raw = next;
                agg->best_len = sv->length;
                memcpy(agg->best_raw, sv->ptr, sv->length);
                agg->best_raw[sv->length] = '\0';
            }
        }
    }
    // NEW: Handle --first logic by printing immediately and signaling exit
    else if (agg->mode == AGG_FIRST) {
        sv_print(sv);
        agg->has_fired = true;
        // We exit the program instantly right here!
        // This stops reading any further bytes from curl.
        exit(0);
    }
}

void agg_flush(Aggregator *agg) {
    if (agg->mode == AGG_MAX_VERSION && agg->best_raw) {
        printf("%s\n", agg->best_raw);
    }
}

void agg_free(Aggregator *agg) {
    if (agg->best_raw) {
        free(agg->best_raw);
        agg->best_raw = NULL;
    }
}
