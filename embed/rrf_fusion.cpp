#include "rrf_fusion.h"
#include <algorithm>
#include <unordered_map>

std::vector<RRFResult> RRFFusion::fuse(
    const std::vector<SearchResult>&           bm25Results,
    const std::vector<std::pair<int, float>>&  denseResults,
    int topK) const
{
    // Map docId → accumulated RRF score + metadata.
    std::unordered_map<int, RRFResult> acc;

    // Reserve capacity to avoid rehashing.
    acc.reserve(bm25Results.size() + denseResults.size());

    // ── BM25 list contribution ─────────────────────────────────────────────
    for (int rank = 0; rank < static_cast<int>(bm25Results.size()); ++rank) {
        const SearchResult& r = bm25Results[rank];
        double rrf = 1.0 / (k_ + rank + 1);   // rank is 0-based; formula is 1-based

        auto& entry = acc[r.docId];
        entry.docId     = r.docId;
        entry.rrfScore += rrf;
        entry.bm25Score = r.score;
        entry.content   = r.content;
    }

    // ── Dense list contribution ────────────────────────────────────────────
    for (int rank = 0; rank < static_cast<int>(denseResults.size()); ++rank) {
        int   docId = denseResults[rank].first;
        float sim   = denseResults[rank].second;
        double rrf  = 1.0 / (k_ + rank + 1);

        auto& entry = acc[docId];
        entry.docId      = docId;
        entry.rrfScore  += rrf;
        entry.denseScore = static_cast<double>(sim);
        // content is already set if the doc was also in bm25Results;
        // if it's dense-only, content will be empty (caller can fill from index).
    }

    // ── Sort by RRF score descending ───────────────────────────────────────
    std::vector<RRFResult> results;
    results.reserve(acc.size());
    for (auto& [id, r] : acc) {
        results.push_back(std::move(r));
    }

    std::sort(results.begin(), results.end(),
              [](const RRFResult& a, const RRFResult& b) {
                  return a.rrfScore > b.rrfScore;
              });

    if (static_cast<int>(results.size()) > topK) {
        results.resize(topK);
    }
    return results;
}
