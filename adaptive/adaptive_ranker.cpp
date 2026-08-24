#include "adaptive_ranker.h"
#include "../tokenizer/tokenizer.h"
#include <algorithm>
#include <set>

double AdaptiveRanker::calculateScore(const std::vector<std::string>& queryTokens,
                                      int                              docId,
                                      const InvertedIndex&             index) const {
    double bm25Score  = 0.0;
    double tfidfScore = 0.0;

    int    totalDocs  = index.getTotalDocuments();
    int    docLength  = index.getDocLength(docId);
    double avgDocLen  = index.getAverageDocLength();

    for (const auto& token : queryTokens) {
        int tf = index.getTermFrequency(token, docId);
        int df = index.getDocumentFrequency(token);

        if (tf > 0 && df > 0) {
            // Fixed: was passing docId where docLength is required.
            bm25Score  += bm25Ranker.score(tf, df, totalDocs, docLength, avgDocLen);
            tfidfScore += TFIDF::score(tf, df, totalDocs, docLength);
        }
    }

    // Adaptive combination: 70% BM25, 30% TF-IDF
    return 0.7 * bm25Score + 0.3 * tfidfScore;
}

std::vector<SearchResult> AdaptiveRanker::search(const std::string&  query,
                                                  const InvertedIndex& index,
                                                  int                  topK) const {
    // Must use tokenizeAndStem — the index was built with stemmed tokens.
    // Using plain tokenize() would produce term mismatches (e.g. "running" vs "run").
    std::vector<std::string> queryTokens = Tokenizer::tokenizeAndStem(query);

    if (queryTokens.empty()) return {};

    // Union posting lists to gather candidate document IDs.
    std::set<int> candidateDocs;
    for (const auto& token : queryTokens) {
        std::set<int> posting = index.find(token);
        candidateDocs.insert(posting.begin(), posting.end());
    }

    std::vector<SearchResult> results;
    results.reserve(candidateDocs.size());

    for (int docId : candidateDocs) {
        double s = calculateScore(queryTokens, docId, index);
        const Document* doc = index.getDocument(docId);
        if (doc && s > 0.0) {
            results.push_back({docId, s, doc->content});
        }
    }

    std::sort(results.begin(), results.end());  // operator< sorts descending by score

    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }

    return results;
}