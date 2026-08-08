// query/stemmer.cpp — Full Porter Stemmer (Steps 1a–5b)
// Reference: M.F. Porter, "An algorithm for suffix stripping", Program 14(3) 1980
#include "stemmer.h"
#include <algorithm>
#include <cctype>

// ── Internal helpers ──────────────────────────────────────────────────────

bool Stemmer::isConsonant(const std::string& s, int i) {
    char c = s[i];
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return false;
    if (c == 'y') return (i == 0) ? true : !isConsonant(s, i - 1);
    return true;
}

// measure m = number of VC sequences in s
int Stemmer::getMeasure(const std::string& s) {
    int n = 0, i = 0, len = static_cast<int>(s.length());
    while (i < len && isConsonant(s, i)) ++i;   // skip leading C*
    while (i < len) {
        while (i < len && !isConsonant(s, i)) ++i; // skip V+
        while (i < len &&  isConsonant(s, i)) ++i; // skip C+
        ++n;
    }
    return n;
}

bool Stemmer::vowelInStem(const std::string& s) {
    for (int i = 0; i < static_cast<int>(s.length()); ++i)
        if (!isConsonant(s, i)) return true;
    return false;
}

bool Stemmer::doubleConsonant(const std::string& s, int i) {
    return i >= 1 && s[i] == s[i-1] && isConsonant(s, i);
}

bool Stemmer::cvc(const std::string& s, int i) {
    if (i < 2) return false;
    if (!isConsonant(s, i) || isConsonant(s, i-1) || !isConsonant(s, i-2)) return false;
    char c = s[i];
    return c != 'w' && c != 'x' && c != 'y';
}

// ── Suffix helpers ────────────────────────────────────────────────────────

static bool endsWith(const std::string& w, const std::string& suf) {
    if (w.size() < suf.size()) return false;
    return w.compare(w.size() - suf.size(), suf.size(), suf) == 0;
}

// If w ends with suf and measure(stem) > minM, replace suffix with rep.
static bool replaceIfM(std::string& w, const std::string& suf,
                       const std::string& rep, int minM) {
    if (!endsWith(w, suf)) return false;
    std::string stem = w.substr(0, w.size() - suf.size());
    if (Stemmer::getMeasure(stem) > minM) {
        w = stem + rep;
        return true;
    }
    return false;
}

// ── Public API ────────────────────────────────────────────────────────────

