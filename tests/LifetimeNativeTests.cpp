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
    const auto id = sources.addSource("<lifetime-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected lifetime native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) { for (const auto& d : semantic.diagnostics()) std::cerr << d.message << '\n'; fail("semantic analyzer rejected lifetime native test"); }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) { for (const auto& d : flow.diagnostics) std::cerr << d.message << '\n'; fail("flow checker rejected lifetime native test"); }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "lifetime native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "lifetime native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        struct Packet { value: i32 }
        impl Packet { fn as_ref(&self) -> &Packet { self } }
        fn identity(value: &i32) -> &i32 { value }
        fn main() {
            let mut number = 41;
            let reference = identity(&number);
            print(*reference);
            number = 42;
            print(number);
            let packet = Packet { value: 99 };
            let packet_ref = packet.as_ref();
            print(packet_ref.value);
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-lifetime-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("lifetime native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) fail("lifetime native executable failed");
    if (run.stdout_text != "41\n42\n99\n") { std::cerr << "output was:\n" << run.stdout_text; fail("lifetime native output mismatch"); }
    std::error_code ec; std::filesystem::remove(executable, ec);
    std::cout << "lifetime native tests passed\n";
    return 0;
}
