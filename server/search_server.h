#ifndef SEARCH_SERVER_H
#define SEARCH_SERVER_H

#include "../search/search_engine.h"
#include "../embed/hnsw_index.h"
#include "../embed/flat_embed_index.h"
#include "../embed/ort_embedder.h"
#include "../embed/rrf_fusion.h"
#include "../index/tombstone.h"
#include "../ranking/cross_encoder.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// SearchServer — Production HTTP REST & Metrics Server for Adaptive Search Engine.
// Exposes:
//   GET    /          (Interactive Web UI Search Engine Dashboard)
//   GET    /health    (JSON Health status)
//   GET    /metrics   (Prometheus format)
//   GET    /search?q=<query>&mode=<hybrid|rerank|bm25|wand|phrase>&k=<topK>
//   POST   /click     {"docId": <id>}
//   POST   /document  {"docId": <id>, "content": "..."}
//   DELETE /document  {"docId": <id>}

class SearchServer {
public:
    SearchServer(SearchEngine& engine,
                 HNSWIndex* hnswIndex = nullptr,
                 FlatEmbedIndex* flatIndex = nullptr,
                 OrtEmbedder* embedder = nullptr,
                 const std::unordered_map<int, std::string>& docContent = {});
    ~SearchServer();

    // Start server on given port (blocking or non-blocking)
    bool start(int port = 8080, bool background = false);

    // Stop listening server
    void stop();

    bool isRunning() const { return running_.load(); }

    TombstoneManager& tombstones() { return tombstones_; }

private:
    void serverLoop(int serverFd);
    void handleClient(int clientFd);

    std::string handleWebUI();
    std::string handleSearch(const std::string& query, const std::string& mode, int topK);
    std::string handleMetrics();
    std::string handleHealth();
    std::string handleClick(const std::string& body);
    std::string handleInsertDocument(const std::string& body);
    std::string handleDeleteDocument(const std::string& body);

    SearchEngine& engine_;
    HNSWIndex* hnswIndex_;
    FlatEmbedIndex* flatIndex_;
    OrtEmbedder* embedder_;
    RRFFusion rrf_{60};
    CrossEncoder crossEncoder_;
    std::unordered_map<int, std::string> docContent_;
    TombstoneManager tombstones_;

    int serverFd_{-1};
    int port_{8080};
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    std::chrono::steady_clock::time_point startTime_;

    // Metrics counters
    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> totalSearchRequests_{0};
    std::atomic<uint64_t> totalLatencyUs_{0};
    std::atomic<uint64_t> totalClicks_{0};
};

#endif // SEARCH_SERVER_H
