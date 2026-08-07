#ifndef CONCURRENT_INDEX_H
#define CONCURRENT_INDEX_H

#include "inverted_index.h"
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <string>
#include <memory>

// Thread-safe wrapper around InvertedIndex using std::shared_mutex (Reader-Writer Lock).
// Concurrent queries take read locks (shared_lock).
// Document insertion and index finalization take write locks (unique_lock).

class ConcurrentInvertedIndex {
public:
    ConcurrentInvertedIndex() = default;

    // Thread-safe document insertion (Write lock)
    void addDocument(const Document& doc);

    // Thread-safe segment merge (Write lock)
    void mergeSegment(const InvertedIndex& segment);

    // Thread-safe finalization (Write lock)
    void finalize();

    // Thread-safe search/lookup methods (Read lock)
    std::set<int> find(const std::string& term) const;
    int getTermFrequency(const std::string& term, int docId) const;
    int getDocumentFrequency(const std::string& term) const;
    double getMaxTermScore(const std::string& term) const;
    int getTotalDocuments() const;
    double getAverageDocLength() const;
    int getDocLength(int docId) const;

    // Snapshot search (Read lock): safely copies posting data for a query
    InvertedIndex createSnapshot() const;

private:
    mutable std::shared_mutex rwLock;
    InvertedIndex index;
};

#endif // CONCURRENT_INDEX_H
