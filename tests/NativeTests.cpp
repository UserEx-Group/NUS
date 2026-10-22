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
    const auto id = sources.addSource("<native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected native test");
    nus::hir::HirBuilder hir_builder(semantic);
    const auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected native test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}

std::string quote(const std::filesystem::path& value) { return "\"" + value.string() + "\""; }

} // namespace

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "native tests skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "native tests skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        struct Packet {
            source: u32,
            destination: u32,
            payload_size: usize,
        }
        impl Packet {
            fn route_key(&self) -> u32 { self.source + self.destination }
            fn set_source(&mut self, source: u32) { self.source = source; }
            fn consume(self) { print(self.source, self.destination, self.payload_size); }
        }
        fn main() {
            let mut packet = Packet { source: 10, destination: 20, payload_size: 1500 };
            print(packet.route_key());
            packet.set_source(64);
            packet.consume();
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const auto output = std::filesystem::path(base.string() + ".out");
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("native toolchain failed to build LLVM IR");
    }

    const std::string command = quote(executable) + " > " + quote(output);
    if (std::system(command.c_str()) != 0) fail("native NUS executable returned failure");

    std::ifstream stream(output);
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (contents.str() != "30\n64 20 1500\n") {
        std::cerr << "native output was:\n" << contents.str();
        fail("native executable produced unexpected output");
    }

    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::filesystem::remove(output, ec);
    std::cout << "native codegen tests passed\n";
    return 0;
}
