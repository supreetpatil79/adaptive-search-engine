#ifndef TOMBSTONE_H
#define TOMBSTONE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

// TombstoneManager — Lock-free / low-overhead document deletion tracking.
// Provides O(1) checks during candidate evaluation to filter out deleted documents
// without rebuilding posting lists or graph indexes.

class TombstoneManager {
public:
    TombstoneManager() = default;

    // Mark document as deleted
    void markDeleted(int docId) {
        std::lock_guard<std::mutex> lock(mutex_);
        deletedSet_.insert(docId);
    }

    // Check if document is deleted (O(1))
    bool isDeleted(int docId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return deletedSet_.count(docId) > 0;
    }

    // Restore / un-delete document
    void unmarkDeleted(int docId) {
        std::lock_guard<std::mutex> lock(mutex_);
        deletedSet_.erase(docId);
    }

    // Total deleted count
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return deletedSet_.size();
    }

    // Clear all tombstones
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        deletedSet_.clear();
    }

    // Export list of all deleted docIds (for segment compaction)
    std::unordered_set<int> getDeletedSet() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return deletedSet_;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<int> deletedSet_;
};

#endif // TOMBSTONE_H
