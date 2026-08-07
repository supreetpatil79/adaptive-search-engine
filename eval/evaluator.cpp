// eval/evaluator.cpp
// Evaluation Framework computing exact NDCG@10 (Normalized Discounted Cumulative Gain at 10)
// comparing BM25-only Lexical Retrieval vs Hybrid RRF Retrieval.

#include "../search/search_engine.h"
#include "../embed/flat_embed_index.h"
#include "../embed/ort_embedder.h"
#include "../embed/rrf_fusion.h"
#include "../utils/file_loader.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

struct EvalQuery {
    std::string query;
    // Ground truth topic keywords for relevance scoring: 3 = highly relevant, 2 = relevant, 1 = marginal, 0 = irrelevant
    std::vector<std::pair<std::string, double>> relevantKeywords;
};

// Compute ground-truth relevance of document content based on keyword matching
double calculateDocumentRelevance(const std::string& content, const std::vector<std::pair<std::string, double>>& keywords) {
    double rel = 0.0;
    std::string lowerContent = content;
    for (auto& c : lowerContent) c = static_cast<char>(std::tolower(c));

    for (const auto& [kw, weight] : keywords) {
        std::string lowerKw = kw;
        for (auto& c : lowerKw) c = static_cast<char>(std::tolower(c));

        if (lowerContent.find(lowerKw) != std::string::npos) {
            rel += weight;
        }
    }
    return rel;
}

// Compute DCG@K
double computeDCG(const std::vector<double>& gains, int K) {
    double dcg = 0.0;
    int n = std::min(K, static_cast<int>(gains.size()));
    for (int i = 0; i < n; ++i) {
        double rel = gains[i];
        dcg += (std::pow(2.0, rel) - 1.0) / std::log2(i + 2); // i+2 because rank is 1-based (i=0 -> rank 1 -> log2(2))
    }
    return dcg;
}

// Compute Ideal DCG@K
double computeIDCG(std::vector<double> gains, int K) {
    std::sort(gains.begin(), gains.end(), std::greater<double>());
    return computeDCG(gains, K);
}

