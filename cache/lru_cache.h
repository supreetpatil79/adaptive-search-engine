#pragma once
// cache/lru_cache.h
// Thread-safe LRU cache (doubly-linked list + hash map + std::mutex).
// Safe for concurrent get/put across multiple worker threads.
//
// Capacity 0 means nothing is cached (every put immediately evicts).
// Capacity < 0 is treated as 0.

#include <list>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

template<typename K, typename V>
class LRUCache {
    int capacity_;
    std::list<std::pair<K, V>> order_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> pos_;
    mutable std::mutex mutex_;

public:
    explicit LRUCache(int cap) : capacity_(cap > 0 ? cap : 0) {}

    // Copying/moving locks
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    LRUCache(LRUCache&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        capacity_ = other.capacity_;
        order_ = std::move(other.order_);
        pos_ = std::move(other.pos_);
    }

    LRUCache& operator=(LRUCache&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(mutex_, other.mutex_);
            capacity_ = other.capacity_;
            order_ = std::move(other.order_);
            pos_ = std::move(other.pos_);
        }
        return *this;
    }

    // Returns true and fills `value` if key is in cache; false otherwise.
    bool get(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pos_.find(key);
        if (it == pos_.end()) return false;
        order_.splice(order_.begin(), order_, it->second);  // move to front (MRU)
        value = it->second->second;
        return true;
    }

    // Insert or update key→value. Evicts LRU entry when over capacity.
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ <= 0) return;  // no-op for zero-capacity cache

        auto it = pos_.find(key);
        if (it != pos_.end()) {
            order_.erase(it->second);
            pos_.erase(it);
        }

        order_.push_front({key, value});
        pos_[key] = order_.begin();

        if (static_cast<int>(order_.size()) > capacity_) {
            pos_.erase(order_.back().first);
            order_.pop_back();
        }
    }

    // Clear all entries
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        order_.clear();
        pos_.clear();
    }

    int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(order_.size());
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.empty();
    }

    int capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }
};
