#ifndef WAND_SCORER_H
#define WAND_SCORER_H

#include "../index/inverted_index.h"
#include "../ranking/bm25.h"
#include "../adaptive/adaptive_ranker.h"
#include <vector>
#include <string>

struct WANDStats {
    size_t totalCandidatesEvaluated = 0;
    size_t totalCandidatesSkipped = 0;
    size_t fullEvaluations = 0;
};

class WANDScorer {
public:
    WANDScorer() : bm25Scorer(1.5, 0.75) {}

    // Search top-K documents using WAND pruning algorithm with skip pointers
    std::vector<SearchResult> search(
        const std::vector<std::string>& queryTokens,
        const InvertedIndex& index,
        int topK,
        WANDStats* stats = nullptr
    ) const;

private:
    BM25 bm25Scorer;

    struct TermIterator {
        std::string term;
        const std::vector<Posting>* postings = nullptr;
        const std::vector<SkipPointer>* skipList = nullptr;
        size_t currentIdx = 0;
        double maxScore = 0.0;

        int currentDocId() const {
            if (postings && currentIdx < postings->size()) {
                return (*postings)[currentIdx].docId;
            }
            return 2147483647; // INT_MAX sentinel
        }

        // Fast advance to docId >= targetDocId using skip pointers & binary search
        void skipTo(int targetDocId) {
            if (!postings || currentIdx >= postings->size()) return;

            // Use skip pointers if available
            if (skipList && !skipList->empty()) {
                auto skipIt = std::lower_bound(
                    skipList->begin(), skipList->end(), targetDocId,
                    [](const SkipPointer& sp, int val) { return sp.docId < val; }
                );
                if (skipIt != skipList->begin()) {
                    --skipIt;
                    if (skipIt->index > currentIdx) {
                        currentIdx = skipIt->index;
                    }
                }
            }

            // Binary search within posting list from currentIdx
            auto it = std::lower_bound(
                postings->begin() + currentIdx, postings->end(), targetDocId,
                [](const Posting& p, int val) { return p.docId < val; }
            );
            currentIdx = std::distance(postings->begin(), it);
        }
    };
};

#endif // WAND_SCORER_H
