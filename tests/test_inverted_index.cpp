// tests/test_inverted_index.cpp
// Comprehensive unit tests for InvertedIndex.
// Tests: posting lists, skip pointers, phrase search, term frequency,
// document frequency, max term scores, vocabulary extraction, segment merging.
// Run: ./build/test_inverted_index

#include "../index/inverted_index.h"
#include "../utils/vbyte.h"
#include <algorithm>
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

void test_basic_indexing_and_lookup() {
    InvertedIndex index;
    Document d1{1, "the quick brown fox", {"the", "quick", "brown", "fox"}};
    Document d2{2, "jumped over the lazy dog", {"jumped", "over", "the", "lazy", "dog"}};
    Document d3{3, "fox and dog are friends", {"fox", "and", "dog", "are", "friend"}};

    index.addDocument(d1);
    index.addDocument(d2);
    index.addDocument(d3);
    index.finalize();

    ASSERT_EQ(index.getTotalDocuments(), 3);
    ASSERT_EQ(index.getDocumentFrequency("the"), 2);
    ASSERT_EQ(index.getDocumentFrequency("fox"), 2);
    ASSERT_EQ(index.getDocumentFrequency("quick"), 1);
    ASSERT_EQ(index.getDocumentFrequency("nonexistent"), 0);

    ASSERT_EQ(index.getTermFrequency("the", 1), 1);
    ASSERT_EQ(index.getTermFrequency("fox", 3), 1);
    ASSERT_EQ(index.getTermFrequency("fox", 2), 0);

    auto foxDocs = index.find("fox");
    ASSERT_TRUE(foxDocs.count(1) == 1);
    ASSERT_TRUE(foxDocs.count(3) == 1);
    ASSERT_TRUE(foxDocs.count(2) == 0);

    std::cout << "test_basic_indexing_and_lookup PASSED\n";
}

void test_phrase_search() {
    InvertedIndex index;
    // doc 1 has "machine learning is powerful"
    Document d1{1, "machine learning is powerful", {"machin", "learn", "is", "power"}};
    // doc 2 has "learning machine is different"
    Document d2{2, "learning machine is different", {"learn", "machin", "is", "differ"}};
    // doc 3 has "deep learning and machine learning systems", {"deep", "learn", "and", "machin", "learn", "system"}
    Document d3{3, "deep learning and machine learning systems", {"deep", "learn", "and", "machin", "learn", "system"}};

    index.addDocument(d1);
    index.addDocument(d2);
    index.addDocument(d3);
    index.finalize();

    // Search for phrase: "machin learn" -> should match doc 1 and doc 3 (at pos 3,4)
    auto res1 = index.phraseSearch({"machin", "learn"});
    ASSERT_EQ(res1.size(), 2);
    ASSERT_TRUE(res1[0] == 1 || res1[0] == 3);
    ASSERT_TRUE(res1[1] == 1 || res1[1] == 3);

    // Search for phrase: "learn machin" -> should match doc 2 (at pos 0,1)
    auto res2 = index.phraseSearch({"learn", "machin"});
    ASSERT_EQ(res2.size(), 1);
    ASSERT_EQ(res2[0], 2);

    // Search for 3-word phrase: "machin learn is" -> only doc 1
    auto res3 = index.phraseSearch({"machin", "learn", "is"});
    ASSERT_EQ(res3.size(), 1);
    ASSERT_EQ(res3[0], 1);

    // Non-existent phrase
    auto res4 = index.phraseSearch({"machin", "differ"});
    ASSERT_EQ(res4.size(), 0);

    // Single term phrase
    auto res5 = index.phraseSearch({"differ"});
    ASSERT_EQ(res5.size(), 1);
    ASSERT_EQ(res5[0], 2);

    // Empty phrase
    auto res6 = index.phraseSearch({});
    ASSERT_EQ(res6.size(), 0);

    std::cout << "test_phrase_search PASSED\n";
}

void test_skip_pointers_and_wand_scores() {
    InvertedIndex index;
    for (int i = 1; i <= 25; ++i) {
        Document d{i, "common term and unique_" + std::to_string(i), {"common", "term", "and", "unique_" + std::to_string(i)}};
        index.addDocument(d);
    }
    index.finalize();

    const auto* skipList = index.getSkipList("common");
    ASSERT_TRUE(skipList != nullptr);
    // 25 documents with SKIP_INTERVAL = 8 -> skip pointers at 0, 8, 16, 24 = 4 pointers
    ASSERT_TRUE(skipList->size() >= 3);

    double maxScore = index.getMaxTermScore("common");
    ASSERT_TRUE(maxScore > 0.0);

    // Check positions
    auto pos = index.getPositions("common", 1);
    ASSERT_EQ(pos.size(), 1);
    ASSERT_EQ(pos[0], 0);

    std::cout << "test_skip_pointers_and_wand_scores PASSED\n";
}

