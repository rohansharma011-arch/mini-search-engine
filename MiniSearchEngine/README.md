# Mini Search Engine in C++

A small information-retrieval system: it indexes a folder of `.txt` files and ranks
them against a user query using an **inverted index** and **TF-IDF cosine similarity**.
It has two front-ends that share the same C++ engine:

- **Web UI** (`web_server`) — a C++ HTTP server + browser page with live autocomplete,
  highlighted results, a document viewer, an index explorer and "add document".
- **Console** (`search_engine`) — the same features in the terminal.

## Quick start (Windows)

```powershell
.\build.bat                 # compile everything into bin\
.\bin\web_server.exe        # starts the server and opens http://localhost:8080
```
Or just double-click **`start_ui.bat`**. Stop the server with `Ctrl+C`.

> In PowerShell always prefix programs in the current folder with `.\`.
> If `g++` is not recognized, install MinGW-w64 (e.g. via MSYS2: `pacman -S mingw-w64-ucrt-x86_64-gcc`)
> and add `C:\msys64\ucrt64\bin` to your PATH.

```
=================================
       MINI SEARCH ENGINE
=================================
Indexed 6 documents from 'documents' in 0.90 ms.

Enter search query:
> binary tree

Results (6):

[1] dsa.txt
    Score: 0.269
    Matched: binary, tree
    AVL [trees] and red black [trees] are self balancing [binary] [trees].

[2] algorithms.txt
    Score: 0.092
    Matched: binary, tree
    [Binary] search finds an element in a sorted array in O(log n) time.
...
Enter document number to open (or press Enter to search again):
> 1
```

## Features

| Level | Feature | Where |
|---|---|---|
| 1 | Read files, tokenize, keyword search | `SearchEngine::loadDirectory`, `TextProcessor::tokenize` |
| 2 | Inverted index with positions (hash map → ordered map → vector) | `InvertedIndex` |
| 3 | Case normalization, stop-word removal, plural stemming | `TextProcessor` |
| 3 | TF-IDF weighting + cosine-similarity ranking | `SearchEngine::search` |
| 3 | Top-K ranking with a **priority queue** (max-heap) | `SearchEngine::search` |
| 4 | `AND`, `OR`, and `"exact phrase"` queries | `SearchEngine::parse`, `InvertedIndex::containsPhrase` |
| 5 | Autocomplete with a **Trie** + min-heap top-k | `Trie` |
| 5 | "Did you mean" suggestions when nothing matches | `main.cpp` |
| — | Snippets with highlighted matches, open full document | `SearchEngine::snippet / highlight` |
| 6 | Web UI: C++ HTTP server (raw sockets, no libraries) + HTML/JS front-end | `src/web_server.cpp`, `web/index.html` |
| — | Unit tests (29 checks) | `tests/tests.cpp` |

## Project structure

```
MiniSearchEngine/
├── include/
│   ├── Document.h          one indexed file
│   ├── TextProcessor.h     tokenize, stop words, stemming
│   ├── InvertedIndex.h     word -> {doc -> positions}
│   ├── Trie.h              prefix tree for suggestions
│   └── SearchEngine.h      parsing, scoring, ranking
├── src/
│   ├── TextProcessor.cpp
│   ├── InvertedIndex.cpp
│   ├── Trie.cpp
│   ├── SearchEngine.cpp
│   ├── main.cpp            console interface
│   └── web_server.cpp      HTTP server + JSON API for the web UI
├── web/index.html          browser front-end
├── tests/tests.cpp
├── documents/              sample collection (add your own .txt files)
├── .vscode/tasks.json      Ctrl+Shift+B to build in VS Code
├── CMakeLists.txt
├── build.sh                Linux / macOS / Git Bash
├── build.bat               Windows (MinGW)
└── start_ui.bat            Windows: build if needed + launch web UI
```

## Build & run

Needs a C++17 compiler (g++ 8+, clang 7+, or MSVC 2019+).

**Windows (MinGW g++)**
```powershell
.\build.bat
.\bin\web_server.exe                  # web UI
.\bin\search_engine.exe documents     # console
.\bin\tests.exe
```

**Linux / macOS**
```sh
./build.sh
./bin/web_server
./bin/search_engine documents
./bin/tests
```

**VS Code** — open the folder, press `Ctrl+Shift+B` to build, then
*Terminal → Run Task → Run Mini Search Engine*.

**CMake**
```sh
cmake -S . -B build
cmake --build build
./build/search_engine documents
```

## Web UI architecture

```
 Browser (web/index.html)                    C++ (web_server.exe)
 ────────────────────────                    ────────────────────────────────
 search box, results, viewer  ── HTTP ──▶   socket accept → parse request
                              ◀── JSON ──   route → SearchEngine → JSON response
