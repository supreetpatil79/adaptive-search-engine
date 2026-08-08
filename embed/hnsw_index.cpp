// embed/hnsw_index.cpp
// Full HNSW implementation.
// Reference: Malkov & Yashunin 2018, Algorithm 1 (INSERT) + Algorithm 5 (KNN-SEARCH).
//
// Key design choices:
//   - Similarity metric: inner product (cosine-equivalent for unit vectors)
//   - "distance" in the algorithm = 1 - inner_product (lower is better / closer)
//   - Layer 0 stores M*2 connections, upper layers store M connections
//   - Neighbor selection: simple greedy heuristic (sort candidates by distance, pick top-M)
//   - No multi-threading during insert (safe for sequential offline build)
//   - Binary file format: header (magic+version+stats) + node data

#include "hnsw_index.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <queue>
#include <random>
#include <stdexcept>

// ── Constants ────────────────────────────────────────────────────────────────
static constexpr uint32_t HNSW_MAGIC   = 0x48455857u; // "HEXW"
static constexpr uint32_t HNSW_VERSION = 1;

// ── Constructor ──────────────────────────────────────────────────────────────

HNSWIndex::HNSWIndex(int dim, int M, int efConstruction)
    : dim_(dim), M_(M), efConstruction_(efConstruction),
      mL_(1.0 / std::log(static_cast<double>(M))) {}

// ── Distance helpers ─────────────────────────────────────────────────────────

float HNSWIndex::innerProduct(const float* a, const float* b) const {
    float dot = 0.0f;
    for (int d = 0; d < dim_; ++d) dot += a[d] * b[d];
    return dot;
}

// "distance" = 1 - inner_product  (lower = more similar, works with min-heap)
float HNSWIndex::distance(const float* a, const float* b) const {
    return 1.0f - innerProduct(a, b);
}

// ── Level selection ───────────────────────────────────────────────────────────
// Draw from exponential distribution: level = floor(-ln(U[0,1]) * mL_)

int HNSWIndex::randomLevel() const {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    double r = dist(rng);
    if (r == 0.0) r = 1e-10;  // avoid log(0)
    return static_cast<int>(std::floor(-std::log(r) * mL_));
}

// ── Greedy layer search ───────────────────────────────────────────────────────
// Returns at most `ef` candidates from `layer` as (distance, nodeIndex) pairs,
// starting from `entryNode`. Uses a max-heap for the candidate set W and
// a min-heap for the exploration frontier C.

std::vector<std::pair<float,int>>
HNSWIndex::searchLayer(const float* query, int entryNode, int ef, int layer) const {
    // W  = result set (max-heap by distance — furthest at top for easy eviction)
    // C  = candidate set (min-heap by distance — nearest at top for greedy explore)
    using Pair = std::pair<float,int>;  // (distance, nodeIndex)

    auto maxCmp = [](const Pair& a, const Pair& b){ return a.first < b.first; };
    auto minCmp = [](const Pair& a, const Pair& b){ return a.first > b.first; };

    std::priority_queue<Pair, std::vector<Pair>, decltype(maxCmp)> W(maxCmp);
    std::priority_queue<Pair, std::vector<Pair>, decltype(minCmp)> C(minCmp);
    std::vector<bool> visited(nodes_.size(), false);

    float d = distance(query, nodes_[entryNode].vec.data());
    W.emplace(d, entryNode);
    C.emplace(d, entryNode);
    visited[entryNode] = true;

    while (!C.empty()) {
        auto [cDist, cIdx] = C.top(); C.pop();

        // If nearest candidate is further than furthest in W, we're done.
        if (!W.empty() && cDist > W.top().first) break;

        // Explore neighbours of cIdx at this layer
        if (layer < static_cast<int>(nodes_[cIdx].neighbors.size())) {
            for (int nbr : nodes_[cIdx].neighbors[layer]) {
                if (visited[nbr]) continue;
                visited[nbr] = true;
                float nd = distance(query, nodes_[nbr].vec.data());
                if (static_cast<int>(W.size()) < ef || nd < W.top().first) {
                    C.emplace(nd, nbr);
                    W.emplace(nd, nbr);
                    if (static_cast<int>(W.size()) > ef) W.pop();
                }
            }
        }
    }

    std::vector<Pair> results;
    results.reserve(W.size());
    while (!W.empty()) { results.push_back(W.top()); W.pop(); }
    // Sort ascending by distance (closest first)
    std::sort(results.begin(), results.end());
    return results;
}

