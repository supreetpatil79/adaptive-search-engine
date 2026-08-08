#pragma once
// utils/simd_math.h
// Hardware-accelerated SIMD vector dot product (ARM Neon & AVX2).
// Auto-detects architecture at compile time with a scalar fallback.

#include <cstddef>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace simd {

// Hardware-accelerated dot product (a · b) for 384-dimensional float vectors.
// Returns cosine similarity when a and b are unit vectors.
inline float innerProduct(const float* a, const float* b, int dim) {
    int i = 0;
    float sum = 0.0f;

#if defined(__ARM_NEON) || defined(__aarch64__)
    // 4-way unrolled 128-bit ARM Neon loop (16 floats / 64 bytes per iteration)
    float32x4_t sum0 = vdupq_n_f32(0.0f);
    float32x4_t sum1 = vdupq_n_f32(0.0f);
    float32x4_t sum2 = vdupq_n_f32(0.0f);
    float32x4_t sum3 = vdupq_n_f32(0.0f);

    for (; i + 15 < dim; i += 16) {
        float32x4_t va0 = vld1q_f32(a + i);
        float32x4_t vb0 = vld1q_f32(b + i);
        sum0 = vmlaq_f32(sum0, va0, vb0);

        float32x4_t va1 = vld1q_f32(a + i + 4);
        float32x4_t vb1 = vld1q_f32(b + i + 4);
        sum1 = vmlaq_f32(sum1, va1, vb1);

        float32x4_t va2 = vld1q_f32(a + i + 8);
        float32x4_t vb2 = vld1q_f32(b + i + 8);
        sum2 = vmlaq_f32(sum2, va2, vb2);

        float32x4_t va3 = vld1q_f32(a + i + 12);
        float32x4_t vb3 = vld1q_f32(b + i + 12);
        sum3 = vmlaq_f32(sum3, va3, vb3);
    }

    // Process remaining 4-float vectors
    for (; i + 3 < dim; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        sum0 = vmlaq_f32(sum0, va, vb);
    }

    float32x4_t sum_all = vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3));
    sum = vaddvq_f32(sum_all);

#elif defined(__AVX2__)
    // 2-way unrolled 256-bit AVX2 loop (16 floats per iteration)
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();

    for (; i + 15 < dim; i += 16) {
        __m256 va0 = _mm256_loadu_ps(a + i);
        __m256 vb0 = _mm256_loadu_ps(b + i);
        acc0 = _mm256_fmadd_ps(va0, vb0, acc0);

        __m256 va1 = _mm256_loadu_ps(a + i + 8);
        __m256 vb1 = _mm256_loadu_ps(b + i + 8);
        acc1 = _mm256_fmadd_ps(va1, vb1, acc1);
    }

    for (; i + 7 < dim; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        acc0 = _mm256_fmadd_ps(va, vb, acc0);
    }

    __m256 acc = _mm256_add_ps(acc0, acc1);
    alignas(32) float tmp[8];
    _mm256_storeu_ps(tmp, acc);
    sum = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];

#endif

    // Remainder scalar loop for tail dimensions
    for (; i < dim; ++i) {
        sum += a[i] * b[i];
    }

    return sum;
}

// Plain scalar reference implementation (for benchmarking comparison)
inline float scalarInnerProduct(const float* a, const float* b, int dim) {
    float sum = 0.0f;
    for (int d = 0; d < dim; ++d) {
        sum += a[d] * b[d];
    }
    return sum;
}

} // namespace simd
