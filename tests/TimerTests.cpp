#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
bool valid(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<timer-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) return false;
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) return false;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    const auto mir = nus::mir::MirBuilder{}.lower(hir);
    return !nus::mir::FlowChecker{}.check(mir).hasErrors();
}
}

int main() {
    if (!valid(R"(
        async fn delayed() -> i32 {
            await sleep_ms(5u32);
            42
        }
        fn main() { print(block_on(delayed())); }
    )")) fail("sleep_ms future was rejected");

    if (valid(R"(
        fn main() { let future = sleep_ms(-1); }
    )")) fail("sleep_ms must require u32 milliseconds");

    std::cout << "timer tests passed\n";
    return 0;
}
