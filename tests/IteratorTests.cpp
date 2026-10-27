#include "nus/lexer/Lexer.hpp"
#include "nus/hir/HirBuilder.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
std::vector<nus::Diagnostic> check(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<iterator-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected iterator test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    std::vector<nus::Diagnostic> diagnostics = semantic.diagnostics();
    if (!diagnostics.empty()) return diagnostics;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    const auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    diagnostics.insert(diagnostics.end(), flow.diagnostics.begin(), flow.diagnostics.end());
    return diagnostics;
}
void expectValid(std::string_view source) {
    auto diagnostics = check(source);
    if (!diagnostics.empty()) {
        for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
        fail("expected valid iterator program");
    }
}
void expectError(std::string_view source, std::string_view text) {
    auto diagnostics = check(source);
    for (const auto& diagnostic : diagnostics) if (diagnostic.message.find(text) != std::string::npos) return;
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
    fail("expected iterator diagnostic was not produced");
}
}

int main() {
    expectValid(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(10);
            values.push(20);
            print(values.len(), values.get(1));
            for value in values { print(value); }
        }
    )");

    expectValid(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            let iterator = values.into_iter();
            for value in iterator { print(value); }
        }
    )");

    expectError(R"(
        fn main() {
            let literal = "owned";
            let mut values: Vec<string> = vec_new::<string>();
            values.push(string_clone(&literal));
            for value in values { print(value); }
        }
    )", "requires a Copy element type");

    expectError(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            for value in values { print(value); }
            print(values.len());
        }
    )", "moved value `values`");

    std::cout << "iterator tests passed\n";
    return 0;
}
