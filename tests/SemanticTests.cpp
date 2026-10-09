#include "nus/lexer/Lexer.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

struct Checked {
    nus::SourceManager sources;
    nus::ast::SourceFile file;
    std::vector<nus::Diagnostic> parser_diagnostics;
    std::vector<nus::Diagnostic> semantic_diagnostics;
};

Checked check(std::string_view source) {
    Checked checked;
    const auto file_id = checked.sources.addSource("<test>", std::string(source));
    nus::Lexer lexer(checked.sources, file_id);
    nus::Parser parser(checked.sources, lexer.tokenize());
    checked.file = parser.parseSourceFile();
    checked.parser_diagnostics = parser.diagnostics();

    if (checked.parser_diagnostics.empty()) {
        nus::sema::SemanticAnalyzer analyzer(true);
        analyzer.analyze(checked.file);
        checked.semantic_diagnostics = analyzer.diagnostics();
    }
    return checked;
}

void printDiagnostics(const Checked& checked) {
    for (const auto& diagnostic : checked.parser_diagnostics) {
        const auto location = checked.sources.location(diagnostic.span.file, diagnostic.span.start);
        std::cerr << "parser " << location.line << ':' << location.column << ": " << diagnostic.message << '\n';
    }
    for (const auto& diagnostic : checked.semantic_diagnostics) {
        const auto location = checked.sources.location(diagnostic.span.file, diagnostic.span.start);
        std::cerr << "semantic " << location.line << ':' << location.column << ": " << diagnostic.message << '\n';
    }
}

void expectValid(const Checked& checked) {
    if (!checked.parser_diagnostics.empty() || !checked.semantic_diagnostics.empty()) {
        printDiagnostics(checked);
        fail("expected semantically valid program");
    }
}

void expectSemanticErrorContaining(const Checked& checked, std::string_view text) {
    if (!checked.parser_diagnostics.empty()) {
        printDiagnostics(checked);
        fail("unexpected parser error");
    }
    for (const auto& diagnostic : checked.semantic_diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) return;
    }
    printDiagnostics(checked);
    fail("expected semantic diagnostic was not produced");
}

void testValidProgram() {
    expectValid(check(R"(
        fn add(a: i32, b: i32) -> i32 {
            a + b
        }

        fn main() {
            let mut result: i32 = add(10, 20);
            result += 1;

            if result > 20 {
                print(result);
            }

            let buffer = [0; 4];
            print(buffer[0]);

            for index in 0..3 {
                print(index);
            }
        }
    )"));
}

void testForwardFunctionResolution() {
    expectValid(check(R"(
        fn main() {
            print(add(1, 2));
        }

        fn add(a: i32, b: i32) -> i32 {
            return a + b;
        }
    )"));
}

void testUndefinedName() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            print(missing);
        }
    )"), "undefined name `missing`");
}

void testDuplicateDeclaration() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let value = 1;
            let value = 2;
        }
    )"), "duplicate declaration");
}

void testImmutableAssignment() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let value = 1;
            value = 2;
        }
    )"), "immutable");
}

void testMutableAssignment() {
    expectValid(check(R"(
        fn main() {
            let mut value = 1;
            value = 2;
            value += 3;
        }
    )"));
}

void testTypedLiteralCoercion() {
    expectValid(check(R"(
        fn port() -> u16 {
            let value: u16 = 8080;
            return value;
        }
    )"));
}

void testTypedLetMismatch() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let enabled: bool = 10;
        }
    )"), "cannot initialize");
}

void testFunctionArity() {
    expectSemanticErrorContaining(check(R"(
        fn add(a: i32, b: i32) -> i32 { a + b }
        fn main() {
            add(1);
        }
    )"), "expects 2 argument");
}

void testFunctionArgumentType() {
    expectSemanticErrorContaining(check(R"(
        fn invert(value: bool) -> bool { !value }
        fn main() {
            invert(10);
        }
    )"), "argument 1 expects `bool`");
}

void testReturnTypeMismatch() {
    expectSemanticErrorContaining(check(R"(
        fn enabled() -> bool {
            return 1;
        }
    )"), "return type mismatch");
}

void testConditionType() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            if 10 {
                print(10);
            }
        }
    )"), "if condition must be `bool`");
}

