#pragma once

#include <cmath>

class TFIDF {
public:
    // Calculate TF-IDF score for a term in a document
    static double score(int termFreq, int docFreq, int totalDocs, int docLength);
    
    // Calculate TF component (normalized term frequency)
    static double tf(int termFreq, int docLength);
    
    // Calculate IDF component
    static double idf(int docFreq, int totalDocs);
};