void test_segment_merging() {
    InvertedIndex seg1;
    Document d1{1, "alpha beta", {"alpha", "beta"}};
    Document d2{2, "beta gamma", {"beta", "gamma"}};
    seg1.addDocument(d1);
    seg1.addDocument(d2);

    InvertedIndex seg2;
    Document d3{3, "gamma delta", {"gamma", "delta"}};
    Document d4{4, "alpha delta", {"alpha", "delta"}};
    seg2.addDocument(d3);
    seg2.addDocument(d4);

    InvertedIndex merged;
    merged.mergePostingsFrom(seg1);
    merged.mergePostingsFrom(seg2);
    merged.finalize();

    ASSERT_EQ(merged.getTotalDocuments(), 4);
    ASSERT_EQ(merged.getDocumentFrequency("alpha"), 2);
    ASSERT_EQ(merged.getDocumentFrequency("beta"), 2);
    ASSERT_EQ(merged.getDocumentFrequency("gamma"), 2);
    ASSERT_EQ(merged.getDocumentFrequency("delta"), 2);

    auto vocab = merged.getVocabulary();
    ASSERT_EQ(vocab.size(), 4);

    // Phrase search after merge
    auto phraseRes = merged.phraseSearch({"alpha", "beta"});
    ASSERT_EQ(phraseRes.size(), 1);
    ASSERT_EQ(phraseRes[0], 1);

    std::cout << "test_segment_merging PASSED\n";
}

void test_inverted_index_serialization() {
    InvertedIndex index;
    Document d1{1, "information retrieval system", {"inform", "retriev", "system"}};
    Document d2{2, "neural network and search engine", {"neural", "network", "and", "search", "engin"}};
    Document d3{3, "search engine indexing", {"search", "engin", "index"}};

    index.addDocument(d1);
    index.addDocument(d2);
    index.addDocument(d3);
    index.finalize();

    std::string testPath = "/tmp/test_inverted_index.bin";
    ASSERT_TRUE(index.saveToFile(testPath));

    InvertedIndex loaded;
    ASSERT_TRUE(loaded.loadFromFile(testPath));

    ASSERT_EQ(loaded.getTotalDocuments(), 3);
    ASSERT_EQ(loaded.getDocumentFrequency("search"), 2);
    ASSERT_EQ(loaded.getDocumentFrequency("inform"), 1);
    ASSERT_EQ(loaded.getDocumentFrequency("missing"), 0);

    ASSERT_EQ(loaded.getTermFrequency("engin", 2), 1);
    ASSERT_EQ(loaded.getTermFrequency("engin", 3), 1);

    auto phraseRes = loaded.phraseSearch({"search", "engin"});
    ASSERT_EQ(phraseRes.size(), 2);

    const Document* doc1 = loaded.getDocument(1);
    ASSERT_TRUE(doc1 != nullptr);
    ASSERT_EQ(doc1->content, "information retrieval system");

    std::cout << "test_inverted_index_serialization PASSED\n";
}

void test_vbyte_compression() {
    // 1. Single integer encoding / decoding
    uint8_t buf[8];
    uint32_t decoded = 0;

    size_t s1 = VByte::encodeUint32(0, buf);
    ASSERT_EQ(s1, 1);
    VByte::decodeUint32(buf, decoded);
    ASSERT_EQ(decoded, 0);

    size_t s2 = VByte::encodeUint32(127, buf);
    ASSERT_EQ(s2, 1);
    VByte::decodeUint32(buf, decoded);
    ASSERT_EQ(decoded, 127);

    size_t s3 = VByte::encodeUint32(128, buf);
    ASSERT_EQ(s3, 2);
    VByte::decodeUint32(buf, decoded);
    ASSERT_EQ(decoded, 128);

    size_t s4 = VByte::encodeUint32(1048576, buf);
    ASSERT_TRUE(s4 >= 3);
    VByte::decodeUint32(buf, decoded);
    ASSERT_EQ(decoded, 1048576);

    // 2. Vector delta encoding / decoding
    std::vector<uint32_t> postingList = {10, 15, 23, 100, 105, 500, 10000};
    auto compressed = VByte::encodeDelta(postingList);
    // Delta encoding should be much smaller than 7 * 4 = 28 bytes
    ASSERT_TRUE(compressed.size() <= 12);

    auto decompressed = VByte::decodeDelta(compressed.data(), compressed.size());
    ASSERT_EQ(decompressed.size(), postingList.size());
    for (size_t i = 0; i < postingList.size(); ++i) {
        ASSERT_EQ(decompressed[i], postingList[i]);
    }

    std::cout << "test_vbyte_compression PASSED\n";
}

int main() {
    std::cout << "=== Inverted Index Unit Tests ===\n\n";

    test_basic_indexing_and_lookup();
    test_phrase_search();
    test_skip_pointers_and_wand_scores();
    test_segment_merging();
    test_inverted_index_serialization();
    test_vbyte_compression();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
