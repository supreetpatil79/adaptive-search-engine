#ifndef SQ8_INDEX_H
#define SQ8_INDEX_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// SQ8Index — 8-bit Scalar Quantization Index for dense embedding vectors.
// Compresses 384-dim float32 vectors (1536 bytes) to uint8 (384 bytes) + 8-byte scalar header (392 bytes total).
// Provides ~74.5% memory reduction with Asymmetric Distance Computation (ADC).
//
// Quantization:
//   q[i] = round((x[i] - min_val) / scale)   with scale = (max_val - min_val) / 255.0
//
// Dequantization / Asymmetric Inner Product:
//   dot(Q, D) = sum(Q[i] * (min_val + D[i] * scale))
//             = min_val * sum(Q[i]) + scale * sum(Q[i] * D[i])

struct QuantizedVector {
    int docId;
    float minVal;
    float scale;
    std::vector<uint8_t> quantized;
};

class SQ8Index {
public:
    explicit SQ8Index(int dim = 384);

    // Quantize and insert a single float32 vector
    void add(int docId, const float* vector);

    // Build quantized index from flat float32 array (numDocs * dim)
    void buildFromFlat(const float* data, int numDocs, int dim = 384);

    // Search top-K nearest neighbors using Asymmetric Distance Computation (ADC)
    std::vector<std::pair<int, float>> search(const float* queryVec, int topK = 10) const;

    // Serialization: save / load to binary file
    bool saveToFile(const std::string& filepath) const;
    bool loadFromFile(const std::string& filepath);

    int size() const { return static_cast<int>(vectors_.size()); }
    int dim() const { return dim_; }
    size_t memoryBytes() const;

private:
    int dim_;
    std::vector<QuantizedVector> vectors_;
};

#endif // SQ8_INDEX_H
