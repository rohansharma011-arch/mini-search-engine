#pragma once
// ---------------------------------------------------------------
// SearchEngine — ties everything together.
//
//   TEXT FILES -> TextProcessor -> InvertedIndex (+ Trie)
//   USER QUERY -> parse -> match -> TF-IDF cosine score -> rank (heap) -> results
//
// Query syntax
//   binary tree            free text: any term may match, ranked by relevance
//   binary AND tree        both terms must appear
//   binary OR network      either term
//   "binary tree"          exact phrase (consecutive words)
//   "data structure" AND tree   phrases and operators can be combined
//   (AND binds tighter than OR:  a AND b OR c  ==  (a AND b) OR c)
// ---------------------------------------------------------------
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Document.h"
#include "InvertedIndex.h"
#include "TextProcessor.h"
#include "Trie.h"

struct QueryItem {
    std::vector<std::string> terms;  // 1 term = word, 2+ terms = phrase
    std::string original;            // what the user typed
    bool isPhrase() const { return terms.size() > 1; }
};

struct ParsedQuery {
    bool booleanMode = false;                    // true if AND / OR was used
    std::vector<std::vector<QueryItem>> groups;  // OR of groups; each group = AND of items
    std::vector<std::string> allTerms;           // every processed term (for scoring)
};

struct SearchResult {
    int docId = 0;
    double score = 0.0;                      // cosine similarity, 0..1
    std::vector<std::string> matchedTerms;
};

class SearchEngine {
public:
    // Index every .txt file in a folder. Returns number of documents loaded.
    size_t loadDirectory(const std::string& folder);

    ParsedQuery parse(const std::string& query) const;
    std::vector<SearchResult> search(const std::string& query, size_t topK = 10) const;
    std::vector<std::pair<std::string, int>> suggest(const std::string& prefix, size_t k = 5) const;

    // Display helpers
    // Matching words are wrapped in open/close markers ("[" "]" for the console, <mark> for the web UI).
    std::string snippet(int docId, const std::vector<std::string>& terms, size_t maxLen = 100,
                        const std::string& open = "[", const std::string& close = "]") const;
    std::string highlight(const std::string& text, const std::vector<std::string>& terms,
                          const std::string& open = "[", const std::string& close = "]") const;

    // Accessors / statistics
    const Document& document(int id) const { return docs.at(id); }
    size_t documentCount() const { return docs.size(); }
    size_t totalTerms() const;
    const InvertedIndex& index() const { return idx; }
    const TextProcessor& processor() const { return tp; }
    double idf(const std::string& term) const;
    double indexTimeMs() const { return buildMs; }

private:
    void addDocument(const std::string& name, const std::string& path, const std::string& content);
    void computeNorms();
    double termWeight(int tf, const std::string& term) const;
    bool itemMatches(int docId, const QueryItem& item) const;

    std::vector<Document> docs;
    InvertedIndex idx;
    TextProcessor tp;
    Trie trie;
    double buildMs = 0.0;
};
