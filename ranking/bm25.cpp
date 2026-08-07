#include "bm25.h"

BM25::BM25(double k1, double b) : k1(k1), b(b) {}

double BM25::idf(int docFreq, int totalDocs) const {
    if (docFreq <= 0) return 0.0;
    // Robertson IDF with +1 smoothing to avoid log(0)
    return std::log(1.0 + (static_cast<double>(totalDocs - docFreq) + 0.5) /
                               (static_cast<double>(docFreq) + 0.5));
}

double BM25::score(int termFreq,
                   int docFreq,
                   int totalDocs,
                   int docLength,
                   double avgDocLength) const {
    if (termFreq <= 0 || docFreq <= 0 || avgDocLength <= 0.0) return 0.0;

    double idfScore   = idf(docFreq, totalDocs);
    double lengthNorm = 1.0 - b + b * (static_cast<double>(docLength) / avgDocLength);

    // BM25 saturation: (tf * (k1 + 1)) / (tf + k1 * lengthNorm)
    double tfComp = (static_cast<double>(termFreq) * (k1 + 1.0)) /
                    (static_cast<double>(termFreq) + k1 * lengthNorm);

    return idfScore * tfComp;
}