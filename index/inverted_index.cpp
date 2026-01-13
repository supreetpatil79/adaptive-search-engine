#include "inverted_index.h"

void InvertedIndex::addDocument(const Document& doc) {
    documents[doc.id] = doc;
    docLengths[doc.id] = doc.tokens.size();
    
    std::unordered_map<std::string, int> termCount;
    
    // Count term frequencies in this document
    for (const auto& token : doc.tokens) {
        termCount[token]++;
    }
    
    // Update index and term frequencies
    for (const auto& [term, count] : termCount) {
        index[term].insert(doc.id);
        termFrequencies[term][doc.id] = count;
    }
}

std::set<int> InvertedIndex::find(const std::string& term) const {
    auto it = index.find(term);
    if (it != index.end()) {
        return it->second;
    }
    return std::set<int>();
}

int InvertedIndex::getTermFrequency(const std::string& term, int docId) const {
    auto termIt = termFrequencies.find(term);
    if (termIt != termFrequencies.end()) {
        auto docIt = termIt->second.find(docId);
        if (docIt != termIt->second.end()) {
            return docIt->second;
        }
    }
    return 0;
}

int InvertedIndex::getDocumentFrequency(const std::string& term) const {
    auto it = index.find(term);
    if (it != index.end()) {
        return it->second.size();
    }
    return 0;
}

const Document* InvertedIndex::getDocument(int docId) const {
    auto it = documents.find(docId);
    if (it != documents.end()) {
        return &(it->second);
    }
    return nullptr;
}

double InvertedIndex::getAverageDocLength() const {
    if (documents.empty()) return 0.0;
    
    double total = 0.0;
    for (const auto& [id, len] : docLengths) {
        total += len;
    }
    
    return total / documents.size();
}

int InvertedIndex::getDocLength(int docId) const {
    auto it = docLengths.find(docId);
    if (it != docLengths.end()) {
        return it->second;
    }
    return 0;
}