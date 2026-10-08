#include "nus/ast/Ast.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/parser/Parser.hpp"
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

struct Parsed {
    nus::SourceManager sources;
    nus::ast::SourceFile file;
    std::vector<nus::Diagnostic> diagnostics;
};

Parsed parse(std::string_view source) {
    Parsed parsed;
    const auto file_id = parsed.sources.addSource("<test>", std::string(source));
    nus::Lexer lexer(parsed.sources, file_id);
    nus::Parser parser(parsed.sources, lexer.tokenize());
    parsed.file = parser.parseSourceFile();
    parsed.diagnostics = parser.diagnostics();
    return parsed;
}

void expectNoDiagnostics(const Parsed& parsed) {
    if (!parsed.diagnostics.empty()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            const auto location = parsed.sources.location(diagnostic.span.file, diagnostic.span.start);
            std::cerr << location.line << ':' << location.column << ": " << diagnostic.message << '\n';
        }
        fail("unexpected parser diagnostics");
    }
}

void testFunctionsAndTypes() {
    auto parsed = parse(R"(
        fn add(a: i32, b: i32) -> i32 {
            return a + b;
        }
    )");
    expectNoDiagnostics(parsed);

    if (parsed.file.functions.size() != 1) fail("expected one function");
    const auto& function = parsed.file.functions[0];
    if (function.name != "add") fail("wrong function name");
    if (function.parameters.size() != 2) fail("wrong parameter count");
    if (function.parameters[0].name != "a" || function.parameters[0].type.name() != "i32") {
        fail("wrong first parameter");
    }
    if (!function.return_type || function.return_type->name() != "i32") fail("wrong return type");
}

