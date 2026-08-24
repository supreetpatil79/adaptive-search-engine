// src/server_main.cpp
// Entry point for launching Adaptive Search Engine HTTP REST & Metrics Server.

#include "../server/search_server.h"
#include "../utils/file_loader.h"
#include <iostream>
#include <string>
#include <unordered_map>

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string dataPath  = "data/documents.txt";
    std::string embedPath = "data/embeddings.bin";
    std::string hnswPath  = "data/hnsw.bin";
    std::string modelPath = "models/all-MiniLM-L6-v2.onnx";
    std::string vocabPath = "models/tokenizer_config/vocab.txt";

    if (argc >= 2) {
        try { port = std::stoi(argv[1]); } catch (...) {}
    }
    if (argc >= 3) dataPath = argv[2];

    std::cout << "======================================================================\n";
    std::cout << "  Adaptive Search Engine — REST HTTP Server (Port " << port << ")\n";
    std::cout << "======================================================================\n";

    // 1. Load corpus
    std::vector<Document> docs = FileLoader::loadFromFile(dataPath);
    if (docs.empty()) {
        std::cerr << "Error: No documents loaded from " << dataPath << "\n";
        return 1;
    }

    std::unordered_map<int, std::string> docContent;
    docContent.reserve(docs.size());
    for (const auto& d : docs) docContent[d.id] = d.content;

    // 2. Index documents
    SearchEngine engine(1024);
    for (const auto& d : docs) engine.addDocument(d.id, d.content);
    engine.finalizeIndex();
    std::cout << "Indexed " << engine.totalDocs() << " documents into Inverted Index.\n";

    // 3. Dense embeddings
    HNSWIndex hnswIndex;
    FlatEmbedIndex embedIndex;
    OrtEmbedder embedder;
    bool hybridReady = false;

    if (hnswIndex.loadFromFile(hnswPath) && embedder.load(modelPath, vocabPath)) {
        hybridReady = true;
        std::cout << "HNSW ANN Index loaded (" << hnswIndex.size() << " vectors)\n";
    } else if (embedIndex.loadFromFile(embedPath) && embedder.load(modelPath, vocabPath)) {
        hybridReady = true;
        std::cout << "Flat Embedding Index loaded (" << embedIndex.size() << " vectors)\n";
    }

    if (!hybridReady) {
        std::cout << "Note: Running in lexical-only mode.\n";
    }

    // 4. Start Server
    SearchServer server(engine,
                        hybridReady ? (hnswIndex.size() > 0 ? &hnswIndex : nullptr) : nullptr,
                        hybridReady ? (hnswIndex.size() == 0 ? &embedIndex : nullptr) : nullptr,
                        hybridReady ? &embedder : nullptr,
                        docContent);

    std::cout << "\nEndpoints:\n";
    std::cout << "  GET  http://localhost:" << port << "/search?q=machine+learning&mode=hybrid&k=5\n";
    std::cout << "  GET  http://localhost:" << port << "/search?q=cloud+computing&mode=wand\n";
    std::cout << "  GET  http://localhost:" << port << "/metrics\n";
    std::cout << "  GET  http://localhost:" << port << "/health\n";
    std::cout << "  POST http://localhost:" << port << "/click (body: {\"docId\": 1})\n\n";

    server.start(port, false); // blocking
    return 0;
}
