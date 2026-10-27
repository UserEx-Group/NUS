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

struct Checked {
    nus::SourceManager sources;
    nus::ast::SourceFile ast;
    nus::sema::SemanticAnalyzer semantic;
};

Checked check(std::string_view source) {
    Checked result;
    const auto id = result.sources.addSource("<enum-test>", std::string(source));
    nus::Lexer lexer(result.sources, id);
    nus::Parser parser(result.sources, lexer.tokenize());
    result.ast = parser.parseSourceFile();
    if (parser.hasErrors()) {
        for (const auto& diagnostic : parser.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("parser rejected enum test");
    }
    result.semantic.analyze(result.ast);
    return result;
}

bool hasDiagnostic(const nus::sema::SemanticAnalyzer& semantic, std::string_view text) {
    for (const auto& diagnostic : semantic.diagnostics()) {
        if (diagnostic.message.find(text) != std::string::npos) return true;
    }
    return false;
}

void testEnumAndMatch() {
    auto checked = check(R"(
        enum AppError { Timeout, Code(i32), }
        fn classify(error: AppError) -> i32 {
            return match error {
                AppError::Timeout => 1,
                AppError::Code(code) => code,
            };
        }
    )");
    if (checked.semantic.hasErrors()) {
        for (const auto& diagnostic : checked.semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("valid enum/match program was rejected");
    }
}

void testExhaustiveness() {
    auto checked = check(R"(
        enum State { Ready, Closed, }
        fn value(state: State) -> i32 {
            return match state {
                State::Ready => 1,
            };
        }
    )");
    if (!hasDiagnostic(checked.semantic, "non-exhaustive match")) fail("missing enum variant was not diagnosed");
}

void testPayloadChecking() {
    auto checked = check(R"(
        enum Message { Code(i32), }
        fn main() {
            let message = Message::Code(true);
        }
    )");
    if (!hasDiagnostic(checked.semantic, "variant payload")) fail("invalid enum payload type was not diagnosed");
}

void testOptionResultPatterns() {
    auto checked = check(R"(
        fn maybe(flag: bool) -> Result<i32, i32> {
            return if flag { ok::<i32, i32>(10) } else { err::<i32, i32>(7) };
        }
        fn forward(flag: bool) -> Result<i32, i32> {
            let value = maybe(flag)?;
            ok::<i32, i32>(value + 1)
        }
        fn main() {
            let option = some(5);
            let value = match option {
                Some(v) => v,
                None => 0,
            };
            print(value);
        }
    )");
    if (checked.semantic.hasErrors()) {
        for (const auto& diagnostic : checked.semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("Option/Result match or ? propagation was rejected");
    }
}

void testTryContext() {
    auto checked = check(R"(
        fn bad() -> i32 {
            let value = some(1)?;
            value
        }
    )");
    if (!hasDiagnostic(checked.semantic, "requires the enclosing function to return Option")) {
        fail("invalid Option ? context was not diagnosed");
    }
}
}

int main() {
    testEnumAndMatch();
    testExhaustiveness();
    testPayloadChecking();
    testOptionResultPatterns();
    testTryContext();
    std::cout << "enum tests passed\n";
    return 0;
}
