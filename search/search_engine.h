#pragma once
#include "../index/inverted_index.h"
#include "../adaptive/user_profile.h"
#include "../cache/lru_cache.h"
#include <vector>
#include <string>

using namespace std;

struct SearchResult {
    int docId;
    double score;
};

class SearchEngine {
    InvertedIndex index;
    UserProfile* user;
    int totalDocs;
    LRUCache<string, vector<SearchResult>> cache;

public:
    SearchEngine(UserProfile* profile);
    void addDocument(int id, const string& text);
    vector<SearchResult> search(const string& query);
};
