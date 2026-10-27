#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/MirBuilder.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/sema/Type.hpp"
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
    const auto id = sources.addSource("<network-service-test>", std::string(source));
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

    if (Type::simple(TypeKind::TcpListener).isCopy()) fail("TcpListener must own its socket handle");
    if (!Type::simple(TypeKind::NetworkError).isCopy()) fail("NetworkError must remain Copy");

    if (!semanticallyValid(R"(
        fn resolve(host: &string) -> Result<IpAddr, NetworkError> {
            dns_resolve_ipv4(host)
        }

        fn listen(address: &SocketAddr) -> Result<TcpListener, NetworkError> {
            tcp_listen(address, 32, true)
        }

        fn accept_one(listener: &mut TcpListener) -> Result<TcpStream, NetworkError> {
            tcp_accept(listener)
        }

        fn configure(stream: &mut TcpStream) -> Result<i32, NetworkError> {
            let _nodelay = tcp_set_nodelay(stream, true)?;
            let _timeout = tcp_set_timeout(stream, 500u32)?;
            ok::<i32, NetworkError>(0)
        }

        fn classify(error: &NetworkError) -> i32 {
            network_error_code(error) + network_error_reason(error)
        }

        fn main() {}
    )")) fail("valid milestone 16 network service API program was rejected");

    if (semanticallyValid(R"(
        fn bad(listener: &TcpListener) -> Result<TcpStream, NetworkError> {
            tcp_accept(listener)
        }
        fn main() {}
    )", false)) fail("tcp_accept must require a mutable listener borrow");

    std::cout << "network service tests passed\n";
    return 0;
}
