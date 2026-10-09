#include "nus/mir/MirBuilder.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace nus::mir {
namespace {
sema::Type unitType() { return sema::Type::simple(sema::TypeKind::Unit); }
sema::Type boolType() { return sema::Type::simple(sema::TypeKind::Bool); }

std::string baseStructName(sema::Type type) {
    while (type.kind == sema::TypeKind::Reference && type.element) type = *type.element;
    return type.kind == sema::TypeKind::Struct ? type.nominal_name : std::string{};
}
}

Program MirBuilder::lower(const hir::Program& program) {
    Program result;
    for (const auto& structure : program.structs) {
        Struct lowered;
        lowered.name = structure.name;
        for (const auto& field : structure.fields) lowered.fields.push_back(StructField{field.name, field.type});
        result.structs.push_back(std::move(lowered));
    }
    for (const auto& function : program.functions) result.functions.push_back(lowerFunction(function));
    return result;
}

Function MirBuilder::lowerFunction(const hir::Function& source) {
    Function result;
    result.name = source.name;
    result.owner_type = source.owner_type;
    result.return_type = source.return_type;
    for (const auto& local : source.locals) {
        bool is_parameter = false;
        for (const auto& parameter : source.parameters) {
            if (parameter.id == local.id) { is_parameter = true; break; }
        }
        const bool is_receiver = source.receiver && source.receiver->id == local.id;
        result.locals.push_back(Local{
            .id = local.id,
            .name = local.name,
            .type = local.type,
            .is_mutable = local.is_mutable,
            .is_temporary = false,
            .is_parameter = is_parameter,
            .is_receiver = is_receiver,
        });
    }
    result.blocks.push_back(BasicBlock{.id = 0});
    result.entry = 0;

    function_ = &result;
    current_ = 0;
    loop_targets_.clear();

    const Operand tail = source.body ? lowerBlock(*source.body) : unitOperand();
    if (isOpen()) {
        Terminator ret;
        ret.kind = TerminatorKind::Return;
        ret.value = source.return_type.isUnit() ? unitOperand() : tail;
        terminate(std::move(ret));
    }
    for (auto& block : result.blocks) {
        if (!block.terminator) block.terminator = Terminator{.kind = TerminatorKind::Unreachable};
    }
    function_ = nullptr;
    return result;
}