void testLexicalScope() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            if true {
                let inside = 10;
                print(inside);
            }
            print(inside);
        }
    )"), "undefined name `inside`");
}

void testBreakOutsideLoop() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            break;
        }
    )"), "inside a loop");
}

void testContinueOutsideLoop() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            continue;
        }
    )"), "inside a loop");
}

void testLoopContext() {
    expectValid(check(R"(
        fn main() {
            let mut i = 0;
            while i < 3 {
                i += 1;
                if i == 2 {
                    continue;
                }
            }
            loop {
                break;
            }
        }
    )"));
}

void testArrayElementMismatch() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let values = [1, true, 3];
        }
    )"), "array element has type");
}

void testIndexType() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let values = [1, 2, 3];
            print(values[true]);
        }
    )"), "index must be an integer or range");
}

void testImmutableIndexedAssignment() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let values = [1, 2, 3];
            values[0] = 4;
        }
    )"), "immutable");
}

void testMutableIndexedAssignment() {
    expectValid(check(R"(
        fn main() {
            let mut values = [1, 2, 3];
            values[0] = 4;
        }
    )"));
}


void testNumericLiteralOnEitherSide() {
    expectValid(check(R"(
        fn main() {
            let value: u16 = 10;
            let left = value + 1;
            let right = 1 + value;
            print(left, right);
        }
    )"));
}

void testInvalidNumericSuffix() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let value = 10watts;
        }
    )"), "invalid integer literal suffix");
}

