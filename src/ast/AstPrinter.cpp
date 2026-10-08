#include "nus/ast/AstPrinter.hpp"
#include "nus/lexer/TokenKind.hpp"

#include <string_view>

namespace nus::ast {

void AstPrinter::line(std::string& out, int depth, std::string_view text) {
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
    out.append(text);
    out.push_back('\n');
}

std::string AstPrinter::print(const SourceFile& file) const {
    std::string out;
    line(out, 0, "SourceFile");
    for (const auto& function : file.functions) {
        printFunction(function, out, 1);
    }
    return out;
}

void AstPrinter::printFunction(const FunctionDecl& function, std::string& out, int depth) const {
    line(out, depth, "FunctionDecl " + function.name);
    for (const auto& parameter : function.parameters) {
        line(out, depth + 1, "Parameter " + parameter.name + ": " + parameter.type.name());
    }
    if (function.return_type) {
        line(out, depth + 1, "ReturnType " + function.return_type->name());
    }
    if (function.body) {
        printBlock(*function.body, out, depth + 1);
    }
}

void AstPrinter::printBlock(const BlockExpr& block, std::string& out, int depth) const {
    line(out, depth, "Block");
    for (const auto& statement : block.statements) {
        printStatement(*statement, out, depth + 1);
    }
    if (block.tail_expression) {
        line(out, depth + 1, "TailExpression");
        printExpression(*block.tail_expression, out, depth + 2);
    }
}

void AstPrinter::printStatement(const Stmt& statement, std::string& out, int depth) const {
    switch (statement.kind) {
        case StmtKind::Let: {
            const auto& let = static_cast<const LetStmt&>(statement);
            line(out, depth, std::string("Let ") + (let.is_mutable ? "mut " : "") + let.name);
            if (let.type) line(out, depth + 1, "Type " + let.type->name());
            if (let.initializer) printExpression(*let.initializer, out, depth + 1);
            break;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(statement);
            line(out, depth, "Return");
            if (ret.value) printExpression(*ret.value, out, depth + 1);
            break;
        }
        case StmtKind::Break: {
            const auto& brk = static_cast<const BreakStmt&>(statement);
            line(out, depth, "Break");
            if (brk.value) printExpression(*brk.value, out, depth + 1);
            break;
        }
        case StmtKind::Continue:
            line(out, depth, "Continue");
            break;
        case StmtKind::Expression: {
            const auto& expr = static_cast<const ExprStmt&>(statement);
            line(out, depth, expr.has_semicolon ? "ExpressionStmt" : "ExpressionStmt(no-semicolon)");
            if (expr.expression) printExpression(*expr.expression, out, depth + 1);
            break;
        }
    }
}

void AstPrinter::printExpression(const Expr& expression, std::string& out, int depth) const {
    switch (expression.kind) {
        case ExprKind::Literal: {
            const auto& literal = static_cast<const LiteralExpr&>(expression);
            line(out, depth, "Literal " + literal.text);
            break;
        }
        case ExprKind::Identifier: {
            const auto& identifier = static_cast<const IdentifierExpr&>(expression);
            line(out, depth, "Identifier " + identifier.name);
            break;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expression);
            line(out, depth, "Unary " + std::string(tokenKindName(unary.op)));
            printExpression(*unary.operand, out, depth + 1);
            break;
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expression);
            line(out, depth, "Binary " + std::string(tokenKindName(binary.op)));
            printExpression(*binary.left, out, depth + 1);
            printExpression(*binary.right, out, depth + 1);
            break;
        }
        case ExprKind::Assignment: {
            const auto& assignment = static_cast<const AssignmentExpr&>(expression);
            line(out, depth, "Assignment " + std::string(tokenKindName(assignment.op)));
            line(out, depth + 1, "Target");
            printExpression(*assignment.target, out, depth + 2);
            line(out, depth + 1, "Value");
            printExpression(*assignment.value, out, depth + 2);
            break;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expression);
            line(out, depth, "Call");
            line(out, depth + 1, "Callee");
            printExpression(*call.callee, out, depth + 2);
            for (const auto& argument : call.arguments) {
                line(out, depth + 1, "Argument");
                printExpression(*argument, out, depth + 2);
            }
            break;
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expression);
            line(out, depth, "Member ." + member.member);
            printExpression(*member.object, out, depth + 1);
            break;
        }
        case ExprKind::Index: {
            const auto& index = static_cast<const IndexExpr&>(expression);
            line(out, depth, "Index");
            line(out, depth + 1, "Object");
            printExpression(*index.object, out, depth + 2);
            line(out, depth + 1, "Subscript");
            printExpression(*index.index, out, depth + 2);
            break;
        }
        case ExprKind::Array: {
            const auto& array = static_cast<const ArrayExpr&>(expression);
            line(out, depth, array.isRepeated() ? "ArrayRepeat" : "Array");
            if (array.isRepeated()) {
                line(out, depth + 1, "Value");
                printExpression(*array.repeat_value, out, depth + 2);
                line(out, depth + 1, "Count");
                printExpression(*array.repeat_count, out, depth + 2);
            } else {
                for (const auto& element : array.elements) {
                    line(out, depth + 1, "Element");
                    printExpression(*element, out, depth + 2);
                }
            }
            break;
        }
        case ExprKind::Range: {
            const auto& range = static_cast<const RangeExpr&>(expression);
            line(out, depth, range.inclusive ? "RangeInclusive" : "Range");
            line(out, depth + 1, "Start");
            printExpression(*range.start, out, depth + 2);
            line(out, depth + 1, "End");
            printExpression(*range.end, out, depth + 2);
            break;
        }
        case ExprKind::Block:
            printBlock(static_cast<const BlockExpr&>(expression), out, depth);
            break;
        case ExprKind::If: {
            const auto& if_expr = static_cast<const IfExpr&>(expression);
            line(out, depth, "If");
            line(out, depth + 1, "Condition");
            printExpression(*if_expr.condition, out, depth + 2);
            line(out, depth + 1, "Then");
            printBlock(*if_expr.then_branch, out, depth + 2);
            if (if_expr.else_branch) {
                line(out, depth + 1, "Else");
                printExpression(*if_expr.else_branch, out, depth + 2);
            }
            break;
        }
        case ExprKind::While: {
            const auto& while_expr = static_cast<const WhileExpr&>(expression);
            line(out, depth, "While");
            line(out, depth + 1, "Condition");
            printExpression(*while_expr.condition, out, depth + 2);
            line(out, depth + 1, "Body");
            printBlock(*while_expr.body, out, depth + 2);
            break;
        }
        case ExprKind::Loop: {
            const auto& loop_expr = static_cast<const LoopExpr&>(expression);
            line(out, depth, "Loop");
            printBlock(*loop_expr.body, out, depth + 1);
            break;
        }
        case ExprKind::For: {
            const auto& for_expr = static_cast<const ForExpr&>(expression);
            line(out, depth, "For " + for_expr.binding);
            line(out, depth + 1, "Iterable");
            printExpression(*for_expr.iterable, out, depth + 2);
            line(out, depth + 1, "Body");
            printBlock(*for_expr.body, out, depth + 2);
            break;
        }
    }
}

} // namespace nus::ast
