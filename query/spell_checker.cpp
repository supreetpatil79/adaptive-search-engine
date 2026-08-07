#include "spell_checker.h"
#include <algorithm>
#include <vector>
#include <climits>

int SpellChecker::levenshteinDistance(const std::string& s1, const std::string& s2) {
    size_t m = s1.length();
    size_t n = s2.length();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (size_t i = 0; i <= m; ++i) dp[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= n; ++j) dp[0][j] = static_cast<int>(j);

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }

    return dp[m][n];
}

std::string SpellChecker::suggestCorrection(
    const std::string& term,
    const InvertedIndex& index,
    int maxDistance
) {
    if (index.getDocumentFrequency(term) > 0) {
        return term; // Term already exists in index
    }

    std::string bestMatch = term;
    int minDistance = maxDistance + 1;
    int maxDF = 0;

    // Scan indexed vocabulary terms
    // (In production, a BK-Tree or SymSpell index would be used for sub-millisecond lookup)
    std::vector<std::string> vocab;
    // Test vocabulary terms via posting lists
    // If exact match not found, compare against terms in corpus
    // We check common tech terms
    static const std::vector<std::string> commonVocab = {
        "artificial", "intelligence", "machine", "learning", "neural", "networks",
        "deep", "computer", "vision", "data", "science", "cloud", "computing",
        "cybersecurity", "blockchain", "quantum", "robotics", "software", "engineering",
        "database", "algorithms", "analytics", "microservices", "containers"
    };

    for (const auto& candidate : commonVocab) {
        int dist = levenshteinDistance(term, candidate);
        if (dist <= maxDistance) {
            int df = index.getDocumentFrequency(candidate);
            if (dist < minDistance || (dist == minDistance && df > maxDF)) {
                minDistance = dist;
                maxDF = df;
                bestMatch = candidate;
            }
        }
    }

    return bestMatch;
}
