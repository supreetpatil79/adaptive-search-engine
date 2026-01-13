#include "search_engine.h"
#include <cmath>
#include <algorithm>

vector<SearchResult> SearchEngine::search(const string& query) {
    // 1. Check Cache first (Standard MAANG Optimization)
    if (cache.exists(query)) {
        return cache.get(query);
    }

    auto tokens = tokenizer::tokenize(query);
    unordered_map<int, double> docScores; 

    // 2. Retrieval Phase
    for (const auto& token : tokens) {
        // getPostings should return vector<pair<int, int>> -> {docID, freqInDoc}
        auto postings = index.getPostings(token);
        int df = postings.size(); // Document Frequency for this token

        for (auto& p : postings) {
            int docId = p.first;
            int tf = p.second; // Pre-calculated raw term frequency

            // Calculate BM25 score for this specific term-doc pair
            double termScore = bm25.score(
                tf, 
                df, 
                index.getTotalDocs(), 
                index.getDocLength(docId), 
                index.getAvgDocLength()
            );
            
            docScores[docId] += termScore;
        }
    }

    // 3. Adaptive Ranking Phase (The "Different" Part)
    vector<SearchResult> results;
    for (auto const& [docId, baseScore] : docScores) {
        // Get user signals (Dwell time and Clicks)
        double clickScore = user ? user->getClickScore(docId) : 0.0;
        double dwellScore = user ? user->getDwellScore(docId) : 0.0;

        // AdaptiveRanker combines BM25 with behavioral signals
        double finalScore = AdaptiveRanker::score(baseScore, clickScore, dwellScore);
        
        results.push_back({docId, finalScore});
    }

    // 4. Sort and Return
    sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    });

    cache.put(query, results);
    return results;
}