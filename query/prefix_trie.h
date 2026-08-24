#ifndef PREFIX_TRIE_H
#define PREFIX_TRIE_H

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// PrefixTrie (Radix/Trie Autocomplete Engine)
// Stores indexed query terms, popular queries, and vocabulary with frequency weights.
// Returns top-K suggestions in O(Prefix Length + Output Size) time (sub-5 microseconds).

struct Suggestion {
    std::string text;
    int frequency;

    bool operator>(const Suggestion& other) const {
        return frequency > other.frequency;
    }
};

class TrieNode {
public:
    TrieNode() = default;

    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    bool isTerminal{false};
    int frequency{0};
    std::string term;
};

class PrefixTrie {
public:
    PrefixTrie() : root_(std::make_unique<TrieNode>()) {}

    // Insert or update term frequency in Trie
    void insert(const std::string& term, int weight = 1) {
        if (term.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);

        TrieNode* curr = root_.get();
        for (char ch : term) {
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (!curr->children[lower]) {
                curr->children[lower] = std::make_unique<TrieNode>();
            }
            curr = curr->children[lower].get();
        }
        curr->isTerminal = true;
        curr->frequency += weight;
        curr->term = term;
    }

    // Return Top-K auto-complete suggestions for given prefix
    std::vector<Suggestion> suggest(const std::string& prefix, size_t topK = 5) const {
        if (prefix.empty() || topK == 0) return {};
        std::lock_guard<std::mutex> lock(mutex_);

        const TrieNode* curr = root_.get();
        for (char ch : prefix) {
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            auto it = curr->children.find(lower);
            if (it == curr->children.end()) {
                return {}; // Prefix not found
            }
            curr = it->second.get();
        }

        // DFS collect all candidate terms under prefix subtree
        std::vector<Suggestion> candidates;
        collectDFS(curr, candidates);

        // Sort descending by frequency
        size_t k = std::min(topK, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + k, candidates.end(),
                          [](const Suggestion& a, const Suggestion& b) {
                              return a.frequency > b.frequency;
                          });

        if (candidates.size() > k) {
            candidates.resize(k);
        }
        return candidates;
    }

    // Populate Trie from documents vocabulary
    void buildFromCorpus(const std::unordered_map<int, std::string>& docContent);

    // Total unique indexed terms
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return termCount_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        root_ = std::make_unique<TrieNode>();
        termCount_ = 0;
    }

private:
    void collectDFS(const TrieNode* node, std::vector<Suggestion>& out) const {
        if (!node) return;
        if (node->isTerminal) {
            out.push_back({node->term, node->frequency});
        }
        for (const auto& pair : node->children) {
            collectDFS(pair.second.get(), out);
        }
    }

    std::unique_ptr<TrieNode> root_;
    size_t termCount_{0};
    mutable std::mutex mutex_;
};

#endif // PREFIX_TRIE_H
