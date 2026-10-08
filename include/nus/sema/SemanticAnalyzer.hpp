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
#include <vector>

namespace nus::sema {

class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    void analyze(const ast::SourceFile& file);

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] const Type* typeOf(const ast::Expr& expression) const noexcept;

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

    struct StructInfo {
        const ast::StructDecl* declaration{nullptr};
        std::unordered_map<std::string, Type> fields;
        std::unordered_map<std::string, MethodInfo> methods;
    };

    struct BorrowRecord {
        std::size_t symbol_id{0};
        bool is_mutable{false};
    };

    void reset();
    void registerBuiltins();
    void collectStructNames(const ast::SourceFile& file);
    void collectStructFields(const ast::SourceFile& file);
    void collectFunctions(const ast::SourceFile& file);
    void collectMethods(const ast::SourceFile& file);
    void analyzeFunction(const ast::FunctionDecl& function);
    void analyzeMethod(const std::string& target_name, const ast::FunctionDecl& method);

    [[nodiscard]] Type resolveType(const ast::TypeRef& type_ref);
    [[nodiscard]] Type checkBlock(const ast::BlockExpr& block, bool create_scope = true);
    void checkStatement(const ast::Stmt& statement);
    [[nodiscard]] Type checkExpr(const ast::Expr& expression);
    [[nodiscard]] Type checkLiteral(const ast::LiteralExpr& expression);
    [[nodiscard]] Type checkUnary(const ast::UnaryExpr& expression);
    [[nodiscard]] Type checkBinary(const ast::BinaryExpr& expression);
    [[nodiscard]] Type checkAssignment(const ast::AssignmentExpr& expression);
    [[nodiscard]] Type checkCall(const ast::CallExpr& expression);
    [[nodiscard]] Type checkMethodCall(const ast::CallExpr& expression, const ast::MemberExpr& member);
    [[nodiscard]] Type checkMember(const ast::MemberExpr& expression);
    [[nodiscard]] Type checkIndex(const ast::IndexExpr& expression);
    [[nodiscard]] Type checkArray(const ast::ArrayExpr& expression);
    [[nodiscard]] Type checkRange(const ast::RangeExpr& expression);
    [[nodiscard]] Type checkStructLiteral(const ast::StructLiteralExpr& expression);
    [[nodiscard]] Type checkIf(const ast::IfExpr& expression);
    [[nodiscard]] Type checkWhile(const ast::WhileExpr& expression);
    [[nodiscard]] Type checkLoop(const ast::LoopExpr& expression);
    [[nodiscard]] Type checkFor(const ast::ForExpr& expression);

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
    std::unordered_map<std::string, StructInfo> structs_;
    Type current_return_type_{Type::simple(TypeKind::Unit)};
    int loop_depth_{0};

    std::size_t next_symbol_id_{1};
    std::vector<std::vector<BorrowRecord>> borrow_scopes_;
    std::vector<std::vector<BorrowRecord>> temporary_borrow_frames_;
    bool persist_borrows_{false};
};

} // namespace nus::sema
