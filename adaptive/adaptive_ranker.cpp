#include "adaptive_ranker.h"
#include "../tokenizer/tokenizer.h"
#include <algorithm>
#include <set>

double AdaptiveRanker::calculateScore(
    const std::vector<std::string>& queryTokens,
    int docId,
    const InvertedIndex& index
) {
    double bm25Score = 0.0;
    double tfidfScore = 0.0;
    
    int totalDocs = index.getTotalDocuments();
    int docLength = index.getDocLength(docId);
    
    for (const auto& token : queryTokens) {
        int tf = index.getTermFrequency(token, docId);
        int df = index.getDocumentFrequency(token);
        
        if (tf > 0) {
            // Calculate BM25 score
            bm25Score += bm25Ranker.score(tf, df, totalDocs, docId);
            
            // Calculate TF-IDF score
            tfidfScore += TFIDF::score(tf, df, totalDocs, docLength);
        }
    }
    
    // Adaptive combination: 70% BM25, 30% TF-IDF
    return 0.7 * bm25Score + 0.3 * tfidfScore;
}

std::vector<SearchResult> AdaptiveRanker::search(
    const std::string& query,
    const InvertedIndex& index,
    int topK
) {
    // Tokenize query
    std::vector<std::string> queryTokens = Tokenizer::tokenize(query);
    queryTokens = Tokenizer::removeStopWords(queryTokens);
    
    if (queryTokens.empty()) {
        return std::vector<SearchResult>();
    }
    
    // Find all documents containing at least one query term
    std::set<int> candidateDocs;
    for (const auto& token : queryTokens) {
        std::set<int> docs = index.find(token);
        candidateDocs.insert(docs.begin(), docs.end());
    }
    
    // Calculate scores for all candidate documents
    std::vector<SearchResult> results;
    for (int docId : candidateDocs) {
        double score = calculateScore(queryTokens, docId, index);
        
        const Document* doc = index.getDocument(docId);
        if (doc && score > 0.0) {
            SearchResult result;
            result.docId = docId;
            result.score = score;
            result.content = doc->content;
            results.push_back(result);
        }
    }
    
    // Sort by score (descending)
    std::sort(results.begin(), results.end());
    
    // Return top K results
    if (results.size() > static_cast<size_t>(topK)) {
        results.resize(topK);
    }
    
    return results;
}