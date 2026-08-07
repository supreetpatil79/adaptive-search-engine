#ifndef SPELL_CHECKER_H
#define SPELL_CHECKER_H

#include "../index/inverted_index.h"
#include <string>
#include <vector>

// SpellChecker using Levenshtein Edit Distance over index vocabulary.

class SpellChecker {
public:
    SpellChecker() = default;

    // Calculate edit distance between two strings
    static int levenshteinDistance(const std::string& s1, const std::string& s2);

    // Suggest closest vocabulary term for a misspelled query term
    static std::string suggestCorrection(
        const std::string& term,
        const InvertedIndex& index,
        int maxDistance = 2
    );
};

#endif // SPELL_CHECKER_H
