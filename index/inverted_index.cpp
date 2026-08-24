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

namespace {
constexpr uint32_t IIDX_MAGIC = 0x49494458u; // "IIDX"
constexpr uint32_t IIDX_VERSION = 1;
}

bool InvertedIndex::saveToFile(const std::string& filepath) const {
    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    uint32_t header[4] = {
        IIDX_MAGIC,
        IIDX_VERSION,
        static_cast<uint32_t>(documents.size()),
        static_cast<uint32_t>(postingsMap.size())
    };
    if (std::fwrite(header, sizeof(uint32_t), 4, f) != 4) {
        std::fclose(f);
        return false;
    }

    // 1. Documents
    for (const auto& [id, doc] : documents) {
        int32_t docId = id;
        uint32_t contentLen = static_cast<uint32_t>(doc.content.size());
        int32_t docLen = getDocLength(docId);
        uint32_t tokensCount = static_cast<uint32_t>(doc.tokens.size());

        std::fwrite(&docId, sizeof(int32_t), 1, f);
        std::fwrite(&docLen, sizeof(int32_t), 1, f);
        std::fwrite(&contentLen, sizeof(uint32_t), 1, f);
        if (contentLen > 0) {
            std::fwrite(doc.content.data(), sizeof(char), contentLen, f);
        }
        std::fwrite(&tokensCount, sizeof(uint32_t), 1, f);
        for (const auto& t : doc.tokens) {
            uint16_t tlen = static_cast<uint16_t>(t.size());
            std::fwrite(&tlen, sizeof(uint16_t), 1, f);
            if (tlen > 0) std::fwrite(t.data(), sizeof(char), tlen, f);
        }
    }

    // 2. Postings & Inverted index
    for (const auto& [term, postings] : postingsMap) {
        uint16_t termLen = static_cast<uint16_t>(term.size());
        double maxScore = getMaxTermScore(term);
        uint32_t numPostings = static_cast<uint32_t>(postings.size());

        std::fwrite(&termLen, sizeof(uint16_t), 1, f);
        if (termLen > 0) std::fwrite(term.data(), sizeof(char), termLen, f);
        std::fwrite(&maxScore, sizeof(double), 1, f);
        std::fwrite(&numPostings, sizeof(uint32_t), 1, f);

        for (const auto& p : postings) {
            int32_t pDocId = p.docId;
            int32_t pTf = p.tf;
            uint32_t numPos = static_cast<uint32_t>(p.positions.size());

            std::fwrite(&pDocId, sizeof(int32_t), 1, f);
            std::fwrite(&pTf, sizeof(int32_t), 1, f);
            std::fwrite(&numPos, sizeof(uint32_t), 1, f);
            if (numPos > 0) {
                std::fwrite(p.positions.data(), sizeof(int), numPos, f);
            }
        }

        // Skip pointers
        const auto* skipList = getSkipList(term);
        uint32_t numSkip = skipList ? static_cast<uint32_t>(skipList->size()) : 0;
        std::fwrite(&numSkip, sizeof(uint32_t), 1, f);
        if (numSkip > 0) {
            for (const auto& sp : *skipList) {
                int32_t spDocId = sp.docId;
                uint64_t spIdx = static_cast<uint64_t>(sp.index);
                std::fwrite(&spDocId, sizeof(int32_t), 1, f);
                std::fwrite(&spIdx, sizeof(uint64_t), 1, f);
            }
        }
    }

    std::fclose(f);
    return true;
}

