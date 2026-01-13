#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <cctype>

using namespace std;

/* -------- Tokenizer -------- */
vector<string> tokenize(const string &text) {
    vector<string> tokens;
    string cur;
    for (char c : text) {
        if (isalnum((unsigned char)c)) {
            cur.push_back((char)tolower(c));
        } else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

/* -------- Document -------- */
struct Doc {
    int id;
    string text;
    vector<pair<string,int>> tf;  // Changed from map to vector
    int length;
};

/* -------- BM25 -------- */
struct BM25 {
    double k1 = 1.5;
    double b  = 0.75;
    double avgdl = 1.0;

    void init(const vector<Doc> &docs) {
        double sum = 0.0;
        for (auto &d : docs) sum += d.length;
        if (!docs.empty()) avgdl = sum / docs.size();
    }

    double score(int tf, int df, int N, int dl) {
        if (tf == 0 || df == 0 || df > N) return 0.0;
        if (avgdl <= 0) return 0.0;
        
        double idf = log(1.0 + (double)(N - df + 0.5) / (double)(df + 0.5));
        double norm_factor = (1.0 - b) + (b * (double)dl / avgdl);
        if (norm_factor <= 0) norm_factor = 1.0;
        
        double denom = (double)tf + k1 * norm_factor;
        if (denom <= 0) return 0.0;
        
        return idf * ((double)tf * (k1 + 1.0)) / denom;
    }
};

int main() {
    vector<Doc> docs;

    /* Load documents */
    ifstream fin("data/documents.txt");
    int docId = 1;
    if (fin) {
        string line;
        while (getline(fin, line)) {
            if (line.empty()) continue;

            Doc d;
            d.id = docId++;
            d.text = line;

            auto tokens = tokenize(d.text);
            d.length = tokens.size();
            
            // Build term frequency vector
            for (auto &t : tokens) {
                bool found = false;
                for (auto &p : d.tf) {
                    if (p.first == t) {
                        p.second++;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    d.tf.push_back({t, 1});
                }
            }
            
            docs.push_back(d);
        }
        fin.close();
    }
    
    if (docs.empty()) {
        cout << "No documents loaded. Please check data/documents.txt\n";
        return 1;
    }

    /* Build DF correctly - count documents containing each term */
    auto getDocFreq = [&docs](const string& term) {
        int count = 0;
        for (auto &d : docs) {
            for (auto &tf_pair : d.tf) {
                if (tf_pair.first == term) {
                    count++;
                    break;
                }
            }
        }
        return count;
    };

    BM25 bm25;
    bm25.init(docs);

    cout << "=== Adaptive Search Engine ===\n";
    cout << "Loaded " << docs.size() << " documents\n\n";

    string query;
    while (true) {
        try {
            cout << "Enter query (or 'quit' to exit): ";
            cout.flush();
            
            if (!getline(cin, query)) {
                break;
            }
            
            if (query == "quit") break;
            
            // Trim whitespace
            size_t start = query.find_first_not_of(" \t\r\n");
            size_t end = query.find_last_not_of(" \t\r\n");
            if (start == string::npos) {
                query = "";
            } else {
                query = query.substr(start, end - start + 1);
            }
            
            if (query.empty()) {
                cout << "\n";
                continue;
            }
            
            vector<string> qtokens = tokenize(query);
            
            if (qtokens.empty()) {
                cout << "No valid query tokens.\n\n";
                continue;
            }

            vector<pair<int,double>> scores;

            for (size_t doc_idx = 0; doc_idx < docs.size(); doc_idx++) {
                double score = 0.0;
                int match_count = 0;
                auto &d = docs[doc_idx];
                
                for (size_t tok_idx = 0; tok_idx < qtokens.size(); tok_idx++) {
                    const string &qt = qtokens[tok_idx];
                    
                    // Linear search in d.tf vector
                    int term_freq = 0;
                    for (auto &tf_pair : d.tf) {
                        if (tf_pair.first == qt) {
                            term_freq = tf_pair.second;
                            break;
                        }
                    }
                    
                    if (term_freq == 0) continue;
                    
                    // Compute document frequency on the fly
                    int doc_freq = getDocFreq(qt);
                    
                    if (doc_freq == 0) continue;

                    int total_docs = (int)docs.size();
                    int doc_len = d.length;
                    
                    if (doc_freq > 0 && doc_freq <= total_docs && doc_len > 0) {
                        double term_score = bm25.score(term_freq, doc_freq, total_docs, doc_len);
                        if (!isnan(term_score) && !isinf(term_score)) {
                            score += term_score;
                            match_count++;
                        }
                    }
                }
                if (score > 0.0 && match_count > 0) {
                    scores.push_back({d.id, score});
                }
            }

            if (scores.empty()) {
                cout << "No results found for: " << query << "\n\n";
                continue;
            }

            // Sort by score in descending order
            for (size_t i = 0; i < scores.size(); i++) {
                for (size_t j = i + 1; j < scores.size(); j++) {
                    if (scores[j].second > scores[i].second) {
                        swap(scores[i], scores[j]);
                    }
                }
            }

            cout << "\nResults for query: " << query << "\n";
            cout << "--------------------------------------------------------------------------------\n";
            int rank = 1;
            for (auto &p : scores) {
                for (auto &d : docs) {
                    if (d.id == p.first) {
                        cout << rank << ". [Score: " << p.second << "] Doc ID: " << p.first << "\n";
                        cout << "   " << d.text.substr(0, 100);
                        if (d.text.length() > 100) cout << "...";
                        cout << "\n\n";
                        rank++;
                        if (rank > 10) break;  // Show top 10
                    }
                }
                if (rank > 10) break;
            }
        } catch (const bad_alloc& e) {
            cout << "Memory error - try a shorter query\n";
            continue;
        } catch (const exception& e) {
            cout << "Query error - trying simpler search...\n";
            // Continue with empty results rather than crashing
            continue;
        }
    }

    cout << "\nGoodbye!\n";
    return 0;
}