void testUnknownType() {
    expectSemanticErrorContaining(check(R"(
        fn use_it(value: Router) {
            print(value);
        }
    )"), "unknown type `Router`");
}

void testDuplicateFunction() {
    expectSemanticErrorContaining(check(R"(
        fn ping() {}
        fn ping() {}
    )"), "duplicate function `ping`");
}


void testStructFieldsAndMethods() {
    expectValid(check(R"(
        struct Packet {
            source: u32,
            size: usize,
        }

        impl Packet {
            fn get_source(&self) -> u32 {
                self.source
            }

            fn set_source(&mut self, value: u32) {
                self.source = value;
            }
        }

        fn read(packet: Packet) -> u32 {
            packet.get_source()
        }

        fn main() {
            let mut packet = Packet {
                source: 10,
                size: 64,
            };
            packet.set_source(20);
            print(packet.source, packet.size, read(packet));
        }
    )"));
}

void testStructLiteralMissingField() {
    expectSemanticErrorContaining(check(R"(
        struct Point { x: i32, y: i32 }
        fn main() {
            let point = Point { x: 1 };
            print(point.x);
        }
    )"), "missing initializer for field `y`");
}

void testStructLiteralUnknownField() {
    expectSemanticErrorContaining(check(R"(
        struct Point { x: i32 }
        fn main() {
            let point = Point { x: 1, z: 2 };
            print(point.x);
        }
    )"), "has no field `z`");
}

void testStructLiteralFieldTypeMismatch() {
    expectSemanticErrorContaining(check(R"(
        struct Point { x: i32 }
        fn main() {
            let point = Point { x: true };
            print(point.x);
        }
    )"), "field `x` expects `i32`");
}

void testUnknownMember() {
    expectSemanticErrorContaining(check(R"(
        struct Point { x: i32 }
        fn main() {
            let point = Point { x: 1 };
            print(point.y);
        }
    )"), "has no member `y`");
}

void testMutableReceiverRequiresMutableValue() {
    expectSemanticErrorContaining(check(R"(
        struct Counter { value: i32 }
        impl Counter {
            fn increment(&mut self) {
                self.value += 1;
            }
        }
        fn main() {
            let counter = Counter { value: 0 };
            counter.increment();
        }
    )"), "requires a mutable receiver");
}

void testMutableFieldAssignment() {
    expectValid(check(R"(
        struct Counter { value: i32 }
        fn main() {
            let mut counter = Counter { value: 0 };
            counter.value = 10;
            counter.value += 1;
        }
    )"));
}

void testImmutableFieldAssignment() {
    expectSemanticErrorContaining(check(R"(
        struct Counter { value: i32 }
        fn main() {
            let counter = Counter { value: 0 };
            counter.value = 10;
        }
    )"), "immutable");
}

void testDuplicateStructField() {
    expectSemanticErrorContaining(check(R"(
        struct Broken { value: i32, value: bool }
        fn main() {}
    )"), "duplicate field `value`");
}

void testUnknownImplTarget() {
    expectSemanticErrorContaining(check(R"(
        impl Missing {
            fn ping(&self) {}
        }
        fn main() {}
    )"), "cannot implement unknown struct `Missing`");
}

void testCopyValuesRemainUsable() {
    expectValid(check(R"(
        fn main() {
            let a = 10;
            let b = a;
            print(a, b);
        }
    )"));
}

void testStructMoveUseAfterMove() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let moved = packet;
            print(packet.value, moved.value);
        }
    )"), "use of moved value `packet`");
}

void testFunctionArgumentMovesOwnedValue() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn consume(packet: Packet) {}
        fn main() {
            let packet = Packet { value: 1 };
            consume(packet);
            print(packet.value);
        }
    )"), "use of moved value `packet`");
}

void testSharedBorrowsCanCoexist() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn inspect(packet: &Packet) {
            print(packet.value);
        }
        fn main() {
            let packet = Packet { value: 1 };
            let first = &packet;
            let second = &packet;
            inspect(first);
            inspect(second);
            print(packet.value);
        }
    )"));
}

void testMutableBorrowConflictsWithSharedBorrow() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let shared = &packet;
            let exclusive = &mut packet;
            print(shared.value, exclusive.value);
        }
    )"), "already borrowed");
}

void testSecondMutableBorrowRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let first = &mut packet;
            let second = &mut packet;
            print(first.value, second.value);
        }
    )"), "already borrowed");
}

void testUseWhileMutablyBorrowedRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let reference = &mut packet;
            print(packet.value);
            print(reference.value);
        }
    )"), "while it is mutably borrowed");
}

void testMutableBorrowRequiresMutableOwner() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let reference = &mut packet;
            print(reference.value);
        }
    )"), "mutably borrow an immutable value");
}

void testBorrowEndsWithLexicalScope() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            if true {
                let reference = &packet;
                print(reference.value);
            }
            packet.value = 2;
            print(packet.value);
        }
    )"));
}

void testTemporaryBorrowEndsAfterCall() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn inspect(packet: &Packet) { print(packet.value); }
        fn mutate(packet: &mut Packet) { packet.value = 2; }
        fn main() {
            let mut packet = Packet { value: 1 };
            inspect(&packet);
            mutate(&mut packet);
            print(packet.value);
        }
    )"));
}

void testAssignmentWhileBorrowedRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let reference = &packet;
            packet.value = 2;
            print(reference.value);
        }
    )"), "while it is borrowed");
}

void testMoveWhileBorrowedRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let reference = &packet;
            let moved = packet;
            print(reference.value, moved.value);
        }
    )"), "cannot move `packet` while it is borrowed");
}

void testConsumingMethodMovesReceiver() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        impl Packet {
            fn consume(self) {}
        }
        fn main() {
            let packet = Packet { value: 1 };
            packet.consume();
            print(packet.value);
        }
    )"), "use of moved value `packet`");
}

void testConsumingMethodThroughReferenceRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        impl Packet {
            fn consume(self) {}
        }
        fn main() {
            let packet = Packet { value: 1 };
            let reference = &packet;
            reference.consume();
        }
    )"), "consuming method");
}

