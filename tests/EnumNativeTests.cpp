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
    const auto id = sources.addSource("<enum-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected enum native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected enum native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected enum native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "enum native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "enum native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        enum NetworkError { Timeout, Code(i32), }
        enum Message { Ping, Text(string), }

        fn classify(error: NetworkError) -> i32 {
            return match error {
                NetworkError::Timeout => 1,
                NetworkError::Code(code) => code,
            };
        }

        fn maybe(flag: bool) -> Result<i32, string> {
            let literal = "failure";
            return if flag {
                ok::<i32, string>(42)
            } else {
                err::<i32, string>(string_clone(&literal))
            };
        }

        fn forward(flag: bool) -> Result<i32, string> {
            let value = maybe(flag)?;
            ok::<i32, string>(value + 1)
        }

        fn main() {
            print(classify(NetworkError::Timeout));
            print(classify(NetworkError::Code(9)));

            let option = some(5);
            let option_value = match option {
                Some(value) => value,
                None => 0,
            };
            print(option_value);

            let good = forward(true);
            match good {
                Ok(value) => print(value),
                Err(_) => print("unexpected"),
            }

            let bad = forward(false);
            match bad {
                Ok(value) => print(value),
                Err(_) => print("propagated"),
            }

            let literal = "owned";
            let message = Message::Text(string_clone(&literal));
            match message {
                Message::Ping => print("ping"),
                Message::Text(_) => print("text"),
            }
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-enum-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("enum native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("enum executable failed");
    }
    if (run.stdout_text != "1\n9\n5\n43\npropagated\ntext\n") {
        std::cerr << "output was:\n" << run.stdout_text;
        fail("enum executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "enum native tests passed\n";
    return 0;
}
