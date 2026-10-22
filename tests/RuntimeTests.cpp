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
    const auto id = sources.addSource("<runtime-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected runtime test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected runtime test");
    nus::hir::HirBuilder hir_builder(semantic);
    const auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected runtime test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}

std::string quote(const std::filesystem::path& value) { return "\"" + value.string() + "\""; }
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "native tests skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "runtime native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        fn consume(value: string) { print(value); }
        fn main() {
            let literal = "network";
            let owned = string_clone(&literal);
            print(string_len(&owned), owned);
            let moved = owned;
            consume(moved);

            let branch_literal = "branch";
            let branch_owned = string_clone(&branch_literal);
            if false {
                consume(branch_owned);
            }

            let mut values: Vec<i32> = vec_new_i32();
            vec_push_i32(&mut values, 10);
            vec_push_i32(&mut values, 20);
            vec_push_i32(&mut values, 30);
            print(vec_len_i32(&values), vec_get_i32(&values, 1));
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-runtime-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const auto output = std::filesystem::path(base.string() + ".out");
    const auto build = nus::codegen::NativeToolchain{}.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("runtime native toolchain failed");
    }

    if (std::system((quote(executable) + " > " + quote(output)).c_str()) != 0) fail("runtime executable failed");
    std::ifstream stream(output);
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (contents.str() != "7 network\nnetwork\n3 20\n") {
        std::cerr << "runtime output was:\n" << contents.str();
        fail("runtime executable produced unexpected output");
    }

    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::filesystem::remove(output, ec);
    std::cout << "runtime tests passed\n";
    return 0;
}
