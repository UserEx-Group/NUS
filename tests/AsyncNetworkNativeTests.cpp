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
[[noreturn]] void fail(std::string_view message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
std::string compileIr(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<async-network-native-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected async network native test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected async network native test");
    const auto hir = nus::hir::HirBuilder(semantic).lower(ast);
    auto mir = nus::mir::MirBuilder{}.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) {
        for (const auto& diagnostic : flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("flow checker rejected async network native test");
    }
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}
}

int main() {
#ifdef _WIN32
    if (nus::codegen::NativeToolchain::mingwRoot().empty()) { std::cout << "async network native test skipped: MinGW-w64 is not available\n"; return 0; }
#endif
    if (!nus::codegen::NativeToolchain::clangAvailable()) { std::cout << "async network native test skipped: clang is not available\n"; return 0; }

    const auto ir = compileIr(R"(
        fn accept_probe(listener: &mut TcpListener) {
            let pending = tcp_accept_async(listener);
        }
        fn receive_probe(stream: &mut TcpStream) {
            let pending = tcp_receive_async(stream, 128usize);
        }
        fn send_probe(stream: &mut TcpStream, buffer: &Vec<u8>) {
            let pending = tcp_send_async(stream, buffer);
        }
        fn native_probe() -> Result<i32, NetworkError> {
            let address = socket_addr(ipv4(127, 0, 0, 1), 0u16);
            let mut listener = tcp_listen(&address, 8, true)?;
            let pending = tcp_accept_async(&mut listener);
            ok::<i32, NetworkError>(1)
        }
        fn main() {
            let result = native_probe();
            let code = match result { Ok(value) => value, Err(_) => -1, };
            print(code);
        }
    )");

    if (ir.find("@nus_async_accept_poll") == std::string::npos ||
        ir.find("@nus_async_recv_poll") == std::string::npos ||
        ir.find("@nus_async_send_poll") == std::string::npos ||
#ifdef _WIN32
        ir.find("@ioctlsocket") == std::string::npos || ir.find("@WSAPoll") == std::string::npos
#else
        ir.find("@fcntl") == std::string::npos || ir.find("@poll") == std::string::npos
#endif
    ) fail("milestone 18 IR is missing async socket polling helpers");

    const auto base = std::filesystem::temp_directory_path() / "nus-async-network-native-test";
#ifdef _WIN32
    const auto executable = std::filesystem::path(base.string() + ".exe");
#else
    const auto executable = base;
#endif
    const nus::codegen::NativeToolchain toolchain;
    const auto build = toolchain.build(ir, executable);
    if (!build.success) { std::cerr << build.message << '\n' << build.command << '\n'; fail("async network native toolchain failed"); }
    const auto run = toolchain.run(executable);
    if (!run.success) { std::cerr << run.message << " (exit " << run.exit_code << ")\n"; fail("async network native executable failed"); }
    if (run.stdout_text != "1\n") { std::cerr << "async network output was:\n" << run.stdout_text; fail("async network native executable produced unexpected output"); }
    std::error_code ec;
    std::filesystem::remove(executable, ec);
    std::cout << "async network native tests passed\n";
    return 0;
}
