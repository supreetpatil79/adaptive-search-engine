#include "search_engine.h"
#include "../tokenizer/tokenizer.h"

SearchEngine::SearchEngine(int cacheCapacity)
    : cache(cacheCapacity) {}

void SearchEngine::addDocument(int docId, const std::string& content) {
    std::vector<std::string> tokens = Tokenizer::tokenize(content);
    tokens = Tokenizer::removeStopWords(tokens);

    Document doc{docId, content, tokens};
    index.addDocument(doc);
}

void SearchEngine::finalizeIndex() {
    index.finalize();
}

std::vector<SearchResult> SearchEngine::search(const std::string& query, int topK) {
    std::vector<SearchResult> cached;
    if (cache.get(query, cached)) {
        return cached;
    }

    std::vector<SearchResult> results = ranker.search(query, index, topK);

    bool anyBoost = false;
    for (auto& r : results) {
        double boost = userProfile.getBoost(r.docId);
        if (boost > 0.0) {
            r.score *= (1.0 + boost);
            anyBoost = true;
        }
    }
    if (anyBoost) {
        std::sort(results.begin(), results.end());
    }

    cache.put(query, results);
    return results;
}

std::vector<SearchResult> SearchEngine::searchWAND(const std::string& query, int topK, WANDStats* stats) {
    std::vector<std::string> queryTokens = Tokenizer::tokenize(query);
    queryTokens = Tokenizer::removeStopWords(queryTokens);

    std::vector<SearchResult> results = wandScorer.search(queryTokens, index, topK, stats);

    bool anyBoost = false;
    for (auto& r : results) {
        double boost = userProfile.getBoost(r.docId);
        if (boost > 0.0) {
            r.score *= (1.0 + boost);
            anyBoost = true;
        }
    }
    if (anyBoost) {
        std::sort(results.begin(), results.end());
    }

    return results;
}

std::vector<SearchResult> SearchEngine::searchPhrase(const std::string& phraseQuery) {
    std::vector<std::string> phraseTokens = Tokenizer::tokenize(phraseQuery);
    // Don't remove stop words for phrase search to preserve exact sequence!

    std::vector<int> docIds = index.phraseSearch(phraseTokens);
    std::vector<SearchResult> results;

    for (int docId : docIds) {
        const Document* doc = index.getDocument(docId);
        if (doc) {
            results.push_back(SearchResult{docId, 1.0, doc->content});
        }
    }

    return results;
}

void SearchEngine::recordClick(int docId) {
    userProfile.recordClick(docId);
}
