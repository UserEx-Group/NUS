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
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }

bool valid(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<structured-concurrency-test>", std::string(source));
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
    using nus::sema::Type;
    using nus::sema::TypeKind;
    if (Type::taskHandle(Type::simple(TypeKind::I32)).isCopy()) fail("TaskHandle<i32> must be non-Copy");
    if (Type::simple(TypeKind::TaskScope).isCopy()) fail("TaskScope must be non-Copy");

    if (!valid(R"(
        async fn worker() -> i32 {
            await yield_now();
            42
        }
        fn main() {
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, worker());
            let value = task_join(&mut executor, handle);
            match value { Some(x) => print(x), None => print(-1), };
        }
    )")) fail("TaskHandle spawn/join program was rejected");

    if (!valid(R"(
        async fn done() {
            await yield_now();
        }
        fn main() {
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, done());
            let value = task_join(&mut executor, handle);
            match value { Some(_) => print(1), None => print(0), };
        }
    )")) fail("TaskHandle<unit> join program was rejected");

    if (!valid(R"(
        async fn fallible() -> Result<i32, i32> {
            await yield_now();
            err::<i32, i32>(7)
        }
        fn main() {
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, fallible());
            let joined = task_join(&mut executor, handle);
            match joined {
                Some(result) => match result { Ok(value) => print(value), Err(error) => print(error), },
                None => print(-1),
            };
        }
    )")) fail("Copy Result values must propagate through TaskHandle<T>");

    if (valid(R"(
        async fn owned() -> string {
            let text = "owned";
            string_clone(&text)
        }
        fn main() {
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, owned());
        }
    )")) fail("task_spawn must require a Copy task result in milestone 19");

    if (valid(R"(
        async fn borrowed(value: &mut i32) -> i32 {
            await yield_now();
            *value += 1;
            *value
        }
        fn main() {
            let mut value = 1;
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, borrowed(&mut value));
        }
    )")) fail("detached task_spawn must reject a Future that borrows a stack local");

    if (!valid(R"(
        async fn add_one(value: &mut i32) {
            await yield_now();
            *value += 1;
        }
        fn main() {
            let mut value = 41;
            let mut tasks = scope_new();
            scope_spawn(&mut tasks, add_one(&mut value));
            scope_run(&mut tasks);
            print(value);
        }
    )")) fail("TaskScope must accept borrowing futures and release the borrow after its last use");

    if (valid(R"(
        async fn add_one(value: &mut i32) {
            await yield_now();
            *value += 1;
        }
        fn main() {
            let mut value = 41;
            let mut tasks = scope_new();
            scope_spawn(&mut tasks, add_one(&mut value));
            value = 0;
            scope_run(&mut tasks);
        }
    )")) fail("borrowed roots must remain protected while a TaskScope is live");

    std::cout << "structured concurrency tests passed\n";
    return 0;
}
