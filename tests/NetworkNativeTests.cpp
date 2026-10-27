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
    const auto id = sources.addSource("<network-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected networking native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        for (const auto& diagnostic : semantic.diagnostics()) std::cerr << diagnostic.message << '\n';
        fail("semantic analyzer rejected networking native test");
    }
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    nus::mir::MirBuilder builder;
    auto mir = builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected networking native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) {
        std::cout << "network native test skipped: MinGW-w64 is not available\n";
        return 0;
    }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) {
        std::cout << "network native test skipped: clang is not available\n";
        return 0;
    }

    const auto ir = compileIr(R"(
        fn connect_once(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
            tcp_connect(address)
        }

        fn connect_forward(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
            let stream = tcp_connect(address)?;
            ok::<TcpStream, NetworkError>(stream)
        }

        fn send_once(stream: &mut TcpStream, data: &Vec<u8>) -> Result<usize, NetworkError> {
            tcp_send(stream, data)
        }

        fn receive_once(stream: &mut TcpStream) -> Result<Vec<u8>, NetworkError> {
            tcp_receive(stream, 64)
        }

        fn send_datagram(socket: &mut UdpSocket, address: &SocketAddr, data: &Vec<u8>) -> Result<usize, NetworkError> {
            udp_send_to(socket, address, data)
        }

        fn receive_datagram(socket: &mut UdpSocket) -> Result<Vec<u8>, NetworkError> {
            udp_receive(socket, 64)
        }

        fn main() {
            let local = socket_addr(ipv4(127, 0, 0, 1), 0);
            print(socket_port(&local));
            let socket = udp_bind(&local);
            match socket {
                Ok(_) => print(1),
                Err(error) => print(network_error_code(&error)),
            }
        }
    )");

    const auto base = std::filesystem::temp_directory_path() / "nus-network-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) {
        std::cerr << build.message << '\n' << build.command << '\n';
        fail("network native toolchain failed");
    }
    const auto run = toolchain.run(executable);
    if (!run.success) {
        std::cerr << run.message << " (exit " << run.exit_code << ")\n";
        fail("network native executable failed");
    }
    if (run.stdout_text != "0\n1\n") {
        std::cerr << "network output was:\n" << run.stdout_text;
        fail("network native executable produced unexpected output");
    }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "network native tests passed\n";
    return 0;
}
