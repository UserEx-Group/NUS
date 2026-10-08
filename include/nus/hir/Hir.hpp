#pragma once

#include "nus/lexer/TokenKind.hpp"
#include "nus/sema/Type.hpp"
#include "nus/source/SourceSpan.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nus::hir {

using LocalId = std::uint32_t;
inline constexpr LocalId InvalidLocalId = static_cast<LocalId>(-1);

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

enum class ExprKind {
    Literal,
    Local,
    Global,
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

enum class StmtKind { Let, Return, Break, Continue, Expression };
enum class ReceiverMode { None, Value, Shared, Mutable };

struct Local {
    LocalId id{InvalidLocalId};
    std::string name;
    sema::Type type;
    bool is_mutable{false};
    SourceSpan span{};
};

struct FieldInit {
    std::string name;
    ExprPtr value;
    SourceSpan span{};
};

struct Expr {
    ExprKind kind{ExprKind::Literal};
    sema::Type type;
    SourceSpan span{};

    // Payload shared by compact HIR node kinds.
    std::string text;
    TokenKind op{TokenKind::Invalid};
    LocalId local{InvalidLocalId};
    bool flag{false};
    ReceiverMode receiver{ReceiverMode::None};
    std::vector<ExprPtr> operands;
    std::vector<FieldInit> fields;
    std::vector<StmtPtr> statements;
    ExprPtr tail;
    LocalId binding{InvalidLocalId};
};

struct Stmt {
    StmtKind kind{StmtKind::Expression};
    SourceSpan span{};
    Local local;
    ExprPtr value;
    bool has_semicolon{true};
};

struct Function {
    std::string name;
    std::string owner_type;
    std::optional<Local> receiver;
    std::vector<Local> parameters;
    std::vector<Local> locals;
    sema::Type return_type;
    ExprPtr body;
    SourceSpan span{};
};

struct StructField {
    std::string name;
    sema::Type type;
    SourceSpan span{};
};

struct Struct {
    std::string name;
    std::vector<StructField> fields;
    SourceSpan span{};
};

struct Program {
    std::vector<Struct> structs;
    std::vector<Function> functions;
};

} // namespace nus::hir
