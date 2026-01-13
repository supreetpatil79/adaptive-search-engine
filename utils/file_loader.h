#ifndef FILE_LOADER_H
#define FILE_LOADER_H

#include "inverted_index.h"
#include <string>
#include <vector>

class FileLoader {
public:
    // Load documents from a file (one document per line)
    static std::vector<Document> loadFromFile(const std::string& filename);
    
    // Load documents from a directory
    static std::vector<Document> loadFromDirectory(const std::string& dirPath);
    
private:
    static std::string readFile(const std::string& filename);
};

#endif // FILE_LOADER_H