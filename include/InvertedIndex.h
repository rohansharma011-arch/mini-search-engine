#pragma once
// ---------------------------------------------------------------
// InvertedIndex
//   Normal view:    Document -> Words
//   Inverted view:  Word     -> { Document -> [positions] }
//
//   "tree" -> { 1: [4, 9], 3: [12] }
//   means "tree" appears in doc 1 at positions 4 and 9, and in doc 3 at 12.
//   Positions make phrase search ("binary tree") possible.
// ---------------------------------------------------------------
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Document.h"

class InvertedIndex {
public:
    using PostingList = std::map<int, std::vector<int>>;  // docId -> sorted positions

    void clear();
    void addDocument(const Document& doc);

    // nullptr if the term is not in any document
    const PostingList* postings(const std::string& term) const;

    int documentFrequency(const std::string& term) const;
    bool contains(const std::string& term, int docId) const;

    // True if terms appear consecutively in the document.
    bool containsPhrase(int docId, const std::vector<std::string>& terms) const;

    size_t vocabularySize() const { return index.size(); }

private:
    std::unordered_map<std::string, PostingList> index;
};
