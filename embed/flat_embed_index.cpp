#include "flat_embed_index.h"
#include "../utils/simd_math.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numeric>

bool FlatEmbedIndex::loadFromFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::cerr << "[FlatEmbedIndex] Cannot open: " << path << "\n";
        return false;
    }

    int32_t header[2] = {};
    if (std::fread(header, sizeof(int32_t), 2, f) != 2) {
        std::cerr << "[FlatEmbedIndex] Truncated header in: " << path << "\n";
        std::fclose(f);
        return false;
    }
    numDocs_ = header[0];
    dim_     = header[1];

    if (numDocs_ <= 0 || dim_ <= 0 || dim_ > 4096) {
        std::cerr << "[FlatEmbedIndex] Invalid header: numDocs=" << numDocs_
                  << " dim=" << dim_ << "\n";
        std::fclose(f);
        return false;
    }

    std::size_t total = static_cast<std::size_t>(numDocs_) * dim_;
    data_.resize(total);
    std::size_t read = std::fread(data_.data(), sizeof(float), total, f);
    std::fclose(f);

    if (read != total) {
        std::cerr << "[FlatEmbedIndex] Expected " << total
                  << " floats, got " << read << "\n";
        data_.clear();
        return false;
    }

    std::cout << "[FlatEmbedIndex] Loaded " << numDocs_
              << " × " << dim_ << " embeddings from " << path << "\n";
    return true;
}

std::vector<std::pair<int, float>>
FlatEmbedIndex::search(const float* queryVec, int topK) const {
    if (data_.empty() || queryVec == nullptr) return {};

    // Compute dot products (= cosine sim for unit vectors) using SIMD.
    std::vector<std::pair<float, int>> sims;  // (sim, docId)
    sims.reserve(numDocs_);

    for (int i = 0; i < numDocs_; ++i) {
        const float* row = rowPtr(i);
        float dot = simd::innerProduct(row, queryVec, dim_);
        sims.emplace_back(dot, i + 1);  // docId is 1-based (matches InvertedIndex)
    }


    // Partial sort: only pull the topK to the front — O(N log K) instead of O(N log N).
    int k = std::min(topK, static_cast<int>(sims.size()));
    std::partial_sort(sims.begin(), sims.begin() + k, sims.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<std::pair<int, float>> results;
    results.reserve(k);
    for (int i = 0; i < k; ++i) {
        results.emplace_back(sims[i].second, sims[i].first);
    }
    return results;
}
