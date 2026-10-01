#include "TextProcessor.h"

#include <cctype>

TextProcessor::TextProcessor() {
    stopWords = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "of", "and", "or", "in", "on", "at", "to", "for", "with", "by",
        "from", "as", "it", "its", "this", "that", "these", "those", "into",
        "than", "then", "so", "such", "can", "will", "also", "not", "but",
        "if", "has", "have", "had", "do", "does", "did", "which", "who",
        "what", "when", "where", "how", "we", "you", "they", "he", "she",
        "i", "our", "your", "their", "them", "there", "here", "about",
        "each", "all", "any", "both", "more", "most", "other", "some", "no",
        "only", "very", "just", "may", "many", "one"
    };
}

std::vector<std::string> TextProcessor::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            current += static_cast<char>(std::tolower(c));
        } else if ((c == '+' || c == '#') && !current.empty()) {
            current += static_cast<char>(c);   // c++  c#
        } else {
            flush();                            // space / punctuation ends a word
        }
    }
    flush();
    return tokens;
}

bool TextProcessor::isStopWord(const std::string& word) const {
    return stopWords.count(word) > 0;
}

std::string TextProcessor::stem(const std::string& w) const {
    if (w.size() <= 3 || !std::isalpha(static_cast<unsigned char>(w.back()))) return w;

    auto endsWith = [&](const std::string& suffix) {
        return w.size() > suffix.size() &&
               w.compare(w.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (endsWith("sses")) return w.substr(0, w.size() - 2);       // classes -> class
    if (endsWith("ies"))  return w.substr(0, w.size() - 3) + "y";  // queries -> query
    if (endsWith("ss") || endsWith("us") || endsWith("is")) return w; // class, status, analysis
    if (endsWith("s"))    return w.substr(0, w.size() - 1);        // trees -> tree
    return w;
}

std::vector<std::string> TextProcessor::process(const std::string& text) const {
    std::vector<std::string> terms;
    for (const auto& token : tokenize(text)) {
        if (isStopWord(token)) continue;
        terms.push_back(stem(token));
    }
    return terms;
}