std::string Stemmer::stem(const std::string& word) {
    if (word.size() <= 2) return word;

    std::string w = word;
    for (auto& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // ── Step 1a ───────────────────────────────────────────────────────────
    if      (endsWith(w, "sses")) { w.erase(w.size()-2); }
    else if (endsWith(w, "ies"))  { w.erase(w.size()-2); }
    else if (endsWith(w, "ss"))   { /* no-op */ }
    else if (endsWith(w, "s"))    { w.pop_back(); }

    // ── Step 1b ───────────────────────────────────────────────────────────
    bool extra = false;
    if (endsWith(w, "eed")) {
        std::string stem = w.substr(0, w.size()-3);
        if (getMeasure(stem) > 0) w = stem + "ee";
    } else if (endsWith(w, "ed")) {
        std::string stem = w.substr(0, w.size()-2);
        if (vowelInStem(stem)) { w = stem; extra = true; }
    } else if (endsWith(w, "ing")) {
        std::string stem = w.substr(0, w.size()-3);
        if (vowelInStem(stem)) { w = stem; extra = true; }
    }
    if (extra) {
        if      (endsWith(w,"at") || endsWith(w,"bl") || endsWith(w,"iz")) { w += 'e'; }
        else if (doubleConsonant(w, static_cast<int>(w.size())-1)) {
            char last = w.back();
            if (last!='l' && last!='s' && last!='z') w.pop_back();
        } else if (getMeasure(w)==1 && cvc(w, static_cast<int>(w.size())-1)) {
            w += 'e';
        }
    }

    // ── Step 1c ───────────────────────────────────────────────────────────
    if (endsWith(w, "y") && vowelInStem(w.substr(0, w.size()-1))) {
        w.back() = 'i';
    }

    // ── Step 2 ────────────────────────────────────────────────────────────
    // All require m > 0
    if      (replaceIfM(w, "ational", "ate",  0)) {}
    else if (replaceIfM(w, "tional",  "tion", 0)) {}
    else if (replaceIfM(w, "enci",    "ence", 0)) {}
    else if (replaceIfM(w, "anci",    "ance", 0)) {}
    else if (replaceIfM(w, "izer",    "ize",  0)) {}
    else if (replaceIfM(w, "abli",    "able", 0)) {}
    else if (replaceIfM(w, "alli",    "al",   0)) {}
    else if (replaceIfM(w, "entli",   "ent",  0)) {}
    else if (replaceIfM(w, "eli",     "e",    0)) {}
    else if (replaceIfM(w, "ousli",   "ous",  0)) {}
    else if (replaceIfM(w, "ization", "ize",  0)) {}
    else if (replaceIfM(w, "ation",   "ate",  0)) {}
    else if (replaceIfM(w, "ator",    "ate",  0)) {}
    else if (replaceIfM(w, "alism",   "al",   0)) {}
    else if (replaceIfM(w, "iveness", "ive",  0)) {}
    else if (replaceIfM(w, "fulness", "ful",  0)) {}
    else if (replaceIfM(w, "ousness", "ous",  0)) {}
    else if (replaceIfM(w, "aliti",   "al",   0)) {}
    else if (replaceIfM(w, "iviti",   "ive",  0)) {}
    else if (replaceIfM(w, "biliti",  "ble",  0)) {}

    // ── Step 3 ────────────────────────────────────────────────────────────
    if      (replaceIfM(w, "icate", "ic",  0)) {}
    else if (replaceIfM(w, "ative", "",    0)) {}
    else if (replaceIfM(w, "alize", "al",  0)) {}
    else if (replaceIfM(w, "iciti", "ic",  0)) {}
    else if (replaceIfM(w, "ical",  "ic",  0)) {}
    else if (replaceIfM(w, "ful",   "",    0)) {}
    else if (replaceIfM(w, "ness",  "",    0)) {}

    // ── Step 4 ────────────────────────────────────────────────────────────
    // All require m > 1
    if      (replaceIfM(w, "al",    "", 1)) {}
    else if (replaceIfM(w, "ance",  "", 1)) {}
    else if (replaceIfM(w, "ence",  "", 1)) {}
    else if (replaceIfM(w, "er",    "", 1)) {}
    else if (replaceIfM(w, "ic",    "", 1)) {}
    else if (replaceIfM(w, "able",  "", 1)) {}
    else if (replaceIfM(w, "ible",  "", 1)) {}
    else if (replaceIfM(w, "ant",   "", 1)) {}
    else if (replaceIfM(w, "ement", "", 1)) {}
    else if (replaceIfM(w, "ment",  "", 1)) {}
    else if (replaceIfM(w, "ent",   "", 1)) {}
    else if (endsWith(w, "ion")) {
        std::string stem = w.substr(0, w.size()-3);
        if (getMeasure(stem) > 1) {
            char last = stem.empty() ? '\0' : stem.back();
            if (last == 's' || last == 't') w = stem;
        }
    }
    else if (replaceIfM(w, "ou",    "", 1)) {}
    else if (replaceIfM(w, "ism",   "", 1)) {}
    else if (replaceIfM(w, "ate",   "", 1)) {}
    else if (replaceIfM(w, "iti",   "", 1)) {}
    else if (replaceIfM(w, "ous",   "", 1)) {}
    else if (replaceIfM(w, "ive",   "", 1)) {}
    else if (replaceIfM(w, "ize",   "", 1)) {}

    // ── Step 5a ───────────────────────────────────────────────────────────
    if (endsWith(w, "e")) {
        std::string stem = w.substr(0, w.size()-1);
        int m = getMeasure(stem);
        if (m > 1 || (m == 1 && !cvc(stem, static_cast<int>(stem.size())-1))) {
            w = stem;
        }
    }

    // ── Step 5b ───────────────────────────────────────────────────────────
    if (endsWith(w, "ll") && getMeasure(w) > 1) {
        w.pop_back();
    }

    return w;
}
