#pragma once
// ---------------------------------------------------------------
// TextProcessor
//   Turns raw text into clean, comparable index terms:
//     1. Tokenization      "C++ is a Programming Language." -> c++, is, a, programming, language
//     2. Case normalization (everything lower-case)
//     3. Stop-word removal  (is, a, the, of, and, ...)
//     4. Light stemming     (trees -> tree, classes -> class, queries -> query)
// ---------------------------------------------------------------
#include <string>
#include <unordered_set>
#include <vector>

class TextProcessor {
public:
    TextProcessor();

    // Split text into lower-case words. Keeps '+' and '#' inside words so
    // "C++" and "C#" survive as real terms.
    std::vector<std::string> tokenize(const std::string& text) const;

    bool isStopWord(const std::string& word) const;

    // Very small suffix stripper for plurals (a simplified Porter step 1a).
    std::string stem(const std::string& word) const;

    // tokenize -> remove stop words -> stem. This is what goes into the index.
    std::vector<std::string> process(const std::string& text) const;

private:
    std::unordered_set<std::string> stopWords;
};
