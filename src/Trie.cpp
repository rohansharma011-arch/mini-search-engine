#include "Trie.h"

#include <algorithm>
#include <queue>

void Trie::clear() { root.children.clear(); root.count = 0; }

void Trie::insert(const std::string& word) {
    Node* node = &root;
    for (char c : word) {
        auto& child = node->children[c];
        if (!child) child = std::make_unique<Node>();
        node = child.get();
    }
    node->count++;
}

void Trie::collect(const Node* node, std::string& word,
                   std::vector<std::pair<std::string, int>>& out) const {
    if (node->count > 0) out.emplace_back(word, node->count);
    for (const auto& [c, child] : node->children) {
        word.push_back(c);
        collect(child.get(), word, out);
        word.pop_back();
    }
}

std::vector<std::pair<std::string, int>> Trie::suggest(const std::string& prefix, size_t k) const {
    // 1. Walk down to the node for the prefix.
    const Node* node = &root;
    for (char c : prefix) {
        auto it = node->children.find(c);
        if (it == node->children.end()) return {};
        node = it->second.get();
    }

    // 2. Collect every word below it.
    std::vector<std::pair<std::string, int>> all;
    std::string word = prefix;
    collect(node, word, all);

    // 3. Keep the k most frequent using a min-heap of size k.
    //    "Better" = higher count, then alphabetical.
    auto better = [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    };
    std::priority_queue<std::pair<std::string, int>,
                        std::vector<std::pair<std::string, int>>,
                        decltype(better)> heap(better);   // top() = worst of the kept
    for (const auto& entry : all) {
        heap.push(entry);
        if (heap.size() > k) heap.pop();
    }

    std::vector<std::pair<std::string, int>> result;
    while (!heap.empty()) { result.push_back(heap.top()); heap.pop(); }
    std::reverse(result.begin(), result.end());
    return result;
}
