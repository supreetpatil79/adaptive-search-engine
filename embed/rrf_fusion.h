#ifndef RRF_FUSION_H
#define RRF_FUSION_H

#include "../adaptive/adaptive_ranker.h"  // SearchResult
#include <utility>
#include <vector>

// Reciprocal Rank Fusion (Cormack et al., SIGIR 2009).
//
// Formula:  RRF(d) = Σ_r  1 / (k + rank_r(d))
//
// Where rank_r(d) is the 1-based rank of document d in ranker r's list,
// and k is a constant (default 60) that dampens the impact of high ranks.
//
// Properties:
//   - No score calibration needed — only rank position matters.
//   - Handles documents appearing in only one list (absent from the other
//     list get no contribution from that ranker — not penalised).
//   - k=60 is the empirically validated default from the original paper.
//
// Here we fuse two lists:
//   bm25Results    — from AdaptiveRanker (lexical BM25+TF-IDF)
//   denseResults   — from FlatEmbedIndex (semantic cosine similarity)

struct RRFResult {
    int    docId;
    double rrfScore;
    double bm25Score;   // original BM25 score (for display/debugging)
    double denseScore;  // original cosine similarity (for display/debugging)
    std::string content;
};

class RRFFusion {
public:
    // k: RRF constant (paper recommends 60; higher k = less rank-sensitivity).
    explicit RRFFusion(int k = 60) : k_(k) {}

    // Fuse two ranked lists. bm25Results must include content strings.
    // denseResults are (docId, cosineSim) pairs.
    std::vector<RRFResult> fuse(
        const std::vector<SearchResult>&            bm25Results,
        const std::vector<std::pair<int, float>>&   denseResults,
        int topK = 10) const;

    int k() const { return k_; }

private:
    int k_;
};

#endif // RRF_FUSION_H
