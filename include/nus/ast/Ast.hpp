#pragma once

#include "nus/lexer/TokenKind.hpp"
#include "nus/source/SourceSpan.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nus::ast {

struct Expr;
struct Stmt;
struct BlockExpr;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Node {
    SourceSpan span{};
    virtual ~Node() = default;
};

struct TypeRef : Node {
    std::vector<std::string> path;

    [[nodiscard]] std::string name() const {
        std::string result;
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0) result += "::";
            result += path[i];
        }
        return result;
    }
};

enum class ExprKind {
    Literal,
    Identifier,
    Unary,
    Binary,
    Call,
    Block,
    If,
};

struct Expr : Node {
    explicit Expr(ExprKind kind) : kind(kind) {}
    ExprKind kind;
};

struct LiteralExpr final : Expr {
    LiteralExpr(TokenKind literal_kind, std::string text)
        : Expr(ExprKind::Literal), literal_kind(literal_kind), text(std::move(text)) {}

    TokenKind literal_kind;
    std::string text;
};

struct IdentifierExpr final : Expr {
    explicit IdentifierExpr(std::string name)
        : Expr(ExprKind::Identifier), name(std::move(name)) {}

    std::string name;
};

struct UnaryExpr final : Expr {
    UnaryExpr(TokenKind op, ExprPtr operand)
        : Expr(ExprKind::Unary), op(op), operand(std::move(operand)) {}

    TokenKind op;
    ExprPtr operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(TokenKind op, ExprPtr left, ExprPtr right)
        : Expr(ExprKind::Binary), op(op), left(std::move(left)), right(std::move(right)) {}

    TokenKind op;
    ExprPtr left;
    ExprPtr right;
};

struct CallExpr final : Expr {
    explicit CallExpr(ExprPtr callee)
        : Expr(ExprKind::Call), callee(std::move(callee)) {}

    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};

enum class StmtKind {
    Let,
    Return,
    Expression,
};

struct Stmt : Node {
    explicit Stmt(StmtKind kind) : kind(kind) {}
    StmtKind kind;
};

struct LetStmt final : Stmt {
    LetStmt() : Stmt(StmtKind::Let) {}

    std::string name;
    bool is_mutable{false};
    std::optional<TypeRef> type;
    ExprPtr initializer;
};

struct ReturnStmt final : Stmt {
    ReturnStmt() : Stmt(StmtKind::Return) {}
    ExprPtr value;
};

struct ExprStmt final : Stmt {
    ExprStmt() : Stmt(StmtKind::Expression) {}
    ExprPtr expression;
    bool has_semicolon{true};
};

struct BlockExpr final : Expr {
    BlockExpr() : Expr(ExprKind::Block) {}

    std::vector<StmtPtr> statements;
    ExprPtr tail_expression;
};

struct IfExpr final : Expr {
    IfExpr() : Expr(ExprKind::If) {}

    ExprPtr condition;
    std::unique_ptr<BlockExpr> then_branch;
    ExprPtr else_branch;
};

struct Parameter : Node {
    std::string name;
    TypeRef type;
};

struct FunctionDecl : Node {
    std::string name;
    std::vector<Parameter> parameters;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockExpr> body;
};

struct SourceFile : Node {
    std::vector<FunctionDecl> functions;
};

} // namespace nus::ast
