#!/bin/sh
# Build without CMake (Linux / macOS / Git Bash). Needs g++ with C++17.
set -e
cd "$(dirname "$0")"
mkdir -p bin
SRC="src/TextProcessor.cpp src/InvertedIndex.cpp src/Trie.cpp src/SearchEngine.cpp"
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude $SRC src/web_server.cpp -o bin/web_server
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude $SRC src/main.cpp       -o bin/search_engine
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude $SRC tests/tests.cpp    -o bin/tests
echo "Built bin/web_server, bin/search_engine and bin/tests"
echo "Web UI:   ./bin/web_server        (http://localhost:8080)"
echo "Console:  ./bin/search_engine documents"
