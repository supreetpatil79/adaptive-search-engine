// bench/benchmark_pruning.cpp
// Benchmark comparing Unpruned candidate evaluation vs WAND top-k pruning.

#include "../search/search_engine.h"
#include "../tokenizer/tokenizer.h"
#include "../utils/file_loader.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <numeric>
#include <iomanip>
#include <cmath>

int main(int argc, char* argv[]) {
    std::string dataPath = "data/documents.txt";
    if (argc >= 2) dataPath = argv[1];

    std::vector<Document> baseDocs = FileLoader::loadFromFile(dataPath);
    if (baseDocs.empty()) {
        std::cerr << "Cannot load base corpus from " << dataPath << "\n";
        return 1;
    }

    // Replicate corpus to 10,000 documents with variations
    SearchEngine engine;
    AdaptiveRanker unprunedRanker;
    WANDScorer wandScorer;

    int targetDocs = 2000;
    int currentId = 1;

    for (int r = 0; r < (targetDocs / static_cast<int>(baseDocs.size())) + 1; ++r) {
        for (const auto& doc : baseDocs) {
            std::string content = doc.content + " term_" + std::to_string(r) + " doc_" + std::to_string(currentId);
            engine.addDocument(currentId++, content);
            if (currentId > targetDocs) break;
        }
        if (currentId > targetDocs) break;
    }

    engine.finalizeIndex();
    const auto& index = engine.getIndex();
    std::cout << "Benchmarking corpus size: " << index.getTotalDocuments() << " documents\n";

    std::vector<std::string> testQueries = {
        "artificial intelligence machine learning",
        "neural networks deep learning data science",
        "cloud computing software engineering DevOps",
        "algorithms data statistics optimization",
        "quantum computing parallel processing hardware"
    };

    int iterations = 10;
    int topK = 10;

    std::cout << "\nRunning " << iterations << " iterations per query (bypassing LRU cache)...\n";
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(45) << "Query"
              << std::setw(15) << "Unpruned (µs)"
              << std::setw(15) << "WAND (µs)"
              << std::setw(15) << "Speedup" << "\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    double totalUnprunedUs = 0;
    double totalWandUs = 0;

    for (const auto& q : testQueries) {
        std::vector<std::string> tokens = Tokenizer::tokenize(q);
        tokens = Tokenizer::removeStopWords(tokens);

        // Warmup
        for (int i = 0; i < 10; ++i) {
            unprunedRanker.search(q, index, topK);
            wandScorer.search(tokens, index, topK);
        }

        // Benchmark Unpruned (scoring every document in posting lists)
        auto startUnpruned = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            unprunedRanker.search(q, index, topK);
        }
        auto endUnpruned = std::chrono::high_resolution_clock::now();
        double unprunedUs = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(endUnpruned - startUnpruned).count()) / (iterations * 1000.0);

        // Benchmark WAND (top-k threshold pruning)
        WANDStats stats;
        auto startWand = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            wandScorer.search(tokens, index, topK, &stats);
        }
        auto endWand = std::chrono::high_resolution_clock::now();
        double wandUs = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(endWand - startWand).count()) / (iterations * 1000.0);

        totalUnprunedUs += unprunedUs;
        totalWandUs += wandUs;

        double speedup = (wandUs > 0) ? unprunedUs / wandUs : 1.0;

        std::string shortQ = q.substr(0, 42);
        if (q.size() > 42) shortQ += "...";

        std::cout << std::left << std::setw(45) << shortQ
                  << std::fixed << std::setprecision(2)
                  << std::setw(15) << unprunedUs
                  << std::setw(15) << wandUs
                  << std::setprecision(2) << speedup << "x\n";
    }

    std::cout << "------------------------------------------------------------------------------------\n";
    double avgUnpruned = totalUnprunedUs / testQueries.size();
    double avgWand = totalWandUs / testQueries.size();
    double overallSpeedup = (avgWand > 0) ? avgUnpruned / avgWand : 1.0;

    std::cout << "Average Unpruned Latency: " << std::fixed << std::setprecision(2) << avgUnpruned << " µs\n";
    std::cout << "Average WAND Latency    : " << std::fixed << std::setprecision(2) << avgWand << " µs\n";
    std::cout << "Overall Speedup         : " << std::fixed << std::setprecision(2) << overallSpeedup << "x\n";

    return 0;
}
