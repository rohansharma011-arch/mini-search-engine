#include "InvertedIndex.h"

#include <algorithm>

void InvertedIndex::clear() { index.clear(); }

void InvertedIndex::addDocument(const Document& doc) {
    for (int pos = 0; pos < static_cast<int>(doc.terms.size()); ++pos) {
        // positions are pushed in increasing order, so each list stays sorted
        index[doc.terms[pos]][doc.id].push_back(pos);
    }
}

const InvertedIndex::PostingList* InvertedIndex::postings(const std::string& term) const {
    auto it = index.find(term);
    return it == index.end() ? nullptr : &it->second;
}

int InvertedIndex::documentFrequency(const std::string& term) const {
    const PostingList* list = postings(term);
    return list ? static_cast<int>(list->size()) : 0;
}

bool InvertedIndex::contains(const std::string& term, int docId) const {
    const PostingList* list = postings(term);
    return list && list->count(docId);
}

bool InvertedIndex::containsPhrase(int docId, const std::vector<std::string>& terms) const {
    if (terms.empty()) return false;

    // Fetch the position list of every term in this document.
    std::vector<const std::vector<int>*> positions;
    for (const auto& term : terms) {
        const PostingList* list = postings(term);
        if (!list) return false;
        auto it = list->find(docId);
        if (it == list->end()) return false;
        positions.push_back(&it->second);
    }

    // For each start position p of the first term, check that term i is at p + i.
    // Binary search keeps each check O(log n).
    for (int start : *positions[0]) {
        bool match = true;
        for (size_t i = 1; i < positions.size() && match; ++i) {
            match = std::binary_search(positions[i]->begin(), positions[i]->end(),
                                       start + static_cast<int>(i));
        }
        if (match) return true;
    }
    return false;
}
