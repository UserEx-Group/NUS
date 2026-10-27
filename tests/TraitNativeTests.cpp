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
    const auto id = sources.addSource("<trait-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected trait native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected trait native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected trait native test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "trait native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "trait native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        trait Readable { type Item; fn read(&self) -> Self::Item; }
        struct Box { value: i32 }
        impl Readable for Box {
            type Item = i32;
            fn read(&self) -> i32 { self.value }
        }

        trait Counted { type Item; fn count(&self) -> usize; }
        impl<T> Counted for Vec<T> {
            type Item = T;
            fn count(&self) -> usize { self.len() }
        }
        fn require_counted<T: Counted>(value: T) -> i32 { 1 }

        fn main() {
            let box = Box { value: 42 };
            print(box.read());
            let mut values: Vec<i32> = vec_new::<i32>();
            values.push(10);
            values.push(20);
            print(values.count());
            print(require_counted(values));
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-trait-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("trait native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) fail("trait native executable failed");
    if (run.stdout_text != "42\n2\n1\n") { std::cerr << "output was:\n" << run.stdout_text; fail("trait native output mismatch"); }
    std::error_code ec; std::filesystem::remove(executable, ec);
    std::cout << "trait native tests passed\n";
    return 0;
}
