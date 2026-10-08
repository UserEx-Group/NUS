#include "nus/ast/AstPrinter.hpp"
#include "nus/lexer/TokenKind.hpp"

namespace nus::ast {

void AstPrinter::line(std::string& out, int depth, std::string_view text) {
    out.append(static_cast<std::size_t>(depth * 2), ' ');
    out.append(text);
    out.push_back('\n');
}

std::string AstPrinter::print(const SourceFile& file) const {
    std::string out;
    line(out, 0, "SourceFile");
    for (const auto& structure : file.structs) printStruct(structure, out, 1);
    for (const auto& implementation : file.impls) printImpl(implementation, out, 1);
    for (const auto& function : file.functions) printFunction(function, out, 1);
    return out;
}

void AstPrinter::printStruct(const StructDecl& declaration, std::string& out, int depth) const {
    line(out, depth, "StructDecl " + declaration.name);
    for (const auto& field : declaration.fields) {
        line(out, depth + 1, "Field " + field.name + ": " + field.type.name());
    }
}

void AstPrinter::printImpl(const ImplDecl& declaration, std::string& out, int depth) const {
    line(out, depth, "ImplDecl " + declaration.target.name());
    for (const auto& method : declaration.methods) printFunction(method, out, depth + 1);
}

void AstPrinter::printFunction(const FunctionDecl& function, std::string& out, int depth) const {
    line(out, depth, "FunctionDecl " + function.name);
    switch (function.receiver) {
        case ReceiverKind::Value: line(out, depth + 1, "Receiver self"); break;
        case ReceiverKind::Reference: line(out, depth + 1, "Receiver &self"); break;
        case ReceiverKind::MutableReference: line(out, depth + 1, "Receiver &mut self"); break;
        case ReceiverKind::None: break;
    }
    for (const auto& parameter : function.parameters) {
        line(out, depth + 1, "Parameter " + parameter.name + ": " + parameter.type.name());
    }
    if (function.return_type) line(out, depth + 1, "ReturnType " + function.return_type->name());
    printBlock(*function.body, out, depth + 1);
}

void AstPrinter::printBlock(const BlockExpr& block, std::string& out, int depth) const {
    line(out, depth, "Block");
    for (const auto& statement : block.statements) printStatement(*statement, out, depth + 1);
    if (block.tail_expression) {
        line(out, depth + 1, "TailExpression");
        printExpression(*block.tail_expression, out, depth + 2);
    }
}

void AstPrinter::printStatement(const Stmt& statement, std::string& out, int depth) const {
    switch (statement.kind) {
        case StmtKind::Let: {
            const auto& let = static_cast<const LetStmt&>(statement);
            std::string title = let.is_mutable ? "Let mut " : "Let ";
            title += let.name;
            if (let.type) title += ": " + let.type->name();
            line(out, depth, title);
            printExpression(*let.initializer, out, depth + 1);
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
            printExpression(*expr.expression, out, depth + 1);
            break;
        }
    }
}

void AstPrinter::printExpression(const Expr& expression, std::string& out, int depth) const {
    switch (expression.kind) {
        case ExprKind::Literal: {
            const auto& value = static_cast<const LiteralExpr&>(expression);
            line(out, depth, "Literal " + value.text);
            break;
        }
        case ExprKind::Identifier: {
            const auto& value = static_cast<const IdentifierExpr&>(expression);
            line(out, depth, "Identifier " + value.name);
            break;
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expression);
            if (unary.op == TokenKind::Ampersand) {
                line(out, depth, unary.mutable_borrow ? "Borrow &mut" : "Borrow &");
            } else {
                line(out, depth, "Unary " + std::string(tokenKindName(unary.op)));
            }
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
            if (array.isRepeated()) {
                line(out, depth, "ArrayRepeat");
                line(out, depth + 1, "Value");
                printExpression(*array.repeat_value, out, depth + 2);
                line(out, depth + 1, "Count");
                printExpression(*array.repeat_count, out, depth + 2);
            } else {
                line(out, depth, "Array");
                for (const auto& element : array.elements) printExpression(*element, out, depth + 1);
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
        case ExprKind::StructLiteral: {
            const auto& literal = static_cast<const StructLiteralExpr&>(expression);
            line(out, depth, "StructLiteral " + literal.type.name());
            for (const auto& field : literal.fields) {
                line(out, depth + 1, "Field " + field.name);
                printExpression(*field.value, out, depth + 2);
            }
            break;
        }
        case ExprKind::Block:
            printBlock(static_cast<const BlockExpr&>(expression), out, depth);
            break;
        case ExprKind::If: {
            const auto& value = static_cast<const IfExpr&>(expression);
            line(out, depth, "If");
            line(out, depth + 1, "Condition");
            printExpression(*value.condition, out, depth + 2);
            line(out, depth + 1, "Then");
            printBlock(*value.then_branch, out, depth + 2);
            if (value.else_branch) {
                line(out, depth + 1, "Else");
                printExpression(*value.else_branch, out, depth + 2);
            }
            break;
        }
        case ExprKind::While: {
            const auto& value = static_cast<const WhileExpr&>(expression);
            line(out, depth, "While");
            line(out, depth + 1, "Condition");
            printExpression(*value.condition, out, depth + 2);
            line(out, depth + 1, "Body");
            printBlock(*value.body, out, depth + 2);
            break;
        }
        case ExprKind::Loop: {
            const auto& value = static_cast<const LoopExpr&>(expression);
            line(out, depth, "Loop");
            printBlock(*value.body, out, depth + 1);
            break;
        }
        case ExprKind::For: {
            const auto& value = static_cast<const ForExpr&>(expression);
            line(out, depth, "For " + value.binding);
            line(out, depth + 1, "Iterable");
            printExpression(*value.iterable, out, depth + 2);
            line(out, depth + 1, "Body");
            printBlock(*value.body, out, depth + 2);
            break;
        }
    }
}

} // namespace nus::ast
