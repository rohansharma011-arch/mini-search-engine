cmake_minimum_required(VERSION 3.10)
project(MiniSearchEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(search_core
  src/TextProcessor.cpp
  src/InvertedIndex.cpp
  src/Trie.cpp
  src/SearchEngine.cpp)
target_include_directories(search_core PUBLIC include)

add_executable(search_engine src/main.cpp)
target_link_libraries(search_engine PRIVATE search_core)

add_executable(web_server src/web_server.cpp)
target_link_libraries(web_server PRIVATE search_core)
if(WIN32)
  target_link_libraries(web_server PRIVATE ws2_32)
endif()

add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE search_core)

enable_testing()
add_test(NAME unit_tests COMMAND tests)
