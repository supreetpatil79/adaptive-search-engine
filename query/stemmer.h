#ifndef STEMMER_H
#define STEMMER_H

#include <string>

// Porter Stemming Algorithm implementation for search recall enhancement.

class Stemmer {
public:
    // Stems an English word to its root form (e.g., "transformed" -> "transform")
    static std::string stem(const std::string& word);

private:
    static bool isConsonant(const std::string& str, int i);
    static int getMeasure(const std::string& str);
    static bool vowelInStem(const std::string& str);
    static bool doubleConsonant(const std::string& str, int i);
    static bool cvc(const std::string& str, int i);
};

#endif // STEMMER_H
