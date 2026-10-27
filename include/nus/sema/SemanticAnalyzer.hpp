#pragma once

#include "nus/ast/Ast.hpp"
#include "nus/diagnostics/Diagnostic.hpp"
#include "nus/sema/SymbolTable.hpp"
#include "nus/sema/Type.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nus::sema {

class SemanticAnalyzer {
public:
    struct GenericInstantiation {
        const ast::FunctionDecl* declaration{nullptr};
        std::string source_name;
        std::string symbol_name;
        std::vector<std::pair<std::string, Type>> substitutions;
        Type signature{Type::simple(TypeKind::Error)};
        bool builtin{false};
        bool method{false};
        Type receiver_type{Type::simple(TypeKind::Unknown)};
        ast::ReceiverKind receiver{ast::ReceiverKind::None};
    };
    explicit SemanticAnalyzer(bool lexical_ownership_checks = false);

    void analyze(const ast::SourceFile& file);

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] const Type* typeOf(const ast::Expr& expression) const noexcept;
    [[nodiscard]] const std::vector<GenericInstantiation>& genericInstantiations() const noexcept;
    [[nodiscard]] const std::string* resolvedCallTarget(const ast::CallExpr& expression) const noexcept;
    [[nodiscard]] ast::ReceiverKind resolvedCallReceiver(const ast::CallExpr& expression) const noexcept;

