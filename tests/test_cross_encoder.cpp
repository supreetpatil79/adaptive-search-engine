// tests/test_cross_encoder.cpp
// Unit tests for Stage-2 Cross-Encoder neural re-ranker.
// Run: ./build/test_cross_encoder

#include "../ranking/cross_encoder.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define ASSERT_TRUE(expr)                                             \
    do {                                                              \
        if (!(expr)) {                                                \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  " << #expr << "\n";                       \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

#define ASSERT_EQ(got, expected)                                      \
    do {                                                              \
        if ((got) != (expected)) {                                    \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  got " << (got) << " != expected "        \
                      << (expected) << "\n";                          \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

void test_cross_encoder_scoring_and_reranking() {
    CrossEncoder ce;

    std::string query = "quantum computing optimization";

    std::string highlyRelevant = "Quantum computing principles enable quadratic speedups for combinatorial optimization problems.";
    std::string marginal = "Optimization algorithms in classical computing can be accelerated.";
    std::string irrelevant = "The weather forecast predicts heavy rain and thunderstorms.";

    float scoreHigh = ce.scorePair(query, highlyRelevant);
    float scoreMarg = ce.scorePair(query, marginal);
    float scoreIrrel = ce.scorePair(query, irrelevant);

    ASSERT_TRUE(scoreHigh > scoreMarg);
    ASSERT_TRUE(scoreMarg > scoreIrrel);

    // Test re-ranking candidate pool where L1 ranking was imperfect
    std::vector<SearchResult> candidates = {
        {1, 2.5, irrelevant},       // High lexical noise score
        {2, 3.0, marginal},         // Medium score
        {3, 2.8, highlyRelevant}    // True best match
    };

    auto reranked = ce.rerank(query, candidates, 3);
    ASSERT_EQ(reranked.size(), 3);
    // Highly relevant document #3 must be promoted to rank 1!
    ASSERT_EQ(reranked[0].docId, 3);
    ASSERT_TRUE(reranked[0].score > reranked[1].score);

    std::cout << "test_cross_encoder_scoring_and_reranking PASSED\n";
}

void test_cross_encoder_edge_cases() {
    CrossEncoder ce;

    ASSERT_EQ(ce.scorePair("", "some doc"), 0.0f);
    ASSERT_EQ(ce.scorePair("query", ""), 0.0f);

    auto resEmpty = ce.rerank("", {}, 5);
    ASSERT_TRUE(resEmpty.empty());

    std::cout << "test_cross_encoder_edge_cases PASSED\n";
}

int main() {
    std::cout << "=== Cross-Encoder Re-ranker Unit Tests ===\n\n";

    test_cross_encoder_scoring_and_reranking();
    test_cross_encoder_edge_cases();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
