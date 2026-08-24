#ifndef WAL_H
#define WAL_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

// WriteAheadLog (WAL) — Durability & crash recovery for dynamic search index updates.
// Every document mutation (INSERT, DELETE, UPDATE) is fsync'd before being applied
// to the in-memory MemTable/Index.

enum class WalOpType : uint8_t {
    INSERT = 1,
    DELETE = 2,
    UPDATE = 3
};

struct WalEntry {
    WalOpType op;
    int32_t docId;
    std::string content;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& logPath = "data/search.wal");
    ~WriteAheadLog();

    // Open/initialize log file
    bool open();

    // Append an INSERT mutation
    bool logInsert(int docId, const std::string& content);

    // Append a DELETE mutation
    bool logDelete(int docId);

    // Append an UPDATE mutation
    bool logUpdate(int docId, const std::string& content);

    // Replay all logged mutations from beginning of file
    bool replay(const std::function<void(const WalEntry&)>& handler);

    // Truncate / clear log (e.g. after full checkpoint/snapshot)
    bool clear();

    // Close log file
    void close();

    const std::string& path() const { return logPath_; }

private:
    bool appendRecord(WalOpType op, int32_t docId, const std::string& content);

    std::string logPath_;
    FILE* file_{nullptr};
    std::mutex mutex_;
};

#endif // WAL_H
