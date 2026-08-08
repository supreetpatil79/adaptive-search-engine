#pragma once
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include <sstream>

class Tokenizer {
public:
    // Tokenise raw text into lowercase alpha tokens
    static std::vector<std::string> tokenize(const std::string& text);

    // Lowercase + strip non-alpha
    static std::string normalize(const std::string& text);

    // Remove common English stop words
    static std::vector<std::string> removeStopWords(const std::vector<std::string>& tokens);

    // Tokenise, remove stop words, then Porter-stem each token.
    // Use this for query processing and document indexing to improve recall.
    static std::vector<std::string> tokenizeAndStem(const std::string& text);

private:
    static std::set<std::string> getStopWords();
};