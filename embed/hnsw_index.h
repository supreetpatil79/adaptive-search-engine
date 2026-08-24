#pragma once
// embed/hnsw_index.h
// Hierarchical Navigable Small World (HNSW) approximate nearest-neighbor index.
//
// Algorithm: Malkov & Yashunin 2018 — "Efficient and robust approximate nearest
//   neighbor search using Hierarchical Navigable Small World graphs"
//   https://arxiv.org/abs/1603.09320
//
// Complexity:
//   insert  — O(M · ef_construction · log N)   amortized
//   search  — O(M · ef · log N)                amortized
//   recall  — ≥ 95% @ ef=50, M=16 on standard ANN benchmarks
//
// Usage:
//   HNSWIndex idx(/*dim=*/384, /*M=*/16, /*efConstruction=*/200);
//   idx.insert(docId, vecPtr);        // build-time
//   idx.saveToFile("data/hnsw.bin");
//   idx.loadFromFile("data/hnsw.bin");
//   auto top10 = idx.search(queryVec, 10, /*ef=*/50);

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct HNSWResult {
    int   docId;
    float distance;  // inner-product similarity (higher = more similar)
};

class HNSWIndex {
public:
    // dim    — embedding dimension (e.g. 384 for all-MiniLM-L6-v2)
    // M      — max edges per node per layer (16 = standard default)
    // efC    — ef during construction (higher → better graph, slower build)
    HNSWIndex(int dim = 384, int M = 16, int efConstruction = 200);

    // Insert a single vector. docId must be 1-based (matches InvertedIndex).
    void insert(int docId, const float* vec);

    // ANN search. ef = dynamic candidate list size (≥ topK).
    std::vector<HNSWResult> search(const float* query, int topK, int ef = 50) const;

    // Filtered ANN search with in-graph predicate validation.
    // Dynamically checks candidate docIds during beam search traversal.
    std::vector<HNSWResult> searchFiltered(
        const float* query,
        int topK,
        const std::function<bool(int docId)>& filterPredicate,
        int ef = 50
    ) const;

    // Persistence
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    int  size()  const { return static_cast<int>(nodes_.size()); }
    bool empty() const { return nodes_.empty(); }

private:
    // ── types ──────────────────────────────────────────────────────────────
    struct Node {
        int   docId;
        int   level;               // top layer this node exists in
        std::vector<float> vec;    // embedding (dim_ floats)
        // Adjacency lists per layer: neighbors_[layer] = list of node indices
        std::vector<std::vector<int>> neighbors;
    };

    // ── helpers ────────────────────────────────────────────────────────────
    float  innerProduct(const float* a, const float* b) const;
    float  distance(const float* a, const float* b) const;  // 1 - IP (lower = closer)
    int    randomLevel() const;
    void   selectNeighbors(const std::vector<std::pair<float,int>>& candidates,
                           int maxNeighbors,
                           std::vector<int>& out) const;
    std::vector<std::pair<float,int>>
           searchLayer(const float* query, int entryNode, int ef, int layer) const;

    // ── state ──────────────────────────────────────────────────────────────
    int dim_;
    int M_;           // max connections per layer (upper layers use M, layer 0 uses 2*M)
    int efConstruction_;

    std::vector<Node> nodes_;
    int entryPoint_{-1};
    int maxLevel_{0};
    double mL_;       // level normalisation factor = 1/ln(M)
};