```

| Endpoint | Returns |
|---|---|
| `GET /api/search?q=` | ranked results, scores, snippets, tf/idf per matched term, how the query was parsed, time taken |
| `GET /api/suggest?q=` | Trie autocomplete words with frequencies |
| `GET /api/doc?id=&terms=` | full document text with highlights |
| `GET /api/index?term=` | inverted-index entry: df, idf, tf and positions per document |
| `GET /api/stats` | collection statistics |
| `POST /api/reload` | re-read the documents folder |
| `POST /api/add?name=` | save the request body as `documents/name.txt` and re-index |

The server binds to `127.0.0.1` only, so nothing is exposed to other machines.
Options: `web_server [documents_folder] [--port 8081] [--no-browser]`.

## Using the console version

| Input | Meaning |
|---|---|
| `binary tree` | ranked search; documents with any of the words |
| `binary AND tree` | both words must appear |
| `tcp OR sql` | either word |
| `"binary tree"` | words must appear next to each other |
| `"data structure" AND heap OR sql` | combined (`AND` binds tighter than `OR`) |
| `:suggest prog` | autocomplete → programming, programs, program, programmer |
| `:open 2` | open result 2 from the last search |
| `:index tree` | show the inverted-index entry: df, idf, positions per document |
| `:list` / `:stats` / `:reload` / `:help` / `:quit` | utilities |

`AND` / `OR` must be typed in capitals (lower-case "and"/"or" are stop words).

## How it works

### 1. Text processing
`"C++ is a Programming Language."` → tokens `c++ is a programming language`
→ remove stop words → `c++ programming language`.
Plurals are stemmed (`trees → tree`, `classes → class`, `queries → query`) so a search
for *tree* also finds *trees*. `+` and `#` are kept inside words so `C++` and `C#` are real terms.

### 2. Inverted index
```
tree -> { algorithms.txt: [41], dsa.txt: [15, 22, 23, 30, ...], database.txt: [59] }
```
`unordered_map<string, map<int, vector<int>>>` — O(1) average lookup of a word, and the
positions let us check phrases: for `"binary tree"`, for every position *p* of *binary*,
binary-search for *p + 1* in the positions of *tree*.

### 3. Scoring (TF-IDF + cosine similarity)
- **tf weight** = `1 + log(tf)` — sub-linear, so a word appearing 10× isn't 10× as important
- **idf** = `log((N + 1) / (df + 1)) + 1` — rare words count more; smoothed so it is never 0
- each document and the query become vectors of `tf weight × idf`
- **score** = `cos θ = (q · d) / (|q| |d|)`, a value between 0 and 1

Document vector lengths `|d|` are pre-computed at index time, so a query only touches
the posting lists of its own terms — it never scans every file.

### 4. Ranking
Matching documents go into a `priority_queue` (max-heap) keyed on score; the top 10 are popped.

### 5. Autocomplete
Every non-stop word is inserted into a Trie with a frequency counter. `suggest(prefix)` walks
to the prefix node, collects the words beneath it, and keeps the k most frequent in a
size-k min-heap.

## Complexity

| Operation | Cost |
|---|---|
| Build index | O(T) for T total words |
| Look up a term | O(1) average |
| Search | O(Σ posting list sizes + R log R) for R matching docs |
| Phrase check in one doc | O(p₁ · k · log p) |
| Autocomplete | O(L + S log k), L = prefix length, S = words under prefix |

## Ideas to extend

- Web/GUI front-end (keep this as the backend; expose `search()` over HTTP)
- Full Porter stemmer, spelling correction with edit distance
- Save/load the index to disk instead of rebuilding
- `NOT` operator, wildcard queries (`prog*`)
- BM25 scoring and a comparison with TF-IDF
- PageRank-style ranking if documents link to each other (graphs)
