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
    const auto id = sources.addSource("<executor-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected executor native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected executor native test");
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected executor native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "executor native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "executor native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        async fn suspended(value: i32) -> i32 {
            await yield_now();
            value + 1
        }

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
            print(block_on(suspended(41)));
            let mut executor = executor_new();
            executor_spawn(&mut executor, task_a());
            executor_spawn(&mut executor, task_b());
            executor_run(&mut executor);
        }
    )");

    if (ir.find("@nus_executor_run") == std::string::npos || ir.find("@nus_yield_poll") == std::string::npos ||
        ir.find("_poll") == std::string::npos) fail("milestone 18 IR is missing executor/state-machine helpers");

    const auto base = std::filesystem::temp_directory_path() / "nus-executor-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("executor native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) { std::cerr << run.message << " (exit " << run.exit_code << ")\n"; fail("executor native executable failed"); }
    if (run.stdout_text != "42\nB1\nA\nB2\n") {
        std::cerr << "executor output was:\n" << run.stdout_text;
        fail("executor native executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "executor native tests passed\n";
    return 0;
}
