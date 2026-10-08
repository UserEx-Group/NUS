#include "nus/hir/HirPrinter.hpp"

#include "nus/lexer/TokenKind.hpp"

#include <sstream>

namespace nus::hir {
namespace {
std::string localName(const Local& local) {
    return '%' + std::to_string(local.id) + ' ' + local.name + ": " + local.type.name();
}
}

std::string HirPrinter::print(const Program& program) const {
    std::string out = "HIR\n";
    for (const auto& structure : program.structs) {
        line(out, 1, "Struct " + structure.name);
        for (const auto& field : structure.fields) line(out, 2, field.name + ": " + field.type.name());
    }
    for (const auto& function : program.functions) printFunction(function, out, 1);
    return out;
}

void HirPrinter::printFunction(const Function& function, std::string& out, int depth) const {
    const std::string qualified = function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
    line(out, depth, "Function " + qualified + " -> " + function.return_type.name());
    if (function.receiver) line(out, depth + 1, "Receiver " + localName(*function.receiver));
    for (const auto& parameter : function.parameters) line(out, depth + 1, "Param " + localName(parameter));
    if (function.body) printExpr(*function.body, out, depth + 1);
}

void HirPrinter::printStmt(const Stmt& statement, std::string& out, int depth) const {
    switch (statement.kind) {
        case StmtKind::Let:
            line(out, depth, "Let " + localName(statement.local) + (statement.local.is_mutable ? " mut" : ""));
            if (statement.value) printExpr(*statement.value, out, depth + 1);
            break;
        case StmtKind::Return:
            line(out, depth, "Return");
            if (statement.value) printExpr(*statement.value, out, depth + 1);
            break;
        case StmtKind::Break:
            line(out, depth, "Break");
            if (statement.value) printExpr(*statement.value, out, depth + 1);
            break;
        case StmtKind::Continue:
            line(out, depth, "Continue");
            break;
        case StmtKind::Expression:
            line(out, depth, statement.has_semicolon ? "ExprStmt" : "ExprStmt(no-semicolon)");
            if (statement.value) printExpr(*statement.value, out, depth + 1);
            break;
    }
}

void HirPrinter::printExpr(const Expr& expression, std::string& out, int depth) const {
    const std::string suffix = " : " + expression.type.name();
    switch (expression.kind) {
        case ExprKind::Literal:
            line(out, depth, "Literal `" + expression.text + "`" + suffix);
            break;
        case ExprKind::Local:
            line(out, depth, "Local %" + std::to_string(expression.local) + suffix);
            break;
        case ExprKind::Global:
            line(out, depth, "Global " + expression.text + suffix);
            break;
        case ExprKind::Unary:
            line(out, depth, "Unary " + std::string(tokenKindName(expression.op)) + (expression.flag ? " mut" : "") + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Binary:
            line(out, depth, "Binary " + std::string(tokenKindName(expression.op)) + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Assignment:
            line(out, depth, "Assign " + std::string(tokenKindName(expression.op)) + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Call:
            line(out, depth, "Call" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Member:
            line(out, depth, "Member ." + expression.text + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Index:
            line(out, depth, "Index" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Array:
            line(out, depth, expression.flag ? "ArrayRepeat" + suffix : "Array" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Range:
            line(out, depth, expression.flag ? "RangeInclusive" + suffix : "Range" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::StructLiteral:
            line(out, depth, "StructLiteral " + expression.text + suffix);
            for (const auto& field : expression.fields) {
                line(out, depth + 1, "Field " + field.name);
                printExpr(*field.value, out, depth + 2);
            }
            break;
        case ExprKind::Block:
            line(out, depth, "Block" + suffix);
            for (const auto& statement : expression.statements) printStmt(*statement, out, depth + 1);
            if (expression.tail) {
                line(out, depth + 1, "Tail");
                printExpr(*expression.tail, out, depth + 2);
            }
            break;
        case ExprKind::If:
            line(out, depth, "If" + suffix);
            for (std::size_t i = 0; i < expression.operands.size(); ++i) {
                line(out, depth + 1, i == 0 ? "Condition" : (i == 1 ? "Then" : "Else"));
                printExpr(*expression.operands[i], out, depth + 2);
            }
            break;
        case ExprKind::While:
            line(out, depth, "While" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::Loop:
            line(out, depth, "Loop" + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
        case ExprKind::For:
            line(out, depth, "For %" + std::to_string(expression.binding) + " " + expression.text + suffix);
            for (const auto& child : expression.operands) printExpr(*child, out, depth + 1);
            break;
    }
}

void HirPrinter::line(std::string& out, int depth, const std::string& text) {
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
    out += text;
    out += '\n';
}

} // namespace nus::hir
