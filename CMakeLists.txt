cmake_minimum_required(VERSION 3.20)
project(nus_lang VERSION 0.15.0 LANGUAGES CXX)

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
    src/codegen/TargetInfo.cpp
    src/codegen/Layout.cpp
    src/codegen/LlvmEmitter.cpp
    src/codegen/NativeToolchain.cpp
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

add_executable(nus_codegen_tests tests/CodegenTests.cpp)
target_link_libraries(nus_codegen_tests PRIVATE nus_frontend)
add_test(NAME codegen COMMAND nus_codegen_tests)

add_executable(nus_native_tests tests/NativeTests.cpp)
target_link_libraries(nus_native_tests PRIVATE nus_frontend)
add_test(NAME native COMMAND nus_native_tests)

add_executable(nus_layout_tests tests/LayoutTests.cpp)
target_link_libraries(nus_layout_tests PRIVATE nus_frontend)
add_test(NAME layout COMMAND nus_layout_tests)

add_executable(nus_runtime_tests tests/RuntimeTests.cpp)
target_link_libraries(nus_runtime_tests PRIVATE nus_frontend)
add_test(NAME runtime COMMAND nus_runtime_tests)

add_executable(nus_generic_tests tests/GenericTests.cpp)
target_link_libraries(nus_generic_tests PRIVATE nus_frontend)
add_test(NAME generics COMMAND nus_generic_tests)

add_executable(nus_generic_native_tests tests/GenericNativeTests.cpp)
target_link_libraries(nus_generic_native_tests PRIVATE nus_frontend)
add_test(NAME generic_native COMMAND nus_generic_native_tests)

add_executable(nus_enum_tests tests/EnumTests.cpp)
target_link_libraries(nus_enum_tests PRIVATE nus_frontend)
add_test(NAME enums COMMAND nus_enum_tests)

add_executable(nus_enum_native_tests tests/EnumNativeTests.cpp)
target_link_libraries(nus_enum_native_tests PRIVATE nus_frontend)
add_test(NAME enum_native COMMAND nus_enum_native_tests)

add_executable(nus_trait_tests tests/TraitTests.cpp)
target_link_libraries(nus_trait_tests PRIVATE nus_frontend)
add_test(NAME traits COMMAND nus_trait_tests)

add_executable(nus_trait_native_tests tests/TraitNativeTests.cpp)
target_link_libraries(nus_trait_native_tests PRIVATE nus_frontend)
add_test(NAME trait_native COMMAND nus_trait_native_tests)

add_executable(nus_iterator_tests tests/IteratorTests.cpp)
target_link_libraries(nus_iterator_tests PRIVATE nus_frontend)
add_test(NAME iterators COMMAND nus_iterator_tests)

add_executable(nus_iterator_native_tests tests/IteratorNativeTests.cpp)
target_link_libraries(nus_iterator_native_tests PRIVATE nus_frontend)
add_test(NAME iterator_native COMMAND nus_iterator_native_tests)

add_executable(nus_lifetime_tests tests/LifetimeTests.cpp)
target_link_libraries(nus_lifetime_tests PRIVATE nus_frontend)
add_test(NAME lifetimes COMMAND nus_lifetime_tests)

add_executable(nus_lifetime_native_tests tests/LifetimeNativeTests.cpp)
target_link_libraries(nus_lifetime_native_tests PRIVATE nus_frontend)
add_test(NAME lifetime_native COMMAND nus_lifetime_native_tests)

add_executable(nus_borrowed_iterator_tests tests/BorrowedIteratorTests.cpp)
target_link_libraries(nus_borrowed_iterator_tests PRIVATE nus_frontend)
add_test(NAME borrowed_iterators COMMAND nus_borrowed_iterator_tests)

add_executable(nus_borrowed_iterator_native_tests tests/BorrowedIteratorNativeTests.cpp)
target_link_libraries(nus_borrowed_iterator_native_tests PRIVATE nus_frontend)
add_test(NAME borrowed_iterator_native COMMAND nus_borrowed_iterator_native_tests)

add_executable(nus_network_tests tests/NetworkTests.cpp)
target_link_libraries(nus_network_tests PRIVATE nus_frontend)
add_test(NAME network COMMAND nus_network_tests)

add_executable(nus_network_native_tests tests/NetworkNativeTests.cpp)
target_link_libraries(nus_network_native_tests PRIVATE nus_frontend)
add_test(NAME network_native COMMAND nus_network_native_tests)
