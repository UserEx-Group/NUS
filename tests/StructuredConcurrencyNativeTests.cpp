#include "nus/codegen/LlvmEmitter.hpp"
#include "nus/codegen/NativeToolchain.hpp"
#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/DropElaborator.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
std::string compileIr(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<structured-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected structured concurrency native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected structured concurrency native test");
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected structured concurrency native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "structured native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "structured native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        async fn worker() -> i32 {
            await yield_now();
            42
        }
        async fn slow() -> i32 {
            await sleep_ms(250u32);
            99
        }
        async fn done() {
            await yield_now();
        }
        async fn fallible() -> Result<i32, i32> {
            await yield_now();
            err::<i32, i32>(7)
        }
        async fn add_one(value: &mut i32) {
            await yield_now();
            *value += 1;
        }
        fn main() {
            let mut executor = executor_new();
            let handle = task_spawn(&mut executor, worker());
            print(task_finished(&handle));
            let joined = task_join(&mut executor, handle);
            match joined { Some(value) => print(value), None => print(-1), };

            let unit_handle = task_spawn(&mut executor, done());
            let unit_joined = task_join(&mut executor, unit_handle);
            match unit_joined { Some(_) => print(1), None => print(0), };

            let error_handle = task_spawn(&mut executor, fallible());
            let error_joined = task_join(&mut executor, error_handle);
            match error_joined {
                Some(result) => match result { Ok(value) => print(value), Err(error) => print(error), },
                None => print(-2),
            };

            let mut cancelled = task_spawn(&mut executor, slow());
            print(task_cancel(&mut cancelled));
            let cancelled_value = task_join(&mut executor, cancelled);
            match cancelled_value { Some(value) => print(value), None => print(-1), };

            let mut number = 41;
            let mut tasks = scope_new();
            scope_spawn(&mut tasks, add_one(&mut number));
            scope_run(&mut tasks);
            print(number);
        }
    )");

    if (ir.find("%nus.task.shared") == std::string::npos || ir.find("@nus_executor_run_until") == std::string::npos ||
        ir.find("@nus_task_shared_release") == std::string::npos || ir.find("@nus.executor.next_id") == std::string::npos ||
        ir.find("taskjoinsameexecutor") == std::string::npos) fail("milestone 19 IR is missing task-handle safety helpers");

    const auto base = std::filesystem::temp_directory_path() / "nus-structured-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("structured native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) { std::cerr << run.message << " (exit " << run.exit_code << ")\n"; fail("structured native executable failed"); }
    if (run.stdout_text != "false\n42\n1\n7\ntrue\n-1\n42\n") {
        std::cerr << "structured concurrency output was:\n" << run.stdout_text;
        fail("structured native executable produced unexpected output");
    }
    const auto wrong_ir = compileIr(R"(
        async fn worker() -> i32 { await yield_now(); 7 }
        fn main() {
            let mut first = executor_new();
            let mut second = executor_new();
            let handle = task_spawn(&mut first, worker());
            let result = task_join(&mut second, handle);
            match result { Some(value) => print(value), None => print(-1), };
        }
    )");
#ifdef _WIN32
    const auto wrong_executable = std::filesystem::path(base.string() + "-wrong.exe");
#else
    const auto wrong_executable = std::filesystem::path(base.string() + "-wrong");
#endif
    const auto wrong_build = toolchain.build(wrong_ir, wrong_executable);
    if (!wrong_build.success) fail("wrong-executor safety program failed to build");
    const auto wrong_run = toolchain.run(wrong_executable);
    if (wrong_run.success) fail("task_join with the wrong executor must fail safely");

    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::filesystem::remove(wrong_executable, ec);
    std::cout << "structured concurrency native tests passed\n";
    return 0;
}
