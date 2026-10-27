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
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

std::string compileIr(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<async-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected async native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected async native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected async native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "async native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "async native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        async fn plus_one(value: i32) -> i32 {
            value + 1
        }

        fn maybe(okay: bool) -> Result<i32, string> {
            let message = "failed";
            return if okay {
                ok::<i32, string>(41)
            } else {
                err::<i32, string>(string_clone(&message))
            };
        }

        async fn compute(okay: bool) -> Result<i32, string> {
            let value = maybe(okay)?;
            let next = await plus_one(value);
            ok::<i32, string>(next)
        }

        fn show(okay: bool) {
            let result = block_on(compute(okay));
            match result {
                Ok(value) => print(value),
                Err(_) => print(-1),
            };
        }

        fn main() {
            show(true);
            show(false);
        }
    )");

    if (ir.find("future") == std::string::npos || ir.find("assert") == std::string::npos) {
        fail("milestone 17 native IR is missing eager future lowering");
    }

    const auto base = std::filesystem::temp_directory_path() / "nus-async-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("async native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("async native executable failed");
    }
    if (run.stdout_text != "42\n-1\n") {
        std::cerr << "async output was:\n" << run.stdout_text;
        fail("async native executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "async native tests passed\n";
    return 0;
}
