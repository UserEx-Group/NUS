#include "nus/ast/Ast.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/parser/Parser.hpp"
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
    const auto& if_expr = static_cast<const nus::ast::IfExpr&>(*if_statement.expression);
    if (if_expr.condition->kind != nus::ast::ExprKind::Binary) fail("expected binary if condition");
}

void testElseIf() {
    auto parsed = parse(R"(
        fn classify(x: i32) -> i32 {
            if x > 0 {
                return 1;
            } else if x < 0 {
                return -1;
            } else {
                return 0;
            }
        }
    )");
    expectNoDiagnostics(parsed);

    const auto& function = parsed.file.functions[0];
    if (function.body->statements.size() != 1) fail("expected if statement");
    const auto& statement = static_cast<const nus::ast::ExprStmt&>(*function.body->statements[0]);
    const auto& root = static_cast<const nus::ast::IfExpr&>(*statement.expression);
    if (!root.else_branch || root.else_branch->kind != nus::ast::ExprKind::If) {
        fail("expected else-if chain");
    }
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
    const auto& if_expr = static_cast<const nus::ast::IfExpr&>(*let.initializer);
    if (!if_expr.then_branch->tail_expression) fail("expected then tail expression");
    if (!if_expr.else_branch || if_expr.else_branch->kind != nus::ast::ExprKind::Block) {
        fail("expected else block expression");
    }
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
    testElseIf();
    testIfAsValue();
    testMutableTypedLet();
    testParserDiagnostic();
    std::cout << "all parser tests passed\n";
    return 0;
}
