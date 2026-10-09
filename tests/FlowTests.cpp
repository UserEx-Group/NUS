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

struct Checked {
    nus::SourceManager sources;
    nus::mir::FlowResult flow;
    bool frontend_failed{false};
};

Checked check(std::string_view source) {
    Checked result;
    const auto id = result.sources.addSource("<flow-test>", std::string(source));
    nus::Lexer lexer(result.sources, id);
    nus::Parser parser(result.sources, lexer.tokenize());
    auto ast = parser.parseSourceFile();
    if (parser.hasErrors()) {
        result.frontend_failed = true;
        return result;
    }
    nus::sema::SemanticAnalyzer semantic;
    semantic.analyze(ast);
    if (semantic.hasErrors()) {
        result.frontend_failed = true;
        return result;
    }
    nus::hir::HirBuilder hir_builder(semantic);
    auto hir = hir_builder.lower(ast);
    nus::mir::MirBuilder mir_builder;
    auto mir = mir_builder.lower(hir);
    result.flow = nus::mir::FlowChecker{}.check(mir);
    return result;
}

void expectValid(const Checked& checked) {
    if (checked.frontend_failed) fail("frontend unexpectedly rejected flow test");
    if (!checked.flow.diagnostics.empty()) {
        for (const auto& diagnostic : checked.flow.diagnostics) std::cerr << diagnostic.message << '\n';
        fail("expected flow-valid program");
    }
}

void expectError(const Checked& checked, std::string_view message) {
    if (checked.frontend_failed) fail("frontend unexpectedly rejected flow test");
    for (const auto& diagnostic : checked.flow.diagnostics) {
        if (diagnostic.message.find(message) != std::string::npos) return;
    }
    for (const auto& diagnostic : checked.flow.diagnostics) std::cerr << diagnostic.message << '\n';
    fail("expected flow diagnostic was not produced");
}

void testNllEndsSharedBorrowAfterLastUse() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let view = &packet;
            print(view.value);
            packet.value = 2;
            print(packet.value);
        }
    )"));
}

void testLiveSharedBorrowConflictsWithMutableBorrow() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let view = &packet;
            let write = &mut packet;
            print(view.value, write.value);
        }
    )"), "already borrowed");
}

void testUseAfterMoveThroughCfg() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            if true {
                let moved = packet;
                print(moved.value);
            }
            print(packet.value);
        }
    )"), "may have been moved");
}

void testReinitializeAfterMove() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let moved = packet;
            packet = Packet { value: 2 };
            print(moved.value, packet.value);
        }
    )"));
}

void testTemporaryMutableBorrowEndsAfterCall() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn update(packet: &mut Packet) { packet.value = 2; }
        fn main() {
            let mut packet = Packet { value: 1 };
            update(&mut packet);
            print(packet.value);
        }
    )"));
}

} // namespace

int main() {
    testNllEndsSharedBorrowAfterLastUse();
    testLiveSharedBorrowConflictsWithMutableBorrow();
    testUseAfterMoveThroughCfg();
    testReinitializeAfterMove();
    testTemporaryMutableBorrowEndsAfterCall();
    std::cout << "flow tests passed\n";
    return 0;
}
