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

nus::mir::Program lower(std::string_view source) {
    nus::SourceManager sources;
    const auto id = sources.addSource("<drop-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected drop test");
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) fail("semantic analyzer rejected drop test");
    nus::hir::HirBuilder hir_builder(semantic);
    auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    const auto flow = nus::mir::FlowChecker{}.check(mir);
    if (flow.hasErrors()) fail("flow checker rejected drop test");
    nus::mir::DropElaborator{}.run(mir, flow);
    return mir;
}

void testDropsOwnedLocalOnReturn() {
    auto program = lower(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            print(packet.value);
        }
    )");
    bool saw_drop = false;
    for (const auto& block : program.functions.front().blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind == nus::mir::InstructionKind::Drop && instruction.name == "packet") saw_drop = true;
        }
    }
    if (!saw_drop) fail("owned local should receive an explicit drop");
}

void testMovedLocalIsNotDroppedTwice() {
    auto program = lower(R"(
        struct Packet { value: i32 }
        fn consume(packet: Packet) {}
        fn main() {
            let packet = Packet { value: 1 };
            consume(packet);
        }
    )");
    const nus::mir::Function* main = nullptr;
    for (const auto& function : program.functions) if (function.name == "main") main = &function;
    if (!main) fail("missing main function");
    for (const auto& block : main->blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind == nus::mir::InstructionKind::Drop && instruction.name == "packet") {
                fail("moved local must not be dropped again");
            }
        }
    }
}

void testConditionalDropAfterBranchMove() {
    auto program = lower(R"(
        struct Packet { value: i32 }
        fn consume(packet: Packet) {}
        fn main() {
            let packet = Packet { value: 1 };
            if true { consume(packet); }
        }
    )");
    const nus::mir::Function* main = nullptr;
    for (const auto& function : program.functions) if (function.name == "main") main = &function;
    if (!main) fail("missing main function");
    bool saw_conditional = false;
    for (const auto& block : main->blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind == nus::mir::InstructionKind::DropIf && instruction.name == "packet") {
                saw_conditional = true;
            }
        }
    }
    if (!saw_conditional) fail("path-dependent move should produce a conditional drop");
}

} // namespace

int main() {
    testDropsOwnedLocalOnReturn();
    testMovedLocalIsNotDroppedTwice();
    testConditionalDropAfterBranchMove();
    std::cout << "drop tests passed\n";
    return 0;
}
