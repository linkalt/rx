#ifndef RX_SIMD_H
#define RX_SIMD_H

#include <stddef.h>
#include <stdbool.h>

/* CPU feature detection */
typedef struct {
    bool has_sse42;
    bool has_avx2;
    bool has_bmi2;
} CpuFeatures;

CpuFeatures cpu_detect_features(void);

/* Unified dispatcher - automatically selects best implementation */
const void* simd_memmem(const void* haystack, size_t haystack_len,
                        const void* needle, size_t needle_len);

/* Check if SIMD acceleration is available at runtime. */
bool simd_available(void);

/* Get SIMD implementation name for debugging. */
const char* simd_implementation(void);

#endif