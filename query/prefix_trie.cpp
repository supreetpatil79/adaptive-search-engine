// query/prefix_trie.cpp
// Implementation of PrefixTrie query autocomplete and suggestion engine.

#include "prefix_trie.h"
#include "../tokenizer/tokenizer.h"
#include <iostream>

void PrefixTrie::buildFromCorpus(const std::unordered_map<int, std::string>& docContent) {
    // Add single terms with occurrence frequency
    for (const auto& pair : docContent) {
        auto tokens = Tokenizer::tokenize(pair.second);
        for (const auto& token : tokens) {
            if (token.size() >= 3) {
                insert(token, 1);
            }
        }
        // Also extract high-value 2-gram phrases
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].size() >= 3 && tokens[i + 1].size() >= 3) {
                std::string bigram = tokens[i] + " " + tokens[i + 1];
                insert(bigram, 2); // Higher priority for bigrams
            }
        }
    }
}
