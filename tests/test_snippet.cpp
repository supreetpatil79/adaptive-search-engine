// tests/test_snippet.cpp
// Unit tests for SnippetGenerator and Term Highlighter.
// Run: ./build/test_snippet

#include "../query/snippet_generator.h"
#include <iostream>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

#define ASSERT_TRUE(expr)                                             \
    do {                                                              \
        if (!(expr)) {                                                \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  " << #expr << "\n";                       \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

#define ASSERT_EQ(got, expected)                                      \
    do {                                                              \
        if ((got) != (expected)) {                                    \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__     \
                      << "  got \"" << (got) << "\" != expected \""   \
                      << (expected) << "\"\n";                        \
            ++failed;                                                 \
        } else { ++passed; }                                          \
    } while (false)

void test_basic_snippet_generation() {
    std::string text = "Artificial intelligence and machine learning algorithms allow computers to learn patterns from large datasets without explicit instructions.";
    std::vector<std::string> query = {"machine", "learning"};

    // HTML format
    std::string snippetHtml = SnippetGenerator::generateSnippet(text, query, 80, HighlightFormat::HTML);
    ASSERT_TRUE(snippetHtml.find("<b>machine</b>") != std::string::npos || snippetHtml.find("<b>Machine</b>") != std::string::npos);
    ASSERT_TRUE(snippetHtml.find("<b>learning</b>") != std::string::npos);

    // ANSI terminal format
    std::string snippetAnsi = SnippetGenerator::generateSnippet(text, query, 80, HighlightFormat::ANSI);
    ASSERT_TRUE(snippetAnsi.find("\033[1;33m") != std::string::npos);

    std::cout << "test_basic_snippet_generation PASSED\n";
}

void test_sliding_window_density() {
    // A document where query terms appear far in the middle
    std::string text = "The weather today is cloudy with light rain. Many people are staying home. However, quantum computing principles enable exponential speedups for optimization. The evening will be cool.";
    std::vector<std::string> query = {"quantum", "computing", "optimization"};

    std::string snippet = SnippetGenerator::generateSnippet(text, query, 85, HighlightFormat::NONE);
    // Snippet window must contain "quantum computing" rather than the irrelevant weather intro
    ASSERT_TRUE(snippet.find("quantum") != std::string::npos || snippet.find("computing") != std::string::npos);
    ASSERT_TRUE(snippet.find("weather") == std::string::npos);

    std::cout << "test_sliding_window_density PASSED\n";
}

void test_highlight_full_text() {
    std::string text = "deep neural network models";
    std::vector<std::string> query = {"neural", "models"};

    std::string res = SnippetGenerator::highlight(text, query, HighlightFormat::HTML);
    ASSERT_TRUE(res.find("<b>neural</b>") != std::string::npos);
    ASSERT_TRUE(res.find("<b>models</b>") != std::string::npos);

    std::cout << "test_highlight_full_text PASSED\n";
}

int main() {
    std::cout << "=== Snippet Generator Unit Tests ===\n\n";

    test_basic_snippet_generation();
    test_sliding_window_density();
    test_highlight_full_text();

    std::cout << "\n══════════════════════════════\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return (failed == 0) ? 0 : 1;
}
