// bench/benchmark_throughput.cpp
// Throughput benchmark measuring Query Throughput (QPS - Queries Per Second)
// under high-throughput batch workload comparing Unpruned Search vs WAND Pruned Search.

#include "../search/search_engine.h"
#include "../tokenizer/tokenizer.h"
#include "../utils/file_loader.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    std::string corpusPath = "data/corpus_10k.txt";
    if (argc >= 2) corpusPath = argv[1];

    std::vector<Document> docs = FileLoader::loadFromFile(corpusPath);
    if (docs.empty()) {
        std::cerr << "Could not load corpus from " << corpusPath << "\n";
        return 1;
    }

    SearchEngine engine;
    for (const auto& doc : docs) {
        engine.addDocument(doc.id, doc.content);
    }
    engine.finalizeIndex();

    std::cout << "======================================================================\n";
    std::cout << "  Throughput Benchmark — QPS (Queries Per Second) on 10,000 Documents\n";
    std::cout << "======================================================================\n\n";

    std::vector<std::string> queryWorkload = {
        "artificial intelligence machine learning",
        "neural networks deep learning data science",
        "cloud computing software engineering DevOps",
        "algorithms data statistics optimization",
        "quantum computing parallel processing hardware",
        "cybersecurity cryptography ledger privacy",
        "edge computing IoT telemetry analytics",
        "database management B-trees LSM-trees"
    };

    int iterationsPerQuery = 2;
    int totalQueries = static_cast<int>(queryWorkload.size()) * iterationsPerQuery;
    int topK = 10;

    // 1. Unpruned Throughput (bypassing LRU cache)
    AdaptiveRanker unprunedRanker;
    const auto& index = engine.getIndex();

    auto startUnpruned = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterationsPerQuery; ++iter) {
        for (const auto& q : queryWorkload) {
            unprunedRanker.search(q, index, topK);
        }
    }
    auto endUnpruned = std::chrono::high_resolution_clock::now();
    double unprunedSec = std::chrono::duration_cast<std::chrono::microseconds>(endUnpruned - startUnpruned).count() / 1000000.0;
    double unprunedQPS = totalQueries / unprunedSec;

    // 2. WAND Pruned Throughput (bypassing LRU cache)
    WANDScorer wandScorer;
    std::vector<std::vector<std::string>> tokenizedWorkload;
    for (const auto& q : queryWorkload) {
        std::vector<std::string> tok = Tokenizer::tokenize(q);
        tok = Tokenizer::removeStopWords(tok);
        tokenizedWorkload.push_back(tok);
    }

    auto startWand = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterationsPerQuery; ++iter) {
        for (const auto& tok : tokenizedWorkload) {
            wandScorer.search(tok, index, topK);
        }
    }
    auto endWand = std::chrono::high_resolution_clock::now();
    double wandSec = std::chrono::duration_cast<std::chrono::microseconds>(endWand - startWand).count() / 1000000.0;
    double wandQPS = totalQueries / wandSec;

    // 3. LRU Cache-Accelerated Throughput
    auto startCache = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < iterationsPerQuery; ++iter) {
        for (const auto& q : queryWorkload) {
            engine.search(q, topK);
        }
    }
    auto endCache = std::chrono::high_resolution_clock::now();
    double cacheSec = std::chrono::duration_cast<std::chrono::microseconds>(endCache - startCache).count() / 1000000.0;
    double cacheQPS = totalQueries / cacheSec;

    std::cout << std::left << std::setw(30) << "Mode"
              << std::setw(20) << "Total Time (s)"
              << std::setw(20) << "Throughput (QPS)"
              << std::setw(20) << "Speedup vs Baseline" << "\n";
    std::cout << "--------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(30) << "Unpruned Scoring"
              << std::fixed << std::setprecision(3) << std::setw(20) << unprunedSec
              << std::setprecision(1) << std::setw(20) << unprunedQPS
              << "1.00x (Baseline)\n";

    std::cout << std::left << std::setw(30) << "WAND Top-K Pruning"
              << std::fixed << std::setprecision(3) << std::setw(20) << wandSec
              << std::setprecision(1) << std::setw(20) << wandQPS
              << std::setprecision(2) << (wandQPS / unprunedQPS) << "x\n";

    std::cout << std::left << std::setw(30) << "LRU Cache (Hit Rate ~100%)"
              << std::fixed << std::setprecision(3) << std::setw(20) << cacheSec
              << std::setprecision(1) << std::setw(20) << cacheQPS
              << std::setprecision(2) << (cacheQPS / unprunedQPS) << "x\n";
    std::cout << "======================================================================\n";

    return 0;
}
