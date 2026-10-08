#include "nus/hir/HirBuilder.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/mir/CfgVerifier.hpp"
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
    const auto id = sources.addSource("<mir-test>", std::string(source));
    nus::Lexer lexer(sources, id);
    nus::Parser parser(sources, lexer.tokenize());
    const auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) fail("parser rejected MIR test program");
    nus::sema::SemanticAnalyzer analyzer;
    analyzer.analyze(ast);
    if (analyzer.hasErrors()) fail("semantic analyzer rejected MIR test program");
    nus::hir::HirBuilder hir_builder(analyzer);
    const auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    return mir_builder.lower(hir);
}

void testIfCreatesCfg() {
    auto program = lower(R"(
        fn main() {
            let mut value = 1;
            if value > 0 {
                value = 2;
            } else {
                value = 3;
            }
            print(value);
        }
    )");
    if (program.functions.size() != 1) fail("expected one MIR function");
    const auto& function = program.functions.front();
    if (function.blocks.size() < 4) fail("if expression should create branch CFG blocks");
    bool has_branch = false;
    for (const auto& block : function.blocks) {
        if (block.terminator && block.terminator->kind == nus::mir::TerminatorKind::Branch) has_branch = true;
    }
    if (!has_branch) fail("MIR should contain a branch terminator");
    const nus::mir::CfgVerifier verifier;
    if (!verifier.verify(program).empty()) fail("generated MIR CFG should verify");
}

void testWhileAndForHaveBackEdges() {
    auto program = lower(R"(
        fn main() {
            let mut n = 0;
            while n < 2 { n += 1; }
            for i in 0..3 { print(i); }
        }
    )");
    const auto& function = program.functions.front();
    std::size_t branches = 0;
    std::size_t gotos = 0;
    for (const auto& block : function.blocks) {
        if (!block.terminator) continue;
        if (block.terminator->kind == nus::mir::TerminatorKind::Branch) ++branches;
        if (block.terminator->kind == nus::mir::TerminatorKind::Goto) ++gotos;
    }
    if (branches < 2 || gotos < 2) fail("loops should lower to explicit CFG branches and gotos");
    const nus::mir::CfgVerifier verifier;
    if (!verifier.verify(program).empty()) fail("loop MIR CFG should verify");
}

void testMethodReceiverLowering() {
    auto program = lower(R"(
        struct Packet { value: i32 }
        impl Packet {
            fn update(&mut self, value: i32) { self.value = value; }
        }
        fn main() {
            let mut packet = Packet { value: 1 };
            packet.update(2);
        }
    )");
    const auto& main = program.functions.at(0);
    bool saw_method_call = false;
    bool saw_mut_borrow = false;
    for (const auto& block : main.blocks) {
        for (const auto& inst : block.instructions) {
            if (inst.kind == nus::mir::InstructionKind::Borrow && inst.name == "mut") saw_mut_borrow = true;
            if (inst.kind == nus::mir::InstructionKind::Call && !inst.operands.empty() &&
                inst.operands.front().kind == nus::mir::OperandKind::Global &&
                inst.operands.front().text == "Packet::update") {
                saw_method_call = true;
                if (inst.operands.size() != 3) fail("method call should include callee, receiver and explicit argument");
            }
        }
    }
    if (!saw_method_call) fail("MIR should resolve method call to a qualified function");
    if (!saw_mut_borrow) fail("&mut self call should materialize an implicit mutable borrow");
}
}

int main() {
    testIfCreatesCfg();
    testWhileAndForHaveBackEdges();
    testMethodReceiverLowering();
    std::cout << "mir tests passed\n";
    return 0;
}
