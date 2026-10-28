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
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }

bool valid(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<async-network-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) return false;
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) return false;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    const auto mir = nus::mir::MirBuilder{}.lower(hir);
    return !nus::mir::FlowChecker{}.check(mir).hasErrors();
}
}

int main() {
    if (!valid(R"(
        async fn accept_one(listener: &mut TcpListener) -> Result<i32, NetworkError> {
            let accepted = await tcp_accept_async(listener);
            let stream = accepted?;
            ok::<i32, NetworkError>(1)
        }

        async fn read_once(stream: &mut TcpStream) -> Result<usize, NetworkError> {
            let received = await tcp_receive_async(stream, 1024usize);
            let bytes = received?;
            ok::<usize, NetworkError>(bytes.len())
        }

        fn main() {}
    )")) fail("valid async socket operations were rejected");

    if (valid(R"(
        fn bad() -> Result<i32, NetworkError> {
            let address = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&address, 8, true)?;
            let future = tcp_accept_async(&mut listener);
            let moved = listener;
            let result = block_on(future);
            ok::<i32, NetworkError>(0)
        }
        fn main() {}
    )")) fail("moving a socket while its async future remains live must be rejected");

    if (valid(R"(
        fn bad(stream: &mut TcpStream) {
            let mut buffer: Vec<u8> = vec_new::<u8>();
            buffer.push(1u8);
            let future = tcp_send_async(stream, &buffer);
            buffer.push(2u8);
            let result = block_on(future);
        }
        fn main() {}
    )")) fail("mutating a buffer while tcp_send_async borrows it must be rejected");

    std::cout << "async network tests passed\n";
    return 0;
}