bool InvertedIndex::loadFromFile(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    uint32_t header[4] = {};
    if (std::fread(header, sizeof(uint32_t), 4, f) != 4 || header[0] != IIDX_MAGIC || header[1] != IIDX_VERSION) {
        std::fclose(f);
        return false;
    }

    uint32_t numDocs = header[2];
    uint32_t numTerms = header[3];

    documents.clear();
    docLengths.clear();
    postingsMap.clear();
    skipListMap.clear();
    maxTermScores.clear();
    index.clear();
    termFrequencies.clear();
    avgDocLenValid_ = false;

    // 1. Documents
    for (uint32_t i = 0; i < numDocs; ++i) {
        int32_t docId = 0;
        int32_t docLen = 0;
        uint32_t contentLen = 0;
        uint32_t tokensCount = 0;

        if (std::fread(&docId, sizeof(int32_t), 1, f) != 1 ||
            std::fread(&docLen, sizeof(int32_t), 1, f) != 1 ||
            std::fread(&contentLen, sizeof(uint32_t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }

        std::string content(contentLen, '\0');
        if (contentLen > 0) {
            if (std::fread(&content[0], sizeof(char), contentLen, f) != contentLen) {
                std::fclose(f);
                return false;
            }
        }

        if (std::fread(&tokensCount, sizeof(uint32_t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }

        std::vector<std::string> tokens;
        tokens.reserve(tokensCount);
        for (uint32_t t = 0; t < tokensCount; ++t) {
            uint16_t tlen = 0;
            if (std::fread(&tlen, sizeof(uint16_t), 1, f) != 1) {
                std::fclose(f);
                return false;
            }
            std::string tok(tlen, '\0');
            if (tlen > 0) {
                if (std::fread(&tok[0], sizeof(char), tlen, f) != tlen) {
                    std::fclose(f);
                    return false;
                }
            }
            tokens.push_back(std::move(tok));
        }

        documents[docId] = Document{docId, std::move(content), std::move(tokens)};
        docLengths[docId] = docLen;
    }

    // 2. Postings
    for (uint32_t i = 0; i < numTerms; ++i) {
        uint16_t termLen = 0;
        if (std::fread(&termLen, sizeof(uint16_t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        std::string term(termLen, '\0');
        if (termLen > 0) {
            if (std::fread(&term[0], sizeof(char), termLen, f) != termLen) {
                std::fclose(f);
                return false;
            }
        }

        double maxScore = 0.0;
        uint32_t numPostings = 0;
        if (std::fread(&maxScore, sizeof(double), 1, f) != 1 ||
            std::fread(&numPostings, sizeof(uint32_t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        maxTermScores[term] = maxScore;

        auto& pvec = postingsMap[term];
        pvec.resize(numPostings);

        for (uint32_t p = 0; p < numPostings; ++p) {
            int32_t pDocId = 0;
            int32_t pTf = 0;
            uint32_t numPos = 0;

            if (std::fread(&pDocId, sizeof(int32_t), 1, f) != 1 ||
                std::fread(&pTf, sizeof(int32_t), 1, f) != 1 ||
                std::fread(&numPos, sizeof(uint32_t), 1, f) != 1) {
                std::fclose(f);
                return false;
            }

            pvec[p].docId = pDocId;
            pvec[p].tf = pTf;
            pvec[p].positions.resize(numPos);

            if (numPos > 0) {
                if (std::fread(pvec[p].positions.data(), sizeof(int), numPos, f) != numPos) {
                    std::fclose(f);
                    return false;
                }
            }

            index[term].insert(pDocId);
            termFrequencies[term][pDocId] = pTf;
        }

        uint32_t numSkip = 0;
        if (std::fread(&numSkip, sizeof(uint32_t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        if (numSkip > 0) {
            auto& svec = skipListMap[term];
            svec.resize(numSkip);
            for (uint32_t s = 0; s < numSkip; ++s) {
                int32_t spDocId = 0;
                uint64_t spIdx = 0;
                if (std::fread(&spDocId, sizeof(int32_t), 1, f) != 1 ||
                    std::fread(&spIdx, sizeof(uint64_t), 1, f) != 1) {
                    std::fclose(f);
                    return false;
                }
                svec[s] = SkipPointer{spDocId, static_cast<size_t>(spIdx)};
            }
        }
    }

    std::fclose(f);
    return true;
}