#include "search_engine.h"
#include "../tokenizer/tokenizer.h"
#include "../query/spell_checker.h"

SearchEngine::SearchEngine(int cacheCapacity)
    : cache(cacheCapacity) {}

void SearchEngine::addDocument(int docId, const std::string& content) {
    std::vector<std::string> tokens = Tokenizer::tokenizeAndStem(content);

    Document doc{docId, content, tokens};
    index.addDocument(doc);
}

void SearchEngine::finalizeIndex() {
    index.finalize();
}

// Spell-correct a query: for each token with 0 hits, substitute the closest
// vocabulary term within edit distance 2.
static std::string spellCorrectQuery(const std::string& query,
                                     const InvertedIndex& index) {
    auto tokens = Tokenizer::tokenize(query);
    tokens      = Tokenizer::removeStopWords(tokens);
    bool changed = false;
    for (auto& tok : tokens) {
        if (index.getDocumentFrequency(tok) == 0) {
            std::string corrected = SpellChecker::suggestCorrection(tok, index, 2);
            if (corrected != tok) {
                tok     = corrected;
                changed = true;
            }
        }
    }
    if (!changed) return query;
    // Reassemble corrected query string
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) out += ' ';
        out += tokens[i];
    }
    return out;
}

std::vector<SearchResult> SearchEngine::search(const std::string& rawQuery, int topK) {
    // 1. Spell-correct
    std::string query = spellCorrectQuery(rawQuery, index);

    // 2. Cache lookup (use corrected query as key)
    std::vector<SearchResult> cached;
    if (cache.get(query, cached)) {
        return cached;
    }

    // 3. Retrieve
    std::vector<SearchResult> results = ranker.search(query, index, topK);

    // 4. Personalisation boost
    bool anyBoost = false;
    for (auto& r : results) {
        double boost = userProfile.getBoost(r.docId);
        if (boost > 0.0) {
            r.score *= (1.0 + boost);
            anyBoost = true;
        }
    }
    if (anyBoost) std::sort(results.begin(), results.end());

    cache.put(query, results);
    return results;
}

std::vector<SearchResult> SearchEngine::searchWAND(const std::string& rawQuery, int topK, WANDStats* stats) {
    std::string query = spellCorrectQuery(rawQuery, index);
    std::vector<std::string> queryTokens = Tokenizer::tokenizeAndStem(query);

    std::vector<SearchResult> results = wandScorer.search(queryTokens, index, topK, stats);

    bool anyBoost = false;
    for (auto& r : results) {
        double boost = userProfile.getBoost(r.docId);
        if (boost > 0.0) {
            r.score *= (1.0 + boost);
            anyBoost = true;
        }
    }
    if (anyBoost) std::sort(results.begin(), results.end());

    return results;
}

std::vector<SearchResult> SearchEngine::searchPhrase(const std::string& phraseQuery) {
    // Do NOT stem or stop-word-remove for phrase queries — preserve exact sequence
    std::vector<std::string> phraseTokens = Tokenizer::tokenize(phraseQuery);

    std::vector<int> docIds = index.phraseSearch(phraseTokens);
    std::vector<SearchResult> results;
    for (int docId : docIds) {
        const Document* doc = index.getDocument(docId);
        if (doc) results.push_back(SearchResult{docId, 1.0, doc->content});
    }
    return results;
}

void SearchEngine::recordClick(int docId) {
    userProfile.recordClick(docId);
}
