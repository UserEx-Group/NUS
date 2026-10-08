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
    const auto file_id = checked.sources.addSource("<ownership-test>", std::string(source));
    nus::Lexer lexer(checked.sources, file_id);
    nus::Parser parser(checked.sources, lexer.tokenize());
    checked.file = parser.parseSourceFile();
    checked.parser_diagnostics = parser.diagnostics();
    if (checked.parser_diagnostics.empty()) {
        nus::sema::SemanticAnalyzer analyzer;
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
        fail("expected valid ownership program");
    }
}

void expectError(const Checked& checked, std::string_view text) {
    if (!checked.parser_diagnostics.empty()) {
        printDiagnostics(checked);
        fail("unexpected parser diagnostic");
    }
    for (const auto& diagnostic : checked.semantic_diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) return;
    }
    printDiagnostics(checked);
    fail("expected ownership diagnostic was not produced");
}


void testMoveAndUseAfterMove() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let moved = packet;
            print(packet.value, moved.value);
        }
    )"), "use of moved value `packet`");
}

void testCopyValue() {
    expectValid(check(R"(
        fn main() {
            let a = 1;
            let b = a;
            print(a, b);
        }
    )"));
}

void testMultipleSharedBorrows() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let a = &packet;
            let b = &packet;
            print(a.value, b.value, packet.value);
        }
    )"));
}

void testSharedThenMutableConflict() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let a = &packet;
            let b = &mut packet;
            print(a.value, b.value);
        }
    )"), "already borrowed");
}

void testMutableThenSharedConflict() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let a = &mut packet;
            let b = &packet;
            print(a.value, b.value);
        }
    )"), "mutably borrowed");
}

void testMutableBorrowRequiresMutability() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let a = &mut packet;
            print(a.value);
        }
    )"), "mutably borrow an immutable value");
}

void testLexicalBorrowRelease() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            if true {
                let view = &packet;
                print(view.value);
            }
            packet.value = 2;
        }
    )"));
}

void testTemporaryCallBorrowRelease() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn read(packet: &Packet) { print(packet.value); }
        fn write(packet: &mut Packet) { packet.value = 2; }
        fn main() {
            let mut packet = Packet { value: 1 };
            read(&packet);
            write(&mut packet);
            read(&packet);
        }
    )"));
}

void testMoveWhileBorrowed() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let packet = Packet { value: 1 };
            let view = &packet;
            let moved = packet;
            print(view.value, moved.value);
        }
    )"), "cannot move `packet` while it is borrowed");
}

void testConsumingReceiver() {
    expectError(check(R"(
        struct Packet { value: i32 }
        impl Packet { fn consume(self) {} }
        fn main() {
            let packet = Packet { value: 1 };
            packet.consume();
            print(packet.value);
        }
    )"), "use of moved value `packet`");
}

void testMutableReferenceIsNotCopy() {
    expectError(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let first = &mut packet;
            let second = first;
            print(first.value, second.value);
        }
    )"), "use of moved value `first`");
}

void testReinitializationAfterMove() {
    expectValid(check(R"(
        struct Packet { value: i32 }
        fn main() {
            let mut packet = Packet { value: 1 };
            let old = packet;
            packet = Packet { value: 2 };
            print(old.value, packet.value);
        }
    )"));
}

} // namespace

int main() {
    testMoveAndUseAfterMove();
    testCopyValue();
    testMultipleSharedBorrows();
    testSharedThenMutableConflict();
    testMutableThenSharedConflict();
    testMutableBorrowRequiresMutability();
    testLexicalBorrowRelease();
    testTemporaryCallBorrowRelease();
    testMoveWhileBorrowed();
    testConsumingReceiver();
    testMutableReferenceIsNotCopy();
    testReinitializationAfterMove();
    std::cout << "ownership tests passed\n";
    return 0;
}
