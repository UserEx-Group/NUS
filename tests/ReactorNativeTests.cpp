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
    const auto id = sources.addSource("<reactor-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected reactor native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected reactor native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected reactor native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "reactor native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "reactor native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        fn reactor_demo() -> Result<i32, NetworkError> {
            let address = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&address, 8, true)?;
            let _nonblocking = tcp_listener_set_nonblocking(&mut listener, true)?;
            let mut reactor = reactor_new()?;
            let _registered = reactor_register_listener(&mut reactor, &listener, 7usize)?;
            let ready = reactor_wait(&mut reactor, 0)?;
            print(reactor_len(&reactor), ready, reactor_event_token(&reactor, 0usize));
            ok::<i32, NetworkError>(0)
        }

        fn main() {
            let result = reactor_demo();
            match result {
                Ok(_) => print("reactor-ok"),
                Err(error) => print(network_error_code(&error)),
            };
        }
    )");

    if (ir.find("%nus.reactor") == std::string::npos ||
#ifdef _WIN32
        ir.find("@WSAPoll") == std::string::npos || ir.find("@ioctlsocket") == std::string::npos
#else
        ir.find("@poll") == std::string::npos || ir.find("@fcntl") == std::string::npos
#endif
    ) {
        fail("milestone 17 native IR is missing reactor/non-blocking lowering");
    }

    const auto base = std::filesystem::temp_directory_path() / "nus-reactor-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("reactor native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("reactor native executable failed");
    }
    if (run.stdout_text != "1 0 7\nreactor-ok\n") {
        std::cerr << "reactor output was:\n" << run.stdout_text;
        fail("reactor native executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "reactor native tests passed\n";
    return 0;
}