// ── Neighbor selection ────────────────────────────────────────────────────────
// Simple heuristic: keep the `maxNeighbors` nearest candidates.
// (Full paper uses a diversity heuristic; this is the "simple select" variant.)

void HNSWIndex::selectNeighbors(const std::vector<std::pair<float,int>>& candidates,
                                 int maxNeighbors,
                                 std::vector<int>& out) const {
    out.clear();
    int n = std::min(maxNeighbors, static_cast<int>(candidates.size()));
    out.reserve(n);
    // candidates is already sorted ascending by distance
    for (int i = 0; i < n; ++i) {
        out.push_back(candidates[i].second);
    }
}

// ── INSERT ────────────────────────────────────────────────────────────────────

void HNSWIndex::insert(int docId, const float* vec) {
    // Assign a level for the new node
    int level = randomLevel();

    // Create the node
    Node node;
    node.docId = docId;
    node.level = level;
    node.vec.assign(vec, vec + dim_);
    node.neighbors.resize(level + 1);

    int newIdx = static_cast<int>(nodes_.size());
    nodes_.push_back(std::move(node));

    if (entryPoint_ == -1) {
        // First node — becomes the entry point
        entryPoint_ = newIdx;
        maxLevel_   = level;
        return;
    }

    int ep = entryPoint_;

    // Phase 1: Greedy descent from maxLevel_ down to level+1 (find closest entry)
    for (int lc = maxLevel_; lc > level; --lc) {
        auto W = searchLayer(vec, ep, 1, lc);
        if (!W.empty()) ep = W[0].second;
    }

    // Phase 2: Insert node layer by layer from min(level, maxLevel_) down to 0
    for (int lc = std::min(level, maxLevel_); lc >= 0; --lc) {
        int Mmax = (lc == 0) ? 2 * M_ : M_;

        // Find ef_construction nearest candidates at this layer
        auto W = searchLayer(vec, ep, efConstruction_, lc);

        // Select neighbors for the new node at this layer
        std::vector<int> selected;
        selectNeighbors(W, Mmax, selected);
        nodes_[newIdx].neighbors[lc] = selected;

        // Update bidirectional connections: add newIdx as neighbor of each selected node
        for (int nbr : selected) {
            auto& nbrLayer = nodes_[nbr].neighbors;
            if (lc >= static_cast<int>(nbrLayer.size())) {
                nbrLayer.resize(lc + 1);
            }
            nbrLayer[lc].push_back(newIdx);

            // Prune if too many neighbors
            if (static_cast<int>(nbrLayer[lc].size()) > Mmax) {
                // Rebuild candidates sorted by distance and keep top-Mmax
                std::vector<std::pair<float,int>> cands;
                cands.reserve(nbrLayer[lc].size());
                const float* nbrVec = nodes_[nbr].vec.data();
                for (int n2 : nbrLayer[lc]) {
                    cands.emplace_back(distance(nbrVec, nodes_[n2].vec.data()), n2);
                }
                std::sort(cands.begin(), cands.end());
                std::vector<int> pruned;
                selectNeighbors(cands, Mmax, pruned);
                nbrLayer[lc] = pruned;
            }
        }

        // Advance entry point to closest found in this layer
        if (!W.empty()) ep = W[0].second;
    }

    // Update global entry point if new node has higher level
    if (level > maxLevel_) {
        maxLevel_   = level;
        entryPoint_ = newIdx;
    }
}

// ── SEARCH ───────────────────────────────────────────────────────────────────

