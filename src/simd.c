#define _GNU_SOURCE
#include <stdbool.h>
#include "simd.h"
#include <string.h>
#include <cpuid.h>
#include <stddef.h>

/* ============================================================
 * CPU Feature Detection
 * ============================================================ */

static CpuFeatures g_cpu_features = {0};
static bool g_cpu_detected = false;

CpuFeatures cpu_detect_features(void) {
    if (g_cpu_detected) return g_cpu_features;

    unsigned int eax, ebx, ecx, edx;
    
    /* Check for CPUID support */
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        g_cpu_detected = true;
        return g_cpu_features;
    }

    /* SSE4.2: ECX bit 20 */
    g_cpu_features.has_sse42 = (ecx & (1u << 20)) != 0;
    
    /* AVX2: Need CPUID leaf 7, EBX bit 5 */
    if (__get_cpuid(7, &eax, &ebx, &ecx, &edx)) {
        g_cpu_features.has_avx2 = (ebx & (1u << 5)) != 0;
        g_cpu_features.has_bmi2 = (ebx & (1u << 8)) != 0;
    }

    /* Also check OS support for AVX (XCR0) */
    if (g_cpu_features.has_avx2) {
        unsigned int xcr0_lo, xcr0_hi;
        __asm__ volatile ("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
        if ((xcr0_lo & 0x6) != 0x6) {
            g_cpu_features.has_avx2 = false;  /* OS doesn't support AVX */
        }
    }

    g_cpu_detected = true;
    return g_cpu_features;
}

/* ============================================================
 * Generic memmem fallback (Two-Way algorithm via glibc)
 * ============================================================ */

const void* generic_memmem(const void* haystack, size_t haystack_len,
                            const void* needle, size_t needle_len) {
    return memmem(haystack, haystack_len, needle, needle_len);
}

/* ============================================================
 * SSE4.2 Implementation (for needles <= 16 bytes)
 * Uses PCMPESTRI for substring search
 * ============================================================ */

#if defined(__SSE4_2__)
#include <nmmintrin.h>

static const void* simd_memmem_sse42(const void* haystack, size_t haystack_len,
                                      const void* needle, size_t needle_len) {
    if (needle_len == 0) return haystack;
    if (needle_len > 16) return NULL;  /* SSE4.2 limited to 16 bytes */
    if (haystack_len < needle_len) return NULL;

    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;
    const unsigned char* h_end = h + haystack_len - needle_len + 1;

    /* Build needle vector */
    __m128i needle_vec = _mm_loadu_si128((const __m128i*)n);
    /* Create mask for valid bytes in needle */
    __m128i len_mask = _mm_cvtsi32_si128((1 << needle_len) - 1);
    len_mask = _mm_cmpeq_epi8(len_mask, _mm_setzero_si128());

    for (; h < h_end; h += 16) {
        __m128i haystack_vec = _mm_loadu_si128((const __m128i*)h);
        
        /* PCMPESTRI: Packed Compare Explicit Length Strings, Return Index */
        /* _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_LEAST_SIGNIFICANT */
        int idx = _mm_cmpestri(needle_vec, needle_len, haystack_vec, 16,
                                _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_LEAST_SIGNIFICANT);
        
        if (idx < 16 && (size_t)(h + idx - (const unsigned char*)haystack) <= haystack_len - needle_len) {
            /* Verify match (PCMPESTRI can have false positives at boundaries) */
            if (memcmp(h + idx, n, needle_len) == 0) {
                return h + idx;
            }
        }
    }

    return NULL;
}
#endif

/* ============================================================
 * AVX2 Implementation (for needles <= 32 bytes)
 * Uses VPCMPEQB + VPMOVMSKB for first-byte matching
 * ============================================================ */

#if defined(__AVX2__)
#include <immintrin.h>

static const void* simd_memmem_avx2(const void* haystack, size_t haystack_len,
                                     const void* needle, size_t needle_len) {
    if (needle_len == 0) return haystack;
    if (needle_len > 32) return NULL;  /* AVX2 limited to 32 bytes */
    if (haystack_len < needle_len) return NULL;

    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;
    const unsigned char* h_end = h + haystack_len - needle_len + 1;
    unsigned char first_byte = n[0];

    /* Broadcast first byte to all 32 lanes */
    __m256i first_vec = _mm256_set1_epi8((char)first_byte);

    /* Process 32 bytes at a time */
    for (; h + 32 <= h_end; h += 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)h);
        __m256i cmp = _mm256_cmpeq_epi8(chunk, first_vec);
        int mask = _mm256_movemask_epi8(cmp);

        while (mask) {
            int bit = __builtin_ctz(mask);
            const unsigned char* candidate = h + bit;
            if (memcmp(candidate, n, needle_len) == 0) {
                return candidate;
            }
            mask &= mask - 1;  /* Clear lowest set bit */
        }
    }

    /* Handle remaining bytes with scalar */
    for (; h < h_end; h++) {
        if (*h == first_byte && memcmp(h, n, needle_len) == 0) {
            return h;
        }
    }

    return NULL;
}
#endif

/* ============================================================
 * Unified Dispatcher
 * ============================================================ */

const void* simd_memmem(const void* haystack, size_t haystack_len,
                         const void* needle, size_t needle_len) {
    CpuFeatures features = cpu_detect_features();

    /* Use SIMD for short patterns */
    if (needle_len <= 32 && haystack_len >= needle_len) {
#if defined(__AVX2__)
        if (features.has_avx2) {
            const void* result = simd_memmem_avx2(haystack, haystack_len, needle, needle_len);
            if (result) return result;
            /* Fall through to generic for remainder */
        }
#endif
#if defined(__SSE4_2__)
        if (features.has_sse42 && needle_len <= 16) {
            const void* result = simd_memmem_sse42(haystack, haystack_len, needle, needle_len);
            if (result) return result;
        }
#endif
    }

    return generic_memmem(haystack, haystack_len, needle, needle_len);
}

/* ============================================================
 * Public API
 * ============================================================ */

bool simd_available(void) {
    CpuFeatures features = cpu_detect_features();
    return features.has_avx2 || features.has_sse42;
}

const char* simd_implementation(void) {
    CpuFeatures features = cpu_detect_features();
    if (features.has_avx2) return "AVX2";
    if (features.has_sse42) return "SSE4.2";
    return "scalar";
}