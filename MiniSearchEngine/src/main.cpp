// ===============================================================
//  MINI SEARCH ENGINE  —  console front-end
//
//  Usage:  search_engine [documents_folder]
//          (default folder: ./documents, falls back to ../documents)
// ===============================================================
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "SearchEngine.h"

namespace {

void printBanner() {
    std::cout << "=================================\n"
              << "       MINI SEARCH ENGINE\n"
              << "=================================\n";
}

void printHelp() {
    std::cout << "\nType a search query, or one of these commands:\n"
              << "  binary tree            ranked search (any word)\n"
              << "  binary AND tree        all words must appear\n"
              << "  tcp OR sql             either word\n"
              << "  \"binary tree\"          exact phrase\n"
              << "  :suggest <prefix>      autocomplete, e.g. :suggest prog\n"
              << "  :open <n>              open result n from the last search\n"
              << "  :list                  list indexed documents\n"
              << "  :index <word>          show the inverted-index entry for a word\n"
              << "  :stats                 index statistics\n"
              << "  :reload                re-read the documents folder\n"
              << "  :help                  show this help\n"
              << "  :quit                  exit\n\n";
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool isNumber(const std::string& s) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string findDocumentsFolder(int argc, char* argv[]) {
    if (argc > 1) return argv[1];
    for (const char* candidate : {"documents", "../documents", "../../documents"}) {
        if (std::filesystem::is_directory(candidate)) return candidate;
    }
    return "documents";
}

void openDocument(const SearchEngine& engine, const SearchResult& r) {
    const Document& doc = engine.document(r.docId);
    std::cout << "\n---------------- " << doc.name << " ----------------\n"
              << engine.highlight(doc.content, r.matchedTerms)
              << "\n---------------- end of " << doc.name << " ----------------\n\n";
}

void showResults(const SearchEngine& engine, const std::vector<SearchResult>& results,
                 const std::string& query) {
    if (results.empty()) {
        std::cout << "\nNo documents match \"" << query << "\".\n";
        // Help the user: suggest completions for the last word typed.
        std::string last = query.substr(query.find_last_of(" \"") == std::string::npos
                                            ? 0 : query.find_last_of(" \"") + 1);
        auto sugg = engine.suggest(last, 5);
        if (!last.empty() && !sugg.empty()) {
            std::cout << "Did you mean: ";
            for (size_t i = 0; i < sugg.size(); ++i)
                std::cout << (i ? ", " : "") << sugg[i].first;
            std::cout << " ?\n";
        }
        std::cout << "\n";
        return;
    }

    std::cout << "\nResults (" << results.size() << "):\n\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const Document& doc = engine.document(r.docId);
        std::cout << "[" << i + 1 << "] " << doc.name << "\n"
                  << "    Score: " << std::fixed << std::setprecision(3) << r.score << "\n"
                  << "    Matched: ";
        for (size_t t = 0; t < r.matchedTerms.size(); ++t)
            std::cout << (t ? ", " : "") << r.matchedTerms[t];
        std::cout << "\n    " << engine.snippet(r.docId, r.matchedTerms) << "\n\n";
    }
}

void showStats(const SearchEngine& engine) {
    std::cout << "\nDocuments indexed : " << engine.documentCount()
              << "\nUnique terms      : " << engine.index().vocabularySize()
              << "\nTotal terms       : " << engine.totalTerms()
              << "\nIndex build time  : " << std::fixed << std::setprecision(2)
              << engine.indexTimeMs() << " ms\n\n";
}

void showIndexEntry(const SearchEngine& engine, const std::string& word) {
    auto terms = engine.processor().process(word);
    if (terms.empty()) {
        std::cout << "\"" << word << "\" is a stop word, it is not indexed.\n\n";
        return;
    }
    const std::string& term = terms[0];
    const auto* list = engine.index().postings(term);
    std::cout << "\nTerm: " << term;
    if (!list) { std::cout << "  ->  not found in any document\n\n"; return; }
    std::cout << "   df = " << list->size()
              << "   idf = " << std::fixed << std::setprecision(3) << engine.idf(term) << "\n";
    for (const auto& [docId, positions] : *list) {
        std::cout << "  " << std::left << std::setw(22) << engine.document(docId).name
                  << std::right << " tf = " << positions.size() << "   positions: ";
        for (size_t i = 0; i < positions.size(); ++i) std::cout << (i ? ", " : "") << positions[i];
        std::cout << "\n";
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    SearchEngine engine;
    std::string folder = findDocumentsFolder(argc, argv);

    printBanner();
    try {
        size_t n = engine.loadDirectory(folder);
        std::cout << "Indexed " << n << " documents from '" << folder << "' in "
                  << std::fixed << std::setprecision(2) << engine.indexTimeMs() << " ms.\n";
        if (n == 0) std::cout << "(Add some .txt files to that folder and type :reload)\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n"
                  << "Run as: search_engine <documents_folder>\n";
        return 1;
    }
    std::cout << "Type :help for commands.\n\n";

    std::vector<SearchResult> lastResults;
    std::string line;

    while (true) {
        std::cout << "Enter search query:\n> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        // ---------- commands ----------
        if (line[0] == ':') {
            std::istringstream ss(line.substr(1));
            std::string cmd, arg;
            ss >> cmd;
            std::getline(ss, arg);
            arg = trim(arg);

            if (cmd == "quit" || cmd == "q" || cmd == "exit") break;
            else if (cmd == "help") printHelp();
            else if (cmd == "stats") showStats(engine);
            else if (cmd == "index") showIndexEntry(engine, arg);
            else if (cmd == "list") {
                std::cout << "\n";
                for (size_t i = 0; i < engine.documentCount(); ++i)
                    std::cout << "  " << i + 1 << ". " << engine.document(static_cast<int>(i)).name
                              << "  (" << engine.document(static_cast<int>(i)).terms.size() << " terms)\n";
                std::cout << "\n";
            } else if (cmd == "suggest") {
                auto sugg = engine.suggest(arg, 8);
                if (sugg.empty()) std::cout << "No suggestions for \"" << arg << "\".\n\n";
                else {
                    std::cout << "\nSuggestions for \"" << arg << "\":\n";
                    for (const auto& [word, count] : sugg)
                        std::cout << "  " << std::left << std::setw(18) << word << std::right
                                  << " (" << count << "x)\n";
                    std::cout << "\n";
                }
            } else if (cmd == "open") {
                if (isNumber(arg) && std::stoi(arg) >= 1 &&
                    std::stoi(arg) <= static_cast<int>(lastResults.size()))
                    openDocument(engine, lastResults[std::stoi(arg) - 1]);
                else std::cout << "Give a result number from the last search.\n\n";
            } else if (cmd == "reload") {
                size_t n = engine.loadDirectory(folder);
                lastResults.clear();
                std::cout << "Re-indexed " << n << " documents.\n\n";
            } else {
                std::cout << "Unknown command. Type :help\n\n";
            }
            continue;
        }

        // ---------- search ----------
        std::cout << "\nSearching...\n";
        lastResults = engine.search(line, 10);
        showResults(engine, lastResults, line);
        if (lastResults.empty()) continue;

        std::cout << "Enter document number to open (or press Enter to search again):\n> " << std::flush;
        std::string choice;
        if (!std::getline(std::cin, choice)) break;
        choice = trim(choice);
        if (isNumber(choice)) {
            int n = std::stoi(choice);
            if (n >= 1 && n <= static_cast<int>(lastResults.size()))
                openDocument(engine, lastResults[n - 1]);
            else
                std::cout << "No result number " << n << ".\n\n";
        } else {
            std::cout << "\n";
        }
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}