void testMutableReferenceCanMutatePointee() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn mutate(packet: &mut Packet) {
            packet.value = 3;
        }
        fn main() {
            let mut packet = Packet { value: 1 };
            mutate(&mut packet);
            print(packet.value);
        }
    )"));
}

void testSharedReferenceCannotMutatePointee() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn mutate(packet: &Packet) {
            packet.value = 3;
        }
        fn main() {
            let packet = Packet { value: 1 };
            mutate(&packet);
        }
    )"), "immutable");
}

void testReferenceReturnRejectedForNow() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn identity(packet: &Packet) -> &Packet {
            packet
        }
    )"), "returning references is not supported");
}

void testReferenceFieldRejectedForNow() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        struct View { packet: &Packet }
        fn main() {}
    )"), "reference fields require lifetime inference");
}

void testNonCopyArrayRepeatRejected() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let packets = [packet; 4];
            print(packets[0].value);
        }
    )"), "array repetition requires a Copy value");
}

void testStringMove() {
    expectSemanticErrorContaining(check(R"(
        fn main() {
            let source = "hello";
            let destination = source;
            print(source, destination);
        }
    )"), "use of moved value `source`");
}

void testReinitializeAfterMove() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let moved = packet;
            packet = Packet { value: 2 };
            print(packet.value, moved.value);
        }
    )"));
}

void testMutableReferenceMovePreventsAliasing() {
    expectSemanticErrorContaining(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let first = &mut packet;
            let second = first;
            print(first.value, second.value);
        }
    )"), "use of moved value `first`");
}

} // namespace

int main() {
    testValidProgram();
    testForwardFunctionResolution();
    testUndefinedName();
    testDuplicateDeclaration();
    testImmutableAssignment();
    testMutableAssignment();
    testTypedLiteralCoercion();
    testTypedLetMismatch();
    testFunctionArity();
    testFunctionArgumentType();
    testReturnTypeMismatch();
    testConditionType();
    testLexicalScope();
    testBreakOutsideLoop();
    testContinueOutsideLoop();
    testLoopContext();
    testArrayElementMismatch();
    testIndexType();
    testImmutableIndexedAssignment();
    testMutableIndexedAssignment();
    testNumericLiteralOnEitherSide();
    testInvalidNumericSuffix();
    testUnknownType();
    testDuplicateFunction();
    testStructFieldsAndMethods();
    testStructLiteralMissingField();
    testStructLiteralUnknownField();
    testStructLiteralFieldTypeMismatch();
    testUnknownMember();
    testMutableReceiverRequiresMutableValue();
    testMutableFieldAssignment();
    testImmutableFieldAssignment();
    testDuplicateStructField();
    testUnknownImplTarget();
    testCopyValuesRemainUsable();
    testStructMoveUseAfterMove();
    testFunctionArgumentMovesOwnedValue();
    testSharedBorrowsCanCoexist();
    testMutableBorrowConflictsWithSharedBorrow();
    testSecondMutableBorrowRejected();
    testUseWhileMutablyBorrowedRejected();
    testMutableBorrowRequiresMutableOwner();
    testBorrowEndsWithLexicalScope();
    testTemporaryBorrowEndsAfterCall();
    testAssignmentWhileBorrowedRejected();
    testMoveWhileBorrowedRejected();
    testConsumingMethodMovesReceiver();
    testConsumingMethodThroughReferenceRejected();
    testMutableReferenceCanMutatePointee();
    testSharedReferenceCannotMutatePointee();
    testReferenceReturnRejectedForNow();
    testReferenceFieldRejectedForNow();
    testNonCopyArrayRepeatRejected();
    testStringMove();
    testReinitializeAfterMove();
    testMutableReferenceMovePreventsAliasing();
    std::cout << "semantic tests passed\n";
    return 0;
}
