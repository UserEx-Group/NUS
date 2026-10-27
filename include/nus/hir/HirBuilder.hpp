#pragma once

#include "nus/ast/Ast.hpp"
#include "nus/hir/Hir.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace nus::hir {

class HirBuilder {
public:
    explicit HirBuilder(const sema::SemanticAnalyzer& semantic);

    [[nodiscard]] Program lower(const ast::SourceFile& file);

private:
    [[nodiscard]] Function lowerFunction(const ast::FunctionDecl& function, std::string owner_type = {},
                                         std::string symbol_name = {},
                                         std::unordered_map<std::string, sema::Type> substitutions = {},
                                         sema::Type receiver_base = sema::Type::simple(sema::TypeKind::Unknown));
    [[nodiscard]] ExprPtr lowerExpr(const ast::Expr& expression);
    [[nodiscard]] StmtPtr lowerStmt(const ast::Stmt& statement);
    [[nodiscard]] ExprPtr lowerBlock(const ast::BlockExpr& block, bool create_scope = true);
    [[nodiscard]] sema::Type lowerTypeRef(const ast::TypeRef& type) const;
    [[nodiscard]] sema::Type expressionType(const ast::Expr& expression) const;
    [[nodiscard]] sema::Type substituteType(const sema::Type& type) const;

    void pushScope();
    void popScope();
    LocalId declareLocal(std::string name, sema::Type type, bool is_mutable, SourceSpan span);
    [[nodiscard]] LocalId findLocal(const std::string& name) const;
    [[nodiscard]] Local localInfo(LocalId id) const;

    const sema::SemanticAnalyzer& semantic_;
    std::vector<std::unordered_map<std::string, LocalId>> scopes_;
    std::vector<Local> locals_;
    LocalId next_local_{0};
    std::unordered_map<std::string, sema::Type> struct_types_;
    std::unordered_map<std::string, sema::Type> enum_types_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::pair<std::size_t, std::vector<sema::Type>>>> enum_variants_;
    std::unordered_map<std::string, ReceiverMode> method_receivers_;
    std::unordered_map<std::string, sema::Type> type_substitutions_;
    sema::Type current_return_type_{sema::Type::simple(sema::TypeKind::Unit)};
};

} // namespace nus::hir
