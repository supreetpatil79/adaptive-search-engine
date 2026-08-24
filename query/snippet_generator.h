#ifndef SNIPPET_GENERATOR_H
#define SNIPPET_GENERATOR_H

#include <string>
#include <vector>

// SnippetGenerator — Dynamic Query Snippet Generation & Term Highlighting.
// Employs a sliding-window density scoring model to locate the most relevant
// sentence or passage in a document matching the query terms.

enum class HighlightFormat {
    NONE,
    HTML,  // <b>term</b>
    ANSI   // \033[1;33mterm\033[0m (bold yellow terminal text)
};

class SnippetGenerator {
public:
    // Generate a context-aware snippet around query terms with highlighting
    static std::string generateSnippet(
        const std::string& content,
        const std::vector<std::string>& queryTokens,
        size_t maxChars = 140,
        HighlightFormat format = HighlightFormat::NONE
    );

    // Highlight query terms inside an already extracted string
    static std::string highlight(
        const std::string& text,
        const std::vector<std::string>& queryTokens,
        HighlightFormat format = HighlightFormat::HTML
    );
};

#endif // SNIPPET_GENERATOR_H
