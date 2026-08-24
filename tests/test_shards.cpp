// tests/test_shards.cpp
// Unit tests for Distributed ShardManager, Tombstones, and Write-Ahead Log (WAL).
// Run: ./build/test_shards

#include "../index/shard_manager.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define ASSERT_TRUE(expr)                                             \
    do {                                                              \
        if (!(expr)) {                                                \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  " << #expr << "\n";                       \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

#define ASSERT_EQ(got, expected)                                      \
    do {                                                              \
        if ((got) != (expected)) {                                    \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  got " << (got) << " != expected "        \
                      << (expected) << "\n";                          \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

void test_shard_partitioning_and_search() {
    std::string walFile = "/tmp/test_shards_1.wal";
    std::remove(walFile.c_str());

    ShardManager sm(4, walFile); // 4 shards

    sm.addDocument(1, "distributed search engine indexing");
    sm.addDocument(2, "parallel scatter gather query execution");
    sm.addDocument(3, "information retrieval and web search");
    sm.addDocument(4, "search engine optimization and ranking");
    sm.addDocument(5, "neural networks and deep learning");
    sm.finalize();

    ASSERT_EQ(sm.totalDocs(), 5);

    // Search "search" -> docs 1, 3, 4
    auto res = sm.search("search", 10);
    ASSERT_TRUE(res.size() >= 3);

    // Phrase search "scatter gather" -> doc 2
    auto phraseRes = sm.searchPhrase("scatter gather");
    ASSERT_EQ(phraseRes.size(), 1);
    ASSERT_EQ(phraseRes[0].docId, 2);

    // WAND search
    WANDStats stats;
    auto wandRes = sm.searchWAND("search engine", 5, &stats);
    ASSERT_TRUE(!wandRes.empty());

    std::cout << "test_shard_partitioning_and_search PASSED\n";
}

void test_tombstone_deletion() {
    std::string walFile = "/tmp/test_shards_2.wal";
    std::remove(walFile.c_str());

    ShardManager sm(4, walFile);
    sm.addDocument(10, "alpha beta gamma");
    sm.addDocument(20, "alpha delta epsilon");
    sm.finalize();

    ASSERT_EQ(sm.totalDocs(), 2);
    auto resBefore = sm.search("alpha", 10);
    ASSERT_EQ(resBefore.size(), 2);

    // Delete doc 10
    sm.deleteDocument(10);
    ASSERT_TRUE(sm.isDeleted(10));
    ASSERT_EQ(sm.totalDocs(), 1);

    // Search again -> doc 10 must NOT appear in results
    auto resAfter = sm.search("alpha", 10);
    ASSERT_EQ(resAfter.size(), 1);
    ASSERT_EQ(resAfter[0].docId, 20);

    std::cout << "test_tombstone_deletion PASSED\n";
}

void test_wal_durability_and_recovery() {
    std::string walFile = "/tmp/test_shards_wal.wal";
    std::remove(walFile.c_str());

    {
        ShardManager sm(4, walFile);
        sm.addDocument(101, "quantum computing algorithms");
        sm.addDocument(102, "cloud microservices architecture");
        sm.addDocument(103, "artificial intelligence systems");
        sm.deleteDocument(102);
    }

    // Now instantiate a fresh ShardManager and recover from WAL
    {
        ShardManager smRecovered(4, walFile);
        smRecovered.recoverFromWal();
        smRecovered.finalize();

        ASSERT_EQ(smRecovered.totalDocs(), 2);
        ASSERT_TRUE(smRecovered.isDeleted(102));

        auto res = smRecovered.search("quantum", 5);
        ASSERT_EQ(res.size(), 1);
        ASSERT_EQ(res[0].docId, 101);

        auto resDeleted = smRecovered.search("microservices", 5);
        ASSERT_EQ(resDeleted.size(), 0);
    }

    std::cout << "test_wal_durability_and_recovery PASSED\n";
}

int main() {
    std::cout << "=== Distributed Shard & WAL Unit Tests ===\n\n";

    test_shard_partitioning_and_search();
    test_tombstone_deletion();
    test_wal_durability_and_recovery();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
