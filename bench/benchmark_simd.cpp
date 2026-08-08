// bench/benchmark_simd.cpp
// Micro-benchmark comparing Scalar vs SIMD (ARM Neon / AVX2) vector dot-product performance.

#include "../utils/simd_math.h"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

int main() {
    const int dim = 384;
    const int numVectors = 1000;
    const int iterations = 1000; // 1,000,000 total dot products

    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    // Generate random 384-d unit vectors
    std::vector<std::vector<float>> vecs(numVectors, std::vector<float>(dim));
    for (int i = 0; i < numVectors; ++i) {
        float norm = 0.0f;
        for (int d = 0; d < dim; ++d) {
            vecs[i][d] = dist(rng);
            norm += vecs[i][d] * vecs[i][d];
        }
        norm = std::sqrt(norm);
        for (int d = 0; d < dim; ++d) vecs[i][d] /= norm;
    }

    std::cout << "======================================================================\n";
    std::cout << "  SIMD Vector Math Micro-Benchmark (" << dim << "-d Float Vectors)\n";
#if defined(__ARM_NEON) || defined(__aarch64__)
    std::cout << "  Hardware Acceleration: ARM Neon 128-bit SIMD (4-way unrolled)\n";
#elif defined(__AVX2__)
    std::cout << "  Hardware Acceleration: x86 AVX2 256-bit SIMD (2-way unrolled)\n";
#else
    std::cout << "  Hardware Acceleration: None (Fallback Scalar)\n";
#endif
    std::cout << "======================================================================\n\n";

    // 1. Scalar Dot Product
    double scalarSum = 0.0;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < numVectors; ++i) {
            int nextIdx = (i + 1) % numVectors;
            scalarSum += simd::scalarInnerProduct(vecs[i].data(), vecs[nextIdx].data(), dim);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double scalarMs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;

    // 2. SIMD Dot Product
    double simdSum = 0.0;
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < numVectors; ++i) {
            int nextIdx = (i + 1) % numVectors;
            simdSum += simd::innerProduct(vecs[i].data(), vecs[nextIdx].data(), dim);
        }
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double simdMs = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;

    long totalOps = static_cast<long>(numVectors) * iterations;
    double scalarUsPerOp = (scalarMs * 1000.0) / totalOps;
    double simdUsPerOp   = (simdMs * 1000.0) / totalOps;
    double speedup       = scalarMs / (simdMs > 0 ? simdMs : 0.001);

    std::cout << std::left << std::setw(30) << "Implementation"
              << std::setw(20) << "Total Time (ms)"
              << std::setw(20) << "Per Op Latency"
              << std::setw(20) << "Speedup" << "\n";
    std::cout << "----------------------------------------------------------------------\n";

    std::cout << std::left << std::setw(30) << "Scalar Loop"
              << std::fixed << std::setprecision(2) << std::setw(20) << scalarMs
              << std::setprecision(4) << std::setw(20) << (scalarUsPerOp * 1000.0) << " ns"
              << "1.00x (Baseline)\n";

    std::cout << std::left << std::setw(30) << "SIMD Accelerated"
              << std::fixed << std::setprecision(2) << std::setw(20) << simdMs
              << std::setprecision(4) << std::setw(20) << (simdUsPerOp * 1000.0) << " ns"
              << std::setprecision(2) << speedup << "x\n";

    std::cout << "======================================================================\n";
    std::cout << "Sanity check precision diff: " << std::abs(scalarSum - simdSum) << "\n";

    return 0;
}
