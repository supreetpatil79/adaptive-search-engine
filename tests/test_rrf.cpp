// tests/test_rrf.cpp
// ====================
// Unit tests for RRFFusion.
// Compiled as a separate executable; passes/fails via exit code.
// Run: ./build/test_rrf

#include "../embed/rrf_fusion.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
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
            ++failed;                                                  \
        } else { ++passed; }                                          \
    } while (false)

#define ASSERT_NEAR(a, b, tol)                                        \
    do {                                                              \
        if (std::abs((a) - (b)) > (tol)) {                           \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  |" << (a) << " - " << (b)                \
                      << "| > " << (tol) << "\n";                    \
            ++failed;                                                  \
        } else { ++passed; }                                          \
    } while (false)

// ── Helpers ──────────────────────────────────────────────────────────────────

static SearchResult sr(int id, double score, const std::string& c = "") {
    return SearchResult{id, score, c};
}

// ── Test cases ───────────────────────────────────────────────────────────────

void test_rrf_formula() {
    // Verify the formula: RRF(d) = 1/(k+rank)
    // k=60, rank=1 (first result):  1/61 ≈ 0.016393
    RRFFusion rrf(60);

    std::vector<SearchResult>         bm25   = {sr(1, 4.5, "doc one"),
                                                sr(2, 3.0, "doc two")};
    std::vector<std::pair<int,float>> dense  = {{1, 0.95f}, {3, 0.88f}};

    auto results = rrf.fuse(bm25, dense, 10);

    // doc 1 appears in both lists at rank 1: RRF = 1/61 + 1/61 = 2/61
    // doc 2 appears in bm25 at rank 2:       RRF = 1/62
    // doc 3 appears in dense at rank 2:      RRF = 1/62
    double expected_doc1 = 1.0/61 + 1.0/61;
    double expected_doc2 = 1.0/62;
    double expected_doc3 = 1.0/62;

    ASSERT_TRUE(results.size() == 3);
    ASSERT_TRUE(results[0].docId == 1);          // doc1 ranks first
    ASSERT_NEAR(results[0].rrfScore, expected_doc1, 1e-9);
    ASSERT_NEAR(results[1].rrfScore, expected_doc2, 1e-9);
    ASSERT_NEAR(results[2].rrfScore, expected_doc3, 1e-9);

    std::cout << "test_rrf_formula\n"
              << "  doc1 rrf=" << std::fixed << std::setprecision(6) << results[0].rrfScore
              << "  expected=" << expected_doc1 << "\n"
              << "  doc2 rrf=" << results[1].rrfScore
              << "  expected=" << expected_doc2 << "\n";
}

void test_rrf_only_bm25() {
    // When dense list is empty, RRF equals 1/(k+rank) from bm25 only.
    RRFFusion rrf(60);
    std::vector<SearchResult>         bm25  = {sr(5, 2.0, "x"), sr(7, 1.0, "y")};
    std::vector<std::pair<int,float>> dense = {};

    auto results = rrf.fuse(bm25, dense, 10);
    ASSERT_TRUE(results.size() == 2);
    ASSERT_TRUE(results[0].docId == 5);
    ASSERT_NEAR(results[0].rrfScore, 1.0/61, 1e-9);
    ASSERT_NEAR(results[1].rrfScore, 1.0/62, 1e-9);
    std::cout << "test_rrf_only_bm25 OK\n";
}

void test_rrf_only_dense() {
    // When bm25 list is empty, RRF comes entirely from dense ranks.
    RRFFusion rrf(60);
    std::vector<SearchResult>         bm25  = {};
    std::vector<std::pair<int,float>> dense = {{10, 0.9f}, {20, 0.8f}};

    auto results = rrf.fuse(bm25, dense, 10);
    ASSERT_TRUE(results.size() == 2);
    ASSERT_TRUE(results[0].docId == 10);
    ASSERT_NEAR(results[0].rrfScore, 1.0/61, 1e-9);
    std::cout << "test_rrf_only_dense OK\n";
}

void test_rrf_topk_truncation() {
    // Verify topK is respected.
    RRFFusion rrf(60);
    std::vector<SearchResult>         bm25;
    std::vector<std::pair<int,float>> dense;
    for (int i = 1; i <= 20; ++i) {
        bm25.push_back(sr(i, 20.0 - i, "doc"));
        dense.push_back({i, 0.9f - i * 0.01f});
    }
    auto results = rrf.fuse(bm25, dense, 5);
    ASSERT_TRUE(static_cast<int>(results.size()) == 5);
    std::cout << "test_rrf_topk_truncation OK\n";
}

void test_rrf_score_ordering() {
    // Doc in both lists at rank 1 must beat doc in one list at rank 1.
    RRFFusion rrf(60);
    std::vector<SearchResult>         bm25  = {sr(1, 5.0, "a"), sr(2, 4.0, "b")};
    std::vector<std::pair<int,float>> dense = {{1, 0.9f}, {3, 0.95f}};
    // doc1: 1/61 + 1/61 = 2/61 ≈ 0.03279
    // doc3: 1/62 ≈ 0.01613  (only in dense, rank 2)
    // doc2: 1/62 ≈ 0.01613  (only in bm25, rank 2)

    auto results = rrf.fuse(bm25, dense, 10);
    ASSERT_TRUE(results[0].docId == 1);          // must be first
    ASSERT_TRUE(results[0].rrfScore > results[1].rrfScore);
    std::cout << "test_rrf_score_ordering OK\n";
}

void test_rrf_k_parameter() {
    // Higher k dampens rank-sensitivity: scores at rank 1 should be closer.
    RRFFusion rrf_low(1);
    RRFFusion rrf_high(1000);
    std::vector<SearchResult>         bm25  = {sr(1,1.0,"a"), sr(2,1.0,"b")};
    std::vector<std::pair<int,float>> dense = {};

    auto low  = rrf_low.fuse(bm25, dense, 10);
    auto high = rrf_high.fuse(bm25, dense, 10);
    // With low k: rank 1 = 1/2 = 0.5, rank 2 = 1/3 ≈ 0.333 → diff = 0.167
    // With high k: rank 1 = 1/1001, rank 2 = 1/1002 → diff much smaller
    double diff_low  = low[0].rrfScore  - low[1].rrfScore;
    double diff_high = high[0].rrfScore - high[1].rrfScore;
    ASSERT_TRUE(diff_low > diff_high);
    std::cout << "test_rrf_k_parameter  diff_low=" << diff_low
              << "  diff_high=" << diff_high << "\n";
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== RRF Unit Tests ===\n\n";

    test_rrf_formula();
    test_rrf_only_bm25();
    test_rrf_only_dense();
    test_rrf_topk_truncation();
    test_rrf_score_ordering();
    test_rrf_k_parameter();

    std::cout << "\n══════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
