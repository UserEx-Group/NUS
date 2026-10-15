#include "nus/codegen/LlvmEmitter.hpp"
#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/DropElaborator.hpp"
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

std::string compileIr(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<codegen-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected codegen test");

    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected codegen test");

    nus::hir::HirBuilder hir_builder(semantic);
    const auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected codegen test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return nus::codegen::LlvmEmitter{}.emit(mir);
}

void expectContains(const std::string& text, std::string_view needle) {
    if (text.find(needle) == std::string::npos) {
        std::cerr << text << '\n';
        fail(std::string("expected LLVM IR to contain: ") + std::string(needle));
    }
}

void testStructMethodsAndReferences() {
    const auto ir = compileIr(R"(
        struct Packet { source: u32, destination: u32 }
        impl Packet {
            fn route_key(&self) -> u32 { self.source + self.destination }
            fn set_source(&mut self, value: u32) { self.source = value; }
        }
        fn main() {
            let mut packet = Packet { source: 10, destination: 20 };
            print(packet.route_key());
            packet.set_source(42);
            print(packet.source);
        }
    )");
    expectContains(ir, "%nus.struct.Packet = type { i32, i32 }");
    expectContains(ir, "define i32 @nus_Packet__route_key(ptr %arg0)");
    expectContains(ir, "define void @nus_Packet__set_source(ptr %arg0, i32 %arg1)");
    expectContains(ir, "getelementptr inbounds %nus.struct.Packet");
    expectContains(ir, "define i32 @main()");
}

void testCfgAndCalls() {
    const auto ir = compileIr(R"(
        fn add(a: i32, b: i32) -> i32 { a + b }
        fn main() {
            let mut total = 0;
            let mut i = 0;
            while i < 3 {
                total += i;
                i += 1;
            }
            if total == 3 { print(add(total, 39)); } else { print(0); }
        }
    )");
    expectContains(ir, "br i1");
    expectContains(ir, "call i32 @nus_add");
    expectContains(ir, "@printf");
}

void testContextualLiteralCoercion() {
    const auto ir = compileIr(R"(
        struct Packet { payload_size: usize }
        fn main() {
            let packet = Packet { payload_size: 1500 };
            print(packet.payload_size);
        }
    )");
    if (sizeof(void*) == 8U) expectContains(ir, "zext i32 1500 to i64");
}

void testStringPrinting() {
    const auto ir = compileIr(R"(
        fn main() { print("NUS", true, 9); }
    )");
    expectContains(ir, "%nus.string = type");
    expectContains(ir, "@.nus.str.0");
    expectContains(ir, "@.nus.true");
}

} // namespace

int main() {
    testStructMethodsAndReferences();
    testCfgAndCalls();
    testContextualLiteralCoercion();
    testStringPrinting();
    std::cout << "codegen tests passed\n";
    return 0;
}
