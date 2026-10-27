#include "nus/lexer/Lexer.hpp"
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
    const auto id = sources.addSource("<trait-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) {
        for (const auto& diagnostic : parser.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("parser rejected trait test");
    }
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    return semantic.diagnostics();
}

void expectValid(std::string_view source) {
    const auto diagnostics = check(source);
    if (!diagnostics.empty()) {
        for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
        fail("expected valid trait program");
    }
}

void expectError(std::string_view source, std::string_view text) {
    const auto diagnostics = check(source);
    for (const auto& diagnostic : diagnostics) if (diagnostic.message.find(text) != std::string::npos) return;
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
    fail("expected trait diagnostic was not produced");
}
}

int main() {
    expectValid(R"(
        trait Readable {
            type Item;
            fn read(&self) -> Self::Item;
        }
        struct Box { value: i32 }
        impl Readable for Box {
            type Item = i32;
            fn read(&self) -> i32 { self.value }
        }
        fn main() {
            let box = Box { value: 42 };
            print(box.read());
        }
    )");

    expectValid(R"(
        trait Counted {
            type Item;
            fn count(&self) -> usize;
        }
        impl<T> Counted for Vec<T> {
            type Item = T;
            fn count(&self) -> usize { self.len() }
        }
        fn require_counted<T: Counted>(value: T) -> i32 { 1 }
        fn main() {
            let values: Vec<i32> = vec_new::<i32>();
            print(values.count());
            print(require_counted(values));
        }
    )");

    expectError(R"(
        trait Readable { type Item; fn read(&self) -> Self::Item; }
        struct Box { value: i32 }
        impl Readable for Box { fn read(&self) -> i32 { self.value } }
    )", "missing associated type `Item`");

    expectError(R"(
        trait Readable { type Item; fn read(&self) -> Self::Item; }
        struct Box { value: i32 }
        impl Readable for Box {
            type Item = i32;
            fn read(&self) -> bool { true }
        }
    )", "must return `i32`");

    expectError(R"(
        trait Named { fn name(&self) -> i32; }
        struct Box { value: i32 }
        impl Named for Box { }
    )", "missing method `name`");

    std::cout << "trait tests passed\n";
    return 0;
}
