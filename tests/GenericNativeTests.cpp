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
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

std::string compileIr(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<generic-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected generic native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected generic native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected generic native test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "generic native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "generic native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        fn identity<T>(value: T) -> T { value }
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            vec_push(&mut values, identity(10));
            vec_push(&mut values, identity(20));
            print(vec_len(&values), vec_get(&values, 1), identity(true));

            let literal = "owned";
            let owned = string_clone(&literal);
            let mut strings: Vec<string> = vec_new::<string>();
            vec_push(&mut strings, owned);
            print(vec_len(&strings));
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-generic-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("generic native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("generic executable failed");
    }
    if (run.stdout_text != "2 20 true\n1\n") {
        std::cerr << "output was:\n" << run.stdout_text;
        fail("generic executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "generic native tests passed\n";
    return 0;
}
