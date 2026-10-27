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
    const auto id = sources.addSource("<borrowed-iterator-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected borrowed iterator test");
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
    const auto diagnostics = check(source);
    if (!diagnostics.empty()) { for (const auto& d : diagnostics) std::cerr << d.message << '\n'; fail("expected valid borrowed iterator program"); }
}
void expectError(std::string_view source, std::string_view text) {
    const auto diagnostics = check(source);
    for (const auto& d : diagnostics) if (d.message.find(text) != std::string::npos) return;
    for (const auto& d : diagnostics) std::cerr << d.message << '\n';
    fail("expected borrowed iterator diagnostic was not produced");
}
}

int main() {
    expectValid(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            values.push(2);
            for value in values.iter() { print(*value); }
            values.push(3);
            for value in values.iter_mut() { *value += 10; }
            print(values.len());
        }
    )");

    expectError(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            let iterator = values.iter();
            values.push(2);
            for value in iterator { print(*value); }
        }
    )", "already borrowed");

    expectError(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            let iterator = values.iter_mut();
            print(values.len());
            for value in iterator { print(*value); }
        }
    )", "mutably borrowed");

    std::cout << "borrowed iterator tests passed\n";
    return 0;
}
