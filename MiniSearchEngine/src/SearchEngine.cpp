#include "SearchEngine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

// ===============================================================
// Indexing
// ===============================================================

size_t SearchEngine::loadDirectory(const std::string& folder) {
    if (!fs::exists(folder) || !fs::is_directory(folder))
        throw std::runtime_error("Folder not found: " + folder);

    auto start = std::chrono::steady_clock::now();
    docs.clear();
    idx.clear();
    trie.clear();

    // Collect .txt files and sort them so document ids are stable.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        addDocument(file.filename().string(), file.string(), buffer.str());
    }
    computeNorms();

    auto end = std::chrono::steady_clock::now();
    buildMs = std::chrono::duration<double, std::milli>(end - start).count();
    return docs.size();
}

void SearchEngine::addDocument(const std::string& name, const std::string& path,
                               const std::string& content) {
    Document doc;
    doc.id = static_cast<int>(docs.size());
    doc.name = name;
    doc.path = path;
    doc.content = content;
    doc.terms = tp.process(content);
    for (const auto& term : doc.terms) doc.termFreq[term]++;

    // Raw words (not stemmed) feed the autocomplete trie.
    for (const auto& word : tp.tokenize(content)) {
        if (word.size() >= 2 && !tp.isStopWord(word)) trie.insert(word);
    }

    idx.addDocument(doc);
    docs.push_back(std::move(doc));
}

// ===============================================================
// TF-IDF
//   tf weight  = 1 + log(tf)                 (sub-linear: 10 occurrences != 10x as relevant)
//   idf        = log((N + 1) / (df + 1)) + 1 (smoothed, never zero or negative)
//   weight     = tf weight * idf
//   score      = cosine similarity between query vector and document vector
// ===============================================================

double SearchEngine::idf(const std::string& term) const {
    double n = static_cast<double>(docs.size());
    double df = static_cast<double>(idx.documentFrequency(term));
    return std::log((n + 1.0) / (df + 1.0)) + 1.0;
}

double SearchEngine::termWeight(int tf, const std::string& term) const {
    if (tf <= 0) return 0.0;
    return (1.0 + std::log(static_cast<double>(tf))) * idf(term);
}

void SearchEngine::computeNorms() {
    for (auto& doc : docs) {
        double sum = 0.0;
        for (const auto& [term, tf] : doc.termFreq) {
            double w = termWeight(tf, term);
            sum += w * w;
        }
        doc.norm = std::sqrt(sum);
    }
}

size_t SearchEngine::totalTerms() const {
    size_t total = 0;
    for (const auto& doc : docs) total += doc.terms.size();
    return total;
}

// ===============================================================
// Query parsing
// ===============================================================

ParsedQuery SearchEngine::parse(const std::string& query) const {
    ParsedQuery pq;
    pq.groups.emplace_back();

    size_t i = 0;
    while (i < query.size()) {
        unsigned char c = static_cast<unsigned char>(query[i]);
        if (std::isspace(c)) { ++i; continue; }

        // "quoted phrase"
        if (c == '"') {
            size_t close = query.find('"', i + 1);
            if (close == std::string::npos) close = query.size();
            std::string phrase = query.substr(i + 1, close - i - 1);
            auto terms = tp.process(phrase);
            if (!terms.empty()) pq.groups.back().push_back({terms, "\"" + phrase + "\""});
            i = close + 1;
            continue;
        }

        // plain word or operator
        size_t j = i;
        while (j < query.size() && !std::isspace(static_cast<unsigned char>(query[j])) && query[j] != '"') ++j;
        std::string word = query.substr(i, j - i);
        i = j;

        if (word == "AND" || word == "&&") { pq.booleanMode = true; continue; }
        if (word == "OR"  || word == "||") { pq.booleanMode = true; pq.groups.emplace_back(); continue; }

        for (const auto& term : tp.process(word)) pq.groups.back().push_back({{term}, word});
    }

    // Drop empty groups (e.g. "OR" at the end, or a group of only stop words).
    pq.groups.erase(std::remove_if(pq.groups.begin(), pq.groups.end(),
                                   [](const std::vector<QueryItem>& g) { return g.empty(); }),
                    pq.groups.end());

    for (const auto& group : pq.groups)
        for (const auto& item : group)
            for (const auto& term : item.terms) pq.allTerms.push_back(term);

    return pq;
}

bool SearchEngine::itemMatches(int docId, const QueryItem& item) const {
    if (item.isPhrase()) return idx.containsPhrase(docId, item.terms);
    return idx.contains(item.terms[0], docId);
}

// ===============================================================
// Search + ranking
// ===============================================================

