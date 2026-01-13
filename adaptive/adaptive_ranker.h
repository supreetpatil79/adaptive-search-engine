#ifndef ADAPTIVE_RANKER_H
#define ADAPTIVE_RANKER_H

#include "inverted_index.h"
#include "bm25.h"
#include "tfidf.h"
#include <vector>
#include <string>

struct SearchResult {
    int docId;
    double score;
    std::string content;
    
    bool operator<(const SearchResult& other) const {
        return score > other.score;  // Higher scores first
    }
};

class AdaptiveRanker {
public:
    AdaptiveRanker() : bm25Ranker(1.5, 0.75) {}
    
    // Search using adaptive ranking (combines BM25 and TF-IDF)
    std::vector<SearchResult> search(
        const std::string& query,
        const InvertedIndex& index,
        int topK = 10
    );
    
private:
    BM25 bm25Ranker;
    
    // Calculate combined score
    double calculateScore(
        const std::vector<std::string>& queryTokens,
        int docId,
        const InvertedIndex& index
    );
};

#endif // ADAPTIVE_RANKER_H