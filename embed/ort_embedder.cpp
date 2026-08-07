#include "ort_embedder.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

// ── Load ────────────────────────────────────────────────────────────────────

bool OrtEmbedder::load(const std::string& modelPath, const std::string& vocabPath) {
    // 1. Load vocabulary from vocab.txt (one token per line, index = token_id).
    {
        std::ifstream f(vocabPath);
        if (!f.is_open()) {
            std::cerr << "[OrtEmbedder] Cannot open vocab: " << vocabPath << "\n";
            return false;
        }
        std::string tok;
        while (std::getline(f, tok)) {
            vocab_.push_back(tok);
        }
        std::cout << "[OrtEmbedder] Vocab size: " << vocab_.size() << " tokens\n";
    }

    // 2. Create ONNX Runtime session.
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = Ort::Session(env_, modelPath.c_str(), opts);
        sessionLoaded_ = true;
        std::cout << "[OrtEmbedder] Session loaded: " << modelPath << "\n";
    } catch (const Ort::Exception& e) {
        std::cerr << "[OrtEmbedder] ORT error: " << e.what() << "\n";
        return false;
    }
    return true;
}

// ── Encode ──────────────────────────────────────────────────────────────────

std::vector<float> OrtEmbedder::encode(const std::string& text) const {
    if (!sessionLoaded_) return {};

    TokenisedInput tok = tokenise(text);
    int seqLen = static_cast<int>(tok.input_ids.size());

    // Shape: (batch=1, seq_len)
    std::array<int64_t, 2> shape = {1, seqLen};

    auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value inputs[3] = {
        Ort::Value::CreateTensor<int64_t>(memInfo,
            tok.input_ids.data(),      tok.input_ids.size(),      shape.data(), 2),
        Ort::Value::CreateTensor<int64_t>(memInfo,
            tok.attention_mask.data(), tok.attention_mask.size(), shape.data(), 2),
        Ort::Value::CreateTensor<int64_t>(memInfo,
            tok.token_type_ids.data(),tok.token_type_ids.size(),  shape.data(), 2),
    };

    const char* inputNames[]  = {"input_ids", "attention_mask", "token_type_ids"};
    const char* outputNames[] = {"sentence_embedding"};

    std::vector<Ort::Value> outputs;
    try {
        outputs = const_cast<Ort::Session&>(session_).Run(
            Ort::RunOptions{nullptr},
            inputNames,  inputs,  3,
            outputNames, 1);
    } catch (const Ort::Exception& e) {
        std::cerr << "[OrtEmbedder] Inference error: " << e.what() << "\n";
        return {};
    }

    // Output shape: (1, 384)
    float* data = outputs[0].GetTensorMutableData<float>();
    auto   info = outputs[0].GetTensorTypeAndShapeInfo();
    std::size_t total = info.GetElementCount();   // should be 384

    return std::vector<float>(data, data + total);
}

// ── Tokenisation ─────────────────────────────────────────────────────────────

std::string OrtEmbedder::basicClean(const std::string& text) const {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        // Lowercase ASCII letters; drop non-ascii / control chars.
        if (c >= 'A' && c <= 'Z') { out += static_cast<char>(c + 32); }
        else if (c < 128)          { out += static_cast<char>(c); }
        // Non-ASCII bytes dropped — good enough for English queries.
    }
    return out;
}

std::vector<int64_t> OrtEmbedder::wordPiece(const std::string& word) const {
    // Build a reverse lookup: token → id (done once, lazily; OK for query-length strings).
    // For a full production tokeniser you'd build this map at load time.
    static std::unordered_map<std::string, int64_t> vocabMap;
    static bool mapBuilt = false;
    if (!mapBuilt) {
        for (int64_t i = 0; i < static_cast<int64_t>(vocab_.size()); ++i) {
            vocabMap[vocab_[i]] = i;
        }
        mapBuilt = true;
    }

    std::vector<int64_t> ids;
    if (word.empty()) return ids;

    // Greedy longest-match WordPiece.
    std::size_t start = 0;
    bool first = true;
    while (start < word.size()) {
        bool found = false;
        for (std::size_t end = word.size(); end > start; --end) {
            std::string sub = (first ? "" : "##") + word.substr(start, end - start);
            auto it = vocabMap.find(sub);
            if (it != vocabMap.end()) {
                ids.push_back(it->second);
                start = end;
                first = false;
                found = true;
                break;
            }
        }
        if (!found) {
            ids.push_back(UNK_ID);
            break;
        }
    }
    return ids;
}

OrtEmbedder::TokenisedInput OrtEmbedder::tokenise(const std::string& text) const {
    std::string clean = basicClean(text);
    std::istringstream iss(clean);
    std::string word;

    std::vector<int64_t> ids;
    ids.push_back(CLS_ID);

    while (iss >> word && ids.size() < static_cast<std::size_t>(MAX_SEQ - 1)) {
        for (int64_t id : wordPiece(word)) {
            ids.push_back(id);
            if (ids.size() >= static_cast<std::size_t>(MAX_SEQ - 1)) break;
        }
    }
    ids.push_back(SEP_ID);

    int seqLen = static_cast<int>(ids.size());
    TokenisedInput result;
    result.input_ids.resize(seqLen);
    result.attention_mask.resize(seqLen, 1);
    result.token_type_ids.resize(seqLen, 0);
    for (int i = 0; i < seqLen; ++i) result.input_ids[i] = ids[i];

    return result;
}