std::vector<SearchResult> SearchEngine::search(const std::string& query, size_t topK) const {
    ParsedQuery pq = parse(query);
    if (pq.allTerms.empty() || docs.empty()) return {};

    // 1. Candidate documents = union of posting lists (never scans every file).
    std::set<int> candidates;
    for (const auto& term : pq.allTerms) {
        if (const auto* list = idx.postings(term))
            for (const auto& entry : *list) candidates.insert(entry.first);
    }

    // 2. Query vector.
    std::unordered_map<std::string, int> queryTf;
    for (const auto& term : pq.allTerms) queryTf[term]++;
    std::unordered_map<std::string, double> queryWeight;
    double queryNorm = 0.0;
    for (const auto& [term, tf] : queryTf) {
        double w = termWeight(tf, term);
        queryWeight[term] = w;
        queryNorm += w * w;
    }
    queryNorm = std::sqrt(queryNorm);

    // 3. Filter by the query logic, score the survivors, keep them in a max-heap.
    auto lower = [](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.docId > b.docId;
    };
    std::priority_queue<SearchResult, std::vector<SearchResult>, decltype(lower)> heap(lower);

    for (int docId : candidates) {
        bool ok = false;
        if (pq.booleanMode) {
            // OR of groups, each group needs ALL its items.
            for (const auto& group : pq.groups) {
                bool all = std::all_of(group.begin(), group.end(),
                                       [&](const QueryItem& it) { return itemMatches(docId, it); });
                if (all) { ok = true; break; }
            }
        } else {
            // Free text: phrases are required, single words are optional (at least one item).
            const auto& items = pq.groups.front();
            bool phrasesOk = std::all_of(items.begin(), items.end(), [&](const QueryItem& it) {
                return !it.isPhrase() || itemMatches(docId, it);
            });
            bool anyOk = std::any_of(items.begin(), items.end(),
                                     [&](const QueryItem& it) { return itemMatches(docId, it); });
            ok = phrasesOk && anyOk;
        }
        if (!ok) continue;

        const Document& doc = docs[docId];
        SearchResult r;
        r.docId = docId;
        double dot = 0.0;
        for (const auto& [term, qw] : queryWeight) {
            auto it = doc.termFreq.find(term);
            if (it == doc.termFreq.end()) continue;
            dot += qw * termWeight(it->second, term);
            r.matchedTerms.push_back(term);
        }
        std::sort(r.matchedTerms.begin(), r.matchedTerms.end());
        r.score = (doc.norm > 0 && queryNorm > 0) ? dot / (doc.norm * queryNorm) : 0.0;
        heap.push(r);
    }

    // 4. Pop the best topK.
    std::vector<SearchResult> results;
    while (!heap.empty() && results.size() < topK) {
        results.push_back(heap.top());
        heap.pop();
    }
    return results;
}

std::vector<std::pair<std::string, int>> SearchEngine::suggest(const std::string& prefix, size_t k) const {
    std::string lowered;
    for (unsigned char c : prefix) lowered += static_cast<char>(std::tolower(c));
    return trie.suggest(lowered, k);
}

// ===============================================================
// Display helpers
// ===============================================================

std::string SearchEngine::highlight(const std::string& text, const std::vector<std::string>& terms,
                                    const std::string& open, const std::string& close) const {
    // Wrap matching words in [brackets], using the same word rules as the tokenizer
    // so punctuation stays outside:  "tree."  ->  "[tree]."
    std::unordered_set<std::string> wanted(terms.begin(), terms.end());
    auto isWordChar = [](unsigned char c) { return std::isalnum(c) != 0; };
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (!isWordChar(static_cast<unsigned char>(text[i]))) { out += text[i++]; continue; }
        size_t j = i;
        while (j < text.size() && (isWordChar(static_cast<unsigned char>(text[j])) ||
                                   text[j] == '+' || text[j] == '#')) ++j;
        std::string word = text.substr(i, j - i);
        auto processed = tp.process(word);
        bool hit = !processed.empty() && wanted.count(processed[0]);
        out += hit ? open + word + close : word;
        i = j;
    }
    return out;
}

std::string SearchEngine::snippet(int docId, const std::vector<std::string>& terms, size_t maxLen,
                                  const std::string& open, const std::string& close) const {
    // Pick the line with the most highlighted words.
    std::istringstream lines(docs.at(docId).content);
    std::string line, firstLine, best;
    size_t bestHits = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        if (firstLine.empty()) firstLine = line;

        std::string marked = highlight(line, terms, open, close);
        size_t hits = (marked.size() - line.size()) / (open.size() + close.size());
        if (hits > bestHits) { bestHits = hits; best = marked; }
    }
    if (bestHits == 0) best = firstLine;
    if (best.size() <= maxLen) return best;

    // Too long: cut a window around the first highlight.
    size_t hit = best.find(open);
    size_t start = (hit != std::string::npos && hit > 30) ? hit - 30 : 0;
    std::string cut = best.substr(start, maxLen);
    return (start > 0 ? "..." : "") + cut + (start + maxLen < best.size() ? "..." : "");
}
