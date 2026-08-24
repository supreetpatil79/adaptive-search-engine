// tests/test_metadata.cpp
// Unit tests for Metadata Attribute Index, Bitset filtering, and Filtered HNSW.
// Run: ./build/test_metadata

#include "../index/metadata_index.h"
#include "../embed/hnsw_index.h"
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

void test_bitset_operations() {
    MetadataBitset b1;
    b1.set(1);
    b1.set(5);
    b1.set(65); // Multi-word test (> 64)

    ASSERT_TRUE(b1.test(1));
    ASSERT_TRUE(b1.test(5));
    ASSERT_TRUE(b1.test(65));
    ASSERT_TRUE(!b1.test(2));
    ASSERT_EQ(b1.count(), 3);

    MetadataBitset b2;
    b2.set(5);
    b2.set(10);
    b2.set(65);

    auto bAnd = b1.bitwiseAnd(b2);
    ASSERT_EQ(bAnd.count(), 2);
    ASSERT_TRUE(bAnd.test(5));
    ASSERT_TRUE(bAnd.test(65));
    ASSERT_TRUE(!bAnd.test(1));

    auto bOr = b1.bitwiseOr(b2);
    ASSERT_EQ(bOr.count(), 4);
    ASSERT_TRUE(bOr.test(1));
    ASSERT_TRUE(bOr.test(5));
    ASSERT_TRUE(bOr.test(10));
    ASSERT_TRUE(bOr.test(65));

    std::cout << "test_bitset_operations PASSED\n";
}

void test_metadata_index_filtering() {
    MetadataIndex idx;

    DocumentMetadata d1;
    d1.docId = 1;
    d1.stringFields["category"] = "AI";
    d1.numericFields["year"] = 2024.0;
    idx.setMetadata(1, d1);

    DocumentMetadata d2;
    d2.docId = 2;
    d2.stringFields["category"] = "AI";
    d2.numericFields["year"] = 2021.0;
    idx.setMetadata(2, d2);

    DocumentMetadata d3;
    d3.docId = 3;
    d3.stringFields["category"] = "Cloud";
    d3.numericFields["year"] = 2024.0;
    idx.setMetadata(3, d3);

    // Filter 1: Exact match "category:AI" -> docs 1, 2
    auto mCat = idx.evaluateFilter("category:AI", 10);
    ASSERT_TRUE(mCat.test(1));
    ASSERT_TRUE(mCat.test(2));
    ASSERT_TRUE(!mCat.test(3));

    // Filter 2: Numeric range "year:>=2023" -> docs 1, 3
    auto mYear = idx.evaluateFilter("year:>=2023", 10);
    ASSERT_TRUE(mYear.test(1));
    ASSERT_TRUE(!mYear.test(2));
    ASSERT_TRUE(mYear.test(3));

    // Filter 3: Combined "category:AI,year:>=2023" -> doc 1 ONLY
    auto mCombined = idx.evaluateFilter("category:AI,year:>=2023", 10);
    ASSERT_TRUE(mCombined.test(1));
    ASSERT_TRUE(!mCombined.test(2));
    ASSERT_TRUE(!mCombined.test(3));

    std::cout << "test_metadata_index_filtering PASSED\n";
}

void test_filtered_hnsw_search() {
    HNSWIndex hnsw(4, 16, 200);

    std::vector<float> v1 = {1.0f, 0.0f, 0.0f, 0.0f}; // Closest to query
    std::vector<float> v2 = {0.9f, 0.1f, 0.0f, 0.0f};
    std::vector<float> v3 = {0.0f, 1.0f, 0.0f, 0.0f};

    hnsw.insert(1, v1.data());
    hnsw.insert(2, v2.data());
    hnsw.insert(3, v3.data());

    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};

    // Unfiltered search: doc 1 is rank 1
    auto unfilt = hnsw.search(query.data(), 2);
    ASSERT_EQ(unfilt[0].docId, 1);

    // Filtered search where doc 1 is excluded: doc 2 must be rank 1!
    auto filt = hnsw.searchFiltered(query.data(), 2, [](int docId) {
        return docId != 1;
    });

    ASSERT_EQ(filt.size(), 2);
    ASSERT_EQ(filt[0].docId, 2);
    ASSERT_EQ(filt[1].docId, 3);

    std::cout << "test_filtered_hnsw_search PASSED\n";
}

int main() {
    std::cout << "=== Metadata Index & Filtered HNSW Unit Tests ===\n\n";

    test_bitset_operations();
    test_metadata_index_filtering();
    test_filtered_hnsw_search();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
