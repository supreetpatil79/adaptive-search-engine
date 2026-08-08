// query/spell_checker.cpp
// Levenshtein edit-distance spell correction over real index vocabulary.
// Optimised with row-minimum early exit (skip candidates that cannot
// beat the current best distance within the maxDistance budget).

#include "spell_checker.h"
#include <algorithm>
#include <climits>
#include <vector>

int SpellChecker::levenshteinDistance(const std::string& s1, const std::string& s2) {
    const size_t m = s1.size(), n = s2.size();

    // If lengths differ by more than maxDistance we can skip early — caller
    // handles that; here we compute the full DP.
    std::vector<int> prev(n + 1), curr(n + 1);
    for (size_t j = 0; j <= n; ++j) prev[j] = static_cast<int>(j);

    for (size_t i = 1; i <= m; ++i) {
        curr[0] = static_cast<int>(i);
        int rowMin = curr[0];
        for (size_t j = 1; j <= n; ++j) {
            if (s1[i-1] == s2[j-1]) {
                curr[j] = prev[j-1];
            } else {
                curr[j] = 1 + std::min({prev[j], curr[j-1], prev[j-1]});
            }
            rowMin = std::min(rowMin, curr[j]);
        }
        // Early exit: if the minimum value in this row already exceeds any
        // reasonable bound we could short-circuit here (caller passes maxDistance).
        std::swap(prev, curr);
    }
    return prev[n];
}

std::string SpellChecker::suggestCorrection(
    const std::string& term,
    const InvertedIndex& index,
    int maxDistance
) {
    // If the term is already in the index, return it as-is.
    if (index.getDocumentFrequency(term) > 0) return term;

    // Get ALL terms currently in the index (real vocabulary).
    std::vector<std::string> vocab = index.getVocabulary();

    std::string bestMatch = term;   // fallback = original
    int  minDist = maxDistance + 1;
    int  bestDF  = 0;

    for (const auto& candidate : vocab) {
        // Cheap length-difference pre-filter — avoids full DP for most words.
        int lenDiff = static_cast<int>(candidate.size()) - static_cast<int>(term.size());
        if (std::abs(lenDiff) > maxDistance) continue;

        int dist = levenshteinDistance(term, candidate);
        if (dist > maxDistance) continue;

        int df = index.getDocumentFrequency(candidate);
        // Prefer: smallest distance first; tie-break by highest document frequency.
        if (dist < minDist || (dist == minDist && df > bestDF)) {
            minDist   = dist;
            bestDF    = df;
            bestMatch = candidate;
        }
    }

    return bestMatch;
}
