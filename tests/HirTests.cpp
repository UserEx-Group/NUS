#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

nus::hir::Program lower(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<hir-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected HIR test program");
    nus::sema::SemanticAnalyzer analyzer;
    analyzer.analyze(ast);
    if (analyzer.hasErrors()) fail("semantic analyzer rejected HIR test program");
    nus::hir::HirBuilder builder(analyzer);
    return builder.lower(ast);
}

void testTypedLocalsAndShadowing() {
    auto program = lower(R"(
        fn main() {
            let value = 1;
            if true {
                let value = 2;
                print(value);
            }
            print(value);
        }
    )");
    if (program.functions.size() != 1) fail("expected one function");
    const auto& function = program.functions.front();
    if (function.locals.size() != 2) fail("expected two HIR locals for shadowed bindings");
    if (function.locals[0].id == function.locals[1].id) fail("shadowed locals must have distinct LocalId values");
    if (function.locals[0].type.name() != "i32" || function.locals[1].type.name() != "i32") {
        fail("HIR locals should retain semantic types");
    }
}

void testReceiverAndReferenceType() {
    auto program = lower(R"(
        struct Packet { value: i32 }
        impl Packet {
            fn read(&self) -> i32 { self.value }
        }
    )");
    if (program.functions.size() != 1) fail("expected lowered method");
    const auto& method = program.functions.front();
    if (!method.receiver) fail("expected HIR receiver");
    if (method.receiver->type.name() != "&Packet") fail("receiver should retain reference type");
    if (method.return_type.name() != "i32") fail("method return type should be preserved");
}
}

int main() {
    testTypedLocalsAndShadowing();
    testReceiverAndReferenceType();
    std::cout << "hir tests passed\n";
    return 0;
}
