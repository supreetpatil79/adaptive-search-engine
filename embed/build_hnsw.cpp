// embed/build_hnsw.cpp
// Offline utility to convert flat binary embeddings (data/embeddings.bin)
// into an HNSW graph index (data/hnsw.bin).

#include "flat_embed_index.h"
#include "hnsw_index.h"
#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string embedPath = "data/embeddings.bin";
    std::string hnswPath  = "data/hnsw.bin";

    if (argc >= 2) embedPath = argv[1];
    if (argc >= 3) hnswPath  = argv[2];

    std::cout << "Loading embeddings from: " << embedPath << "\n";
    FlatEmbedIndex flat;
    if (!flat.loadFromFile(embedPath)) {
        std::cerr << "Error: Could not load embeddings from " << embedPath << "\n";
        return 1;
    }

    int numDocs = flat.numDocs();
    int dim     = flat.dim();
    std::cout << "Building HNSW index for " << numDocs << " documents ("
              << dim << "-d, M=16, efConstruction=200)...\n";

    HNSWIndex hnsw(dim, 16, 200);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numDocs; ++i) {
        int docId = i + 1; // 1-based docId
        hnsw.insert(docId, flat.rowPtr(i));
        if ((i + 1) % 1000 == 0 || i + 1 == numDocs) {
            std::cout << "\rInserted " << (i + 1) << " / " << numDocs << " vectors..." << std::flush;
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "\nGraph construction completed in " << ms << " ms.\n";

    std::cout << "Saving HNSW index to: " << hnswPath << "\n";
    if (!hnsw.saveToFile(hnswPath)) {
        std::cerr << "Error: Failed to save HNSW index to " << hnswPath << "\n";
        return 1;
    }

    std::cout << "Done! HNSW index ready.\n";
    return 0;
}
