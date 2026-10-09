#pragma once

#include "nus/lexer/TokenKind.hpp"
#include "nus/sema/Type.hpp"
#include "nus/source/SourceSpan.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nus::mir {

using LocalId = std::uint32_t;
using BlockId = std::uint32_t;
inline constexpr LocalId InvalidLocalId = static_cast<LocalId>(-1);
inline constexpr BlockId InvalidBlockId = static_cast<BlockId>(-1);

enum class OperandKind { Local, Literal, Global, Unit };

struct Operand {
    OperandKind kind{OperandKind::Unit};
    LocalId local{InvalidLocalId};
    std::string text;
    sema::Type type;
};

enum class InstructionKind {
    Assign,
    Unary,
    Binary,
    Borrow,
    Call,
    Member,
    Index,
    MakeArray,
    MakeRange,
    MakeStruct,
    IterInit,
    IterHasNext,
    IterNext,
    StoreMember,
    StoreIndex,
    Drop,
    DropIf,
};

struct Instruction {
    InstructionKind kind{InstructionKind::Assign};
    std::optional<LocalId> destination;
    TokenKind op{TokenKind::Invalid};
    std::string name;
    std::vector<Operand> operands;
    SourceSpan span{};
};

enum class TerminatorKind { Goto, Branch, Return, Unreachable };

struct Terminator {
    TerminatorKind kind{TerminatorKind::Unreachable};
    Operand condition;
    Operand value;
    BlockId then_block{InvalidBlockId};
    BlockId else_block{InvalidBlockId};
    BlockId target{InvalidBlockId};
    SourceSpan span{};
};

struct BasicBlock {
    BlockId id{InvalidBlockId};
    std::vector<Instruction> instructions;
    std::optional<Terminator> terminator;
};

struct Local {
    LocalId id{InvalidLocalId};
    std::string name;
    sema::Type type;
    bool is_mutable{false};
    bool is_temporary{false};
    bool is_parameter{false};
    bool is_receiver{false};
};

struct Function {
    std::string name;
    std::string owner_type;
    std::vector<Local> locals;
    std::vector<BasicBlock> blocks;
    BlockId entry{0};
    sema::Type return_type;
};


struct StructField {
    std::string name;
    sema::Type type;
};

struct Struct {
    std::string name;
    std::vector<StructField> fields;
};

struct Program {
    std::vector<Struct> structs;
    std::vector<Function> functions;
};

} // namespace nus::mir