std::vector<HNSWResult> HNSWIndex::search(const float* query, int topK, int ef) const {
    if (nodes_.empty() || entryPoint_ == -1) return {};

    int ep = entryPoint_;

    // Greedy descent from maxLevel_ down to layer 1
    for (int lc = maxLevel_; lc > 0; --lc) {
        auto W = searchLayer(query, ep, 1, lc);
        if (!W.empty()) ep = W[0].second;
    }

    // Full search at layer 0
    int ef_actual = std::max(ef, topK);
    auto W = searchLayer(query, ep, ef_actual, 0);

    // Convert to HNSWResult (distance → similarity = 1 - distance)
    int k = std::min(topK, static_cast<int>(W.size()));
    std::vector<HNSWResult> results;
    results.reserve(k);
    for (int i = 0; i < k; ++i) {
        results.push_back({nodes_[W[i].second].docId, 1.0f - W[i].first});
    }
    return results;
}

// ── PERSISTENCE ──────────────────────────────────────────────────────────────
// Binary format:
//   [4B magic][4B version][4B dim][4B M][4B efC][4B numNodes][4B entryPoint][4B maxLevel]
//   For each node:
//     [4B docId][4B level][4B*dim vec]
//     [4B numLayers]
//     For each layer:
//       [4B numNeighbors][4B*numNeighbors neighborIndices]

bool HNSWIndex::saveToFile(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::cerr << "[HNSW] Cannot open for write: " << path << "\n"; return false; }

    auto writeI = [&](int32_t v)  { std::fwrite(&v, 4, 1, f); };
    auto writeU = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto writeF = [&](const std::vector<float>& v) {
        std::fwrite(v.data(), sizeof(float), v.size(), f);
    };

    writeU(HNSW_MAGIC);
    writeU(HNSW_VERSION);
    writeI(dim_);
    writeI(M_);
    writeI(efConstruction_);
    writeI(static_cast<int32_t>(nodes_.size()));
    writeI(entryPoint_);
    writeI(maxLevel_);

    for (const auto& n : nodes_) {
        writeI(n.docId);
        writeI(n.level);
        writeF(n.vec);
        writeI(static_cast<int32_t>(n.neighbors.size()));
        for (const auto& layer : n.neighbors) {
            writeI(static_cast<int32_t>(layer.size()));
            for (int nb : layer) writeI(nb);
        }
    }

    std::fclose(f);
    std::cout << "[HNSW] Saved " << nodes_.size() << " nodes to " << path << "\n";
    return true;
}

bool HNSWIndex::loadFromFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::cerr << "[HNSW] Cannot open: " << path << "\n"; return false; }

    auto readI = [&]() -> int32_t  { int32_t v; std::fread(&v, 4, 1, f); return v; };
    auto readU = [&]() -> uint32_t { uint32_t v; std::fread(&v, 4, 1, f); return v; };

    uint32_t magic = readU();
    if (magic != HNSW_MAGIC) {
        std::cerr << "[HNSW] Bad magic in " << path << "\n";
        std::fclose(f); return false;
    }
    uint32_t ver = readU();
    if (ver != HNSW_VERSION) {
        std::cerr << "[HNSW] Unsupported version " << ver << "\n";
        std::fclose(f); return false;
    }

    dim_            = readI();
    M_              = readI();
    efConstruction_ = readI();
    mL_             = 1.0 / std::log(static_cast<double>(M_));
    int32_t numNodes = readI();
    entryPoint_     = readI();
    maxLevel_       = readI();

    nodes_.clear();
    nodes_.resize(numNodes);

    for (int i = 0; i < numNodes; ++i) {
        auto& n = nodes_[i];
        n.docId = readI();
        n.level = readI();
        n.vec.resize(dim_);
        std::fread(n.vec.data(), sizeof(float), dim_, f);
        int32_t numLayers = readI();
        n.neighbors.resize(numLayers);
        for (int l = 0; l < numLayers; ++l) {
            int32_t numNbs = readI();
            n.neighbors[l].resize(numNbs);
            for (int j = 0; j < numNbs; ++j) n.neighbors[l][j] = readI();
        }
    }

    std::fclose(f);
    std::cout << "[HNSW] Loaded " << numNodes << " nodes (dim=" << dim_
              << ", M=" << M_ << ") from " << path << "\n";
    return true;
}
