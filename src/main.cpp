// main.cpp — Adaptive Search Engine entry point
//
// Module call chain:
//   FileLoader      → loads corpus
//   Tokenizer       → tokenise + stop-word removal (inside FileLoader)
//   InvertedIndex   → posting lists (inside SearchEngine::addDocument)
//   BM25 + TFIDF    → lexical scoring (inside AdaptiveRanker)
//   LRUCache        → query-result cache (inside SearchEngine)
//   UserProfile     → click-boost personalisation
//   SearchEngine    → lexical façade
//
//   FlatEmbedIndex  → brute-force cosine similarity over pre-computed vecs
//   OrtEmbedder     → encode query to 384-d unit vector via ONNX Runtime
//   RRFFusion       → Reciprocal Rank Fusion (k=60) of BM25 + dense lists

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../utils/file_loader.h"
#include "../search/search_engine.h"
#include "../embed/flat_embed_index.h"
#include "../embed/ort_embedder.h"
#include "../embed/rrf_fusion.h"

// ──────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────

static void printBanner(int numDocs, bool hybridReady) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║       Adaptive Search Engine  v0.2  (C++17)         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "  Lexical : BM25(k1=1.5, b=0.75)×0.70 + TF-IDF×0.30\n";
    if (hybridReady) {
        std::cout << "  Semantic: all-MiniLM-L6-v2 via ONNX Runtime (384-d)\n";
        std::cout << "  Fusion  : Reciprocal Rank Fusion (k=60)\n";
    } else {
        std::cout << "  Semantic: DISABLED (run scripts/export_model.py +\n";
        std::cout << "            scripts/embed_corpus.py to enable)\n";
    }
    std::cout << "  Cache   : LRU (128 query slots)\n";
    std::cout << "  Corpus  : " << numDocs << " documents\n\n";
    std::cout << "Commands:\n";
    std::cout << "  <query>      search (hybrid if available, lexical otherwise)\n";
    std::cout << "  bm25 <q>     force lexical-only search\n";
    std::cout << "  dense <q>    force semantic-only search\n";
    std::cout << "  click <N>    record click on result N (personalisation)\n";
    std::cout << "  quit         exit\n";
    std::cout << "──────────────────────────────────────────────────────\n\n";
}

static void printLexicalResults(const std::vector<SearchResult>& results,
                                 const std::string& query,
                                 long long latencyUs) {
    if (results.empty()) {
        std::cout << "  No results for: \"" << query << "\"\n\n";
        return;
    }
    std::cout << "  [BM25] Results for: \"" << query << "\""
              << "  [" << latencyUs << " µs]\n";
    std::cout << "  ────────────────────────────────────────────────────\n";
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto& r = results[i];
        std::cout << "  " << std::setw(2) << (i + 1)
                  << ".  bm25=" << std::fixed << std::setprecision(4) << r.score
                  << "  doc#" << r.docId << "\n"
                  << "      " << r.content.substr(0, 115)
                  << (r.content.size() > 115 ? "…" : "") << "\n\n";
    }
}

static void printHybridResults(const std::vector<RRFResult>& results,
                                const std::string& query,
                                long long latencyUs) {
    if (results.empty()) {
        std::cout << "  No results for: \"" << query << "\"\n\n";
        return;
    }
    std::cout << "  [Hybrid RRF] Results for: \"" << query << "\""
              << "  [" << latencyUs << " µs]\n";
    std::cout << "  ────────────────────────────────────────────────────\n";
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto& r = results[i];
        std::cout << "  " << std::setw(2) << (i + 1)
                  << ".  rrf=" << std::fixed << std::setprecision(5) << r.rrfScore
                  << "  bm25=" << std::setprecision(3) << r.bm25Score
                  << "  cos="  << std::setprecision(3) << r.denseScore
                  << "  doc#"  << r.docId << "\n"
                  << "      " << r.content.substr(0, 110)
                  << (r.content.size() > 110 ? "…" : "") << "\n\n";
    }
}

