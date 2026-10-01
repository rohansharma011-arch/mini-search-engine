// Simple unit tests — no external framework needed.
// Build & run:  see README (build.sh / build.bat / CMake target "tests")
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "SearchEngine.h"
#include "TextProcessor.h"
#include "Trie.h"

static int passed = 0, failed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) { ++passed; }                                                  \
        else { ++failed; std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

namespace fs = std::filesystem;

static void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream(p) << text;
}

int main() {
    // ---------- TextProcessor ----------
    TextProcessor tp;
    auto tokens = tp.tokenize("C++ is a Programming Language.");
    CHECK(tokens.size() == 5);
    CHECK(tokens[0] == "c++");
    CHECK(tokens[4] == "language");

    auto terms = tp.process("C++ is a Programming Language.");
    CHECK(terms.size() == 3);                 // is, a removed
    CHECK(tp.stem("trees") == "tree");
    CHECK(tp.stem("classes") == "class");
    CHECK(tp.stem("queries") == "query");
    CHECK(tp.stem("analysis") == "analysis");
    CHECK(tp.stem("class") == "class");

    // ---------- Trie ----------
    Trie trie;
    for (const char* w : {"programming", "programming", "program", "programmer", "process"}) trie.insert(w);
    auto s = trie.suggest("prog", 5);
    CHECK(s.size() == 3);
    CHECK(s[0].first == "programming");       // most frequent first
    CHECK(trie.suggest("xyz", 5).empty());

    // ---------- SearchEngine on a temporary collection ----------
    fs::path dir = fs::temp_directory_path() / "mini_search_engine_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    writeFile(dir / "a.txt", "Binary trees are data structures. A binary tree has two children.");
    writeFile(dir / "b.txt", "C++ supports object oriented programming with classes.");
    writeFile(dir / "c.txt", "TCP is a protocol. The tree of networks uses a spanning tree.");
    writeFile(dir / "d.txt", "Tree binary order is reversed here.");

    SearchEngine engine;
    CHECK(engine.loadDirectory(dir.string()) == 4);

    auto r = engine.search("binary tree");
    CHECK(!r.empty());
    CHECK(engine.document(r[0].docId).name == "a.txt");   // most relevant first
    for (size_t i = 1; i < r.size(); ++i) CHECK(r[i - 1].score >= r[i].score);
    for (const auto& x : r) CHECK(x.score > 0.0 && x.score <= 1.0 + 1e-9);

    // Phrase: d.txt has both words but not consecutive.
    auto phrase = engine.search("\"binary tree\"");
    CHECK(phrase.size() == 1);
    CHECK(engine.document(phrase[0].docId).name == "a.txt");

    // AND / OR
    auto both = engine.search("binary AND tree");
    CHECK(both.size() == 2);                  // a.txt, d.txt
    auto either = engine.search("tcp OR c++");
    CHECK(either.size() == 2);                // b.txt, c.txt
    auto mixed = engine.search("binary AND tcp OR classes");
    CHECK(mixed.size() == 1);                 // only b.txt

    // Stop words only / unknown words
    CHECK(engine.search("the of and").empty());
    CHECK(engine.search("blockchain").empty());

    // Inverted index + idf
    CHECK(engine.index().documentFrequency("tree") == 3);
    CHECK(engine.idf("tcp") > engine.idf("tree"));  // rarer word = higher idf

    fs::remove_all(dir);

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
