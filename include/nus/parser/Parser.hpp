#pragma once

#include "nus/ast/Ast.hpp"
#include "nus/diagnostics/Diagnostic.hpp"
#include "nus/lexer/Token.hpp"
#include "nus/source/SourceManager.hpp"

#include <optional>
#include <vector>

namespace nus {

class Parser {
public:
    Parser(const SourceManager& sources, std::vector<Token> tokens);

    [[nodiscard]] ast::SourceFile parseSourceFile();
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
    [[nodiscard]] bool hasErrors() const noexcept;

private:
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    [[nodiscard]] bool isAtEnd() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    [[nodiscard]] bool checkNext(TokenKind kind) const noexcept;
    const Token& advance() noexcept;
    bool match(TokenKind kind) noexcept;
    const Token& expect(TokenKind kind, std::string message);

    void errorAt(const Token& token, std::string message);
    void synchronizeTopLevel();
    void synchronizeStatement();

    [[nodiscard]] std::optional<ast::FunctionDecl> parseFunction();
    [[nodiscard]] ast::Parameter parseParameter();
    [[nodiscard]] ast::TypeRef parseType();
    [[nodiscard]] std::unique_ptr<ast::BlockExpr> parseBlock();
    [[nodiscard]] ast::StmtPtr parseStatement();
    [[nodiscard]] ast::StmtPtr parseLetStatement();
    [[nodiscard]] ast::StmtPtr parseReturnStatement();
    [[nodiscard]] ast::StmtPtr parseBreakStatement();
    [[nodiscard]] ast::StmtPtr parseContinueStatement();
    [[nodiscard]] ast::StmtPtr parseExpressionStatement();

    [[nodiscard]] ast::ExprPtr parseExpression(int min_precedence = 1);
    [[nodiscard]] ast::ExprPtr parsePrefix();
    [[nodiscard]] ast::ExprPtr parsePrimary();
    [[nodiscard]] ast::ExprPtr parseArrayExpression();
    [[nodiscard]] ast::ExprPtr parsePostfix(ast::ExprPtr expression);
    [[nodiscard]] ast::ExprPtr parseIfExpression();
    [[nodiscard]] ast::ExprPtr parseWhileExpression();
    [[nodiscard]] ast::ExprPtr parseLoopExpression();
    [[nodiscard]] ast::ExprPtr parseForExpression();

    [[nodiscard]] static int binaryPrecedence(TokenKind kind) noexcept;
    [[nodiscard]] static bool isRightAssociative(TokenKind kind) noexcept;
    [[nodiscard]] static bool isAssignmentOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isRangeOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isUnaryOperator(TokenKind kind) noexcept;
    [[nodiscard]] static bool isAssignable(const ast::Expr& expression) noexcept;
    [[nodiscard]] static bool isBlockLike(const ast::Expr& expression) noexcept;
    [[nodiscard]] SourceSpan spanFrom(SourceSpan first, SourceSpan last) const noexcept;
    [[nodiscard]] std::string tokenText(const Token& token) const;

    const SourceManager& sources_;
    std::vector<Token> tokens_;
    std::size_t current_index_{0};
    std::vector<Diagnostic> diagnostics_;
};

} // namespace nus
