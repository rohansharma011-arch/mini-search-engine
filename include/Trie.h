#pragma once
// ---------------------------------------------------------------
// Trie (prefix tree) for autocomplete suggestions.
//   insert("program"), insert("programming"), insert("programmer")
//   suggest("prog") -> programming, program, programmer  (most frequent first)
// ---------------------------------------------------------------
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Trie {
public:
    void clear();
    void insert(const std::string& word);

    // Top-k words starting with prefix, ordered by frequency (uses a min-heap).
    std::vector<std::pair<std::string, int>> suggest(const std::string& prefix, size_t k) const;

private:
    struct Node {
        std::unordered_map<char, std::unique_ptr<Node>> children;
        int count = 0;  // > 0 means a word ends here; value = how many times it was seen
    };

    void collect(const Node* node, std::string& word,
                 std::vector<std::pair<std::string, int>>& out) const;

    Node root;
};
