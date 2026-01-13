#include "bm25.h"
#include <cmath>

using namespace std;

BM25::BM25(double k1_, double b_)
    : k1(k1_), b(b_), avgDocLen(0.0) {}

void BM25::addDocument(int docId, int length) {
    docLen[docId] = length;
    double total = 0.0;
    for (auto &p : docLen) total += p.second;
    avgDocLen = total / docLen.size();
}

double BM25::score(int tf, int df, int totalDocs, int docId) {
    if (tf == 0 || df == 0) return 0.0;

    double idf = log(1.0 + (totalDocs - df + 0.5) / (df + 0.5));
    double dl = docLen[docId];

    double denom = tf + k1 * (1.0 - b + b * dl / avgDocLen);
    return idf * (tf * (k1 + 1.0)) / denom;
}
