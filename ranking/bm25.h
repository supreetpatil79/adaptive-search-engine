#ifndef BM25_H
#define BM25_H

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

class BM25 {
public:
    // k1: term-frequency saturation (typically 1.2–2.0)
    // b:  length normalization (0 = no normalization, 1 = full)
    explicit BM25(double k1 = 1.5, double b = 0.75);

    // Score a single term occurrence in a document.
    // All length information passed explicitly — caller owns the index.
    double score(int termFreq,
                 int docFreq,
                 int totalDocs,
                 int docLength,
                 double avgDocLength) const;

private:
    double k1;
    double b;

    double idf(int docFreq, int totalDocs) const;
};

#endif // BM25_H