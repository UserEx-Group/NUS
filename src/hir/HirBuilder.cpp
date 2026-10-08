#include "nus/hir/HirBuilder.hpp"

#include <stdexcept>
#include <utility>

namespace nus::hir {
namespace {

sema::Type primitiveType(std::string_view name) {
    using sema::Type;
    using sema::TypeKind;
    if (name == "unit" || name == "()") return Type::simple(TypeKind::Unit);
    if (name == "bool") return Type::simple(TypeKind::Bool);
    if (name == "i8") return Type::simple(TypeKind::I8);
    if (name == "i16") return Type::simple(TypeKind::I16);
    if (name == "i32") return Type::simple(TypeKind::I32);
    if (name == "i64") return Type::simple(TypeKind::I64);
    if (name == "isize") return Type::simple(TypeKind::ISize);
    if (name == "u8") return Type::simple(TypeKind::U8);
    if (name == "u16") return Type::simple(TypeKind::U16);
    if (name == "u32") return Type::simple(TypeKind::U32);
    if (name == "u64") return Type::simple(TypeKind::U64);
    if (name == "usize") return Type::simple(TypeKind::USize);
    if (name == "f32") return Type::simple(TypeKind::F32);
    if (name == "f64") return Type::simple(TypeKind::F64);
    if (name == "char") return Type::simple(TypeKind::Char);
    if (name == "string") return Type::simple(TypeKind::String);
    if (name == "bytes") return Type::simple(TypeKind::Bytes);
    return Type::simple(TypeKind::Unknown);
}

sema::Type unitType() { return sema::Type::simple(sema::TypeKind::Unit); }
sema::Type unknownType() { return sema::Type::simple(sema::TypeKind::Unknown); }

} // namespace

HirBuilder::HirBuilder(const sema::SemanticAnalyzer& semantic) : semantic_(semantic) {}

Program HirBuilder::lower(const ast::SourceFile& file) {
    Program program;
    struct_types_.clear();
    method_receivers_.clear();
    for (const auto& implementation : file.impls) {
        const std::string owner = implementation.target.name();
        for (const auto& method : implementation.methods) {
            ReceiverMode mode = ReceiverMode::None;
            switch (method.receiver) {
                case ast::ReceiverKind::Value: mode = ReceiverMode::Value; break;
                case ast::ReceiverKind::Reference: mode = ReceiverMode::Shared; break;
                case ast::ReceiverKind::MutableReference: mode = ReceiverMode::Mutable; break;
                case ast::ReceiverKind::None: break;
            }
            method_receivers_.insert_or_assign(owner + "::" + method.name, mode);
        }
    }
    for (const auto& structure : file.structs) {
        struct_types_.emplace(structure.name, sema::Type::structure(structure.name));
    }

    for (const auto& structure : file.structs) {
        Struct lowered;
        lowered.name = structure.name;
        lowered.span = structure.span;
        for (const auto& field : structure.fields) {
            lowered.fields.push_back(StructField{
                .name = field.name,
                .type = lowerTypeRef(field.type),
                .span = field.span,
            });
        }
        program.structs.push_back(std::move(lowered));
    }

    for (const auto& function : file.functions) {
        program.functions.push_back(lowerFunction(function));
    }
    for (const auto& implementation : file.impls) {
        const std::string owner = implementation.target.name();
        for (const auto& method : implementation.methods) {
            program.functions.push_back(lowerFunction(method, owner));
        }
    }
    return program;
}

Function HirBuilder::lowerFunction(const ast::FunctionDecl& function, std::string owner_type) {
    scopes_.clear();
    locals_.clear();
    next_local_ = 0;
    pushScope();

    Function lowered;
    lowered.name = function.name;
    lowered.owner_type = std::move(owner_type);
    lowered.span = function.span;
    lowered.return_type = function.return_type ? lowerTypeRef(*function.return_type) : unitType();

    if (function.receiver != ast::ReceiverKind::None) {
        sema::Type receiver_type = sema::Type::structure(lowered.owner_type);
        bool is_mutable = false;
        if (function.receiver == ast::ReceiverKind::Reference) {
            receiver_type = sema::Type::reference(receiver_type, false);
        } else if (function.receiver == ast::ReceiverKind::MutableReference) {
            receiver_type = sema::Type::reference(receiver_type, true);
            is_mutable = true;
        }
        const LocalId id = declareLocal("self", receiver_type, is_mutable, function.receiver_span);
        lowered.receiver = localInfo(id);
    }

    for (const auto& parameter : function.parameters) {
        const LocalId id = declareLocal(parameter.name, lowerTypeRef(parameter.type), false, parameter.span);
        lowered.parameters.push_back(localInfo(id));
    }

    lowered.body = lowerBlock(*function.body, false);
    lowered.locals = locals_;
    popScope();
    return lowered;
}

ExprPtr HirBuilder::lowerExpr(const ast::Expr& expression) {
    auto result = std::make_unique<Expr>();
    result->span = expression.span;
    result->type = expressionType(expression);

    switch (expression.kind) {
        case ast::ExprKind::Literal: {
            const auto& node = static_cast<const ast::LiteralExpr&>(expression);
            result->kind = ExprKind::Literal;
            result->text = node.text;
            result->op = node.literal_kind;
            break;
        }
        case ast::ExprKind::Identifier: {
            const auto& node = static_cast<const ast::IdentifierExpr&>(expression);
            const LocalId id = findLocal(node.name);
            if (id != InvalidLocalId) {
                result->kind = ExprKind::Local;
                result->local = id;
                result->type = localInfo(id).type;
            } else {
                result->kind = ExprKind::Global;
                result->text = node.name;
            }
            break;
        }
        case ast::ExprKind::Unary: {
            const auto& node = static_cast<const ast::UnaryExpr&>(expression);
            result->kind = ExprKind::Unary;
            result->op = node.op;
            result->flag = node.mutable_borrow;
            result->operands.push_back(lowerExpr(*node.operand));
            break;
        }
        case ast::ExprKind::Binary: {
            const auto& node = static_cast<const ast::BinaryExpr&>(expression);
            result->kind = ExprKind::Binary;
            result->op = node.op;
            result->operands.push_back(lowerExpr(*node.left));
            result->operands.push_back(lowerExpr(*node.right));
            break;
        }
        case ast::ExprKind::Assignment: {
            const auto& node = static_cast<const ast::AssignmentExpr&>(expression);
            result->kind = ExprKind::Assignment;
            result->op = node.op;
            result->operands.push_back(lowerExpr(*node.target));
            result->operands.push_back(lowerExpr(*node.value));
            break;
        }
        case ast::ExprKind::Call: {
            const auto& node = static_cast<const ast::CallExpr&>(expression);
            result->kind = ExprKind::Call;
            result->operands.push_back(lowerExpr(*node.callee));
            for (const auto& argument : node.arguments) result->operands.push_back(lowerExpr(*argument));
            break;
        }
        case ast::ExprKind::Member: {
            const auto& node = static_cast<const ast::MemberExpr&>(expression);
            result->kind = ExprKind::Member;
            result->text = node.member;
            auto object = lowerExpr(*node.object);
            sema::Type base = object->type;
            while (base.kind == sema::TypeKind::Reference && base.element) base = *base.element;
            if (base.kind == sema::TypeKind::Struct) {
                const auto found = method_receivers_.find(base.nominal_name + "::" + node.member);
                if (found != method_receivers_.end()) result->receiver = found->second;
            }
            result->operands.push_back(std::move(object));
            break;
        }
        case ast::ExprKind::Index: {
            const auto& node = static_cast<const ast::IndexExpr&>(expression);
            result->kind = ExprKind::Index;
            result->operands.push_back(lowerExpr(*node.object));
            result->operands.push_back(lowerExpr(*node.index));
            break;
        }
        case ast::ExprKind::Array: {
            const auto& node = static_cast<const ast::ArrayExpr&>(expression);
            result->kind = ExprKind::Array;
            result->flag = node.isRepeated();
            if (node.isRepeated()) {
                result->operands.push_back(lowerExpr(*node.repeat_value));
                result->operands.push_back(lowerExpr(*node.repeat_count));
            } else {
                for (const auto& element : node.elements) result->operands.push_back(lowerExpr(*element));
            }
            break;
        }
        case ast::ExprKind::Range: {
            const auto& node = static_cast<const ast::RangeExpr&>(expression);
            result->kind = ExprKind::Range;
            result->flag = node.inclusive;
            result->operands.push_back(lowerExpr(*node.start));
            result->operands.push_back(lowerExpr(*node.end));
            break;
        }
        case ast::ExprKind::StructLiteral: {
            const auto& node = static_cast<const ast::StructLiteralExpr&>(expression);
            result->kind = ExprKind::StructLiteral;
            result->text = node.type.name();
            for (const auto& field : node.fields) {
                FieldInit lowered_field;
                lowered_field.name = field.name;
                lowered_field.span = field.span;
                lowered_field.value = lowerExpr(*field.value);
                result->fields.push_back(std::move(lowered_field));
            }
            break;
        }
        case ast::ExprKind::Block:
            return lowerBlock(static_cast<const ast::BlockExpr&>(expression));
        case ast::ExprKind::If: {
            const auto& node = static_cast<const ast::IfExpr&>(expression);
            result->kind = ExprKind::If;
            result->operands.push_back(lowerExpr(*node.condition));
            result->operands.push_back(lowerBlock(*node.then_branch));
            if (node.else_branch) result->operands.push_back(lowerExpr(*node.else_branch));
            break;
        }
        case ast::ExprKind::While: {
            const auto& node = static_cast<const ast::WhileExpr&>(expression);
            result->kind = ExprKind::While;
            result->operands.push_back(lowerExpr(*node.condition));
            result->operands.push_back(lowerBlock(*node.body));
            break;
        }
        case ast::ExprKind::Loop: {
            const auto& node = static_cast<const ast::LoopExpr&>(expression);
            result->kind = ExprKind::Loop;
            result->operands.push_back(lowerBlock(*node.body));
            break;
        }
        case ast::ExprKind::For: {
            const auto& node = static_cast<const ast::ForExpr&>(expression);
            result->kind = ExprKind::For;
            auto iterable = lowerExpr(*node.iterable);
            sema::Type binding_type = unknownType();
            if (iterable->type.kind == sema::TypeKind::Range || iterable->type.kind == sema::TypeKind::Array ||
                iterable->type.kind == sema::TypeKind::Slice) {
                if (iterable->type.element) binding_type = *iterable->type.element;
            } else if (iterable->type.kind == sema::TypeKind::Bytes) {
                binding_type = sema::Type::simple(sema::TypeKind::U8);
            }
            result->operands.push_back(std::move(iterable));
            pushScope();
            result->binding = declareLocal(node.binding, binding_type, false, node.span);
            result->text = node.binding;
            result->operands.push_back(lowerBlock(*node.body, false));
            popScope();
            break;
        }
    }
    return result;
}

StmtPtr HirBuilder::lowerStmt(const ast::Stmt& statement) {
    auto result = std::make_unique<Stmt>();
    result->span = statement.span;
    switch (statement.kind) {
        case ast::StmtKind::Let: {
            const auto& node = static_cast<const ast::LetStmt&>(statement);
            result->kind = StmtKind::Let;
            result->value = lowerExpr(*node.initializer);
            const sema::Type type = node.type ? lowerTypeRef(*node.type) : result->value->type;
            const LocalId id = declareLocal(node.name, type, node.is_mutable, node.span);
            result->local = localInfo(id);
            break;
        }
        case ast::StmtKind::Return: {
            const auto& node = static_cast<const ast::ReturnStmt&>(statement);
            result->kind = StmtKind::Return;
            if (node.value) result->value = lowerExpr(*node.value);
            break;
        }
        case ast::StmtKind::Break: {
            const auto& node = static_cast<const ast::BreakStmt&>(statement);
            result->kind = StmtKind::Break;
            if (node.value) result->value = lowerExpr(*node.value);
            break;
        }
        case ast::StmtKind::Continue:
            result->kind = StmtKind::Continue;
            break;
        case ast::StmtKind::Expression: {
            const auto& node = static_cast<const ast::ExprStmt&>(statement);
            result->kind = StmtKind::Expression;
            result->value = lowerExpr(*node.expression);
            result->has_semicolon = node.has_semicolon;
            break;
        }
    }
    return result;
}

ExprPtr HirBuilder::lowerBlock(const ast::BlockExpr& block, bool create_scope) {
    if (create_scope) pushScope();
    auto result = std::make_unique<Expr>();
    result->kind = ExprKind::Block;
    result->type = expressionType(block);
    result->span = block.span;
    for (const auto& statement : block.statements) result->statements.push_back(lowerStmt(*statement));
    if (block.tail_expression) result->tail = lowerExpr(*block.tail_expression);
    if (result->type.kind == sema::TypeKind::Unknown) {
        result->type = result->tail ? result->tail->type : unitType();
    }
    if (create_scope) popScope();
    return result;
}

sema::Type HirBuilder::lowerTypeRef(const ast::TypeRef& type) const {
    const std::string name = [&] {
        std::string joined;
        for (std::size_t i = 0; i < type.path.size(); ++i) {
            if (i != 0) joined += "::";
            joined += type.path[i];
        }
        return joined;
    }();
    sema::Type base = primitiveType(name);
    if (base.kind == sema::TypeKind::Unknown) base = sema::Type::structure(name);
    if (type.is_reference) return sema::Type::reference(base, type.is_mutable_reference);
    return base;
}

sema::Type HirBuilder::expressionType(const ast::Expr& expression) const {
    if (const auto* type = semantic_.typeOf(expression)) return *type;
    return unknownType();
}

void HirBuilder::pushScope() { scopes_.emplace_back(); }
void HirBuilder::popScope() { scopes_.pop_back(); }

LocalId HirBuilder::declareLocal(std::string name, sema::Type type, bool is_mutable, SourceSpan span) {
    if (scopes_.empty()) pushScope();
    const LocalId id = next_local_++;
    Local local{.id = id, .name = name, .type = std::move(type), .is_mutable = is_mutable, .span = span};
    scopes_.back().insert_or_assign(name, id);
    locals_.push_back(local);
    return id;
}

LocalId HirBuilder::findLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return InvalidLocalId;
}

Local HirBuilder::localInfo(LocalId id) const {
    for (const auto& local : locals_) if (local.id == id) return local;
    throw std::runtime_error("invalid HIR local id");
}

} // namespace nus::hir
