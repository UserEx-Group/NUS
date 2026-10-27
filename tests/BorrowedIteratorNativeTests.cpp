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
    const auto id = sources.addSource("<borrowed-iterator-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected borrowed iterator native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) { for (const auto& d : semantic.diagnostics()) std::cerr << d.message << '\n'; fail("semantic analyzer rejected borrowed iterator native test"); }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) { for (const auto& d : flow.diagnostics) std::cerr << d.message << '\n'; fail("flow checker rejected borrowed iterator native test"); }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "borrowed iterator native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "borrowed iterator native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        fn main() {
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(1);
            values.push(2);
            let mut sum = 0;
            for value in values.iter() { sum += *value; }
            print(sum, values.len());
            for value in values.iter_mut() { *value += 10; }
            for value in values.iter() { print(*value); }

            let a = "alpha";
            let b = "beta";
            let mut names: Vec<string> = vec_new::<string>();
            names.push(string_clone(&a));
            names.push(string_clone(&b));
            for name in names { print(name); }
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-borrowed-iterator-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("borrowed iterator native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) fail("borrowed iterator native executable failed");
    if (run.stdout_text != "3 2\n11\n12\nalpha\nbeta\n") { std::cerr << "output was:\n" << run.stdout_text; fail("borrowed iterator native output mismatch"); }
    std::error_code ec; std::filesystem::remove(executable, ec);
    std::cout << "borrowed iterator native tests passed\n";
    return 0;
}
