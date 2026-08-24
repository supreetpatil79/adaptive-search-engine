// index/metadata_index.cpp
// Implementation of Metadata Attribute Index and Bitset filtering.

#include "metadata_index.h"
#include <algorithm>
#include <iostream>
#include <sstream>

void MetadataIndex::setMetadata(int docId, const DocumentMetadata& meta) {
    std::lock_guard<std::mutex> lock(mutex_);
    docStore_[docId] = meta;

    for (const auto& pair : meta.stringFields) {
        stringInverted_[pair.first][pair.second].set(docId);
    }

    for (const auto& pair : meta.numericFields) {
        numericStore_[pair.first].emplace_back(pair.second, docId);
    }
}

void MetadataIndex::deleteDocument(int docId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = docStore_.find(docId);
    if (it != docStore_.end()) {
        for (const auto& pair : it->second.stringFields) {
            stringInverted_[pair.first][pair.second].unset(docId);
        }
        docStore_.erase(it);
    }
}

bool MetadataIndex::getMetadata(int docId, DocumentMetadata* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = docStore_.find(docId);
    if (it != docStore_.end()) {
        if (out) *out = it->second;
        return true;
    }
    return false;
}

MetadataBitset MetadataIndex::matchExact(const std::string& field, const std::string& value) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto fit = stringInverted_.find(field);
    if (fit != stringInverted_.end()) {
        auto vit = fit->second.find(value);
        if (vit != fit->second.end()) {
            return vit->second;
        }
    }
    return MetadataBitset(0);
}

MetadataBitset MetadataIndex::matchNumericRange(const std::string& field, double minVal, double maxVal) const {
    std::lock_guard<std::mutex> lock(mutex_);
    MetadataBitset bitset(0);
    auto fit = numericStore_.find(field);
    if (fit != numericStore_.end()) {
        for (const auto& pair : fit->second) {
            if (pair.first >= minVal && pair.first <= maxVal) {
                bitset.set(pair.second);
            }
        }
    }
    return bitset;
}

MetadataBitset MetadataIndex::evaluateFilter(const std::string& filterExpr, size_t totalDocs) const {
    if (filterExpr.empty()) {
        MetadataBitset all(totalDocs + 100);
        for (size_t i = 0; i < totalDocs + 100; ++i) all.set(i);
        return all;
    }

    std::vector<std::string> clauses;
    std::istringstream iss(filterExpr);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        while (!token.empty() && std::isspace(token.front())) token.erase(token.begin());
        while (!token.empty() && std::isspace(token.back())) token.pop_back();
        if (!token.empty()) clauses.push_back(token);
    }

    if (clauses.empty()) {
        MetadataBitset all(totalDocs + 100);
        for (size_t i = 0; i < totalDocs + 100; ++i) all.set(i);
        return all;
    }

    MetadataBitset result;
    bool firstClause = true;

    for (const auto& clause : clauses) {
        auto colon = clause.find(':');
        if (colon == std::string::npos) continue;

        std::string field = clause.substr(0, colon);
        std::string val = clause.substr(colon + 1);

        MetadataBitset clauseBitset;

        // Numeric comparison operators: >=, <=, >, <
        if (val.rfind(">=", 0) == 0) {
            double minV = std::stod(val.substr(2));
            clauseBitset = matchNumericRange(field, minV, 1e12);
        } else if (val.rfind("<=", 0) == 0) {
            double maxV = std::stod(val.substr(2));
            clauseBitset = matchNumericRange(field, -1e12, maxV);
        } else if (val.rfind(">", 0) == 0) {
            double minV = std::stod(val.substr(1)) + 1e-6;
            clauseBitset = matchNumericRange(field, minV, 1e12);
        } else if (val.rfind("<", 0) == 0) {
            double maxV = std::stod(val.substr(1)) - 1e-6;
            clauseBitset = matchNumericRange(field, -1e12, maxV);
        } else {
            // Exact string match
            clauseBitset = matchExact(field, val);
        }

        if (firstClause) {
            result = clauseBitset;
            firstClause = false;
        } else {
            result = result.bitwiseAnd(clauseBitset);
        }
    }

    return result;
}
