// index/wal.cpp
// Implementation of Write-Ahead Log (WAL) for persistent mutation logging.

#include "wal.h"
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {
constexpr uint32_t WAL_MAGIC = 0x57414C31u; // "WAL1"
constexpr uint32_t WAL_VERSION = 1;
}

WriteAheadLog::WriteAheadLog(const std::string& logPath) : logPath_(logPath) {}

WriteAheadLog::~WriteAheadLog() {
    close();
}

bool WriteAheadLog::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) return true;

    // Check if file exists, if not create and write header
    FILE* check = std::fopen(logPath_.c_str(), "rb");
    if (!check) {
        file_ = std::fopen(logPath_.c_str(), "wb+");
        if (!file_) return false;

        uint32_t header[2] = {WAL_MAGIC, WAL_VERSION};
        if (std::fwrite(header, sizeof(uint32_t), 2, file_) != 2) {
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        std::fflush(file_);
    } else {
        std::fclose(check);
        file_ = std::fopen(logPath_.c_str(), "ab+");
        if (!file_) return false;
    }
    return true;
}

void WriteAheadLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool WriteAheadLog::appendRecord(WalOpType op, int32_t docId, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) {
        if (!open()) return false;
    }

    uint8_t opByte = static_cast<uint8_t>(op);
    uint32_t contentLen = static_cast<uint32_t>(content.size());

    if (std::fwrite(&opByte, sizeof(uint8_t), 1, file_) != 1 ||
        std::fwrite(&docId, sizeof(int32_t), 1, file_) != 1 ||
        std::fwrite(&contentLen, sizeof(uint32_t), 1, file_) != 1) {
        return false;
    }

    if (contentLen > 0) {
        if (std::fwrite(content.data(), sizeof(char), contentLen, file_) != contentLen) {
            return false;
        }
    }

    std::fflush(file_);
    return true;
}

bool WriteAheadLog::logInsert(int docId, const std::string& content) {
    return appendRecord(WalOpType::INSERT, docId, content);
}

bool WriteAheadLog::logDelete(int docId) {
    return appendRecord(WalOpType::DELETE, docId, "");
}

bool WriteAheadLog::logUpdate(int docId, const std::string& content) {
    return appendRecord(WalOpType::UPDATE, docId, content);
}

bool WriteAheadLog::replay(const std::function<void(const WalEntry&)>& handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    FILE* f = std::fopen(logPath_.c_str(), "rb");
    if (!f) return false;

    uint32_t header[2] = {};
    if (std::fread(header, sizeof(uint32_t), 2, f) != 2 || header[0] != WAL_MAGIC || header[1] != WAL_VERSION) {
        std::fclose(f);
        return false;
    }

    while (true) {
        uint8_t opByte = 0;
        int32_t docId = 0;
        uint32_t contentLen = 0;

        if (std::fread(&opByte, sizeof(uint8_t), 1, f) != 1 ||
            std::fread(&docId, sizeof(int32_t), 1, f) != 1 ||
            std::fread(&contentLen, sizeof(uint32_t), 1, f) != 1) {
            break; // EOF
        }

        std::string content(contentLen, '\0');
        if (contentLen > 0) {
            if (std::fread(&content[0], sizeof(char), contentLen, f) != contentLen) {
                break; // Corrupted / truncated tail
            }
        }

        WalEntry entry;
        entry.op = static_cast<WalOpType>(opByte);
        entry.docId = docId;
        entry.content = std::move(content);

        handler(entry);
    }

    std::fclose(f);
    return true;
}

bool WriteAheadLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    file_ = std::fopen(logPath_.c_str(), "wb+");
    if (!file_) return false;

    uint32_t header[2] = {WAL_MAGIC, WAL_VERSION};
    if (std::fwrite(header, sizeof(uint32_t), 2, file_) != 2) {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    std::fflush(file_);
    return true;
}
