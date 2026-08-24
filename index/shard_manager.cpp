// index/shard_manager.cpp
// Implementation of Distributed ShardManager with parallel scatter-gather execution.

#include "shard_manager.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <queue>

// ── SearchShard ─────────────────────────────────────────────────────────────

SearchShard::SearchShard(int shardId) : shardId_(shardId), engine_(256) {}

void SearchShard::addDocument(int docId, const std::string& content) {
    engine_.addDocument(docId, content);
}

void SearchShard::finalize() {
    engine_.finalizeIndex();
}

std::vector<SearchResult> SearchShard::search(const std::string& query, int topK, const TombstoneManager& tombstones) {
    auto raw = engine_.search(query, topK * 2);
    std::vector<SearchResult> filtered;
    filtered.reserve(raw.size());
    for (auto& r : raw) {
        if (!tombstones.isDeleted(r.docId)) {
            filtered.push_back(std::move(r));
            if (static_cast<int>(filtered.size()) >= topK) break;
        }
    }
    return filtered;
}

std::vector<SearchResult> SearchShard::searchWAND(const std::string& query, int topK, const TombstoneManager& tombstones, WANDStats* stats) {
    auto raw = engine_.searchWAND(query, topK * 2, stats);
    std::vector<SearchResult> filtered;
    filtered.reserve(raw.size());
    for (auto& r : raw) {
        if (!tombstones.isDeleted(r.docId)) {
            filtered.push_back(std::move(r));
            if (static_cast<int>(filtered.size()) >= topK) break;
        }
    }
    return filtered;
}

std::vector<SearchResult> SearchShard::searchPhrase(const std::string& phraseQuery, const TombstoneManager& tombstones) {
    auto raw = engine_.searchPhrase(phraseQuery);
    std::vector<SearchResult> filtered;
    filtered.reserve(raw.size());
    for (auto& r : raw) {
        if (!tombstones.isDeleted(r.docId)) {
            filtered.push_back(std::move(r));
        }
    }
    return filtered;
}

// ── ShardManager ────────────────────────────────────────────────────────────

ShardManager::ShardManager(size_t numShards, const std::string& walPath)
    : wal_(walPath) {
    if (numShards == 0) numShards = 1;
    shards_.reserve(numShards);
    for (size_t i = 0; i < numShards; ++i) {
        shards_.push_back(std::make_unique<SearchShard>(static_cast<int>(i)));
    }
    wal_.open();
}

ShardManager::~ShardManager() {
    wal_.close();
}

void ShardManager::addDocument(int docId, const std::string& content) {
    size_t sIdx = route(docId);
    shards_[sIdx]->addDocument(docId, content);
    tombstones_.unmarkDeleted(docId);
    wal_.logInsert(docId, content);
}

void ShardManager::deleteDocument(int docId) {
    tombstones_.markDeleted(docId);
    wal_.logDelete(docId);
}

void ShardManager::finalize() {
    for (auto& shard : shards_) {
        shard->finalize();
    }
}

size_t ShardManager::totalDocs() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        total += shard->totalDocs();
    }
    size_t del = tombstones_.count();
    return (total >= del) ? (total - del) : 0;
}

std::vector<SearchResult> ShardManager::search(const std::string& query, int topK) {
    if (query.empty() || topK <= 0) return {};

    // 1. Parallel Scatter to all shards
    std::vector<std::future<std::vector<SearchResult>>> futures;
    futures.reserve(shards_.size());

    for (size_t i = 0; i < shards_.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &query, topK]() {
            return shards_[i]->search(query, topK, tombstones_);
        }));
    }

    // 2. Gather results from all shards
    std::vector<SearchResult> aggregated;
    for (auto& f : futures) {
        auto shardRes = f.get();
        aggregated.insert(aggregated.end(), shardRes.begin(), shardRes.end());
    }

    // 3. Top-K K-Way Heap Reduction
    int k = std::min(topK, static_cast<int>(aggregated.size()));
    std::partial_sort(aggregated.begin(), aggregated.begin() + k, aggregated.end());

    if (static_cast<int>(aggregated.size()) > k) {
        aggregated.resize(k);
    }
    return aggregated;
}

std::vector<SearchResult> ShardManager::searchWAND(const std::string& query, int topK, WANDStats* aggregatedStats) {
    if (query.empty() || topK <= 0) return {};

    // 1. Parallel Scatter to all shards with per-shard stats
    struct ShardTaskResult {
        std::vector<SearchResult> results;
        WANDStats stats;
    };

    std::vector<std::future<ShardTaskResult>> futures;
    futures.reserve(shards_.size());

    for (size_t i = 0; i < shards_.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &query, topK]() {
            ShardTaskResult tr;
            tr.results = shards_[i]->searchWAND(query, topK, tombstones_, &tr.stats);
            return tr;
        }));
    }

    // 2. Gather & Aggregate Stats
    std::vector<SearchResult> aggregated;
    for (auto& f : futures) {
        ShardTaskResult tr = f.get();
        aggregated.insert(aggregated.end(), tr.results.begin(), tr.results.end());
        if (aggregatedStats) {
            aggregatedStats->totalCandidatesSkipped += tr.stats.totalCandidatesSkipped;
            aggregatedStats->totalCandidatesEvaluated += tr.stats.totalCandidatesEvaluated;
            aggregatedStats->fullEvaluations += tr.stats.fullEvaluations;
        }
    }

    // 3. Global Top-K Reduction
    int k = std::min(topK, static_cast<int>(aggregated.size()));
    std::partial_sort(aggregated.begin(), aggregated.begin() + k, aggregated.end());

    if (static_cast<int>(aggregated.size()) > k) {
        aggregated.resize(k);
    }
    return aggregated;
}

std::vector<SearchResult> ShardManager::searchPhrase(const std::string& phraseQuery) {
    if (phraseQuery.empty()) return {};

    std::vector<std::future<std::vector<SearchResult>>> futures;
    futures.reserve(shards_.size());

    for (size_t i = 0; i < shards_.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, i, &phraseQuery]() {
            return shards_[i]->searchPhrase(phraseQuery, tombstones_);
        }));
    }

    std::vector<SearchResult> aggregated;
    for (auto& f : futures) {
        auto shardRes = f.get();
        aggregated.insert(aggregated.end(), shardRes.begin(), shardRes.end());
    }
    return aggregated;
}

bool ShardManager::recoverFromWal() {
    return wal_.replay([this](const WalEntry& entry) {
        if (entry.op == WalOpType::INSERT || entry.op == WalOpType::UPDATE) {
            size_t sIdx = route(entry.docId);
            shards_[sIdx]->addDocument(entry.docId, entry.content);
            tombstones_.unmarkDeleted(entry.docId);
        } else if (entry.op == WalOpType::DELETE) {
            tombstones_.markDeleted(entry.docId);
        }
    });
}
