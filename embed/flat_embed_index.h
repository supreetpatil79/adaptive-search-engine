#ifndef FLAT_EMBED_INDEX_H
#define FLAT_EMBED_INDEX_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// FlatEmbedIndex — brute-force cosine similarity over pre-computed embeddings.
//
// Because the ONNX model bakes in L2-normalisation (all vectors are unit
// vectors), cosine similarity reduces to a plain dot product:
//   cos(a, b) = a · b  (when ||a|| = ||b|| = 1)
//
// At 30–10k documents × 384 dims, brute-force O(N·D) is fast enough:
//   10k docs × 384 dims × 4 bytes = 15 MB, one pass ≈ 3–5 ms on Apple M-series.
//
// Binary file format (data/embeddings.bin):
//   [0..3]   int32  num_docs
//   [4..7]   int32  dim
//   [8..]    float32[num_docs * dim]  row-major, unit-norm vectors

class FlatEmbedIndex {
public:
    FlatEmbedIndex() = default;

    // Load pre-computed embeddings from binary file.
    // Returns false and prints an error if the file cannot be opened.
    bool loadFromFile(const std::string& path);

    // Returns the top-K (docId, cosineSim) pairs sorted by score descending.
    // queryVec must have exactly dim() elements and be unit-normalised.
    std::vector<std::pair<int, float>> search(const float* queryVec, int topK) const;

    int  numDocs()    const { return numDocs_;  }
    int  size()       const { return numDocs_;  }
    int  dim()        const { return dim_;      }
    bool isLoaded()   const { return !data_.empty(); }

    const float* rowPtr(int docIdx) const {
        return data_.data() + static_cast<std::size_t>(docIdx) * dim_;
    }

private:
    int                  numDocs_ = 0;
    int                  dim_     = 0;
    std::vector<float>   data_;   // flat row-major [numDocs_ × dim_]
};


#endif // FLAT_EMBED_INDEX_H
