#include "stemmer.h"
#include <algorithm>
#include <cctype>

bool Stemmer::isConsonant(const std::string& str, int i) {
    char c = str[i];
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return false;
    if (c == 'y') return (i == 0) ? true : !isConsonant(str, i - 1);
    return true;
}

int Stemmer::getMeasure(const std::string& str) {
    int n = 0;
    int i = 0;
    int len = static_cast<int>(str.length());

    while (true) {
        if (i >= len) return n;
        if (!isConsonant(str, i)) break;
        i++;
    }
    i++;
    while (true) {
        while (true) {
            if (i >= len) return n;
            if (isConsonant(str, i)) break;
            i++;
        }
        i++;
        n++;
        while (true) {
            if (i >= len) return n;
            if (!isConsonant(str, i)) break;
            i++;
        }
        i++;
    }
}

bool Stemmer::vowelInStem(const std::string& str) {
    for (int i = 0; i < static_cast<int>(str.length()); ++i) {
        if (!isConsonant(str, i)) return true;
    }
    return false;
}

bool Stemmer::doubleConsonant(const std::string& str, int i) {
    if (i < 1) return false;
    if (str[i] != str[i - 1]) return false;
    return isConsonant(str, i);
}

bool Stemmer::cvc(const std::string& str, int i) {
    if (i < 2 || !isConsonant(str, i) || isConsonant(str, i - 1) || !isConsonant(str, i - 2)) return false;
    char ch = str[i];
    if (ch == 'w' || ch == 'x' || ch == 'y') return false;
    return true;
}

std::string Stemmer::stem(const std::string& word) {
    if (word.length() <= 2) return word;

    std::string w = word;
    for (auto& c : w) c = static_cast<char>(std::tolower(c));

    // Step 1a
    if (w.rfind("sses") == w.length() - 4) w = w.substr(0, w.length() - 2);
    else if (w.rfind("ies") == w.length() - 3) w = w.substr(0, w.length() - 2);
    else if (w.rfind("ss") == w.length() - 2) {}
    else if (w.rfind("s") == w.length() - 1) w = w.substr(0, w.length() - 1);

    // Step 1b
    bool step1bExtra = false;
    if (w.rfind("eed") == w.length() - 3) {
        std::string stemPart = w.substr(0, w.length() - 3);
        if (getMeasure(stemPart) > 0) w = stemPart + "ee";
    } else if (w.rfind("ed") == w.length() - 2) {
        std::string stemPart = w.substr(0, w.length() - 2);
        if (vowelInStem(stemPart)) { w = stemPart; step1bExtra = true; }
    } else if (w.rfind("ing") == w.length() - 3) {
        std::string stemPart = w.substr(0, w.length() - 3);
        if (vowelInStem(stemPart)) { w = stemPart; step1bExtra = true; }
    }

    if (step1bExtra) {
        if (w.rfind("at") == w.length() - 2 || w.rfind("bl") == w.length() - 2 || w.rfind("iz") == w.length() - 2) {
            w += "e";
        } else if (doubleConsonant(w, static_cast<int>(w.length()) - 1)) {
            char last = w.back();
            if (last != 'l' && last != 's' && last != 'z') w.pop_back();
        } else if (getMeasure(w) == 1 && cvc(w, static_cast<int>(w.length()) - 1)) {
            w += "e";
        }
    }

    // Step 1c
    if (w.rfind("y") == w.length() - 1) {
        std::string stemPart = w.substr(0, w.length() - 1);
        if (vowelInStem(stemPart)) w = stemPart + "i";
    }

    return w;
}
