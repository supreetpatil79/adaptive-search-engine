#ifndef BM25_H
#define BM25_H

#include <cmath>
#include <unordered_map>
#include <vector>
#include <string>

class BM25 {
public:
    // Constructor with parameters k1 (term frequency saturation) and b (length normalization)
    BM25(double k1 = 1.5, double b = 0.75);
    
    // Add a document and its length for length normalization
    void addDocument(int docId, int length);
    
    // Calculate BM25 score for a single term in a document
    double score(int tf, int df, int totalDocs, int docId);
    
private:
    double k1;           // Term frequency saturation parameter
    double b;            // Length normalization parameter
    double avgDocLen;    // Average document length
    std::unordered_map<int, int> docLen;  // Document lengths
};

#endif // BM25_H