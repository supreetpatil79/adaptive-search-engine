// query/snippet_generator.cpp
// Implementation of dynamic query snippet generation and term highlighting.

#include "snippet_generator.h"
#include "../tokenizer/tokenizer.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace {
struct WordSpan {
    std::string text;
    std::string lowerText;
    size_t startChar;
    size_t endChar;
    bool isQueryMatch;
};

std::string getOpenTag(HighlightFormat format) {
    if (format == HighlightFormat::HTML) return "<b>";
    if (format == HighlightFormat::ANSI) return "\033[1;33m";
    return "";
}

std::string getCloseTag(HighlightFormat format) {
    if (format == HighlightFormat::HTML) return "</b>";
    if (format == HighlightFormat::ANSI) return "\033[0m";
    return "";
}

bool tokenMatches(const std::string& word, const std::unordered_set<std::string>& queryTerms) {
    if (queryTerms.count(word)) return true;
    // Prefix check (e.g. "learn" matches "learning", "algorithms" matches "algorithm")
    for (const auto& q : queryTerms) {
        if (q.size() >= 3) {
            if (word.find(q) != std::string::npos || q.find(word) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}
} // anonymous namespace

std::string SnippetGenerator::generateSnippet(
    const std::string& content,
    const std::vector<std::string>& queryTokens,
    size_t maxChars,
    HighlightFormat format
) {
    if (content.empty()) return "";
    if (content.size() <= maxChars && format == HighlightFormat::NONE) {
        return content;
    }

    std::unordered_set<std::string> querySet;
    for (const auto& t : queryTokens) {
        std::string lower = t;
        for (char& c : lower) c = static_cast<char>(std::tolower(c));
        querySet.insert(lower);
    }

    // Tokenize into word spans with character offsets
    std::vector<WordSpan> words;
    size_t i = 0;
    while (i < content.size()) {
        while (i < content.size() && std::isspace(static_cast<unsigned char>(content[i]))) {
            i++;
        }
        if (i >= content.size()) break;

        size_t start = i;
        while (i < content.size() && !std::isspace(static_cast<unsigned char>(content[i]))) {
            i++;
        }
        size_t end = i;

        std::string rawWord = content.substr(start, end - start);
        std::string cleanWord;
        for (char c : rawWord) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                cleanWord += static_cast<char>(std::tolower(c));
            }
        }

        bool match = !cleanWord.empty() && tokenMatches(cleanWord, querySet);
        words.push_back(WordSpan{rawWord, cleanWord, start, end, match});
    }

    if (words.empty()) {
        return content.substr(0, maxChars);
    }

    // If no query terms specified, take the beginning
    if (querySet.empty()) {
        if (content.size() <= maxChars) return content;
        return content.substr(0, maxChars) + "…";
    }

    // Sliding window: find start/end word indices that fit inside maxChars and maximize matched terms
    size_t bestStartWord = 0;
    size_t bestEndWord = 0;
    double bestScore = -1.0;

    for (size_t startIdx = 0; startIdx < words.size(); ++startIdx) {
        size_t endIdx = startIdx;
        std::unordered_set<std::string> matchedInWindow;
        int totalMatches = 0;

        while (endIdx < words.size()) {
            size_t spanLength = words[endIdx].endChar - words[startIdx].startChar;
            if (spanLength > maxChars && endIdx > startIdx) {
                break;
            }

            if (words[endIdx].isQueryMatch) {
                matchedInWindow.insert(words[endIdx].lowerText);
                totalMatches++;
            }
            endIdx++;
        }

        // Score = unique terms * 10 + total term occurrences
        double score = static_cast<double>(matchedInWindow.size() * 10 + totalMatches);
        if (score > bestScore) {
            bestScore = score;
            bestStartWord = startIdx;
            bestEndWord = endIdx > 0 ? (endIdx - 1) : 0;
        }
    }

    // Format snippet
    std::string snippet;
    size_t charStart = words[bestStartWord].startChar;
    size_t charEnd = words[bestEndWord].endChar;

    bool prefixEllipsis = (charStart > 0);
    bool suffixEllipsis = (charEnd < content.size());

    if (prefixEllipsis) snippet += "…";

    std::string openTag = getOpenTag(format);
    std::string closeTag = getCloseTag(format);

    for (size_t w = bestStartWord; w <= bestEndWord; ++w) {
        if (w > bestStartWord) snippet += " ";
        if (words[w].isQueryMatch && format != HighlightFormat::NONE) {
            snippet += openTag + words[w].text + closeTag;
        } else {
            snippet += words[w].text;
        }
    }

    if (suffixEllipsis) snippet += "…";
    return snippet;
}

std::string SnippetGenerator::highlight(
    const std::string& text,
    const std::vector<std::string>& queryTokens,
    HighlightFormat format
) {
    if (format == HighlightFormat::NONE || text.empty() || queryTokens.empty()) {
        return text;
    }
    return generateSnippet(text, queryTokens, text.size() + 1000, format);
}
