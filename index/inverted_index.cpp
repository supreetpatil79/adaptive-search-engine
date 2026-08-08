#include "inverted_index.h"
#include "../ranking/bm25.h"
#include <algorithm>
#include <cmath>

void InvertedIndex::addDocument(const Document& doc) {
    documents[doc.id] = doc;
    docLengths[doc.id] = static_cast<int>(doc.tokens.size());
    avgDocLenValid_ = false;

    std::unordered_map<std::string, std::vector<int>> termPositions;

    // Track positional occurrences of each term
    for (int pos = 0; pos < static_cast<int>(doc.tokens.size()); ++pos) {
        termPositions[doc.tokens[pos]].push_back(pos);
    }

    // Update postings map, index, and termFrequencies
    for (auto& [term, positions] : termPositions) {
        int tf = static_cast<int>(positions.size());

        index[term].insert(doc.id);
        termFrequencies[term][doc.id] = tf;

        postingsMap[term].push_back(Posting{doc.id, tf, std::move(positions)});
    }
}

void InvertedIndex::finalize() {
    BM25 bm25Scorer(1.5, 0.75);
    int totalDocs = getTotalDocuments();
    double avgDocLen = getAverageDocLength();

    for (auto& [term, postings] : postingsMap) {
        // Ensure postings are sorted by docId
        std::sort(postings.begin(), postings.end(), [](const Posting& a, const Posting& b) {
            return a.docId < b.docId;
        });

        // Build skip pointers every SKIP_INTERVAL elements
        auto& skipList = skipListMap[term];
        skipList.clear();
        for (size_t i = 0; i < postings.size(); i += SKIP_INTERVAL) {
            skipList.push_back(SkipPointer{postings[i].docId, i});
        }

        // Calculate maximum BM25 upper bound score for this term across all docs
        int df = static_cast<int>(postings.size());
        double maxScore = 0.0;
        for (const auto& p : postings) {
            int docLen = getDocLength(p.docId);
            double s = bm25Scorer.score(p.tf, df, totalDocs, docLen, avgDocLen);
            if (s > maxScore) {
                maxScore = s;
            }
        }
        maxTermScores[term] = maxScore;
    }
}

std::set<int> InvertedIndex::find(const std::string& term) const {
    auto it = index.find(term);
    if (it != index.end()) {
        return it->second;
    }
    return std::set<int>();
}

const std::vector<Posting>* InvertedIndex::getPostings(const std::string& term) const {
    auto it = postingsMap.find(term);
    if (it != postingsMap.end()) {
        return &(it->second);
    }
    return nullptr;
}

const std::vector<SkipPointer>* InvertedIndex::getSkipList(const std::string& term) const {
    auto it = skipListMap.find(term);
    if (it != skipListMap.end()) {
        return &(it->second);
    }
    return nullptr;
}

double InvertedIndex::getMaxTermScore(const std::string& term) const {
    auto it = maxTermScores.find(term);
    if (it != maxTermScores.end()) {
        return it->second;
    }
    return 0.0;
}

int InvertedIndex::getTermFrequency(const std::string& term, int docId) const {
    auto termIt = termFrequencies.find(term);
    if (termIt != termFrequencies.end()) {
        auto docIt = termIt->second.find(docId);
        if (docIt != termIt->second.end()) {
            return docIt->second;
        }
    }
    return 0;
}

int InvertedIndex::getDocumentFrequency(const std::string& term) const {
    auto it = postingsMap.find(term);
    if (it != postingsMap.end()) {
        return static_cast<int>(it->second.size());
    }
    return 0;
}

std::vector<int> InvertedIndex::getPositions(const std::string& term, int docId) const {
    const auto* postings = getPostings(term);
    if (postings) {
        auto it = std::lower_bound(postings->begin(), postings->end(), docId,
            [](const Posting& p, int id) { return p.docId < id; });
        if (it != postings->end() && it->docId == docId) {
            return it->positions;
        }
    }
    return {};
}

std::vector<int> InvertedIndex::phraseSearch(const std::vector<std::string>& phraseTokens) const {
    if (phraseTokens.empty()) return {};

    std::vector<const std::vector<Posting>*> tokenPostings;
    tokenPostings.reserve(phraseTokens.size());
    for (const auto& t : phraseTokens) {
        const auto* p = getPostings(t);
        if (!p || p->empty()) return {};
        tokenPostings.push_back(p);
    }

    const auto* firstPostings = tokenPostings[0];
    std::vector<int> result;

    for (const auto& p0 : *firstPostings) {
        int docId = p0.docId;
        bool matchInDoc = false;

        std::vector<const std::vector<int>*> tokenDocPositions;
        tokenDocPositions.reserve(phraseTokens.size());
        tokenDocPositions.push_back(&p0.positions);

        bool allTokensInDoc = true;
        for (size_t i = 1; i < phraseTokens.size(); ++i) {
            const auto& plist = *tokenPostings[i];
            auto it = std::lower_bound(plist.begin(), plist.end(), docId,
                [](const Posting& p, int id) { return p.docId < id; });
            if (it == plist.end() || it->docId != docId) {
                allTokensInDoc = false;
                break;
            }
            tokenDocPositions.push_back(&(it->positions));
        }

        if (!allTokensInDoc) continue;

        for (int pos0 : p0.positions) {
            bool phraseMatches = true;

            for (size_t i = 1; i < phraseTokens.size(); ++i) {
                int targetPos = pos0 + static_cast<int>(i);
                const auto& posVec = *tokenDocPositions[i];
                if (!std::binary_search(posVec.begin(), posVec.end(), targetPos)) {
                    phraseMatches = false;
                    break;
                }
            }

            if (phraseMatches) {
                matchInDoc = true;
                break;
            }
        }

        if (matchInDoc) {
            result.push_back(docId);
        }
    }

    return result;
}

const Document* InvertedIndex::getDocument(int docId) const {
    auto it = documents.find(docId);
    if (it != documents.end()) {
        return &(it->second);
    }
    return nullptr;
}

double InvertedIndex::getAverageDocLength() const {
    if (documents.empty()) return 0.0;
    if (avgDocLenValid_) return cachedAvgDocLen_;

    double total = 0.0;
    for (const auto& [id, len] : docLengths) {
        total += len;
    }

    cachedAvgDocLen_ = total / documents.size();
    avgDocLenValid_ = true;
    return cachedAvgDocLen_;
}

int InvertedIndex::getDocLength(int docId) const {
    auto it = docLengths.find(docId);
    if (it != docLengths.end()) {
        return it->second;
    }
    return 0;
}

std::vector<std::string> InvertedIndex::getVocabulary() const {
    std::vector<std::string> vocab;
    vocab.reserve(postingsMap.size());
    for (const auto& [term, _] : postingsMap) {
        vocab.push_back(term);
    }
    return vocab;
}

// Merge all posting data from 'other' directly — no re-tokenisation.
// Called by SegmentBuilder to combine thread-local segments efficiently.
void InvertedIndex::mergePostingsFrom(const InvertedIndex& other) {
    avgDocLenValid_ = false;
    // Merge document metadata
    for (const auto& [id, doc] : other.documents) {
        documents[id] = doc;
        docLengths[id] = other.getDocLength(id);
    }

    // Merge posting lists, legacy docId sets, and term frequencies
    for (const auto& [term, otherPostings] : other.postingsMap) {
        auto& myPostings = postingsMap[term];
        for (const auto& p : otherPostings) {
            myPostings.push_back(p);
            index[term].insert(p.docId);
            termFrequencies[term][p.docId] = p.tf;
        }
    }
}