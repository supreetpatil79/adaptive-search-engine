// tests/test_stemmer.cpp
// Unit tests for the 5-step Porter Stemmer.
// Run: ./build/test_stemmer

#include "../query/stemmer.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define ASSERT_EQ(got, expected)                                            \
    do {                                                                    \
        std::string g = (got), e = (expected);                             \
        if (g != e) {                                                       \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__           \
                      << "  stem(\"" << e << "\") → \"" << g              \
                      << "\"  expected \"" << e << "\"\n";                 \
            ++failed;                                                       \
        } else {                                                            \
            ++passed;                                                       \
        }                                                                   \
    } while (false)

int main() {
    std::cout << "=== Porter Stemmer Unit Tests ===\n\n";

    // ── Step 1a ──────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("caresses"),  "caress");
    ASSERT_EQ(Stemmer::stem("ponies"),    "poni");
    ASSERT_EQ(Stemmer::stem("cats"),      "cat");

    // ── Step 1b ──────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("agreed"),    "agre");   // eed suffix
    ASSERT_EQ(Stemmer::stem("matting"),   "mat");
    ASSERT_EQ(Stemmer::stem("mating"),    "mate");
    ASSERT_EQ(Stemmer::stem("meeting"),   "meet");
    ASSERT_EQ(Stemmer::stem("running"),   "run");
    ASSERT_EQ(Stemmer::stem("sized"),     "size");

    // ── Step 1c ──────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("happy"),     "happi");
    ASSERT_EQ(Stemmer::stem("sky"),       "sky");    // single char before y — no change

    // ── Step 2 ───────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("relational"),  "relat");
    ASSERT_EQ(Stemmer::stem("conditional"), "condit");
    ASSERT_EQ(Stemmer::stem("hopeful"),     "hope");
    ASSERT_EQ(Stemmer::stem("goodness"),    "good");

    // ── Step 3 ───────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("triplicate"),  "triplic");
    ASSERT_EQ(Stemmer::stem("formative"),   "form");
    ASSERT_EQ(Stemmer::stem("hopeful"),     "hope");

    // ── Step 4 ───────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("revival"),     "reviv");
    ASSERT_EQ(Stemmer::stem("allowance"),   "allow");
    ASSERT_EQ(Stemmer::stem("inference"),   "infer");
    ASSERT_EQ(Stemmer::stem("airliner"),    "airlin");
    ASSERT_EQ(Stemmer::stem("adjustable"),  "adjust");

    // ── Step 5 ───────────────────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem("probate"),     "probat");
    ASSERT_EQ(Stemmer::stem("cease"),       "ceas");

    // ── Short / edge cases ───────────────────────────────────────────────────
    ASSERT_EQ(Stemmer::stem(""),            "");      // empty
    ASSERT_EQ(Stemmer::stem("a"),           "a");     // single char
    ASSERT_EQ(Stemmer::stem("the"),         "the");   // common word unchanged

    // ── Regression — key NLP tokens from the corpus ──────────────────────────
    // Porter correctly: generalization → general (step2: -ation→ate) → gener (step4: -al)
    ASSERT_EQ(Stemmer::stem("generalization"), "gener");
    ASSERT_EQ(Stemmer::stem("algorithms"),     "algorithm");
    ASSERT_EQ(Stemmer::stem("learning"),       "learn");
    ASSERT_EQ(Stemmer::stem("searching"),      "search");
    ASSERT_EQ(Stemmer::stem("databases"),      "databas");

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
