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

bool valid(std::string_view source, bool check_flow = true) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<reactor-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) return false;
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) return false;
    if (!check_flow) return true;
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    const auto mir = nus::mir::MirBuilder{}.lower(hir);
    return !nus::mir::FlowChecker{}.check(mir).hasErrors();
}
}

int main() {
    using nus::sema::Type;
    using nus::sema::TypeKind;
    if (Type::simple(TypeKind::Reactor).isCopy()) fail("Reactor must own its registration storage");
    if (!Type::simple(TypeKind::Reactor).carriesBorrow()) fail("Reactor must carry resource-pin provenance");

    if (!valid(R"(
        fn demo() -> Result<i32, NetworkError> {
            let local = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&local, 8, true)?;
            let _nonblocking = tcp_listener_set_nonblocking(&mut listener, true)?;
            let mut reactor = reactor_new()?;
            let registered = reactor_register_listener(&mut reactor, &listener, 7usize)?;

            // A resource pin protects handle lifetime but does not block ordinary I/O.
            let _accepted = tcp_accept(&mut listener);
            let _ready = reactor_wait(&mut reactor, 0)?;
            let _token = reactor_event_token(&reactor, 0usize);
            let _readable = reactor_event_readable(&reactor, 0usize);
            let _writable = reactor_event_writable(&reactor, 0usize);
            let _len = reactor_len(&reactor);
            ok::<i32, NetworkError>(registered)
        }
        fn main() {}
    )")) fail("valid non-blocking reactor program was rejected");

    if (valid(R"(
        fn bad() -> Result<i32, NetworkError> {
            let local = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&local, 8, true)?;
            let mut reactor = reactor_new()?;
            let _registered = reactor_register_listener(&mut reactor, &listener, 1usize)?;
            let moved = listener;
            print(reactor_len(&reactor));
            ok::<i32, NetworkError>(0)
        }
        fn main() {}
    )")) fail("moving a resource while it is reactor-pinned must be rejected");

    if (valid(R"(
        fn register(listener: &TcpListener, reactor: &mut Reactor) -> Result<i32, NetworkError> {
            reactor_register_listener(reactor, listener, 1usize)
        }
        fn main() {}
    )")) fail("M17 must reject registrations whose owners cannot be traced for pinning");

    std::cout << "reactor tests passed\n";
    return 0;
}
