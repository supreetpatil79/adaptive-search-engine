// main.cpp — Adaptive Search Engine entry point
//
// Module call chain:
//   FileLoader          → loads corpus
//   Tokenizer           → tokenise + stop-word removal + Porter stemming
//   InvertedIndex       → positional posting lists with skip pointers
//   BM25 + TFIDF        → lexical scoring (inside AdaptiveRanker)
//   WANDScorer          → top-K WAND pruning for lexical retrieval
//   SpellChecker        → Levenshtein correction against real index vocabulary
//   LRUCache            → 128-slot query-result cache
//   UserProfile         → click-boost personalisation
//   SearchEngine        → lexical façade (spell-correct → stem → BM25 → cache)
//
//   HNSWIndex           → O(log N) approximate nearest-neighbor dense retrieval
//   FlatEmbedIndex      → brute-force fallback (backward compat)
//   OrtEmbedder         → encode query to 384-d unit vector via ONNX Runtime
//   RRFFusion           → Reciprocal Rank Fusion (k=60) of BM25 + dense lists

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../utils/file_loader.h"
#include "../search/search_engine.h"
#include "../embed/flat_embed_index.h"
#include "../embed/hnsw_index.h"
#include "../embed/ort_embedder.h"
#include "../embed/rrf_fusion.h"
#include "../query/snippet_generator.h"
#include "../tokenizer/tokenizer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void printBanner(int numDocs, bool hybridReady, bool usingHNSW) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║       Adaptive Search Engine  v0.3  (C++17)         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "  Lexical : BM25(k1=1.5, b=0.75)×0.70 + TF-IDF×0.30\n";
    std::cout << "  Query   : Porter Stemmer (Steps 1-5) + Spell Correction\n";
    if (hybridReady) {
        std::cout << "  Semantic: all-MiniLM-L6-v2 via ONNX Runtime (384-d)\n";
        std::cout << "  ANN     : " << (usingHNSW ? "HNSW (M=16, efC=200, O(log N))"
                                                  : "FlatEmbedIndex (brute-force)") << "\n";
        std::cout << "  Fusion  : Reciprocal Rank Fusion (k=60)\n";
    } else {
        std::cout << "  Semantic: DISABLED (run scripts/export_model.py +\n";
        std::cout << "            scripts/embed_corpus.py to enable)\n";
    }
    std::cout << "  Cache   : LRU (128 query slots)\n";
    std::cout << "  Corpus  : " << numDocs << " documents\n\n";
    std::cout << "Commands:\n";
    std::cout << "  <query>        hybrid search (lexical + dense if available)\n";
    std::cout << "  bm25 <q>       force lexical-only BM25 search\n";
    std::cout << "  wand <q>       WAND dynamically pruned top-K BM25 search\n";
    std::cout << "  dense <q>      force semantic-only ANN search\n";
    std::cout << "  phrase <q>     positional phrase search (exact sequence)\n";
    std::cout << "  click <N>      record click on result N (personalisation)\n";
    std::cout << "  quit           exit\n";
    std::cout << "──────────────────────────────────────────────────────\n\n";
}

static void printLexicalResults(const std::vector<SearchResult>& results,
                                 const std::string& query,
                                 long long latencyUs) {
    if (results.empty()) {
        std::cout << "  No results for: \"" << query << "\"\n\n";
        return;
    }
    auto qTokens = Tokenizer::tokenize(query);
    std::cout << "  [BM25] Results for: \"" << query << "\""
              << "  [" << latencyUs << " µs]\n";
    std::cout << "  ────────────────────────────────────────────────────\n";
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto& r = results[i];
        std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 130, HighlightFormat::ANSI);
        std::cout << "  " << std::setw(2) << (i + 1)
                  << ".  bm25=" << std::fixed << std::setprecision(4) << r.score
                  << "  doc#" << r.docId << "\n"
                  << "      " << snippet << "\n\n";
    }
}

