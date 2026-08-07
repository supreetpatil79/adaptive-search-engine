#include "file_loader.h"
#include "../tokenizer/tokenizer.h"
#include <fstream>
#include <iostream>
#include <sstream>

std::string FileLoader::readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << filename << "\n";
        return "";
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

std::vector<Document> FileLoader::loadFromFile(const std::string& filename) {
    std::vector<Document> documents;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << filename << "\n";
        return documents;
    }

    std::string line;
    int docId = 1;  // 1-based so docId == 0 can serve as "not found"

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        Document doc;
        doc.id      = docId++;
        doc.content = line;
        doc.tokens  = Tokenizer::tokenize(line);
        doc.tokens  = Tokenizer::removeStopWords(doc.tokens);

        documents.push_back(std::move(doc));
    }

    std::cout << "Loaded " << documents.size() << " documents from " << filename << "\n";
    return documents;
}