Operand MirBuilder::lowerExpr(const hir::Expr& expression) {
    switch (expression.kind) {
        case hir::ExprKind::Literal:
            return Operand{.kind = OperandKind::Literal, .text = expression.text, .type = expression.type};
        case hir::ExprKind::Local:
            return localOperand(expression.local);
        case hir::ExprKind::Global:
            return Operand{.kind = OperandKind::Global, .text = expression.text, .type = expression.type};
        case hir::ExprKind::Unary: {
            const Operand operand = lowerExpr(*expression.operands.at(0));
            const LocalId dest = createTemp(expression.type);
            Instruction inst;
            inst.kind = expression.op == TokenKind::Ampersand ? InstructionKind::Borrow : InstructionKind::Unary;
            inst.destination = dest;
            inst.op = expression.op;
            inst.name = expression.flag ? "mut" : "";
            inst.operands.push_back(operand);
            inst.span = expression.span;
            emit(std::move(inst));
            return localOperand(dest);
        }
        case hir::ExprKind::Binary: {
            const Operand left = lowerExpr(*expression.operands.at(0));
            const Operand right = lowerExpr(*expression.operands.at(1));
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{
                .kind = InstructionKind::Binary,
                .destination = dest,
                .op = expression.op,
                .operands = {left, right},
                .span = expression.span,
            });
            return localOperand(dest);
        }
        case hir::ExprKind::Assignment: {
            const auto& target = *expression.operands.at(0);
            Operand value = lowerExpr(*expression.operands.at(1));
            if (expression.op != TokenKind::Equal) {
                const Operand old_value = lowerExpr(target);
                const LocalId temp = createTemp(expression.type);
                TokenKind base = TokenKind::Invalid;
                switch (expression.op) {
                    case TokenKind::PlusEqual: base = TokenKind::Plus; break;
                    case TokenKind::MinusEqual: base = TokenKind::Minus; break;
                    case TokenKind::StarEqual: base = TokenKind::Star; break;
                    case TokenKind::SlashEqual: base = TokenKind::Slash; break;
                    case TokenKind::PercentEqual: base = TokenKind::Percent; break;
                    case TokenKind::AmpersandEqual: base = TokenKind::Ampersand; break;
                    case TokenKind::PipeEqual: base = TokenKind::Pipe; break;
                    case TokenKind::CaretEqual: base = TokenKind::Caret; break;
                    case TokenKind::ShiftLeftEqual: base = TokenKind::ShiftLeft; break;
                    case TokenKind::ShiftRightEqual: base = TokenKind::ShiftRight; break;
                    default: break;
                }
                emit(Instruction{.kind = InstructionKind::Binary, .destination = temp, .op = base,
                                 .operands = {old_value, value}, .span = expression.span});
                value = localOperand(temp);
            }
            if (target.kind == hir::ExprKind::Local) {
                emit(Instruction{.kind = InstructionKind::Assign, .destination = target.local,
                                 .operands = {value}, .span = expression.span});
                return localOperand(target.local);
            }
            if (target.kind == hir::ExprKind::Member) {
                const Operand object = lowerExpr(*target.operands.at(0));
                emit(Instruction{.kind = InstructionKind::StoreMember, .name = target.text,
                                 .operands = {object, value}, .span = expression.span});
                return value;
            }
            if (target.kind == hir::ExprKind::Index) {
                const Operand object = lowerExpr(*target.operands.at(0));
                const Operand index = lowerExpr(*target.operands.at(1));
                emit(Instruction{.kind = InstructionKind::StoreIndex,
                                 .operands = {object, index, value}, .span = expression.span});
                return value;
            }
            return value;
        }
        case hir::ExprKind::Call: {
            std::vector<Operand> operands;
            const auto& callee_expr = *expression.operands.at(0);
            if (callee_expr.kind == hir::ExprKind::Member && callee_expr.receiver != hir::ReceiverMode::None) {
                const auto& object_expr = *callee_expr.operands.at(0);
                Operand receiver = lowerExpr(object_expr);
                const std::string owner = baseStructName(object_expr.type);
                sema::Type call_type = callee_expr.type;
                if (call_type.kind == sema::TypeKind::Function) {
                    sema::Type receiver_type = object_expr.type;
                    while (receiver_type.kind == sema::TypeKind::Reference && receiver_type.element) receiver_type = *receiver_type.element;
                    if (callee_expr.receiver == hir::ReceiverMode::Shared) {
                        receiver_type = sema::Type::reference(receiver_type, false);
                    } else if (callee_expr.receiver == hir::ReceiverMode::Mutable) {
                        receiver_type = sema::Type::reference(receiver_type, true);
                    }
                    call_type.parameters.insert(call_type.parameters.begin(), receiver_type);
                }
                operands.push_back(Operand{.kind = OperandKind::Global,
                                           .text = owner + "::" + callee_expr.text,
                                           .type = std::move(call_type)});
                if (callee_expr.receiver == hir::ReceiverMode::Shared && object_expr.type.kind != sema::TypeKind::Reference) {
                    const auto ref_type = sema::Type::reference(object_expr.type, false);
                    const LocalId borrow = createTemp(ref_type);
                    emit(Instruction{.kind = InstructionKind::Borrow, .destination = borrow, .op = TokenKind::Ampersand,
                                     .name = "shared", .operands = {receiver}, .span = callee_expr.span});
                    receiver = localOperand(borrow);
                } else if (callee_expr.receiver == hir::ReceiverMode::Mutable && object_expr.type.kind != sema::TypeKind::Reference) {
                    const auto ref_type = sema::Type::reference(object_expr.type, true);
                    const LocalId borrow = createTemp(ref_type);
                    emit(Instruction{.kind = InstructionKind::Borrow, .destination = borrow, .op = TokenKind::Ampersand,
                                     .name = "mut", .operands = {receiver}, .span = callee_expr.span});
                    receiver = localOperand(borrow);
                }
                operands.push_back(receiver);
                for (std::size_t i = 1; i < expression.operands.size(); ++i) {
                    operands.push_back(lowerExpr(*expression.operands[i]));
                }
            } else {
                for (const auto& child : expression.operands) operands.push_back(lowerExpr(*child));
            }
            if (expression.type.isUnit()) {
                emit(Instruction{.kind = InstructionKind::Call, .operands = std::move(operands), .span = expression.span});
                return unitOperand();
            }
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::Call, .destination = dest,
                             .operands = std::move(operands), .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::Member: {
            const Operand object = lowerExpr(*expression.operands.at(0));
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::Member, .destination = dest, .name = expression.text,
                             .operands = {object}, .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::Index: {
            const Operand object = lowerExpr(*expression.operands.at(0));
            const Operand index = lowerExpr(*expression.operands.at(1));
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::Index, .destination = dest,
                             .operands = {object, index}, .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::Array: {
            std::vector<Operand> values;
            for (const auto& child : expression.operands) values.push_back(lowerExpr(*child));
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::MakeArray, .destination = dest,
                             .name = expression.flag ? "repeat" : "elements",
                             .operands = std::move(values), .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::Range: {
            const Operand start = lowerExpr(*expression.operands.at(0));
            const Operand end = lowerExpr(*expression.operands.at(1));
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::MakeRange, .destination = dest,
                             .name = expression.flag ? "inclusive" : "exclusive",
                             .operands = {start, end}, .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::StructLiteral: {
            std::vector<Operand> values;
            std::string fields;
            for (std::size_t i = 0; i < expression.fields.size(); ++i) {
                if (i != 0) fields += ',';
                fields += expression.fields[i].name;
                values.push_back(lowerExpr(*expression.fields[i].value));
            }
            const LocalId dest = createTemp(expression.type);
            emit(Instruction{.kind = InstructionKind::MakeStruct, .destination = dest,
                             .name = expression.text + "{" + fields + "}",
                             .operands = std::move(values), .span = expression.span});
            return localOperand(dest);
        }
        case hir::ExprKind::Block:
            return lowerBlock(expression);
        case hir::ExprKind::If: {
            const Operand condition = lowerExpr(*expression.operands.at(0));
            const BlockId then_block = createBlock();
            const BlockId else_block = createBlock();
            const BlockId join_block = createBlock();
            terminate(Terminator{.kind = TerminatorKind::Branch, .condition = condition,
                                 .then_block = then_block, .else_block = else_block, .span = expression.span});

            std::optional<LocalId> result_local;
            if (!expression.type.isUnit()) result_local = createTemp(expression.type);

            switchTo(then_block);
            const Operand then_value = lowerExpr(*expression.operands.at(1));
            if (isOpen()) {
                if (result_local) emit(Instruction{.kind = InstructionKind::Assign, .destination = *result_local,
                                                   .operands = {then_value}, .span = expression.span});
                gotoIfOpen(join_block, expression.span);
            }

            switchTo(else_block);
            Operand else_value = unitOperand();
            if (expression.operands.size() > 2) else_value = lowerExpr(*expression.operands.at(2));
            if (isOpen()) {
                if (result_local) emit(Instruction{.kind = InstructionKind::Assign, .destination = *result_local,
                                                   .operands = {else_value}, .span = expression.span});
                gotoIfOpen(join_block, expression.span);
            }

            switchTo(join_block);
            return result_local ? localOperand(*result_local) : unitOperand();
        }
        case hir::ExprKind::While: {
            const BlockId head = createBlock();
            const BlockId body = createBlock();
            const BlockId exit = createBlock();
            gotoIfOpen(head, expression.span);
            switchTo(head);
            const Operand condition = lowerExpr(*expression.operands.at(0));
            terminate(Terminator{.kind = TerminatorKind::Branch, .condition = condition,
                                 .then_block = body, .else_block = exit, .span = expression.span});
            switchTo(body);
            loop_targets_.push_back({exit, head});
            (void)lowerExpr(*expression.operands.at(1));
            loop_targets_.pop_back();
            gotoIfOpen(head, expression.span);
            switchTo(exit);
            return unitOperand();
        }
        case hir::ExprKind::Loop: {
            const BlockId body = createBlock();
            const BlockId exit = createBlock();
            gotoIfOpen(body, expression.span);
            switchTo(body);
            loop_targets_.push_back({exit, body});
            (void)lowerExpr(*expression.operands.at(0));
            loop_targets_.pop_back();
            gotoIfOpen(body, expression.span);
            switchTo(exit);
            return unitOperand();
        }
        case hir::ExprKind::For: {
            const Operand iterable = lowerExpr(*expression.operands.at(0));
            const LocalId iterator = createTemp(expression.operands.at(0)->type);
            emit(Instruction{.kind = InstructionKind::IterInit, .destination = iterator,
                             .operands = {iterable}, .span = expression.span});
            const BlockId head = createBlock();
            const BlockId body = createBlock();
            const BlockId exit = createBlock();
            gotoIfOpen(head, expression.span);
            switchTo(head);
            const LocalId has_next = createTemp(boolType());
            emit(Instruction{.kind = InstructionKind::IterHasNext, .destination = has_next,
                             .operands = {localOperand(iterator)}, .span = expression.span});
            terminate(Terminator{.kind = TerminatorKind::Branch, .condition = localOperand(has_next),
                                 .then_block = body, .else_block = exit, .span = expression.span});
            switchTo(body);
            emit(Instruction{.kind = InstructionKind::IterNext, .destination = expression.binding,
                             .operands = {localOperand(iterator)}, .span = expression.span});
            loop_targets_.push_back({exit, head});
            (void)lowerExpr(*expression.operands.at(1));
            loop_targets_.pop_back();
            gotoIfOpen(head, expression.span);
            switchTo(exit);
            return unitOperand();
        }
    }
    return unitOperand();
}

void MirBuilder::lowerStmt(const hir::Stmt& statement) {
    switch (statement.kind) {
        case hir::StmtKind::Let: {
            const Operand value = statement.value ? lowerExpr(*statement.value) : unitOperand();
            emit(Instruction{.kind = InstructionKind::Assign, .destination = statement.local.id,
                             .operands = {value}, .span = statement.span});
            break;
        }
        case hir::StmtKind::Return: {
            const Operand value = statement.value ? lowerExpr(*statement.value) : unitOperand();
            terminate(Terminator{.kind = TerminatorKind::Return, .value = value, .span = statement.span});
            break;
        }
        case hir::StmtKind::Break: {
            if (!loop_targets_.empty()) {
                if (statement.value) (void)lowerExpr(*statement.value);
                terminate(Terminator{.kind = TerminatorKind::Goto, .target = loop_targets_.back().first,
                                     .span = statement.span});
            }
            break;
        }
        case hir::StmtKind::Continue: {
            if (!loop_targets_.empty()) {
                terminate(Terminator{.kind = TerminatorKind::Goto, .target = loop_targets_.back().second,
                                     .span = statement.span});
            }
            break;
        }
        case hir::StmtKind::Expression:
            if (statement.value) (void)lowerExpr(*statement.value);
            break;
    }
}

Operand MirBuilder::lowerBlock(const hir::Expr& block) {
    Operand result = unitOperand();
    for (const auto& statement : block.statements) {
        if (!isOpen()) {
            const BlockId dead = createBlock();
            switchTo(dead);
        }
        lowerStmt(*statement);
    }
    if (block.tail) {
        if (!isOpen()) {
            const BlockId dead = createBlock();
            switchTo(dead);
        }
        result = lowerExpr(*block.tail);
    }
    return result;
}

LocalId MirBuilder::createTemp(sema::Type type) {
    LocalId id = 0;
    for (const auto& local : function_->locals) id = std::max(id, static_cast<LocalId>(local.id + 1));
    function_->locals.push_back(Local{.id = id, .name = "_t" + std::to_string(id), .type = std::move(type),
                                      .is_mutable = false, .is_temporary = true});
    return id;
}

BlockId MirBuilder::createBlock() {
    const BlockId id = static_cast<BlockId>(function_->blocks.size());
    function_->blocks.push_back(BasicBlock{.id = id});
    return id;
}

BasicBlock& MirBuilder::currentBlock() { return function_->blocks.at(current_); }
void MirBuilder::switchTo(BlockId id) { current_ = id; }
void MirBuilder::emit(Instruction instruction) { currentBlock().instructions.push_back(std::move(instruction)); }
void MirBuilder::terminate(Terminator terminator) {
    if (!currentBlock().terminator) currentBlock().terminator = std::move(terminator);
}
void MirBuilder::gotoIfOpen(BlockId target, SourceSpan span) {
    if (isOpen()) terminate(Terminator{.kind = TerminatorKind::Goto, .target = target, .span = span});
}
bool MirBuilder::isOpen() const { return !function_->blocks.at(current_).terminator.has_value(); }

Operand MirBuilder::localOperand(LocalId id) const {
    for (const auto& local : function_->locals) {
        if (local.id == id) return Operand{.kind = OperandKind::Local, .local = id, .type = local.type};
    }
    return Operand{.kind = OperandKind::Local, .local = id, .type = sema::Type::simple(sema::TypeKind::Unknown)};
}

Operand MirBuilder::unitOperand() const {
    return Operand{.kind = OperandKind::Unit, .type = unitType()};
}

} // namespace nus::mir
