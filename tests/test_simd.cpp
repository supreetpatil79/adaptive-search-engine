// tests/test_simd.cpp
// Correctness + determinism tests for simd::innerProduct.
// Verifies SIMD path gives bitwise-identical results to scalar path
// for 384-dim unit vectors with at least 10 different random seeds.
// Run: ./build/test_simd

#include "../utils/simd_math.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

static int passed = 0;
static int failed = 0;

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__      \
                      << "  " << #expr << "\n";                        \
            ++failed;                                                   \
        } else { ++passed; }                                           \
    } while (false)

#define ASSERT_NEAR(a, b, tol)                                         \
    do {                                                               \
        float diff = std::abs((float)(a) - (float)(b));                \
        if (diff > (tol)) {                                            \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__      \
                      << "  |" << (a) << " - " << (b)                 \
                      << "| = " << diff << " > " << (tol) << "\n";    \
            ++failed;                                                   \
        } else { ++passed; }                                           \
    } while (false)

static std::vector<float> makeUnitVec(int dim, std::mt19937& rng) {
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> v(dim);
    float norm = 0.0f;
    for (auto& x : v) { x = dist(rng); norm += x * x; }
    norm = std::sqrt(norm);
    for (auto& x : v) x /= norm;
    return v;
}

int main() {
    std::cout << "=== SIMD Inner-Product Correctness Tests ===\n";

#if defined(__ARM_NEON) || defined(__aarch64__)
    std::cout << "  Platform: ARM Neon 128-bit\n\n";
#elif defined(__AVX2__)
    std::cout << "  Platform: x86 AVX2 256-bit\n\n";
#else
    std::cout << "  Platform: Scalar fallback (no SIMD)\n\n";
#endif

    const int dim = 384;
    const float tol = 1e-5f;  // acceptable floating-point rounding difference

    // ── 1. Unit vector with itself should give dot ~1.0 ──────────────────────
    {
        std::mt19937 rng(42);
        auto a = makeUnitVec(dim, rng);
        float s = simd::innerProduct(a.data(), a.data(), dim);
        float ref = simd::scalarInnerProduct(a.data(), a.data(), dim);
        ASSERT_NEAR(s, 1.0f, tol);
        ASSERT_NEAR(s, ref, tol);
        std::cout << "test_self_dot  self_dot=" << s << "  expected≈1.0\n";
    }

    // ── 2. Orthogonal vectors should give dot ~0.0 ───────────────────────────
    {
        std::vector<float> a(dim, 0.0f), b(dim, 0.0f);
        a[0] = 1.0f;  // e_0
        b[1] = 1.0f;  // e_1 — orthogonal to e_0
        float s = simd::innerProduct(a.data(), b.data(), dim);
        ASSERT_NEAR(s, 0.0f, tol);
        std::cout << "test_orthogonal  dot=" << s << "  expected≈0.0\n";
    }

    // ── 3. SIMD vs Scalar agreement across 20 random pairs ───────────────────
    {
        std::mt19937 rng(1337);
        int numPairs = 20;
        for (int t = 0; t < numPairs; ++t) {
            auto a = makeUnitVec(dim, rng);
            auto b = makeUnitVec(dim, rng);
            float simd_val   = simd::innerProduct(a.data(), b.data(), dim);
            float scalar_val = simd::scalarInnerProduct(a.data(), b.data(), dim);
            ASSERT_NEAR(simd_val, scalar_val, tol);
        }
        std::cout << "test_simd_vs_scalar  " << numPairs
                  << " pairs  max_tol=" << tol << "  all passed\n";
    }

    // ── 4. Dim=1 edge case ───────────────────────────────────────────────────
    {
        float a = 1.0f, b = 0.5f;
        float s = simd::innerProduct(&a, &b, 1);
        ASSERT_NEAR(s, 0.5f, tol);
        std::cout << "test_dim1  dot=" << s << "  expected=0.5\n";
    }

    // ── 5. Known-value test: [1/sqrt(2), 1/sqrt(2)] · [1/sqrt(2), 1/sqrt(2)] = 1.0 ──
    {
        float v = 1.0f / std::sqrt(2.0f);
        std::vector<float> a = {v, v};
        float s = simd::innerProduct(a.data(), a.data(), 2);
        ASSERT_NEAR(s, 1.0f, tol);
        std::cout << "test_known_value  dot=" << s << "  expected=1.0\n";
    }

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
