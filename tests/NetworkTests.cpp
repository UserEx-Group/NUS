#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

bool semanticallyValid(std::string_view source, bool check_flow = true) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<network-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) return false;
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) return false;
    if (!check_flow) return true;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    nus::mir::MirBuilder builder;
    const auto mir = builder.lower(hir);
    return !nus::mir::FlowChecker{}.check(mir).hasErrors();
}
}

int main() {
    using nus::sema::Type;
    using nus::sema::TypeKind;

    if (!Type::simple(TypeKind::IpAddr).isCopy()) fail("IpAddr must be Copy");
    if (!Type::simple(TypeKind::SocketAddr).isCopy()) fail("SocketAddr must be Copy");
    if (!Type::simple(TypeKind::NetworkError).isCopy()) fail("NetworkError must be Copy");
    if (Type::simple(TypeKind::TcpStream).isCopy()) fail("TcpStream must own its socket handle");
    if (Type::simple(TypeKind::UdpSocket).isCopy()) fail("UdpSocket must own its socket handle");

    if (!semanticallyValid(R"(
        fn connect(address: &SocketAddr) -> Result<TcpStream, NetworkError> {
            tcp_connect(address)
        }

        fn transmit(stream: &mut TcpStream, bytes: &Vec<u8>) -> Result<usize, NetworkError> {
            tcp_send(stream, bytes)
        }

        fn receive(stream: &mut TcpStream) -> Result<Vec<u8>, NetworkError> {
            tcp_receive(stream, 4096)
        }

        fn datagram(socket: &mut UdpSocket, remote: &SocketAddr, bytes: &Vec<u8>) -> Result<usize, NetworkError> {
            udp_send_to(socket, remote, bytes)
        }

        fn main() {
            let ip = ipv4(127, 0, 0, 1);
            let address = socket_addr(ip, 8080);
            print(socket_port(&address));
        }
    )")) fail("valid native networking API program was rejected");

    if (semanticallyValid(R"(
        fn bad(stream: &mut TcpStream, text: &string) -> Result<usize, NetworkError> {
            tcp_send(stream, text)
        }
        fn main() {}
    )", false)) fail("tcp_send must reject string payloads; networking uses Vec<u8>");

    std::cout << "network tests passed\n";
    return 0;
}
