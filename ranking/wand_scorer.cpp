#include "wand_scorer.h"
#include "../ranking/tfidf.h"
#include <algorithm>
#include <queue>
#include <climits>

std::vector<SearchResult> WANDScorer::search(
    const std::vector<std::string>& queryTokens,
    const InvertedIndex& index,
    int topK,
    WANDStats* stats
) const {
    if (queryTokens.empty() || topK <= 0) return {};

    std::vector<TermIterator> terms;
    terms.reserve(queryTokens.size());

    int totalDocs = index.getTotalDocuments();
    double avgDocLen = index.getAverageDocLength();

    for (const auto& token : queryTokens) {
        const auto* postings = index.getPostings(token);
        if (postings && !postings->empty()) {
            TermIterator ti;
            ti.term = token;
            ti.postings = postings;
            ti.skipList = index.getSkipList(token);
            ti.currentIdx = 0;
            ti.maxScore = index.getMaxTermScore(token);
            terms.push_back(ti);
        }
    }

    if (terms.empty()) return {};

    size_t numTerms = terms.size();

    // Min-heap for keeping track of top K scores (score, docId)
    using HeapItem = std::pair<double, int>;
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> minHeap;

    double threshold = 0.0;

    // Helper: insertion sort terms by currentDocId (extremely fast for 2-8 elements)
    auto sortTermsByDocId = [&terms, numTerms]() {
        for (size_t i = 1; i < numTerms; ++i) {
            TermIterator key = terms[i];
            int keyDocId = key.currentDocId();
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && terms[j].currentDocId() > keyDocId) {
                terms[j + 1] = terms[j];
                j--;
            }
            terms[j + 1] = key;
        }
    };

    while (true) {
        sortTermsByDocId();

        int minDocId = terms[0].currentDocId();
        if (minDocId == INT_MAX) break;

        // Sum upper bounds until threshold is exceeded
        double accumMaxScore = 0.0;
        size_t pivotIdx = 0;

        while (pivotIdx < numTerms && accumMaxScore <= threshold) {
            accumMaxScore += terms[pivotIdx].maxScore;
            pivotIdx++;
        }

        if (accumMaxScore <= threshold) break;

        pivotIdx--; // 0-indexed pivot
        int pivotDocId = terms[pivotIdx].currentDocId();
        if (pivotDocId == INT_MAX) break;

        if (minDocId == pivotDocId) {
            // Full evaluation of minDocId
            if (stats) {
                stats->fullEvaluations++;
                stats->totalCandidatesEvaluated++;
            }

            double score = 0.0;
            int docLength = index.getDocLength(minDocId);

            for (size_t i = 0; i < numTerms; ++i) {
                if (terms[i].currentDocId() == minDocId) {
                    int tf = (*terms[i].postings)[terms[i].currentIdx].tf;
                    int df = index.getDocumentFrequency(terms[i].term);

                    double bm25Score = bm25Scorer.score(tf, df, totalDocs, docLength, avgDocLen);
                    double tfidfScore = TFIDF::score(tf, df, totalDocs, docLength);

                    score += (0.7 * bm25Score + 0.3 * tfidfScore);
                    terms[i].currentIdx++;
                }
            }

            if (score > 0.0) {
                if (static_cast<int>(minHeap.size()) < topK) {
                    minHeap.push({score, minDocId});
                    if (static_cast<int>(minHeap.size()) == topK) {
                        threshold = minHeap.top().first;
                    }
                } else if (score > threshold) {
                    minHeap.pop();
                    minHeap.push({score, minDocId});
                    threshold = minHeap.top().first;
                }
            }
        } else {
            // Advance terms using skip lists up to pivotDocId
            if (stats) stats->totalCandidatesSkipped++;
            for (size_t i = 0; i < pivotIdx; ++i) {
                terms[i].skipTo(pivotDocId);
            }
        }
    }

    std::vector<SearchResult> results;
    results.reserve(minHeap.size());

    while (!minHeap.empty()) {
        auto [score, docId] = minHeap.top();
        minHeap.pop();

        const Document* doc = index.getDocument(docId);
        if (doc) {
            results.push_back(SearchResult{docId, score, doc->content});
        }
    }

    std::sort(results.begin(), results.end());
    return results;
}
