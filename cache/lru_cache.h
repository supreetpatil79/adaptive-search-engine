#pragma once
// cache/lru_cache.h
// Thread-unsafe LRU cache (doubly-linked list + hash map).
// Safe to use from a single thread; wrap with a mutex for multi-threaded access.
//
// Capacity 0 means nothing is cached (every put immediately evicts).
// Capacity < 0 is treated as 0.

#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

template<typename K, typename V>
class LRUCache {
    int capacity_;
    std::list<std::pair<K, V>> order_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> pos_;

public:
    explicit LRUCache(int cap) : capacity_(cap > 0 ? cap : 0) {}

    // Returns true and fills `value` if key is in cache; false otherwise.
    bool get(const K& key, V& value) {
        auto it = pos_.find(key);
        if (it == pos_.end()) return false;
        order_.splice(order_.begin(), order_, it->second);  // move to front (MRU)
        value = it->second->second;
        return true;
    }

    // Insert or update key→value. Evicts LRU entry when over capacity.
    void put(const K& key, const V& value) {
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

    int  size()     const { return static_cast<int>(order_.size()); }
    bool empty()    const { return order_.empty(); }
    int  capacity() const { return capacity_; }
};
