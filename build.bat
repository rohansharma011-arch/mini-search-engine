@echo off
REM Build on Windows with MinGW g++ (C++17). Run from the project folder:  .\build.bat
cd /d "%~dp0"
if not exist bin mkdir bin
set SRC=src\TextProcessor.cpp src\InvertedIndex.cpp src\Trie.cpp src\SearchEngine.cpp

echo Building web UI server...
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude %SRC% src\web_server.cpp -o bin\web_server.exe -lws2_32 || goto :error
echo Building console version...
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude %SRC% src\main.cpp -o bin\search_engine.exe || goto :error
echo Building tests...
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude %SRC% tests\tests.cpp -o bin\tests.exe || goto :error

echo.
echo Done.
echo   Web UI:   .\bin\web_server.exe      (opens http://localhost:8080)
echo   Console:  .\bin\search_engine.exe documents
echo   Tests:    .\bin\tests.exe
goto :eof

:error
echo Build failed.
exit /b 1
