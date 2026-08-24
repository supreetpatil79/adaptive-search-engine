#ifndef CROSS_ENCODER_H
#define CROSS_ENCODER_H

#include "../adaptive/adaptive_ranker.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration of ONNX Runtime types
namespace Ort {
    struct Env;
    struct Session;
    struct SessionOptions;
    struct MemoryInfo;
}

// CrossEncoder — Stage-2 Deep Neural Re-Ranker.
// Evaluates full cross-attention between Query and Document candidate text.
// Produces calibrated relevance scores: Score(q, d) = Sigmoid(Linear(BERT([CLS] q [SEP] d [SEP])))
//
// When a full cross-encoder ONNX model is loaded, it executes deep cross-attention inference.
// It also provides a high-performance fast lexical-semantic cross-scoring fallback.

struct RankedCandidate {
    int docId;
    double score;
    double l1Score;
    std::string content;
};

class CrossEncoder {
public:
    CrossEncoder();
    ~CrossEncoder();

    // Load Cross-Encoder ONNX model and vocab
    bool load(const std::string& modelPath, const std::string& vocabPath);

    // Re-rank Top-N L1 candidates into Top-K precision-ranked candidates
    std::vector<RankedCandidate> rerank(
        const std::string& query,
        const std::vector<SearchResult>& candidates,
        int topK = 10
    );

    // Score a single (query, docText) pair
    float scorePair(const std::string& query, const std::string& docText);

    bool isLoaded() const { return isLoaded_; }

private:
    std::vector<int64_t> tokenizePair(const std::string& query, const std::string& docText, int maxLen = 128);
    std::vector<std::string> wordPiece(const std::string& token);

    bool isLoaded_{false};
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::SessionOptions> sessionOptions_;
    std::unordered_map<std::string, int64_t> vocabMap_;
};

#endif // CROSS_ENCODER_H
