#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include <sstream>

class Tokenizer {
public:
   
    static std::vector<std::string> tokenize(const std::string& text);
    
   
    static std::string normalize(const std::string& text);
    
  
    static std::vector<std::string> removeStopWords(const std::vector<std::string>& tokens);
    
private:
    static std::set<std::string> getStopWords();
};

#endif // TOKENIZER_H