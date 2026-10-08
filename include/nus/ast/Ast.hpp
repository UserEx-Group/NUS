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
    bool is_reference{false};
    bool is_mutable_reference{false};

    [[nodiscard]] std::string name() const {
        std::string result;
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0) result += "::";
            result += path[i];
        }
        if (is_reference) {
            return std::string("&") + (is_mutable_reference ? "mut " : "") + result;
        }
        return result;
    }
};

enum class ExprKind {
    Literal,
    Identifier,
    Unary,
    Binary,
    Assignment,
    Call,
    Member,
    Index,
    Array,
    Range,
    StructLiteral,
    Block,
    If,
    While,
    Loop,
    For,
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
    UnaryExpr(TokenKind op, ExprPtr operand, bool mutable_borrow = false)
        : Expr(ExprKind::Unary), op(op), operand(std::move(operand)), mutable_borrow(mutable_borrow) {}
    TokenKind op;
    ExprPtr operand;
    bool mutable_borrow{false};
};

struct BinaryExpr final : Expr {
    BinaryExpr(TokenKind op, ExprPtr left, ExprPtr right)
        : Expr(ExprKind::Binary), op(op), left(std::move(left)), right(std::move(right)) {}
    TokenKind op;
    ExprPtr left;
    ExprPtr right;
};

struct AssignmentExpr final : Expr {
    AssignmentExpr(TokenKind op, ExprPtr target, ExprPtr value)
        : Expr(ExprKind::Assignment), op(op), target(std::move(target)), value(std::move(value)) {}
    TokenKind op;
    ExprPtr target;
    ExprPtr value;
};

struct CallExpr final : Expr {
    explicit CallExpr(ExprPtr callee) : Expr(ExprKind::Call), callee(std::move(callee)) {}
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};

struct MemberExpr final : Expr {
    MemberExpr(ExprPtr object, std::string member)
        : Expr(ExprKind::Member), object(std::move(object)), member(std::move(member)) {}
    ExprPtr object;
    std::string member;
};

struct IndexExpr final : Expr {
    IndexExpr(ExprPtr object, ExprPtr index)
        : Expr(ExprKind::Index), object(std::move(object)), index(std::move(index)) {}
    ExprPtr object;
    ExprPtr index;
};

struct ArrayExpr final : Expr {
    ArrayExpr() : Expr(ExprKind::Array) {}
    std::vector<ExprPtr> elements;
    ExprPtr repeat_value;
    ExprPtr repeat_count;
    [[nodiscard]] bool isRepeated() const noexcept { return repeat_value != nullptr; }
};

struct RangeExpr final : Expr {
    RangeExpr(ExprPtr start, ExprPtr end, bool inclusive)
        : Expr(ExprKind::Range), start(std::move(start)), end(std::move(end)), inclusive(inclusive) {}
    ExprPtr start;
    ExprPtr end;
    bool inclusive{false};
};

struct StructFieldInit : Node {
    std::string name;
    ExprPtr value;
};

struct StructLiteralExpr final : Expr {
    explicit StructLiteralExpr(TypeRef type) : Expr(ExprKind::StructLiteral), type(std::move(type)) {}
    TypeRef type;
    std::vector<StructFieldInit> fields;
};

enum class StmtKind { Let, Return, Break, Continue, Expression };

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

struct BreakStmt final : Stmt {
    BreakStmt() : Stmt(StmtKind::Break) {}
    ExprPtr value;
};

struct ContinueStmt final : Stmt {
    ContinueStmt() : Stmt(StmtKind::Continue) {}
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

struct WhileExpr final : Expr {
    WhileExpr() : Expr(ExprKind::While) {}
    ExprPtr condition;
    std::unique_ptr<BlockExpr> body;
};

struct LoopExpr final : Expr {
    LoopExpr() : Expr(ExprKind::Loop) {}
    std::unique_ptr<BlockExpr> body;
};

struct ForExpr final : Expr {
    ForExpr() : Expr(ExprKind::For) {}
    std::string binding;
    ExprPtr iterable;
    std::unique_ptr<BlockExpr> body;
};

struct Parameter : Node {
    std::string name;
    TypeRef type;
};

enum class ReceiverKind { None, Value, Reference, MutableReference };

struct FunctionDecl : Node {
    std::string name;
    ReceiverKind receiver{ReceiverKind::None};
    SourceSpan receiver_span{};
    std::vector<Parameter> parameters;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockExpr> body;
};

struct StructFieldDecl : Node {
    std::string name;
    TypeRef type;
};

struct StructDecl : Node {
    std::string name;
    std::vector<StructFieldDecl> fields;
};

struct ImplDecl : Node {
    TypeRef target;
    std::vector<FunctionDecl> methods;
};

struct SourceFile : Node {
    std::vector<StructDecl> structs;
    std::vector<ImplDecl> impls;
    std::vector<FunctionDecl> functions;
};

} // namespace nus::ast
