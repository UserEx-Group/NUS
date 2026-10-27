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
    const auto id = sources.addSource("<network-service-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected network service native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected network service native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    nus::mir::MirBuilder builder;
    auto mir = builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected network service native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "network service native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "network service native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        fn accept_one(listener: &mut TcpListener) -> Result<TcpStream, NetworkError> {
            tcp_accept(listener)
        }

        fn configure(stream: &mut TcpStream) -> Result<i32, NetworkError> {
            let _a = tcp_set_nodelay(stream, true)?;
            let _b = tcp_set_timeout(stream, 250u32)?;
            ok::<i32, NetworkError>(0)
        }

        fn main() {
            let host = "localhost";
            let resolved = dns_resolve_ipv4(&host);
            match resolved {
                Ok(_) => print(1),
                Err(error) => print(network_error_code(&error), network_error_reason(&error)),
            };

            let local = socket_addr(ipv4(127, 0, 0, 1), 0);
            let listener = tcp_listen(&local, 8, true);
            match listener {
                Ok(_) => print(2),
                Err(error) => print(network_error_code(&error), network_error_reason(&error)),
            };
        }
    )");

    if (ir.find("@gethostbyname") == std::string::npos ||
        ir.find("@listen") == std::string::npos ||
        ir.find("@accept") == std::string::npos ||
        ir.find("@setsockopt") == std::string::npos ||
        ir.find("%nus.tcplistener") == std::string::npos) {
        fail("milestone 16 native IR is missing networking service lowering");
    }

    const auto base = std::filesystem::temp_directory_path() / "nus-network-service-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("network service native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("network service native executable failed");
    }
    if (run.stdout_text != "1\n2\n") {
        std::cerr << "network service output was:\n" << run.stdout_text;
        fail("network service native executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "network service native tests passed\n";
    return 0;
}
