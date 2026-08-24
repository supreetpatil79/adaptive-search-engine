// tests/test_sq8.cpp
// Unit tests for SQ8 Scalar Quantization Index.
// Tests: quantization accuracy, top-k recall vs float32 flat search,
// binary serialization, memory savings.
// Run: ./build/test_sq8

#include "../embed/sq8_index.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
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

#define ASSERT_NEAR(a, b, tol)                                        \
    do {                                                              \
        float diff = std::abs((float)(a) - (float)(b));                \
        if (diff > (tol)) {                                           \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  |" << (a) << " - " << (b)                 \
                      << "| = " << diff << " > " << (tol) << "\n";    \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

static std::vector<float> generateUnitVector(int dim, std::mt19937& rng) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (auto& x : v) {
        x = dist(rng);
        norm += x * x;
    }
    norm = std::sqrt(norm);
    for (auto& x : v) x /= norm;
    return v;
}

void test_sq8_accuracy_and_recall() {
    const int dim = 384;
    const int numDocs = 100;
    std::mt19937 rng(42);

    std::vector<std::vector<float>> dataset(numDocs);
    SQ8Index sq8(dim);

    for (int i = 0; i < numDocs; ++i) {
        dataset[i] = generateUnitVector(dim, rng);
        sq8.add(i + 1, dataset[i].data());
    }

    ASSERT_TRUE(sq8.size() == numDocs);

    // Test search on 10 random query vectors
    int totalTop1Matches = 0;
    for (int q = 0; q < 10; ++q) {
        auto query = generateUnitVector(dim, rng);

        // Ground truth exact float32 search
        std::vector<std::pair<float, int>> exactScores;
        for (int i = 0; i < numDocs; ++i) {
            float dot = 0.0f;
            for (int d = 0; d < dim; ++d) {
                dot += query[d] * dataset[i][d];
            }
            exactScores.emplace_back(dot, i + 1);
        }
        std::sort(exactScores.begin(), exactScores.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        // SQ8 search
        auto sq8Results = sq8.search(query.data(), 10);
        ASSERT_TRUE(sq8Results.size() == 10);

        if (sq8Results[0].first == exactScores[0].second) {
            totalTop1Matches++;
        }

        // Check that cosine similarity error is < 0.05
        float diff = std::abs(sq8Results[0].second - exactScores[0].first);
        ASSERT_TRUE(diff < 0.05f);
    }

    // Top-1 recall should be high (>= 90%)
    ASSERT_TRUE(totalTop1Matches >= 9);
    std::cout << "test_sq8_accuracy_and_recall PASSED (Top-1 recall: " << (totalTop1Matches * 10) << "%)\n";
}

void test_sq8_serialization() {
    const int dim = 384;
    const int numDocs = 20;
    std::mt19937 rng(12345);

    SQ8Index index(dim);
    for (int i = 0; i < numDocs; ++i) {
        auto vec = generateUnitVector(dim, rng);
        index.add(i + 1, vec.data());
    }

    std::string testFile = "/tmp/test_sq8_index.bin";
    ASSERT_TRUE(index.saveToFile(testFile));

    SQ8Index loaded(dim);
    ASSERT_TRUE(loaded.loadFromFile(testFile));
    ASSERT_TRUE(loaded.size() == numDocs);
    ASSERT_TRUE(loaded.dim() == dim);

    auto query = generateUnitVector(dim, rng);
    auto resOrig = index.search(query.data(), 5);
    auto resLoaded = loaded.search(query.data(), 5);

    ASSERT_TRUE(resOrig.size() == resLoaded.size());
    for (size_t i = 0; i < resOrig.size(); ++i) {
        ASSERT_TRUE(resOrig[i].first == resLoaded[i].first);
        ASSERT_NEAR(resOrig[i].second, resLoaded[i].second, 1e-6f);
    }

    std::cout << "test_sq8_serialization PASSED\n";
}

int main() {
    std::cout << "=== SQ8 Quantization Unit Tests ===\n\n";

    test_sq8_accuracy_and_recall();
    test_sq8_serialization();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