int main(int argc, char* argv[]) {
    std::string corpusPath = "data/corpus_10k.txt";
    std::string embedPath  = "data/embeddings_10k.bin";
    std::string modelPath  = "models/all-MiniLM-L6-v2.onnx";
    std::string vocabPath  = "models/tokenizer_config/vocab.txt";

    if (argc >= 2) corpusPath = argv[1];
    if (argc >= 3) embedPath  = argv[2];

    std::cout << "======================================================================\n";
    std::cout << "  IR Evaluation Framework — NDCG@10 Comparison (BM25 vs Hybrid RRF)\n";
    std::cout << "======================================================================\n\n";

    // 1. Load 10k Corpus
    std::vector<Document> docs = FileLoader::loadFromFile(corpusPath);
    if (docs.empty()) {
        std::cerr << "Error loading corpus from " << corpusPath << "\n";
        return 1;
    }

    SearchEngine engine;
    for (const auto& doc : docs) {
        engine.addDocument(doc.id, doc.content);
    }
    engine.finalizeIndex();

    // 2. Load Embeddings and ONNX Model
    FlatEmbedIndex embedIndex;
    OrtEmbedder embedder;
    RRFFusion rrf(60);

    bool hybridReady = embedIndex.loadFromFile(embedPath) && embedder.load(modelPath, vocabPath);
    if (!hybridReady) {
        std::cerr << "Error: Hybrid components not ready\n";
        return 1;
    }

    // 3. Ground Truth Evaluation Dataset (10 Test Queries with weighted topic relevance)
    std::vector<EvalQuery> evalSet = {
        {
            "artificial intelligence neural networks",
            {{"artificial intelligence", 3.0}, {"neural networks", 3.0}, {"deep learning", 2.0}, {"computer vision", 2.0}, {"machine learning", 2.0}}
        },
        {
            "cloud computing microservices DevOps",
            {{"cloud computing", 3.0}, {"microservices", 3.0}, {"devops", 3.0}, {"serverless", 2.0}, {"containerized", 2.0}}
        },
        {
            "cybersecurity cryptography ledger privacy",
            {{"cybersecurity", 3.0}, {"cryptographic", 3.0}, {"decentralized", 2.0}, {"zero-trust", 2.0}, {"distributed ledger", 3.0}}
        },
        {
            "quantum computing optimization superposition",
            {{"quantum computing", 3.0}, {"superposition", 3.0}, {"optimization", 2.0}, {"computational", 1.0}}
        },
        {
            "data science statistical inference modeling",
            {{"data science", 3.0}, {"statistical inference", 3.0}, {"predictive modeling", 3.0}, {"feature engineering", 2.0}}
        },
        {
            "natural language processing text semantics",
            {{"natural language processing", 3.0}, {"semantic", 3.0}, {"text patterns", 2.0}, {"documentation", 1.0}}
        },
        {
            "edge computing IoT telemetry analytics",
            {{"edge computing", 3.0}, {"iot", 3.0}, {"telemetry", 3.0}, {"real-time analytics", 2.0}}
        },
        {
            "database management B-trees LSM-trees",
            {{"database management", 3.0}, {"b-trees", 3.0}, {"lsm-trees", 3.0}, {"index records", 2.0}}
        },
        {
            "autonomous vehicle navigation robotics",
            {{"autonomous vehicle", 3.0}, {"robotic", 3.0}, {"navigation", 2.0}, {"sensor networks", 1.0}}
        },
        {
            "genomic sequence processing",
            {{"genomic sequence", 3.0}, {"data processing", 2.0}, {"algorithms", 1.0}}
        }
    };

    int K = 10;
    double totalBM25_NDCG = 0.0;
    double totalHybrid_NDCG = 0.0;

    std::cout << std::left << std::setw(45) << "Query"
              << std::setw(15) << "BM25 NDCG@10"
              << std::setw(15) << "Hybrid NDCG@10"
              << std::setw(15) << "Gain" << "\n";
    std::cout << "--------------------------------------------------------------------------------------\n";

    for (const auto& eq : evalSet) {
        // BM25 Retrieval
        std::vector<SearchResult> bm25Res = engine.search(eq.query, K);
        std::vector<double> bm25Gains;
        for (const auto& r : bm25Res) {
            bm25Gains.push_back(calculateDocumentRelevance(r.content, eq.relevantKeywords));
        }

        // Hybrid Retrieval (BM25 + ONNX Dense + RRF)
        std::vector<float> qvec = embedder.encode(eq.query);
        std::vector<std::pair<int, float>> denseRes = embedIndex.search(qvec.data(), 20);
        std::vector<RRFResult> hybridRes = rrf.fuse(engine.search(eq.query, 20), denseRes, K);

        std::vector<double> hybridGains;
        for (auto& r : hybridRes) {
            if (r.content.empty()) {
                const Document* doc = engine.getIndex().getDocument(r.docId);
                if (doc) r.content = doc->content;
            }
            hybridGains.push_back(calculateDocumentRelevance(r.content, eq.relevantKeywords));
        }

        // Compute NDCG@10 for all candidate gains in corpus
        std::vector<double> allCorpusGains;
        for (const auto& doc : docs) {
            allCorpusGains.push_back(calculateDocumentRelevance(doc.content, eq.relevantKeywords));
        }

        double idcg = computeIDCG(allCorpusGains, K);
        double bm25_dcg = computeDCG(bm25Gains, K);
        double hybrid_dcg = computeDCG(hybridGains, K);

        double bm25_ndcg = (idcg > 0.0) ? bm25_dcg / idcg : 0.0;
        double hybrid_ndcg = (idcg > 0.0) ? hybrid_dcg / idcg : 0.0;

        totalBM25_NDCG += bm25_ndcg;
        totalHybrid_NDCG += hybrid_ndcg;

        double gainPct = (bm25_ndcg > 0.0) ? ((hybrid_ndcg - bm25_ndcg) / bm25_ndcg) * 100.0 : 0.0;

        std::string shortQ = eq.query.substr(0, 42);
        std::cout << std::left << std::setw(45) << shortQ
                  << std::fixed << std::setprecision(4)
                  << std::setw(15) << bm25_ndcg
                  << std::setw(15) << hybrid_ndcg
                  << std::setprecision(1) << (gainPct >= 0 ? "+" : "") << gainPct << "%\n";
    }

    std::cout << "--------------------------------------------------------------------------------------\n";
    double meanBM25_NDCG = totalBM25_NDCG / evalSet.size();
    double meanHybrid_NDCG = totalHybrid_NDCG / evalSet.size();
    double meanGainPct = ((meanHybrid_NDCG - meanBM25_NDCG) / meanBM25_NDCG) * 100.0;

    std::cout << "Mean BM25 NDCG@10   : " << std::fixed << std::setprecision(4) << meanBM25_NDCG << "\n";
    std::cout << "Mean Hybrid NDCG@10 : " << std::fixed << std::setprecision(4) << meanHybrid_NDCG << "\n";
    std::cout << "Overall NDCG Gain   : " << std::fixed << std::setprecision(2) << (meanGainPct >= 0 ? "+" : "") << meanGainPct << "%\n";
    std::cout << "======================================================================\n";

    return 0;
}
