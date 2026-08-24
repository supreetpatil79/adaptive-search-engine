#ifndef SHARD_MANAGER_H
#define SHARD_MANAGER_H

#include "../search/search_engine.h"
#include "tombstone.h"
#include "wal.h"
#include <memory>
#include <string>
#include <vector>

// ShardManager — Distributed / Partitioned Search Index with Parallel Scatter-Gather.
// Replicates the core Google Web Search cluster architecture:
//   1. Data is partitioned across N independent shards (Index Shards).
//   2. Queries are scattered in parallel to all shards.
//   3. Results are gathered via a K-way min-heap merge with O(1) Tombstone filtering.
//   4. Mutations (Insert/Update/Delete) are durable via WriteAheadLog (WAL).

class SearchShard {
public:
    explicit SearchShard(int shardId);

    void addDocument(int docId, const std::string& content);
    void finalize();

    std::vector<SearchResult> search(const std::string& query, int topK, const TombstoneManager& tombstones);
    std::vector<SearchResult> searchWAND(const std::string& query, int topK, const TombstoneManager& tombstones, WANDStats* stats = nullptr);
    std::vector<SearchResult> searchPhrase(const std::string& phraseQuery, const TombstoneManager& tombstones);

    int shardId() const { return shardId_; }
    int totalDocs() const { return engine_.totalDocs(); }
    const SearchEngine& engine() const { return engine_; }

private:
    int shardId_;
    SearchEngine engine_;
};

class ShardManager {
public:
    explicit ShardManager(size_t numShards = 4, const std::string& walPath = "data/search.wal");
    ~ShardManager();

    // Ingest a document into its routed shard and append to WAL
    void addDocument(int docId, const std::string& content);

    // Delete a document in O(1) via Tombstone and log to WAL
    void deleteDocument(int docId);

    // Finalize all shards after initial bulk load
    void finalize();

    // Parallel Scatter-Gather BM25 Search
    std::vector<SearchResult> search(const std::string& query, int topK = 10);

    // Parallel Scatter-Gather WAND Pruned Search
    std::vector<SearchResult> searchWAND(const std::string& query, int topK = 10, WANDStats* aggregatedStats = nullptr);

    // Parallel Scatter-Gather Phrase Search
    std::vector<SearchResult> searchPhrase(const std::string& phraseQuery);

    // Recover index state by replaying WAL
    bool recoverFromWal();

    // Check if doc is deleted
    bool isDeleted(int docId) const { return tombstones_.isDeleted(docId); }

    // Total documents across all shards (excluding deleted)
    size_t totalDocs() const;

    size_t numShards() const { return shards_.size(); }

    const TombstoneManager& tombstones() const { return tombstones_; }

private:
    size_t route(int docId) const {
        return static_cast<size_t>(docId >= 0 ? docId : -docId) % shards_.size();
    }

    std::vector<std::unique_ptr<SearchShard>> shards_;
    TombstoneManager tombstones_;
    WriteAheadLog wal_;
};

#endif // SHARD_MANAGER_H
