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

// Positional Posting structure
struct Posting {
    int docId;
    int tf;
    std::vector<int> positions;
};

// Skip pointer for O(sqrt(L)) posting list intersection / skipping
struct SkipPointer {
    int docId;
    size_t index;  // position in the vector<Posting> list
};

class InvertedIndex {
public:
    InvertedIndex() = default;

    // Add a document to the index (calculates positions & updates posting lists)
    void addDocument(const Document& doc);

    // Rebuild skip lists and precompute term upper bounds for WAND pruning
    void finalize();

    // Find document IDs containing a term (returns set for backwards compatibility)
    std::set<int> find(const std::string& term) const;

    // Get sorted posting list for a term
    const std::vector<Posting>* getPostings(const std::string& term) const;

    // Get skip list for a term
    const std::vector<SkipPointer>* getSkipList(const std::string& term) const;

    // Get pre-computed BM25 score upper bound for a term across all docs
    double getMaxTermScore(const std::string& term) const;

    // Get term frequency in a document
    int getTermFrequency(const std::string& term, int docId) const;

    // Get document frequency
    int getDocumentFrequency(const std::string& term) const;

    // Get positions of a term in a document
    std::vector<int> getPositions(const std::string& term, int docId) const;

    // Phrase query search: returns doc IDs where sequence of tokens appears contiguously
    std::vector<int> phraseSearch(const std::vector<std::string>& phraseTokens) const;

    // Get total number of documents
    int getTotalDocuments() const { return static_cast<int>(documents.size()); }

    // Get document by ID
    const Document* getDocument(int docId) const;

    // Get average document length
    double getAverageDocLength() const;

    // Get document length
    int getDocLength(int docId) const;

private:
    // term -> vector of Postings (sorted by docId)
    std::unordered_map<std::string, std::vector<Posting>> postingsMap;

    // term -> skip list for fast skipping
    std::unordered_map<std::string, std::vector<SkipPointer>> skipListMap;

    // term -> max BM25 score possible (used by WAND algorithm)
    std::unordered_map<std::string, double> maxTermScores;

    // term -> set of document IDs (for legacy fast lookup)
    std::unordered_map<std::string, std::set<int>> index;

    // term -> docId -> frequency
    std::unordered_map<std::string, std::unordered_map<int, int>> termFrequencies;

    // docId -> Document
    std::unordered_map<int, Document> documents;

    // docId -> document length
    std::unordered_map<int, int> docLengths;

    static constexpr size_t SKIP_INTERVAL = 8; // skip pointer every 8 postings
};

#endif // INVERTED_INDEX_H