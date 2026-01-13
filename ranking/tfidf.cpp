#include "tfidf.h"

double TFIDF::tf(int termFreq, int docLength) {
    if (docLength == 0) return 0.0;
    // Normalized term frequency
    return static_cast<double>(termFreq) / docLength;
}

double TFIDF::idf(int docFreq, int totalDocs) {
    if (docFreq == 0) return 0.0;
    // IDF formula: log(N / df)
    return std::log(static_cast<double>(totalDocs) / docFreq);
}

double TFIDF::score(int termFreq, int docFreq, int totalDocs, int docLength) {
    return tf(termFreq, docLength) * idf(docFreq, totalDocs);
}