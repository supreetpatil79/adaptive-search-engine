// bench/benchmark_throughput.cpp
// QPS throughput benchmark with per-mode P50/P95/P99 tail latency.
// Reports: QPS, mean, P50, P95, P99 for each retrieval mode.

#include "../search/search_engine.h"
#include "../tokenizer/tokenizer.h"
#include "../utils/file_loader.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

// ── Statistics ────────────────────────────────────────────────────────────────
struct Stats {
    double mean, stddev, p50, p95, p99, qps;
};

static Stats computeStats(std::vector<double> samples, int numQueries) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    double mean = sum / samples.size();
    double sq = 0.0;
    for (double v : samples) sq += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq / samples.size());

    auto pct = [&](double p) {
        double idx = p * (samples.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = lo + 1;
        if (hi >= samples.size()) return samples.back();
        return samples[lo] * (1.0 - (idx - lo)) + samples[hi] * (idx - lo);
    };

    double totalUs = sum;
    double qps = numQueries / (totalUs / 1e6);

    return {mean, stddev, pct(0.50), pct(0.95), pct(0.99), qps};
}

static void printMode(const char* name, const Stats& s, double baseQPS) {
    std::cout << std::left  << std::setw(28) << name
              << "  QPS=" << std::right << std::fixed << std::setprecision(0)
              << std::setw(8) << s.qps
              << "  mean=" << std::setprecision(2) << std::setw(8) << s.mean << "µs"
              << "  P50=" << std::setw(8) << s.p50 << "µs"
              << "  P95=" << std::setw(8) << s.p95 << "µs"
              << "  P99=" << std::setw(8) << s.p99 << "µs"
              << "  speedup=" << std::setprecision(1) << s.qps / (baseQPS > 0 ? baseQPS : 1) << "x"
              << "\n";
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string corpusPath = "data/corpus_10k.txt";
    if (argc >= 2) corpusPath = argv[1];

    std::vector<Document> docs = FileLoader::loadFromFile(corpusPath);
    if (docs.empty()) {
        std::cerr << "Could not load corpus from " << corpusPath << "\n";
        return 1;
    }

    SearchEngine engine;
    for (const auto& doc : docs) engine.addDocument(doc.id, doc.content);
    engine.finalizeIndex();

    const auto& index = engine.getIndex();
    std::cout << "======================================================================\n";
    std::cout << "  Throughput Benchmark — " << index.getTotalDocuments() << " documents\n";
    std::cout << "  Reports: QPS + tail latency (P50 / P95 / P99)\n";
    std::cout << "======================================================================\n\n";

    std::vector<std::string> queryWorkload = {
        "artificial intelligence machine learning",
        "neural networks deep learning data science",
        "cloud computing software engineering",
        "algorithms data statistics optimization",
        "quantum computing parallel processing hardware",
        "cybersecurity cryptography privacy",
        "edge computing IoT telemetry analytics",
        "database management indexing retrieval"
    };

    // Tokenise + stem once for WAND
    std::vector<std::vector<std::string>> tokenizedWorkload;
    for (const auto& q : queryWorkload) {
        tokenizedWorkload.push_back(Tokenizer::tokenizeAndStem(q));
    }

    const int iterations = 100;  // enough for reliable P99
    const int topK = 10;

    AdaptiveRanker unprunedRanker;
    WANDScorer     wandScorer;

    // Warmup (not measured)
    for (int i = 0; i < 20; ++i) {
        for (size_t qi = 0; qi < queryWorkload.size(); ++qi) {
            unprunedRanker.search(queryWorkload[qi], index, topK);
            wandScorer.search(tokenizedWorkload[qi], index, topK);
            engine.search(queryWorkload[qi], topK);
        }
    }

    // ── Mode 1: Unpruned ─────────────────────────────────────────────────
    std::vector<double> unprunedSamples;
    unprunedSamples.reserve(iterations * queryWorkload.size());
    for (int i = 0; i < iterations; ++i) {
        for (const auto& q : queryWorkload) {
            auto t0 = std::chrono::high_resolution_clock::now();
            unprunedRanker.search(q, index, topK);
            auto t1 = std::chrono::high_resolution_clock::now();
            unprunedSamples.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / 1000.0);
        }
    }

    // ── Mode 2: WAND ─────────────────────────────────────────────────────
    std::vector<double> wandSamples;
    wandSamples.reserve(iterations * tokenizedWorkload.size());
    for (int i = 0; i < iterations; ++i) {
        for (const auto& tok : tokenizedWorkload) {
            WANDStats stats;
            auto t0 = std::chrono::high_resolution_clock::now();
            wandScorer.search(tok, index, topK, &stats);
            auto t1 = std::chrono::high_resolution_clock::now();
            wandSamples.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / 1000.0);
        }
    }

    // ── Mode 3: LRU Cache ────────────────────────────────────────────────
    std::vector<double> cacheSamples;
    cacheSamples.reserve(iterations * queryWorkload.size());
    for (int i = 0; i < iterations; ++i) {
        for (const auto& q : queryWorkload) {
            auto t0 = std::chrono::high_resolution_clock::now();
            engine.search(q, topK);
            auto t1 = std::chrono::high_resolution_clock::now();
            cacheSamples.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / 1000.0);
        }
    }

    int totalN = iterations * static_cast<int>(queryWorkload.size());
    auto su = computeStats(unprunedSamples, totalN);
    auto sw = computeStats(wandSamples, totalN);
    auto sc = computeStats(cacheSamples, totalN);

    std::cout << std::string(100, '-') << "\n";
    printMode("Unpruned (BM25)",   su, su.qps);
    printMode("WAND Top-K Pruned", sw, su.qps);
    printMode("LRU Cache (warm)",  sc, su.qps);
    std::cout << std::string(100, '=') << "\n";
    std::cout << "Samples: " << totalN << " per mode  |  Queries: " << queryWorkload.size()
              << "  |  Iterations: " << iterations << "\n";

    return 0;
}
