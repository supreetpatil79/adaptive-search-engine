// tests/test_prefix_trie.cpp
// Unit tests for PrefixTrie query autocomplete and suggestion engine.
// Run: ./build/test_prefix_trie

#include "../query/prefix_trie.h"
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

void test_prefix_trie_suggestions_and_frequency() {
    PrefixTrie trie;

    trie.insert("machine", 10);
    trie.insert("machine learning", 50);
    trie.insert("macbook pro", 20);
    trie.insert("macroeconomics", 5);
    trie.insert("deep learning", 100);

    // Prefix "mac" -> should return "machine learning", "macbook pro", "machine", "macroeconomics"
    auto sugg = trie.suggest("mac", 3);
    ASSERT_EQ(sugg.size(), 3);

    // Highest frequency must come first
    ASSERT_EQ(sugg[0].text, "machine learning");
    ASSERT_EQ(sugg[0].frequency, 50);

    ASSERT_EQ(sugg[1].text, "macbook pro");
    ASSERT_EQ(sugg[1].frequency, 20);

    ASSERT_EQ(sugg[2].text, "machine");
    ASSERT_EQ(sugg[2].frequency, 10);

    // Prefix "deep"
    auto suggDeep = trie.suggest("deep", 5);
    ASSERT_EQ(suggDeep.size(), 1);
    ASSERT_EQ(suggDeep[0].text, "deep learning");

    // Missing prefix
    auto suggMissing = trie.suggest("quantum", 5);
    ASSERT_TRUE(suggMissing.empty());

    std::cout << "test_prefix_trie_suggestions_and_frequency PASSED\n";
}

void test_prefix_trie_case_insensitivity_and_edge_cases() {
    PrefixTrie trie;

    trie.insert("Artificial Intelligence", 30);
    trie.insert("art gallery", 5);

    // Case-insensitive query "art"
    auto res = trie.suggest("art", 5);
    ASSERT_EQ(res.size(), 2);
    ASSERT_EQ(res[0].text, "Artificial Intelligence");

    // Empty query
    auto empty = trie.suggest("", 5);
    ASSERT_TRUE(empty.empty());

    // 0 topK
    auto zeroK = trie.suggest("art", 0);
    ASSERT_TRUE(zeroK.empty());

    std::cout << "test_prefix_trie_case_insensitivity_and_edge_cases PASSED\n";
}

int main() {
    std::cout << "=== PrefixTrie Autocomplete Unit Tests ===\n\n";

    test_prefix_trie_suggestions_and_frequency();
    test_prefix_trie_case_insensitivity_and_edge_cases();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
