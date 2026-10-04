#ifndef METADATA_INDEX_H
#define METADATA_INDEX_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// MetadataBitset — Compact dense bitset for fast bitwise filter evaluation (AND/OR/NOT).
class MetadataBitset {
public:
    MetadataBitset() = default;
    explicit MetadataBitset(size_t numBits, bool setAll = false) : bits_((numBits + 63) / 64, setAll ? ~0ULL : 0), size_(numBits), isMatchAll_(false) {
        if (setAll && numBits % 64 != 0) {
            bits_.back() &= (1ULL << (numBits % 64)) - 1;
        }
    }

    static MetadataBitset matchAll() {
        MetadataBitset b;
        b.isMatchAll_ = true;
        return b;
    }

    bool isMatchAll() const { return isMatchAll_; }

    void set(size_t index) {
        if (isMatchAll_) return;
        if (index >= size_) {
            size_ = index + 1;
            bits_.resize((size_ + 63) / 64, 0);
        }
        bits_[index / 64] |= (1ULL << (index % 64));
    }

    void unset(size_t index) {
        if (index < size_) {
            bits_[index / 64] &= ~(1ULL << (index % 64));
        }
    }

    bool test(size_t index) const {
        if (isMatchAll_) return true;
        if (index >= size_) return false;
        return (bits_[index / 64] & (1ULL << (index % 64))) != 0;
    }

    MetadataBitset bitwiseAnd(const MetadataBitset& other) const {
        if (isMatchAll_) return other;
        if (other.isMatchAll_) return *this;
        size_t minSize = std::min(size_, other.size_);
        MetadataBitset result(minSize);
        size_t words = (minSize + 63) / 64;
        for (size_t i = 0; i < words; ++i) {
            result.bits_[i] = bits_[i] & other.bits_[i];
        }
        return result;
    }

    MetadataBitset bitwiseOr(const MetadataBitset& other) const {
        if (isMatchAll_ || other.isMatchAll_) return matchAll();
        size_t maxSize = std::max(size_, other.size_);
        MetadataBitset result(maxSize);
        size_t words = (maxSize + 63) / 64;
        for (size_t i = 0; i < words; ++i) {
            uint64_t w1 = (i < bits_.size()) ? bits_[i] : 0;
            uint64_t w2 = (i < other.bits_.size()) ? other.bits_[i] : 0;
            result.bits_[i] = w1 | w2;
        }
        return result;
    }

    size_t count() const {
        if (isMatchAll_) return SIZE_MAX;
        size_t total = 0;
        for (uint64_t w : bits_) {
            total += __builtin_popcountll(w);
        }
        return total;
    }

    bool empty() const {
        if (isMatchAll_) return false;
        for (uint64_t w : bits_) {
            if (w != 0) return false;
        }
        return true;
    }

    size_t size() const { return size_; }

private:
    std::vector<uint64_t> bits_;
    size_t size_{0};
    bool isMatchAll_{false};
};

// DocumentMetadata — Structured key-value fields per document.
struct DocumentMetadata {
    int docId{0};
    std::unordered_map<std::string, std::string> stringFields;
    std::unordered_map<std::string, double> numericFields;
};

// MetadataIndex — Inverted index mapping structured attributes to document bitsets.
class MetadataIndex {
public:
    MetadataIndex() = default;

    // Index document metadata attributes
    void setMetadata(int docId, const DocumentMetadata& meta);

    // Delete metadata for document
    void deleteDocument(int docId);

    // Evaluate structured filter string (e.g. "category:AI,year:>=2023,status:public")
    MetadataBitset evaluateFilter(const std::string& filterExpr, size_t totalDocs) const;

    // Direct attribute query
    MetadataBitset matchExact(const std::string& field, const std::string& value) const;
    MetadataBitset matchNumericRange(const std::string& field, double minVal, double maxVal) const;

    // Get metadata for a specific document
    bool getMetadata(int docId, DocumentMetadata* out) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, DocumentMetadata> docStore_;
    // field -> value -> docId bitset
    std::unordered_map<std::string, std::unordered_map<std::string, MetadataBitset>> stringInverted_;
    // field -> list of (value, docId) pairs
    std::unordered_map<std::string, std::vector<std::pair<double, int>>> numericStore_;
};

#endif // METADATA_INDEX_H
