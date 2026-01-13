#include "tokenizer.h"

std::set<std::string> Tokenizer::getStopWords() {
    return {
        "a", "an", "and", "are", "as", "at", "be", "by", "for",
        "from", "has", "he", "in", "is", "it", "its", "of", "on",
        "that", "the", "to", "was", "will", "with"
    };
}

std::string Tokenizer::normalize(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (std::isalnum(c)) {
            result += std::tolower(c);
        } else if (std::isspace(c) && !result.empty() && result.back() != ' ') {
            result += ' ';
        }
    }
    
    // Trim trailing space
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string normalized = normalize(text);
    std::istringstream iss(normalized);
    std::string token;
    
    while (iss >> token) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

std::vector<std::string> Tokenizer::removeStopWords(const std::vector<std::string>& tokens) {
    std::set<std::string> stopWords = getStopWords();
    std::vector<std::string> filtered;
    
    for (const auto& token : tokens) {
        if (stopWords.find(token) == stopWords.end()) {
            filtered.push_back(token);
        }
    }
    
    return filtered;
}