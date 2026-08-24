// embed/sq8_index.cpp
// Implementation of SQ8 scalar quantized embedding index with SIMD-accelerated ADC.

#include "sq8_index.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numeric>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

namespace {
constexpr uint32_t SQ8_MAGIC = 0x53513831u; // "SQ81"
constexpr uint32_t SQ8_VERSION = 1;

// SIMD-accelerated dot product between float32 query and uint8 quantized vector
inline float dotQueryQuantized(const float* q, const uint8_t* d, int dim) {
    float sum = 0.0f;
    int i = 0;

#if defined(__ARM_NEON) || defined(__aarch64__)
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);

    for (; i + 8 <= dim; i += 8) {
        // Load 8 uint8 values
        uint8x8_t d_raw = vld1_u8(d + i);
        uint16x8_t d_u16 = vmovl_u8(d_raw);

        // Convert low 4 to float32
        uint32x4_t d_u32_0 = vmovl_u16(vget_low_u16(d_u16));
        float32x4_t d_f0 = vcvtq_f32_u32(d_u32_0);
        float32x4_t q_f0 = vld1q_f32(q + i);
        acc0 = vmlaq_f32(acc0, q_f0, d_f0);

        // Convert high 4 to float32
        uint32x4_t d_u32_1 = vmovl_u16(vget_high_u16(d_u16));
        float32x4_t d_f1 = vcvtq_f32_u32(d_u32_1);
        float32x4_t q_f1 = vld1q_f32(q + i + 4);
        acc1 = vmlaq_f32(acc1, q_f1, d_f1);
    }
    float32x4_t acc = vaddq_f32(acc0, acc1);
    sum = vaddvq_f32(acc);

#elif defined(__AVX2__)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 8 <= dim; i += 8) {
        __m128i d_raw = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(d + i));
        __m256i d_i32 = _mm256_cvtepu8_epi32(d_raw);
        __m256 d_f = _mm256_cvtepi32_ps(d_i32);
        __m256 q_f = _mm256_loadu_ps(q + i);
        acc = _mm256_fmadd_ps(q_f, d_f, acc);
    }
    __m128 low = _mm256_castps256_ps128(acc);
    __m128 high = _mm256_extractf128_ps(acc, 1);
    __m128 combined = _mm_add_ps(low, high);
    combined = _mm_hadd_ps(combined, combined);
    combined = _mm_hadd_ps(combined, combined);
    sum = _mm_cvtss_f32(combined);
#endif

    // Scalar remainder
    for (; i < dim; ++i) {
        sum += q[i] * static_cast<float>(d[i]);
    }
    return sum;
}
} // anonymous namespace

SQ8Index::SQ8Index(int dim) : dim_(dim) {}

void SQ8Index::add(int docId, const float* vector) {
    if (vector == nullptr || dim_ <= 0) return;

    float minVal = vector[0];
    float maxVal = vector[0];
    for (int i = 1; i < dim_; ++i) {
        if (vector[i] < minVal) minVal = vector[i];
        if (vector[i] > maxVal) maxVal = vector[i];
    }

    float range = maxVal - minVal;
    float scale = (range > 1e-8f) ? (range / 255.0f) : 1.0f;
    float invScale = 1.0f / scale;

    QuantizedVector qv;
    qv.docId = docId;
    qv.minVal = minVal;
    qv.scale = scale;
    qv.quantized.resize(dim_);

    for (int i = 0; i < dim_; ++i) {
        float normalized = (vector[i] - minVal) * invScale;
        int clamped = static_cast<int>(std::round(normalized));
        if (clamped < 0) clamped = 0;
        if (clamped > 255) clamped = 255;
        qv.quantized[i] = static_cast<uint8_t>(clamped);
    }

    vectors_.push_back(std::move(qv));
}

void SQ8Index::buildFromFlat(const float* data, int numDocs, int dim) {
    dim_ = dim;
    vectors_.clear();
    vectors_.reserve(numDocs);

    for (int i = 0; i < numDocs; ++i) {
        add(i + 1, data + static_cast<size_t>(i) * dim_);
    }
}

std::vector<std::pair<int, float>> SQ8Index::search(const float* queryVec, int topK) const {
    if (vectors_.empty() || queryVec == nullptr || topK <= 0) return {};

    // Precompute query sum for ADC
    float querySum = 0.0f;
    for (int i = 0; i < dim_; ++i) {
        querySum += queryVec[i];
    }

    std::vector<std::pair<float, int>> scores; // (score, docId)
    scores.reserve(vectors_.size());

    for (const auto& qv : vectors_) {
        float dotQD = dotQueryQuantized(queryVec, qv.quantized.data(), dim_);
        float reconstructedDot = qv.minVal * querySum + qv.scale * dotQD;
        scores.emplace_back(reconstructedDot, qv.docId);
    }

    int k = std::min(topK, static_cast<int>(scores.size()));
    std::partial_sort(scores.begin(), scores.begin() + k, scores.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<std::pair<int, float>> results;
    results.reserve(k);
    for (int i = 0; i < k; ++i) {
        results.emplace_back(scores[i].second, scores[i].first);
    }
    return results;
}

bool SQ8Index::saveToFile(const std::string& filepath) const {
    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    uint32_t header[4] = {SQ8_MAGIC, SQ8_VERSION, static_cast<uint32_t>(vectors_.size()), static_cast<uint32_t>(dim_)};
    if (std::fwrite(header, sizeof(uint32_t), 4, f) != 4) {
        std::fclose(f);
        return false;
    }

    for (const auto& qv : vectors_) {
        int32_t docId = qv.docId;
        float meta[2] = {qv.minVal, qv.scale};
        if (std::fwrite(&docId, sizeof(int32_t), 1, f) != 1 ||
            std::fwrite(meta, sizeof(float), 2, f) != 2 ||
            std::fwrite(qv.quantized.data(), sizeof(uint8_t), dim_, f) != static_cast<size_t>(dim_)) {
            std::fclose(f);
            return false;
        }
    }

    std::fclose(f);
    return true;
}

bool SQ8Index::loadFromFile(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    uint32_t header[4] = {};
    if (std::fread(header, sizeof(uint32_t), 4, f) != 4 || header[0] != SQ8_MAGIC || header[1] != SQ8_VERSION) {
        std::fclose(f);
        return false;
    }

    uint32_t numDocs = header[2];
    dim_ = static_cast<int>(header[3]);
    vectors_.clear();
    vectors_.reserve(numDocs);

    for (uint32_t i = 0; i < numDocs; ++i) {
        QuantizedVector qv;
        int32_t docId = 0;
        float meta[2] = {0.0f, 0.0f};

        if (std::fread(&docId, sizeof(int32_t), 1, f) != 1 ||
            std::fread(meta, sizeof(float), 2, f) != 2) {
            std::fclose(f);
            return false;
        }

        qv.docId = docId;
        qv.minVal = meta[0];
        qv.scale = meta[1];
        qv.quantized.resize(dim_);

        if (std::fread(qv.quantized.data(), sizeof(uint8_t), dim_, f) != static_cast<size_t>(dim_)) {
            std::fclose(f);
            return false;
        }
        vectors_.push_back(std::move(qv));
    }

    std::fclose(f);
    return true;
}

size_t SQ8Index::memoryBytes() const {
    size_t total = sizeof(SQ8Index);
    for (const auto& v : vectors_) {
        total += sizeof(QuantizedVector) + v.quantized.capacity();
    }
    return total;
}
