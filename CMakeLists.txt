cmake_minimum_required(VERSION 3.20)
project(nus_lang VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(nus_frontend
    src/source/SourceManager.cpp
    src/lexer/Token.cpp
    src/lexer/Lexer.cpp
    src/ast/AstPrinter.cpp
    src/parser/Parser.cpp
    src/sema/Type.cpp
    src/sema/SymbolTable.cpp
    src/sema/SemanticAnalyzer.cpp
    src/hir/HirBuilder.cpp
    src/hir/HirPrinter.cpp
    src/mir/MirBuilder.cpp
    src/mir/MirPrinter.cpp
    src/mir/CfgVerifier.cpp
    src/mir/Dataflow.cpp
    src/mir/FlowChecker.cpp
    src/mir/DropElaborator.cpp
    src/mir/FlowPrinter.cpp
)

target_include_directories(nus_frontend PUBLIC include)

target_compile_options(nus_frontend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wno-missing-field-initializers>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)

add_executable(nusc app/main.cpp)
target_link_libraries(nusc PRIVATE nus_frontend)

enable_testing()

add_executable(nus_lexer_tests tests/LexerTests.cpp)
target_link_libraries(nus_lexer_tests PRIVATE nus_frontend)
add_test(NAME lexer COMMAND nus_lexer_tests)

add_executable(nus_parser_tests tests/ParserTests.cpp)
target_link_libraries(nus_parser_tests PRIVATE nus_frontend)
add_test(NAME parser COMMAND nus_parser_tests)

add_executable(nus_semantic_tests tests/SemanticTests.cpp)
target_link_libraries(nus_semantic_tests PRIVATE nus_frontend)
add_test(NAME semantic COMMAND nus_semantic_tests)

add_executable(nus_ownership_tests tests/OwnershipTests.cpp)
target_link_libraries(nus_ownership_tests PRIVATE nus_frontend)
add_test(NAME ownership COMMAND nus_ownership_tests)

add_executable(nus_hir_tests tests/HirTests.cpp)
target_link_libraries(nus_hir_tests PRIVATE nus_frontend)
add_test(NAME hir COMMAND nus_hir_tests)

add_executable(nus_mir_tests tests/MirTests.cpp)
target_link_libraries(nus_mir_tests PRIVATE nus_frontend)
add_test(NAME mir COMMAND nus_mir_tests)

add_executable(nus_flow_tests tests/FlowTests.cpp)
target_link_libraries(nus_flow_tests PRIVATE nus_frontend)
add_test(NAME flow COMMAND nus_flow_tests)

add_executable(nus_drop_tests tests/DropTests.cpp)
target_link_libraries(nus_drop_tests PRIVATE nus_frontend)
add_test(NAME drop COMMAND nus_drop_tests)