static void printHybridResults(const std::vector<RRFResult>& results,
                                const std::string& query,
                                long long latencyUs) {
    if (results.empty()) {
        std::cout << "  No results for: \"" << query << "\"\n\n";
        return;
    }
    auto qTokens = Tokenizer::tokenize(query);
    std::cout << "  [Hybrid RRF] Results for: \"" << query << "\""
              << "  [" << latencyUs << " µs]\n";
    std::cout << "  ────────────────────────────────────────────────────\n";
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto& r = results[i];
        std::string snippet = SnippetGenerator::generateSnippet(r.content, qTokens, 130, HighlightFormat::ANSI);
        std::cout << "  " << std::setw(2) << (i + 1)
                  << ".  rrf=" << std::fixed << std::setprecision(5) << r.rrfScore
                  << "  bm25=" << std::setprecision(3) << r.bm25Score
                  << "  cos="  << std::setprecision(3) << r.denseScore
                  << "  doc#"  << r.docId << "\n"
                  << "      " << snippet << "\n\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string dataPath  = "data/documents.txt";
    std::string embedPath = "data/embeddings.bin";
    std::string hnswPath  = "data/hnsw.bin";
    std::string modelPath = "models/all-MiniLM-L6-v2.onnx";
    std::string vocabPath = "models/tokenizer_config/vocab.txt";

    if (argc >= 2) dataPath  = argv[1];
    if (argc >= 3) embedPath = argv[2];

    // ── 1. Load corpus ────────────────────────────────────────────────────
    std::vector<Document> docs = FileLoader::loadFromFile(dataPath);
    if (docs.empty()) {
        std::cerr << "No documents loaded from " << dataPath << ". Aborting.\n";
        return 1;
    }

    // ── O(1) content lookup by docId ──────────────────────────────────────
    // Replaces the O(N) linear scan used to fill missing content in hybrid results.
    std::unordered_map<int, std::string> docContent;
    docContent.reserve(docs.size());
    for (const auto& d : docs) docContent[d.id] = d.content;

    // ── 2. Build lexical index ────────────────────────────────────────────
    SearchEngine engine(128);
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) engine.addDocument(doc.id, doc.content);
        engine.finalizeIndex();
        auto t1 = std::chrono::high_resolution_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
        std::cout << "Lexical index built in " << ms << " ms  ("
                  << engine.totalDocs() << " docs, stemmed vocabulary)\n";
    }

    // ── 3. Load HNSW index (preferred) or flat fallback ──────────────────
    HNSWIndex   hnswIndex;
    FlatEmbedIndex embedIndex;
    OrtEmbedder embedder;
    RRFFusion   rrf(60);
    bool hybridReady = false;
    bool usingHNSW   = false;

    // Try HNSW first
    if (hnswIndex.loadFromFile(hnswPath)) {
        if (embedder.load(modelPath, vocabPath)) {
            hybridReady = true;
            usingHNSW   = true;
            std::cout << "Hybrid path ready  (ORT + HNSW, " << hnswIndex.size() << " nodes)\n";
        }
    }
    // Fall back to FlatEmbedIndex
    if (!hybridReady && embedIndex.loadFromFile(embedPath)) {
        if (embedder.load(modelPath, vocabPath)) {
            hybridReady = true;
            std::cout << "Hybrid path ready  (ORT + FlatEmbedIndex, brute-force)\n";
        }
    }
    if (!hybridReady) {
        std::cout << "No embeddings found — running lexical-only\n";
        std::cout << "  To enable hybrid: python3 scripts/export_model.py\n";
        std::cout << "                    python3 scripts/embed_corpus.py\n";
    }

    printBanner(engine.totalDocs(), hybridReady, usingHNSW);

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
                if (docId != -1) {
                    engine.recordClick(docId);
                    std::cout << "  Recorded click on doc#" << docId << "\n\n";
                } else {
                    std::cout << "  Invalid rank.\n\n";
                }
            } catch (...) { std::cout << "  Usage: click <rank>\n\n"; }
            continue;
        }

        // ── phrase command ───────────────────────────────────────────────
        if (line.rfind("phrase ", 0) == 0) {
            std::string q = line.substr(7);
            auto t0 = std::chrono::high_resolution_clock::now();
            std::vector<SearchResult> phraseRes = engine.searchPhrase(q);
            auto t1 = std::chrono::high_resolution_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();

            if (phraseRes.empty()) {
                std::cout << "  No phrase results for: \"" << q << "\"\n\n";
            } else {
                std::cout << "  [Phrase] Results for: \"" << q << "\""
                          << "  [" << us << " µs]\n";
                std::cout << "  ──────────────────────────────────────────\n";
                for (int i = 0; i < static_cast<int>(phraseRes.size()); ++i) {
                    const auto& r = phraseRes[i];
                    std::cout << "  " << std::setw(2) << (i+1)
                              << ".  doc#" << r.docId << "\n"
                              << "      " << r.content.substr(0, 115)
                              << (r.content.size() > 115 ? "…" : "") << "\n\n";
                }
            }
            lastWasHybrid = false;
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

        // ── wand command ─────────────────────────────────────────────────
        if (line.rfind("wand ", 0) == 0) {
            std::string q = line.substr(5);
            WANDStats stats;
            auto t0 = std::chrono::high_resolution_clock::now();
            lastLexical = engine.searchWAND(q, 10, &stats);
            auto t1 = std::chrono::high_resolution_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();
            printLexicalResults(lastLexical, q, us);
            std::cout << "  [WAND Stats] Skipped: " << stats.totalCandidatesSkipped
                      << " postings | Evaluated: " << stats.totalCandidatesEvaluated << "\n\n";
            lastWasHybrid = false;
            continue;
        }

        // ── force-dense command ──────────────────────────────────────────
        if (line.rfind("dense ", 0) == 0 && hybridReady) {
            std::string q = line.substr(6);
            auto t0 = std::chrono::high_resolution_clock::now();
            std::vector<float> qvec = embedder.encode(q);
            auto t1 = std::chrono::high_resolution_clock::now();
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();

            std::cout << "  [Dense] Results for: \"" << q << "\"  [" << us << " µs]\n";
            std::cout << "  ──────────────────────────────────────────\n";

            if (usingHNSW) {
                auto denseRes = hnswIndex.search(qvec.data(), 10, 50);
                for (int i = 0; i < static_cast<int>(denseRes.size()); ++i) {
                    std::cout << "  " << std::setw(2) << (i+1)
                              << ".  cos=" << std::fixed << std::setprecision(4)
                              << denseRes[i].distance << "  doc#" << denseRes[i].docId << "\n\n";
                }
            } else {
                auto denseRes = embedIndex.search(qvec.data(), 10);
                for (int i = 0; i < static_cast<int>(denseRes.size()); ++i) {
                    std::cout << "  " << std::setw(2) << (i+1)
                              << ".  cos=" << std::fixed << std::setprecision(4)
                              << denseRes[i].second << "  doc#" << denseRes[i].first << "\n\n";
                }
            }
            continue;
        }

        // ── Normal query — hybrid if ready, lexical otherwise ────────────
        auto t0 = std::chrono::high_resolution_clock::now();
        if (hybridReady) {
            lastLexical = engine.search(line, 20);
            std::vector<float> qvec = embedder.encode(line);

            std::vector<std::pair<int,float>> denseRes;
            if (usingHNSW) {
                for (auto& r : hnswIndex.search(qvec.data(), 20, 50))
                    denseRes.emplace_back(r.docId, r.distance);
            } else {
                denseRes = embedIndex.search(qvec.data(), 20);
            }

            lastHybrid = rrf.fuse(lastLexical, denseRes, 10);

            // O(1) content fill using pre-built unordered_map
            for (auto& r : lastHybrid) {
                if (r.content.empty()) {
                    auto it = docContent.find(r.docId);
                    if (it != docContent.end()) r.content = it->second;
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
