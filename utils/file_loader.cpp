#include "file_loader.h"
#include "tokenizer.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string FileLoader::readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<Document> FileLoader::loadFromFile(const std::string& filename) {
    std::vector<Document> documents;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return documents;
    }
    
    std::string line;
    int docId = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        Document doc;
        doc.id = docId++;
        doc.content = line;
        doc.tokens = Tokenizer::tokenize(line);
        doc.tokens = Tokenizer::removeStopWords(doc.tokens);
        
        documents.push_back(doc);
    }
    
    file.close();
    std::cout << "Loaded " << documents.size() << " documents from " << filename << std::endl;
    
    return documents;
}

std::vector<Document> FileLoader::loadFromDirectory(const std::string& dirPath) {
    // This is a simplified version - you would need filesystem support
    // For now, just return empty vector
    std::vector<Document> documents;
    std::cerr << "Directory loading not yet implemented" << std::endl;
    return documents;
}