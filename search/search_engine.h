#pragma once

#include "../index/inverted_index.h"
#include "../adaptive/adaptive_ranker.h"
#include "../ranking/wand_scorer.h"
#include "../adaptive/user_profile.h"
#include "../cache/lru_cache.h"
#include <string>
#include <vector>

class SearchEngine {
public:
    explicit SearchEngine(int cacheCapacity = 128);

    // Index a document.
    void addDocument(int docId, const std::string& content);

    // Finalize index (build skip lists, precalculate WAND upper bounds)
    void finalizeIndex();

    // Standard unpruned search (or cache backed)
    std::vector<SearchResult> search(const std::string& query, int topK = 10);

    // WAND top-k pruned search
    std::vector<SearchResult> searchWAND(const std::string& query, int topK = 10, WANDStats* stats = nullptr);

    // Phrase search using positional index
    std::vector<SearchResult> searchPhrase(const std::string& phraseQuery);

    // Record user click for personalisation boost
    void recordClick(int docId);

    int totalDocs() const { return index.getTotalDocuments(); }

    const InvertedIndex& getIndex() const { return index; }

private:
    InvertedIndex                                  index;
    AdaptiveRanker                                 ranker;
    WANDScorer                                     wandScorer;
    UserProfile                                    userProfile;
    LRUCache<std::string, std::vector<SearchResult>> cache;
};
