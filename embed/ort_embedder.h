#ifndef ORT_EMBEDDER_H
#define ORT_EMBEDDER_H

// ONNX Runtime C++ API — header-included from the prebuilt SDK.
// Include path set in CMakeLists.txt via ORT_INCLUDE_DIR.
#include <onnxruntime_cxx_api.h>

#include <string>
#include <unordered_map>
#include <vector>

// OrtEmbedder — encodes a single query string into a 384-dim unit vector
// using ONNX Runtime's C++ API, with a bundled whitespace tokeniser that
// maps words to WordPiece token IDs from vocab.txt.
//
// Design: We keep a minimal byte-pair tokeniser here (not the full HuggingFace
// C++ tokeniser) because at query time we only need to handle short strings
// (≤128 tokens).  For longer documents we use the offline Python script.
//
// The model's mean-pooling and L2-normalisation are baked into the ONNX graph,
// so Run() returns a (1, 384) unit vector directly.

class OrtEmbedder {
public:
    OrtEmbedder() = default;

    // Load the ONNX model and vocab.
    // modelPath: path to all-MiniLM-L6-v2.onnx
    // vocabPath: path to models/tokenizer_config/vocab.txt
    // Returns false on failure.
    bool load(const std::string& modelPath, const std::string& vocabPath);

    // Encode text to a 384-dim unit vector.
    // Returns empty vector on failure.
    std::vector<float> encode(const std::string& text) const;

    bool isLoaded() const { return sessionLoaded_; }
    int  dim()      const { return 384; }

private:
    // ORT environment and session — Ort::Env must outlive session.
    Ort::Env           env_{ORT_LOGGING_LEVEL_WARNING, "adaptive-search"};
    mutable Ort::Session session_{nullptr};   // mutable: Run() is logically const
    Ort::AllocatorWithDefaultOptions allocator_;
    bool               sessionLoaded_ = false;

    // Vocabulary: index → token string (used by wordPiece for reverse lookup).
    std::vector<std::string> vocab_;   // index = token_id

    // Reverse lookup map built once at load() time (token_string → id).
    // Stored as a member to avoid static and thread-safety concerns.
    std::unordered_map<std::string, int64_t> vocabMap_;

    // Tokenise text → input_ids, attention_mask, token_type_ids
    // using basic WordPiece (max_seq_len = 128, CLS=101, SEP=102, PAD=0).
    struct TokenisedInput {
        std::vector<int64_t> input_ids;
        std::vector<int64_t> attention_mask;
        std::vector<int64_t> token_type_ids;
    };
    TokenisedInput tokenise(const std::string& text) const;

    // Lowercase + strip accents (replicates bert_tokenizer basic_tokenizer).
    std::string basicClean(const std::string& text) const;

    // Greedy WordPiece segmentation of a single word.
    std::vector<int64_t> wordPiece(const std::string& word) const;

    static constexpr int MAX_SEQ = 128;
    static constexpr int64_t CLS_ID = 101;
    static constexpr int64_t SEP_ID = 102;
    static constexpr int64_t PAD_ID = 0;
    static constexpr int64_t UNK_ID = 100;
};

#endif // ORT_EMBEDDER_H
