cmake_minimum_required(VERSION 3.20)
project(ux_lang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ux_frontend
    src/source/SourceManager.cpp
    src/lexer/Token.cpp
    src/lexer/Lexer.cpp
)

target_include_directories(ux_frontend PUBLIC include)

target_compile_options(ux_frontend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)

add_executable(uxc app/main.cpp)
target_link_libraries(uxc PRIVATE ux_frontend)

enable_testing()
add_executable(ux_lexer_tests tests/LexerTests.cpp)
target_link_libraries(ux_lexer_tests PRIVATE ux_frontend)
add_test(NAME lexer COMMAND ux_lexer_tests)
