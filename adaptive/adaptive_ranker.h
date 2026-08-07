#ifndef ADAPTIVE_RANKER_H
#define ADAPTIVE_RANKER_H

#include "../index/inverted_index.h"
#include "../ranking/bm25.h"
#include "../ranking/tfidf.h"
#include <string>
#include <vector>

// Canonical result type — used by AdaptiveRanker and SearchEngine.
// search_engine.h must NOT redeclare this.
struct SearchResult {
    int         docId;
    double      score;
    std::string content;

    // Higher score sorts first (for std::sort on a vector<SearchResult>).
    bool operator<(const SearchResult& other) const {
        return score > other.score;
    }
};

class AdaptiveRanker {
public:
    AdaptiveRanker() : bm25Ranker(1.5, 0.75) {}

    // Search the index for the top-K documents matching query.
    // Combines BM25 (70%) and TF-IDF (30%) scores.
    std::vector<SearchResult> search(const std::string&  query,
                                     const InvertedIndex& index,
                                     int                  topK = 10) const;

private:
    BM25 bm25Ranker;

    double calculateScore(const std::vector<std::string>& queryTokens,
                          int                              docId,
                          const InvertedIndex&             index) const;
};

#endif // ADAPTIVE_RANKER_H