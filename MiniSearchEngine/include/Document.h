#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// One text file in the collection.
struct Document {
    int id = 0;
    std::string name;                                // e.g. dsa.txt
    std::string path;                                // full path on disk
    std::string content;                             // original text (for display)
    std::vector<std::string> terms;                  // processed terms in order
    std::unordered_map<std::string, int> termFreq;   // term -> count in this document
    double norm = 0.0;                               // length of TF-IDF vector (for cosine similarity)
};
