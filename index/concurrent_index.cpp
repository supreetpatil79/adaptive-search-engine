#include "concurrent_index.h"

void ConcurrentInvertedIndex::addDocument(const Document& doc) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    index.addDocument(doc);
}

void ConcurrentInvertedIndex::mergeSegment(const InvertedIndex& segment) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    // Direct posting-list merge — no re-tokenisation, O(terms) not O(docs).
    index.mergePostingsFrom(segment);
}

void ConcurrentInvertedIndex::finalize() {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    index.finalize();
}

std::set<int> ConcurrentInvertedIndex::find(const std::string& term) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.find(term);
}

int ConcurrentInvertedIndex::getTermFrequency(const std::string& term, int docId) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getTermFrequency(term, docId);
}

int ConcurrentInvertedIndex::getDocumentFrequency(const std::string& term) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getDocumentFrequency(term);
}

double ConcurrentInvertedIndex::getMaxTermScore(const std::string& term) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getMaxTermScore(term);
}

int ConcurrentInvertedIndex::getTotalDocuments() const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getTotalDocuments();
}

double ConcurrentInvertedIndex::getAverageDocLength() const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getAverageDocLength();
}

int ConcurrentInvertedIndex::getDocLength(int docId) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index.getDocLength(docId);
}

InvertedIndex ConcurrentInvertedIndex::createSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    return index;
}
