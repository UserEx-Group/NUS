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
    const auto id = sources.addSource("<lifetime-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected lifetime test");
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
    if (!diagnostics.empty()) {
        for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
        fail("expected valid lifetime program");
    }
}
void expectError(std::string_view source, std::string_view text) {
    const auto diagnostics = check(source);
    for (const auto& diagnostic : diagnostics) if (diagnostic.message.find(text) != std::string::npos) return;
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
    fail("expected lifetime diagnostic was not produced");
}
}

int main() {
    expectValid(R"(
        fn identity(value: &i32) -> &i32 { value }
        fn main() {
            let mut value = 7;
            let reference = identity(&value);
            print(*reference);
            value = 8;
            print(value);
        }
    )");

    expectValid(R"(
        struct Packet { value: i32 }
        impl Packet {
            fn as_ref(&self) -> &Packet { self }
        }
        fn main() {
            let packet = Packet { value: 5 };
            let reference = packet.as_ref();
            print(reference.value);
        }
    )");

    expectError(R"(
        fn bad() -> &i32 {
            let value = 1;
            &value
        }
    )", "cannot return a reference without a borrowed input");

    expectError(R"(
        fn choose(left: &i32, right: &i32) -> &i32 { left }
    )", "multiple borrowed inputs");

    expectError(R"(
        fn identity(value: &i32) -> &i32 { value }
        fn main() {
            let mut value = 1;
            let reference = identity(&value);
            value = 2;
            print(*reference);
        }
    )", "while it is borrowed");

    std::cout << "lifetime tests passed\n";
    return 0;
}
