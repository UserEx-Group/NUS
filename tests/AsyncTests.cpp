#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/sema/Type.hpp"
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

bool valid(std::string_view source, bool check_flow = true) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<async-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) return false;
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) return false;
    if (!check_flow) return true;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    const auto mir = nus::mir::MirBuilder{}.lower(hir);
    return !nus::mir::FlowChecker{}.check(mir).hasErrors();
}
}

int main() {
    using nus::sema::Type;
    using nus::sema::TypeKind;

    if (!Type::future(Type::simple(TypeKind::I32)).isCopy()) fail("Future<i32> should be Copy in the eager milestone 17 representation");
    if (Type::future(Type::simple(TypeKind::String)).isCopy()) fail("Future<string> must preserve owned payload semantics");

    if (!valid(R"(
        async fn plus_one(value: i32) -> i32 {
            value + 1
        }

        async fn compute() -> i32 {
            let first = await plus_one(40);
            await plus_one(first)
        }

        fn main() {
            print(block_on(compute()));
        }
    )")) fail("valid async/await program was rejected");

    if (!valid(R"(
        fn maybe(okay: bool) -> Result<i32, string> {
            let text = "bad";
            return if okay {
                ok::<i32, string>(41)
            } else {
                err::<i32, string>(string_clone(&text))
            };
        }

        async fn compute(okay: bool) -> Result<i32, string> {
            let value = maybe(okay)?;
            ok::<i32, string>(value + 1)
        }

        fn main() {
            let result = block_on(compute(true));
        }
    )")) fail("try propagation inside async fn was rejected");

    if (valid(R"(
        fn main() {
            let future = block_on(1);
        }
    )", false)) fail("block_on must require Future<T>");

    if (valid(R"(
        fn main() {
            let value = await 1;
        }
    )", false)) fail("await outside async fn must be rejected");

    if (valid(R"(
        async fn bad() -> i32 {
            await 1
        }
        fn main() {}
    )", false)) fail("await must require Future<T>");

    if (valid(R"(
        async fn main() {}
    )", false)) fail("async main must be rejected before executor entrypoint support");

    if (valid(R"(
        async fn bad(value: &i32) -> &i32 {
            value
        }
        fn main() {}
    )", false)) fail("async borrowed returns must be rejected in milestone 17");

    std::cout << "async tests passed\n";
    return 0;
}
