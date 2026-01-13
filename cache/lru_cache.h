#pragma once
#include <list>
#include <unordered_map>

using namespace std;

template<typename K, typename V>
class LRUCache {
    int capacity;
    list<pair<K, V>> order;
    unordered_map<K, typename list<pair<K, V>>::iterator> pos;

public:
    LRUCache(int cap) : capacity(cap) {}

    bool get(const K& key, V& value) {
        auto it = pos.find(key);
        if (it == pos.end()) return false;

        order.splice(order.begin(), order, it->second);
        value = it->second->second;
        return true;
    }

    void put(const K& key, const V& value) {
        auto it = pos.find(key);
        if (it != pos.end()) {
            order.erase(it->second);
            pos.erase(it);
        }

        order.push_front({key, value});
        pos[key] = order.begin();

        if ((int)order.size() > capacity) {
            auto last = order.back();
            pos.erase(last.first);
            order.pop_back();
        }
    }
};
