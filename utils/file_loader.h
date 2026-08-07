#ifndef FILE_LOADER_H
#define FILE_LOADER_H

#include "../index/inverted_index.h"
#include <string>
#include <vector>

// Loads one-document-per-line text files into Document structs.
// Tokenisation and stop-word removal are applied at load time.
class FileLoader {
public:
    static std::vector<Document> loadFromFile(const std::string& filename);

private:
    static std::string readFile(const std::string& filename);
};

#endif // FILE_LOADER_H