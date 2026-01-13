#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <set>

struct Document {
    int id;
    std::string content;
    std::vector<std::string> tokens;
};

class InvertedIndex {
public:
    // Add a document to the index
    void addDocument(const Document& doc);
    
    // Find documents containing a term
    std::set<int> find(const std::string& term) const;
    
    // Get term frequency in a document
    int getTermFrequency(const std::string& term, int docId) const;
    
    // Get document frequency (number of documents containing the term)
    int getDocumentFrequency(const std::string& term) const;
    
    // Get total number of documents
    int getTotalDocuments() const { return documents.size(); }
    
    // Get document by ID
    const Document* getDocument(int docId) const;
    
    // Get average document length
    double getAverageDocLength() const;
    
    // Get document length
    int getDocLength(int docId) const;
    
private:
    // term -> set of document IDs
    std::unordered_map<std::string, std::set<int>> index;
    
    // term -> docId -> frequency
    std::unordered_map<std::string, std::unordered_map<int, int>> termFrequencies;
    
    // docId -> Document
    std::unordered_map<int, Document> documents;
    
    // docId -> document length
    std::unordered_map<int, int> docLengths;
};

#endif // INVERTED_INDEX_H