private:
    struct LValueInfo {
        Type type{Type::simple(TypeKind::Error)};
        bool is_mutable{false};
    };

    struct MethodInfo {
        const ast::FunctionDecl* declaration{nullptr};
        ast::ReceiverKind receiver{ast::ReceiverKind::None};
        Type signature{Type::simple(TypeKind::Error)};
    };

    struct TraitInfo {
        const ast::TraitDecl* declaration{nullptr};
        std::unordered_set<std::string> associated_types;
        std::unordered_map<std::string, const ast::FunctionDecl*> methods;
    };

    struct GenericTraitImplInfo {
        std::string trait_name;
        Type target_pattern{Type::simple(TypeKind::Error)};
        std::vector<ast::GenericParameter> generic_parameters;
    };

    struct StructInfo {
        const ast::StructDecl* declaration{nullptr};
        std::unordered_map<std::string, Type> fields;
        std::unordered_map<std::string, MethodInfo> methods;
    };

    struct EnumVariantInfo {
        std::size_t tag{0};
        std::vector<Type> payloads;
    };

    struct EnumInfo {
        const ast::EnumDecl* declaration{nullptr};
        std::unordered_map<std::string, EnumVariantInfo> variants;
        std::vector<std::string> variant_order;
    };

    struct GenericFunctionInfo {
        const ast::FunctionDecl* declaration{nullptr};
        std::vector<ast::GenericParameter> generic_parameters;
        Type signature{Type::simple(TypeKind::Error)};
        std::string builtin_base;
        bool builtin{false};
    };


    struct GenericMethodInfo {
        const ast::FunctionDecl* declaration{nullptr};
        Type target_pattern{Type::simple(TypeKind::Error)};
        std::vector<ast::GenericParameter> impl_parameters;
        ast::ReceiverKind receiver{ast::ReceiverKind::None};
        Type signature{Type::simple(TypeKind::Error)};
    };
    struct BorrowRecord {
        std::size_t symbol_id{0};
        bool is_mutable{false};
    };

    void reset();
    void registerBuiltins();
    void collectTraits(const ast::SourceFile& file);
    void collectTraitImpls(const ast::SourceFile& file);
    void collectStructNames(const ast::SourceFile& file);
    void collectEnumNames(const ast::SourceFile& file);
    void collectEnumVariants(const ast::SourceFile& file);
    void collectStructFields(const ast::SourceFile& file);
    void collectFunctions(const ast::SourceFile& file);
    void collectMethods(const ast::SourceFile& file);
    void analyzeFunction(const ast::FunctionDecl& function);
    void analyzeMethod(const Type& target_type, const ast::FunctionDecl& method,
                       const std::vector<ast::GenericParameter>& impl_parameters = {});

    [[nodiscard]] Type resolveType(const ast::TypeRef& type_ref);
    [[nodiscard]] Type resolveTraitType(const ast::TypeRef& type_ref, const Type& self_type,
                                        const std::unordered_map<std::string, Type>& associated_types);
    [[nodiscard]] Type instantiateType(const Type& type, const std::unordered_map<std::string, Type>& substitutions) const;
    [[nodiscard]] bool inferGeneric(const Type& pattern, const Type& actual, std::unordered_map<std::string, Type>& substitutions) const;
    [[nodiscard]] bool satisfiesTrait(const Type& type, std::string_view trait) const;
    [[nodiscard]] Type checkGenericCall(const ast::CallExpr& expression, const ast::IdentifierExpr& callee, const GenericFunctionInfo& generic);
    void registerGenericBuiltin(std::string name, std::vector<ast::GenericParameter> parameters, Type signature, std::string builtin_base);
    [[nodiscard]] Type checkBlock(const ast::BlockExpr& block, bool create_scope = true);
    void checkStatement(const ast::Stmt& statement);
    [[nodiscard]] Type checkExpr(const ast::Expr& expression);
    [[nodiscard]] Type checkLiteral(const ast::LiteralExpr& expression);
    [[nodiscard]] Type checkUnary(const ast::UnaryExpr& expression);
    [[nodiscard]] Type checkBinary(const ast::BinaryExpr& expression);
    [[nodiscard]] Type checkAssignment(const ast::AssignmentExpr& expression);
    [[nodiscard]] Type checkCall(const ast::CallExpr& expression);
    [[nodiscard]] Type checkMethodCall(const ast::CallExpr& expression, const ast::MemberExpr& member);
    [[nodiscard]] Type checkVecMethodCall(const ast::CallExpr& expression, const ast::MemberExpr& member, const Type& object_type);
    [[nodiscard]] Type checkGenericMethodCall(const ast::CallExpr& expression, const ast::MemberExpr& member,
                                              const Type& object_type, const GenericMethodInfo& method,
                                              std::unordered_map<std::string, Type> substitutions);
    [[nodiscard]] Type checkMember(const ast::MemberExpr& expression);
    [[nodiscard]] Type checkIndex(const ast::IndexExpr& expression);
    [[nodiscard]] Type checkArray(const ast::ArrayExpr& expression);
    [[nodiscard]] Type checkRange(const ast::RangeExpr& expression);
    [[nodiscard]] Type checkStructLiteral(const ast::StructLiteralExpr& expression);
    [[nodiscard]] Type checkVariant(const ast::VariantExpr& expression);
    [[nodiscard]] Type checkMatch(const ast::MatchExpr& expression);
    [[nodiscard]] Type checkTry(const ast::TryExpr& expression);
    [[nodiscard]] Type checkIf(const ast::IfExpr& expression);
    [[nodiscard]] Type checkWhile(const ast::WhileExpr& expression);
    [[nodiscard]] Type checkLoop(const ast::LoopExpr& expression);
    [[nodiscard]] Type checkFor(const ast::ForExpr& expression);
    [[nodiscard]] bool validateReferenceReturn(const ast::Expr& expression);

    void checkPattern(const ast::Pattern& pattern, const Type& scrutinee, std::unordered_set<std::string>& covered, bool& irrefutable);
    [[nodiscard]] const StructInfo* findStruct(const Type& type) const;
    [[nodiscard]] StructInfo* findStruct(const Type& type);
    [[nodiscard]] const MethodInfo* findMethod(const Type& type, std::string_view name) const;
    [[nodiscard]] const Type* findField(const Type& type, std::string_view name) const;
    [[nodiscard]] static Type dereferenceForMember(Type type);

    [[nodiscard]] LValueInfo checkLValue(const ast::Expr& expression);
    [[nodiscard]] Symbol* rootSymbol(const ast::Expr& expression);
    [[nodiscard]] const Symbol* rootSymbol(const ast::Expr& expression) const;
    [[nodiscard]] bool borrowPlace(const ast::Expr& expression, bool is_mutable, SourceSpan span);
    void consumeValue(const ast::Expr& expression, const Type& type, std::string_view context);
    void reinitializeIfDirectIdentifier(const ast::Expr& expression);

    void pushScope();
    void popScope();
    void beginTemporaryBorrowFrame();
    void endTemporaryBorrowFrame();
    void releaseBorrow(const BorrowRecord& borrow);
    void registerBorrow(Symbol& symbol, bool is_mutable);

    [[nodiscard]] bool compatible(const Type& expected, const Type& actual, const ast::Expr* value = nullptr) const;
    [[nodiscard]] bool requireBool(const ast::Expr& expression, std::string_view context);
    [[nodiscard]] bool requireInteger(const ast::Expr& expression, std::string_view context);
    [[nodiscard]] static bool isComparisonOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isEqualityOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isLogicalOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isBitwiseOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isShiftOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isArithmeticOperator(TokenKind kind) noexcept;
    [[nodiscard]] static TokenKind compoundBaseOperator(TokenKind kind) noexcept;
    [[nodiscard]] static std::optional<std::size_t> repeatedArrayLength(const ast::Expr& expression);

    void recordType(const ast::Expr& expression, Type type);
    void error(SourceSpan span, std::string message);

    SymbolTable symbols_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<const ast::Expr*, Type> expression_types_;
    std::unordered_map<const ast::FunctionDecl*, Type> function_signatures_;
    std::unordered_map<std::string, GenericFunctionInfo> generic_functions_;
    std::vector<GenericInstantiation> generic_instantiations_;
    std::unordered_map<const ast::CallExpr*, std::string> resolved_call_targets_;
    std::unordered_map<const ast::CallExpr*, ast::ReceiverKind> resolved_call_receivers_;
    std::unordered_map<std::string, Type> generic_type_environment_;
    std::unordered_set<std::string> traits_;
    std::unordered_map<std::string, TraitInfo> trait_infos_;
    std::unordered_map<std::string, std::unordered_set<std::string>> trait_impls_;
    std::vector<GenericTraitImplInfo> generic_trait_impls_;
    std::vector<GenericMethodInfo> generic_methods_;
    std::unordered_map<std::string, StructInfo> structs_;
    std::unordered_map<std::string, EnumInfo> enums_;
    Type current_return_type_{Type::simple(TypeKind::Unit)};
    int loop_depth_{0};

    std::size_t next_symbol_id_{1};
    std::vector<std::vector<BorrowRecord>> borrow_scopes_;
    std::vector<std::vector<BorrowRecord>> temporary_borrow_frames_;
    bool persist_borrows_{false};
    bool lexical_ownership_checks_{false};
    std::unordered_set<std::string> return_reference_roots_;
};

} // namespace nus::sema
