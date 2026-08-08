#pragma once
#include <string>

// Full Porter Stemmer (Steps 1a–5b).
// Usage: Stemmer::stem("generalization") → "general"
class Stemmer {
public:
    static std::string stem(const std::string& word);

    // Exposed for use by stemmer.cpp static helpers and unit tests
    static bool isConsonant(const std::string& s, int i);
    static int  getMeasure(const std::string& s);
    static bool vowelInStem(const std::string& s);
    static bool doubleConsonant(const std::string& s, int i);
    static bool cvc(const std::string& s, int i);
};