// ──────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string dataPath   = "data/documents.txt";
    std::string embedPath  = "data/embeddings.bin";
    std::string modelPath  = "models/all-MiniLM-L6-v2.onnx";
    std::string vocabPath  = "models/tokenizer_config/vocab.txt";

    if (argc >= 2) dataPath  = argv[1];
    if (argc >= 3) embedPath = argv[2];

    // ── 1. Load corpus ────────────────────────────────────────────────────
    std::vector<Document> docs = FileLoader::loadFromFile(dataPath);
    if (docs.empty()) {
        std::cerr << "No documents loaded from " << dataPath << ". Aborting.\n";
        return 1;
    }

    // ── 2. Build lexical index ────────────────────────────────────────────
    SearchEngine engine(128);
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) {
            engine.addDocument(doc.id, doc.content);
        }
        engine.finalizeIndex();
        auto t1 = std::chrono::high_resolution_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
        std::cout << "Lexical index built in " << ms << " ms  ("
                  << engine.totalDocs() << " docs)\n";
    }

    // ── 3. Load embedding index (optional — graceful degradation) ─────────
    FlatEmbedIndex embedIndex;
    OrtEmbedder    embedder;
    RRFFusion      rrf(60);
    bool hybridReady = false;

    if (embedIndex.loadFromFile(embedPath)) {
        if (embedder.load(modelPath, vocabPath)) {
            hybridReady = true;
            std::cout << "Hybrid path ready  (ORT + FlatEmbedIndex)\n";
        } else {
            std::cerr << "ORT session failed — falling back to lexical-only\n";
        }
    } else {
        std::cout << "No embeddings file found — running lexical-only\n";
        std::cout << "  To enable hybrid: python3 scripts/export_model.py\n";
        std::cout << "                    python3 scripts/embed_corpus.py\n";
    }

    printBanner(engine.totalDocs(), hybridReady);

    // ── 4. Interactive loop ───────────────────────────────────────────────
    std::string line;
    std::vector<SearchResult> lastLexical;
    std::vector<RRFResult>    lastHybrid;
    bool lastWasHybrid = false;

    while (true) {
        std::cout << "search> ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;

        // Trim
        auto f = line.find_first_not_of(" \t\r\n");
        auto l = line.find_last_not_of(" \t\r\n");
        if (f == std::string::npos) { std::cout << "\n"; continue; }
        line = line.substr(f, l - f + 1);

        if (line == "quit" || line == "exit") break;

        // ── click command ────────────────────────────────────────────────
        if (line.rfind("click ", 0) == 0) {
            try {
                int rank = std::stoi(line.substr(6));
                int docId = -1;
                if (lastWasHybrid) {
                    if (rank >= 1 && rank <= static_cast<int>(lastHybrid.size()))
                        docId = lastHybrid[rank-1].docId;
                } else {
                    if (rank >= 1 && rank <= static_cast<int>(lastLexical.size()))
                        docId = lastLexical[rank-1].docId;
                }
                if (docId > 0) {
                    engine.recordClick(docId);
                    std::cout << "  Recorded click on doc#" << docId << "\n\n";
                } else {
                    std::cout << "  Invalid rank.\n\n";
                }
            } catch (...) { std::cout << "  Usage: click <rank>\n\n"; }
            continue;
        }

        // ── force-bm25 command ───────────────────────────────────────────
        if (line.rfind("bm25 ", 0) == 0) {
            std::string q = line.substr(5);
            auto t0 = std::chrono::high_resolution_clock::now();
            lastLexical = engine.search(q, 10);
            auto t1 = std::chrono::high_resolution_clock::now();
            printLexicalResults(lastLexical, q,
                std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count());
            lastWasHybrid = false;
            continue;
        }

        // ── force-dense command ──────────────────────────────────────────
        if (line.rfind("dense ", 0) == 0 && hybridReady) {
            std::string q = line.substr(6);
            auto t0 = std::chrono::high_resolution_clock::now();
            std::vector<float> qvec = embedder.encode(q);
            std::vector<std::pair<int,float>> denseRes =
                embedIndex.search(qvec.data(), 10);
            auto t1 = std::chrono::high_resolution_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();

            std::cout << "  [Dense] Results for: \"" << q << "\"  [" << us << " µs]\n";
            std::cout << "  ──────────────────────────────────────────\n";
            for (int i = 0; i < static_cast<int>(denseRes.size()); ++i) {
                std::cout << "  " << std::setw(2) << (i+1)
                          << ".  cos=" << std::fixed << std::setprecision(4)
                          << denseRes[i].second << "  doc#" << denseRes[i].first << "\n\n";
            }
            continue;
        }

        // ── Normal query — hybrid if ready, lexical otherwise ────────────
        auto t0 = std::chrono::high_resolution_clock::now();
        if (hybridReady) {
            // Lexical leg
            lastLexical = engine.search(line, 20);  // wider window for fusion
            // Dense leg
            std::vector<float> qvec = embedder.encode(line);
            std::vector<std::pair<int,float>> denseRes =
                embedIndex.search(qvec.data(), 20);
            // Fuse
            lastHybrid = rrf.fuse(lastLexical, denseRes, 10);
            // Fill missing content for dense-only results
            for (auto& r : lastHybrid) {
                if (r.content.empty()) {
                    // Search engine doesn't expose index directly; look up in docs.
                    for (const auto& d : docs) {
                        if (d.id == r.docId) { r.content = d.content; break; }
                    }
                }
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            printHybridResults(lastHybrid, line,
                std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count());
            lastWasHybrid = true;
        } else {
            lastLexical = engine.search(line, 10);
            auto t1 = std::chrono::high_resolution_clock::now();
            printLexicalResults(lastLexical, line,
                std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count());
            lastWasHybrid = false;
        }
    }

    std::cout << "\nGoodbye.\n";
    return 0;
}
