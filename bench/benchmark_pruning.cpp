// bench/benchmark_pruning.cpp
// Benchmark comparing Unpruned candidate evaluation vs WAND top-k pruning.
// Reports: Mean, StdDev, P50, P95, P99 latency per-query and overall.

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

// ── Percentile helper ─────────────────────────────────────────────────────────
struct Stats {
    double mean, stddev, p50, p95, p99;
};

static Stats computeStats(std::vector<double> samples) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    double mean = sum / samples.size();
    double sq_sum = 0.0;
    for (double v : samples) sq_sum += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq_sum / samples.size());

    auto pct = [&](double p) -> double {
        double idx = p * (samples.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = lo + 1;
        if (hi >= samples.size()) return samples.back();
        double frac = idx - lo;
        return samples[lo] * (1.0 - frac) + samples[hi] * frac;
    };
    return {mean, stddev, pct(0.50), pct(0.95), pct(0.99)};
}

static void printStats(const char* label, const Stats& s) {
    std::cout << std::left  << std::setw(12) << label
              << "  mean=" << std::fixed << std::setprecision(2) << std::setw(9) << s.mean
              << "  std=" << std::setw(9) << s.stddev
              << "  P50=" << std::setw(9) << s.p50
              << "  P95=" << std::setw(9) << s.p95
              << "  P99=" << std::setw(9) << s.p99
              << "  (µs)\n";
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string dataPath = "data/documents.txt";
    if (argc >= 2) dataPath = argv[1];

    std::vector<Document> baseDocs = FileLoader::loadFromFile(dataPath);
    if (baseDocs.empty()) {
        std::cerr << "Cannot load base corpus from " << dataPath << "\n";
        return 1;
    }

    SearchEngine   engine;
    AdaptiveRanker unprunedRanker;
    WANDScorer     wandScorer;

    int targetDocs = 2000;
    int currentId  = 1;

    for (int r = 0; r < (targetDocs / static_cast<int>(baseDocs.size())) + 1; ++r) {
        for (const auto& doc : baseDocs) {
            std::string content = doc.content + " term_" + std::to_string(r)
                                + " doc_" + std::to_string(currentId);
            engine.addDocument(currentId++, content);
            if (currentId > targetDocs) break;
        }
        if (currentId > targetDocs) break;
    }

    engine.finalizeIndex();
    const auto& index = engine.getIndex();
    std::cout << "Corpus: " << index.getTotalDocuments() << " documents\n\n";

    std::vector<std::string> testQueries = {
        "artificial intelligence machine learning",
        "neural networks deep learning data science",
        "cloud computing software engineering DevOps",
        "algorithms data statistics optimization",
        "quantum computing parallel processing hardware"
    };

    const int iterations = 100;  // 100 iterations for reliable percentiles
    const int topK = 10;

    std::cout << "Warming up (" << (iterations/10) << " iters)...\n";
    for (const auto& q : testQueries) {
        auto tokens = Tokenizer::tokenizeAndStem(q);
        for (int i = 0; i < iterations/10; ++i) {
            unprunedRanker.search(q, index, topK);
            wandScorer.search(tokens, index, topK);
        }
    }

    std::cout << "Running " << iterations << " iterations per query...\n";
    std::cout << std::string(90, '-') << "\n";

    std::vector<double> allUnpruned, allWand;

    for (const auto& q : testQueries) {
        auto tokens = Tokenizer::tokenizeAndStem(q);
        std::vector<double> unprunedSamples, wandSamples;
        unprunedSamples.reserve(iterations);
        wandSamples.reserve(iterations);

        for (int i = 0; i < iterations; ++i) {
            {
                auto t0 = std::chrono::high_resolution_clock::now();
                unprunedRanker.search(q, index, topK);
                auto t1 = std::chrono::high_resolution_clock::now();
                unprunedSamples.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / 1000.0);
            }
            {
                WANDStats stats;
                auto t0 = std::chrono::high_resolution_clock::now();
                wandScorer.search(tokens, index, topK, &stats);
                auto t1 = std::chrono::high_resolution_clock::now();
                wandSamples.push_back(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count() / 1000.0);
            }
        }

        allUnpruned.insert(allUnpruned.end(), unprunedSamples.begin(), unprunedSamples.end());
        allWand.insert(allWand.end(), wandSamples.begin(), wandSamples.end());

        std::string shortQ = q.size() > 38 ? q.substr(0, 35) + "..." : q;
        std::cout << "\nQuery: \"" << shortQ << "\"\n";
        printStats("Unpruned", computeStats(unprunedSamples));
        printStats("WAND",     computeStats(wandSamples));
        auto su = computeStats(unprunedSamples);
        auto sw = computeStats(wandSamples);
        if (sw.mean > 0)
            std::cout << "  Speedup (mean): " << std::fixed << std::setprecision(1)
                      << su.mean / sw.mean << "x  |  P99 speedup: "
                      << su.p99 / (sw.p99 > 0 ? sw.p99 : 0.001) << "x\n";
    }

    std::cout << "\n" << std::string(90, '=') << "\n";
    std::cout << "OVERALL AGGREGATE (" << allUnpruned.size() << " samples)\n";
    printStats("Unpruned", computeStats(allUnpruned));
    printStats("WAND",     computeStats(allWand));
    auto su = computeStats(allUnpruned);
    auto sw = computeStats(allWand);
    std::cout << "Overall speedup — mean: " << su.mean / (sw.mean > 0 ? sw.mean : 0.001)
              << "x  |  P50: " << su.p50 / (sw.p50 > 0 ? sw.p50 : 0.001)
              << "x  |  P99: " << su.p99 / (sw.p99 > 0 ? sw.p99 : 0.001) << "x\n";

    return 0;
}
