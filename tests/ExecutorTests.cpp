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
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

bool valid(std::string_view source, bool check_flow = true) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<executor-test>", std::string(source));
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
    if (!valid(R"(
        async fn task_a() {
            await yield_now();
            print("A");
        }

        async fn task_b() {
            print("B1");
            await yield_now();
            print("B2");
        }

        fn main() {
            let mut executor = executor_new();
            executor_spawn(&mut executor, task_a());
            executor_spawn(&mut executor, task_b());
            executor_run(&mut executor);
        }
    )")) fail("valid cooperative executor program was rejected");

    if (valid(R"(
        async fn accept_task(listener: &mut TcpListener) {
            let result = await tcp_accept_async(listener);
        }

        fn bad_spawn() -> Result<i32, NetworkError> {
            let address = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&address, 8, true)?;
            let future = accept_task(&mut listener);
            let mut executor = executor_new();
            executor_spawn(&mut executor, future);
            ok::<i32, NetworkError>(0)
        }

        fn main() {}
    )")) fail("executor_spawn must reject a Future that borrows a local socket");

    std::cout << "executor tests passed\n";
    return 0;
}
