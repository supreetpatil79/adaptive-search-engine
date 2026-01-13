#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <cctype>
#include <algorithm>

using namespace std;

int main() {
    vector<pair<int, string>> docs;  // doc_id, doc_text
    
    // Load documents
    ifstream fin("data/documents.txt");
    int doc_id = 1;
    string line;
    
    if (fin) {
        while (getline(fin, line)) {
            if (!line.empty()) {
                docs.push_back({doc_id++, line});
            }
        }
        fin.close();
    }
    
    if (docs.empty()) {
        cout << "No documents loaded!\n";
        return 1;
    }
    
    cout << "=== Adaptive Search Engine ===\n";
    cout << "Loaded " << docs.size() << " documents\n\n";
    
    // Interactive search loop
    string query;
    while (true) {
        cout << "Enter query (or 'quit' to exit): ";
        if (!getline(cin, query)) break;
        
        if (query == "quit") break;
        
        // Trim whitespace
        query.erase(0, query.find_first_not_of(" \t\r\n"));
        query.erase(query.find_last_not_of(" \t\r\n") + 1);
        
        if (query.empty()) continue;
        
        // Simple substring search
        vector<pair<double, int>> results;  // score, doc_id
        
        for (auto &doc : docs) {
            // Count occurrences of query terms (case-insensitive)
            int matches = 0;
            string doc_lower = doc.second;
            string query_lower = query;
            
            // Convert both to lowercase
            for (auto &c : doc_lower) c = tolower((unsigned char)c);
            for (auto &c : query_lower) c = tolower((unsigned char)c);
            
            // Count query word matches
            size_t pos = 0;
            while ((pos = doc_lower.find(query_lower, pos)) != string::npos) {
                matches++;
                pos += query_lower.length();
            }
            
            if (matches > 0) {
                double score = matches * 10.0 / (1.0 + doc.second.length() / 100.0);
                results.push_back({score, doc.first});
            }
        }
        
        if (results.empty()) {
            cout << "No results found.\n\n";
            continue;
        }
        
        // Sort by score
        sort(results.begin(), results.end(), 
             [](const auto &a, const auto &b) { return a.first > b.first; });
        
        cout << "\nResults for: " << query << "\n";
        cout << "----------" << "\n";
        
        int count = 0;
        for (auto &result : results) {
            if (count >= 10) break;  // Top 10 results
            
            for (auto &doc : docs) {
                if (doc.first == result.second) {
                    cout << (count + 1) << ". [Score: " << result.first << "]\n";
                    cout << "   " << doc.second.substr(0, 80);
                    if (doc.second.length() > 80) cout << "...";
                    cout << "\n\n";
                    count++;
                    break;
                }
            }
        }
    }
    
    cout << "\nGoodbye!\n";
    return 0;
}
