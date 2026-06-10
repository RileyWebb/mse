#ifndef CNES_UTIL_H
#define CNES_UTIL_H

#include <stdlib.h>
#include <stddef.h>

// Detect and include SIMD intrinsics based on compiler definitions
#if defined(__AVX2__)
    #include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP >= 2)
    #include <emmintrin.h> // SSE2
    #include <xmmintrin.h> // SSE
#endif

inline void *UTIL_aligned_alloc(size_t alignment, size_t size) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__) // Windows APIs (MSVC & MinGW)

    return _aligned_malloc(size, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L // C11 aligned_alloc
    size_t remainder = size % alignment;
    if (remainder != 0) {
        size += (alignment - remainder);
    }
    return aligned_alloc(alignment, size);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L // POSIX fallback (Linux/macOS without C11 active)
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
#else
    return malloc(size);
#endif
}

inline void UTIL_aligned_free(void *ptr) {
    if (!ptr) return;

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
    _aligned_free(ptr);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    free(ptr);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    free(ptr);
#else
    free(ptr);
#endif
}

inline float UTIL_clampf(float val, float min_val, float max_val) {
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP >= 2)
    // SSE intrinsics compile directly to native MAXSS and MINSS instructions
    _mm_store_ss(&val, _mm_min_ss(_mm_max_ss(_mm_set_ss(val), _mm_set_ss(min_val)), _mm_set_ss(max_val)));
    return val;
#else
    
    return (val < min_val) ? min_val : ((val > max_val) ? max_val : val);
#endif
}

inline double UTIL_clampd(double val, double min_val, double max_val) {
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP >= 2)
    _mm_store_sd(&val, _mm_min_sd(_mm_max_sd(_mm_set_sd(val), _mm_set_sd(min_val)), _mm_set_sd(max_val)));
    return val;
#else
    return (val < min_val) ? min_val : ((val > max_val) ? max_val : val);
#endif
}

inline void UTIL_fast_clampf_array(float* __restrict src, float* __restrict dest, size_t count, float min_val, float max_val) {
    size_t i = 0;

#if defined(__AVX2__)
    // Processes 8 floats simultaneously
    __m256 vmin = _mm256_set1_ps(min_val);
    __m256 vmax = _mm256_set1_ps(max_val);
    
    for (; i + 7 < count; i += 8) {
        __m256 vval = _mm256_loadu_ps(&src[i]); // Unaligned load
        vval = _mm256_max_ps(vval, vmin);
        vval = _mm256_min_ps(vval, vmax);
        _mm256_storeu_ps(&dest[i], vval);       // Unaligned store
    }
#elif defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP >= 2)
    // Processes 4 floats simultaneously
    __m128 vmin = _mm_set1_ps(min_val);
    __m128 vmax = _mm_set1_ps(max_val);
    
    for (; i + 3 < count; i += 4) {
        __m128 vval = _mm_loadu_ps(&src[i]);
        vval = _mm_max_ps(vval, vmin);
        vval = _mm_min_ps(vval, vmax);
        _mm_storeu_ps(&dest[i], vval);
    }
#endif

    for (; i < count; ++i) {
        dest[i] = UTIL_clampf(src[i], min_val, max_val);
    }
}

inline void UTIL_clampd_array(double* __restrict src, double* __restrict dest, size_t count, double min_val, double max_val) {
    size_t i = 0;

#if defined(__AVX2__)
    // Processes 4 doubles simultaneously
    __m256d vmin = _mm256_set1_pd(min_val);
    __m256d vmax = _mm256_set1_pd(max_val);
    
    for (; i + 3 < count; i += 4) {
        __m256d vval = _mm256_loadu_pd(&src[i]);
        vval = _mm256_max_pd(vval, vmin);
        vval = _mm256_min_pd(vval, vmax);
        _mm256_storeu_pd(&dest[i], vval);
    }
#elif defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP >= 2)
    // Processes 2 doubles simultaneously
    __m128d vmin = _mm_set1_pd(min_val);
    __m128d vmax = _mm_set1_pd(max_val);
    
    for (; i + 1 < count; i += 2) {
        __m128d vval = _mm_loadu_pd(&src[i]);
        vval = _mm_max_pd(vval, vmin);
        vval = _mm_min_pd(vval, vmax);
        _mm_storeu_pd(&dest[i], vval);
    }
#endif

    for (; i < count; ++i) {
        dest[i] = UTIL_clampd(src[i], min_val, max_val);
    }
}

#endif // CNES_UTIL_H