void testPrattPrecedence() {
    auto parsed = parse(R"(
        fn main() {
            let result = 10 + 20 * 3;
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    const auto& let = static_cast<const nus::ast::LetStmt&>(*body.statements[0]);
    const auto& plus = static_cast<const nus::ast::BinaryExpr&>(*let.initializer);
    if (plus.op != nus::TokenKind::Plus) fail("expected + at expression root");
    if (plus.right->kind != nus::ast::ExprKind::Binary) fail("expected binary rhs");
    const auto& multiply = static_cast<const nus::ast::BinaryExpr&>(*plus.right);
    if (multiply.op != nus::TokenKind::Star) fail("expected * to bind tighter than +");
}

void testCallsAndIf() {
    auto parsed = parse(R"(
        fn add(a: i32, b: i32) -> i32 {
            return a + b * 2;
        }

        fn main() {
            let result = add(10, 20);

            if result > 20 {
                print(result);
            }
        }
    )");
    expectNoDiagnostics(parsed);

    if (parsed.file.functions.size() != 2) fail("expected two functions");
    const auto& body = *parsed.file.functions[1].body;
    if (body.statements.size() != 2) fail("expected let and if statements");

    const auto& let = static_cast<const nus::ast::LetStmt&>(*body.statements[0]);
    if (let.initializer->kind != nus::ast::ExprKind::Call) fail("expected call initializer");
    const auto& call = static_cast<const nus::ast::CallExpr&>(*let.initializer);
    if (call.arguments.size() != 2) fail("expected two call arguments");

    const auto& if_statement = static_cast<const nus::ast::ExprStmt&>(*body.statements[1]);
    if (if_statement.expression->kind != nus::ast::ExprKind::If) fail("expected if expression");
}

void testIfAsValue() {
    auto parsed = parse(R"(
        fn sign(x: i32) -> i32 {
            let value = if x >= 0 {
                1
            } else {
                -1
            };
            return value;
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    const auto& let = static_cast<const nus::ast::LetStmt&>(*body.statements[0]);
    if (let.initializer->kind != nus::ast::ExprKind::If) fail("expected if expression initializer");
}

void testMemberMethodAndIndexing() {
    auto parsed = parse(R"(
        fn main() {
            let source = packet.header.source;
            socket.close();
            let first = buffer[0];
            let slice = buffer[0..count];
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    if (body.statements.size() != 4) fail("expected four statements");

    const auto& source = static_cast<const nus::ast::LetStmt&>(*body.statements[0]);
    if (source.initializer->kind != nus::ast::ExprKind::Member) fail("expected member access");
    const auto& outer_member = static_cast<const nus::ast::MemberExpr&>(*source.initializer);
    if (outer_member.member != "source" || outer_member.object->kind != nus::ast::ExprKind::Member) {
        fail("expected chained member access");
    }

    const auto& method_stmt = static_cast<const nus::ast::ExprStmt&>(*body.statements[1]);
    if (method_stmt.expression->kind != nus::ast::ExprKind::Call) fail("expected method call");
    const auto& method_call = static_cast<const nus::ast::CallExpr&>(*method_stmt.expression);
    if (method_call.callee->kind != nus::ast::ExprKind::Member) fail("method callee should be member expression");

    const auto& slice = static_cast<const nus::ast::LetStmt&>(*body.statements[3]);
    if (slice.initializer->kind != nus::ast::ExprKind::Index) fail("expected slice as index expression");
    const auto& index = static_cast<const nus::ast::IndexExpr&>(*slice.initializer);
    if (index.index->kind != nus::ast::ExprKind::Range) fail("expected range inside slice");
}

void testArrayLiterals() {
    auto parsed = parse(R"(
        fn main() {
            let values = [1, 2, 3, 4];
            let empty = [];
            let buffer = [0; 4096];
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    const auto& values = static_cast<const nus::ast::LetStmt&>(*body.statements[0]);
    const auto& array = static_cast<const nus::ast::ArrayExpr&>(*values.initializer);
    if (array.elements.size() != 4 || array.isRepeated()) fail("wrong array literal shape");

    const auto& empty = static_cast<const nus::ast::LetStmt&>(*body.statements[1]);
    const auto& empty_array = static_cast<const nus::ast::ArrayExpr&>(*empty.initializer);
    if (!empty_array.elements.empty() || empty_array.isRepeated()) fail("expected empty array");

    const auto& buffer = static_cast<const nus::ast::LetStmt&>(*body.statements[2]);
    const auto& repeated = static_cast<const nus::ast::ArrayExpr&>(*buffer.initializer);
    if (!repeated.isRepeated() || !repeated.repeat_count) fail("expected repeated array");
}

void testAssignmentExpressions() {
    auto parsed = parse(R"(
        fn main() {
            let mut count = 0;
            count += 1;
            packet.size = count;
            buffer[0] = 42;
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    for (std::size_t i = 1; i < body.statements.size(); ++i) {
        const auto& statement = static_cast<const nus::ast::ExprStmt&>(*body.statements[i]);
        if (statement.expression->kind != nus::ast::ExprKind::Assignment) {
            fail("expected assignment expression");
        }
    }

    const auto& compound_stmt = static_cast<const nus::ast::ExprStmt&>(*body.statements[1]);
    const auto& compound = static_cast<const nus::ast::AssignmentExpr&>(*compound_stmt.expression);
    if (compound.op != nus::TokenKind::PlusEqual) fail("expected += assignment");
}

void testLoopsAndRanges() {
    auto parsed = parse(R"(
        fn main() {
            let mut i = 0;

            while i < 10 {
                i += 1;
            }

            for index in 0..=10 {
                if index == 5 {
                    continue;
                }
            }

            loop {
                break;
            }
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& body = *parsed.file.functions[0].body;
    if (body.statements.size() != 4) fail("expected let plus three loops");

    const auto& while_stmt = static_cast<const nus::ast::ExprStmt&>(*body.statements[1]);
    if (while_stmt.expression->kind != nus::ast::ExprKind::While) fail("expected while expression");

    const auto& for_stmt = static_cast<const nus::ast::ExprStmt&>(*body.statements[2]);
    const auto& for_expr = static_cast<const nus::ast::ForExpr&>(*for_stmt.expression);
    if (for_expr.binding != "index") fail("wrong for-loop binding");
    if (for_expr.iterable->kind != nus::ast::ExprKind::Range) fail("expected range iterable");
    const auto& range = static_cast<const nus::ast::RangeExpr&>(*for_expr.iterable);
    if (!range.inclusive) fail("expected inclusive range");

    const auto& loop_stmt = static_cast<const nus::ast::ExprStmt&>(*body.statements[3]);
    if (loop_stmt.expression->kind != nus::ast::ExprKind::Loop) fail("expected loop expression");
}

void testInvalidAssignmentTargetDiagnostic() {
    auto parsed = parse(R"(
        fn main() {
            (1 + 2) = 3;
        }
    )");
    if (parsed.diagnostics.empty()) fail("expected diagnostic for invalid assignment target");
}

void testParserDiagnostic() {
    auto parsed = parse(R"(
        fn main() {
            let x = 10
            return x;
        }
    )");
    if (parsed.diagnostics.empty()) fail("expected parser diagnostic for missing semicolon");
}

void testMutableTypedLet() {
    auto parsed = parse(R"(
        fn main() {
            let mut port: u16 = 8080;
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& let = static_cast<const nus::ast::LetStmt&>(*parsed.file.functions[0].body->statements[0]);
    if (!let.is_mutable) fail("expected mutable let");
    if (!let.type || let.type->name() != "u16") fail("expected explicit u16 type");
}

} // namespace

int main() {
    testFunctionsAndTypes();
    testPrattPrecedence();
    testCallsAndIf();
    testIfAsValue();
    testMemberMethodAndIndexing();
    testArrayLiterals();
    testAssignmentExpressions();
    testLoopsAndRanges();
    testInvalidAssignmentTargetDiagnostic();
    testMutableTypedLet();
    testParserDiagnostic();
    std::cout << "all parser tests passed\n";
    return 0;
}
