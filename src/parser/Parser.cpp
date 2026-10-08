#include "nus/parser/Parser.hpp"

#include <memory>
#include <string>
#include <utility>

namespace nus {

Parser::Parser(const SourceManager& sources, std::vector<Token> tokens)
    : sources_(sources), tokens_(std::move(tokens)) {}

const std::vector<Diagnostic>& Parser::diagnostics() const noexcept {
    return diagnostics_;
}

bool Parser::hasErrors() const noexcept {
    return !diagnostics_.empty();
}

const Token& Parser::current() const noexcept {
    return tokens_[current_index_];
}

const Token& Parser::previous() const noexcept {
    return tokens_[current_index_ == 0 ? 0 : current_index_ - 1];
}

bool Parser::isAtEnd() const noexcept {
    return current().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind) const noexcept {
    return current().kind == kind;
}

bool Parser::checkNext(TokenKind kind) const noexcept {
    const auto next = current_index_ + 1;
    return next < tokens_.size() && tokens_[next].kind == kind;
}

const Token& Parser::advance() noexcept {
    if (!isAtEnd()) ++current_index_;
    return previous();
}

bool Parser::match(TokenKind kind) noexcept {
    if (!check(kind)) return false;
    advance();
    return true;
}

const Token& Parser::expect(TokenKind kind, std::string message) {
    if (check(kind)) return advance();
    errorAt(current(), std::move(message));
    if (!isAtEnd()) return advance();
    return current();
}

void Parser::errorAt(const Token& token, std::string message) {
    diagnostics_.push_back(Diagnostic{.message = std::move(message), .span = token.span});
}

void Parser::synchronizeTopLevel() {
    while (!isAtEnd()) {
        if (check(TokenKind::KwFn)) return;
        advance();
    }
}

void Parser::synchronizeStatement() {
    while (!isAtEnd()) {
        if (previous().kind == TokenKind::Semicolon) return;
        switch (current().kind) {
            case TokenKind::KwLet:
            case TokenKind::KwReturn:
            case TokenKind::KwIf:
            case TokenKind::RightBrace:
                return;
            default:
                advance();
        }
    }
}

ast::SourceFile Parser::parseSourceFile() {
    ast::SourceFile file;
    if (!tokens_.empty()) {
        file.span.file = tokens_.front().span.file;
        file.span.start = tokens_.front().span.start;
        file.span.end = tokens_.back().span.end;
    }

    while (!isAtEnd()) {
        if (!check(TokenKind::KwFn)) {
            errorAt(current(), "expected top-level declaration; milestone 2 currently supports `fn`");
            synchronizeTopLevel();
            continue;
        }

        if (auto function = parseFunction()) {
            file.functions.push_back(std::move(*function));
        } else {
            synchronizeTopLevel();
        }
    }

    return file;
}

std::optional<ast::FunctionDecl> Parser::parseFunction() {
    const auto start = expect(TokenKind::KwFn, "expected `fn`").span;
    const auto& name = expect(TokenKind::Identifier, "expected function name");
    expect(TokenKind::LeftParen, "expected `(` after function name");

    ast::FunctionDecl function;
    function.name = tokenText(name);

    if (!check(TokenKind::RightParen)) {
        do {
            function.parameters.push_back(parseParameter());
        } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
    }

    expect(TokenKind::RightParen, "expected `)` after parameters");

    if (match(TokenKind::Arrow)) {
        function.return_type = parseType();
    }

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected function body");
        return std::nullopt;
    }

    function.body = parseBlock();
    function.span = spanFrom(start, function.body->span);
    return function;
}

ast::Parameter Parser::parseParameter() {
    const auto& name = expect(TokenKind::Identifier, "expected parameter name");
    expect(TokenKind::Colon, "expected `:` after parameter name");
    auto type = parseType();

    ast::Parameter parameter;
    parameter.name = tokenText(name);
    parameter.span = spanFrom(name.span, type.span);
    parameter.type = std::move(type);
    return parameter;
}

ast::TypeRef Parser::parseType() {
    ast::TypeRef type;

    if (match(TokenKind::LeftParen)) {
        const auto start = previous().span;
        const auto end = expect(TokenKind::RightParen, "expected `)` for unit type").span;
        type.path.push_back("()");
        type.span = spanFrom(start, end);
        return type;
    }

    const auto& first = expect(TokenKind::Identifier, "expected type name");
    type.path.push_back(tokenText(first));
    type.span = first.span;

    while (match(TokenKind::ColonColon)) {
        const auto& part = expect(TokenKind::Identifier, "expected type path segment after `::`");
        type.path.push_back(tokenText(part));
        type.span = spanFrom(type.span, part.span);
    }

    return type;
}

std::unique_ptr<ast::BlockExpr> Parser::parseBlock() {
    const auto start = expect(TokenKind::LeftBrace, "expected `{`").span;
    auto block = std::make_unique<ast::BlockExpr>();

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (check(TokenKind::KwLet) || check(TokenKind::KwReturn)) {
            if (auto statement = parseStatement()) block->statements.push_back(std::move(statement));
            continue;
        }

        auto expression = parseExpression();
        if (!expression) {
            synchronizeStatement();
            continue;
        }

        if (match(TokenKind::Semicolon)) {
            auto statement = std::make_unique<ast::ExprStmt>();
            statement->span = spanFrom(expression->span, previous().span);
            statement->expression = std::move(expression);
            statement->has_semicolon = true;
            block->statements.push_back(std::move(statement));
            continue;
        }

        if (isBlockLike(*expression)) {
            auto statement = std::make_unique<ast::ExprStmt>();
            statement->span = expression->span;
            statement->expression = std::move(expression);
            statement->has_semicolon = false;
            block->statements.push_back(std::move(statement));
            continue;
        }

        if (check(TokenKind::RightBrace)) {
            block->tail_expression = std::move(expression);
            break;
        }

        errorAt(current(), "expected `;` after expression");
        synchronizeStatement();
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after block").span;
    block->span = spanFrom(start, end);
    return block;
}

ast::StmtPtr Parser::parseStatement() {
    if (check(TokenKind::KwLet)) return parseLetStatement();
    if (check(TokenKind::KwReturn)) return parseReturnStatement();
    return parseExpressionStatement();
}

ast::StmtPtr Parser::parseLetStatement() {
    const auto start = expect(TokenKind::KwLet, "expected `let`").span;
    const bool is_mutable = match(TokenKind::KwMut);
    const auto& name = expect(TokenKind::Identifier, "expected variable name");

    auto statement = std::make_unique<ast::LetStmt>();
    statement->name = tokenText(name);
    statement->is_mutable = is_mutable;

    if (match(TokenKind::Colon)) {
        statement->type = parseType();
    }

    expect(TokenKind::Equal, "expected `=` in variable declaration");
    statement->initializer = parseExpression();
    const auto end = expect(TokenKind::Semicolon, "expected `;` after variable declaration").span;
    statement->span = spanFrom(start, end);
    return statement;
}

ast::StmtPtr Parser::parseReturnStatement() {
    const auto start = expect(TokenKind::KwReturn, "expected `return`").span;
    auto statement = std::make_unique<ast::ReturnStmt>();

    if (!check(TokenKind::Semicolon)) {
        statement->value = parseExpression();
    }

    const auto end = expect(TokenKind::Semicolon, "expected `;` after return statement").span;
    statement->span = spanFrom(start, end);
    return statement;
}

ast::StmtPtr Parser::parseExpressionStatement() {
    auto expression = parseExpression();
    if (!expression) return nullptr;

    auto statement = std::make_unique<ast::ExprStmt>();
    statement->span = expression->span;
    statement->expression = std::move(expression);

    if (match(TokenKind::Semicolon)) {
        statement->span.end = previous().span.end;
        statement->has_semicolon = true;
    } else if (isBlockLike(*statement->expression)) {
        statement->has_semicolon = false;
    } else {
        errorAt(current(), "expected `;` after expression");
    }

    return statement;
}

ast::ExprPtr Parser::parseExpression(int min_precedence) {
    auto left = parsePrefix();
    if (!left) return nullptr;

    left = parsePostfix(std::move(left));

    while (true) {
        const auto op = current().kind;
        const int precedence = binaryPrecedence(op);
        if (precedence < min_precedence) break;

        advance();
        const int next_min = isRightAssociative(op) ? precedence : precedence + 1;
        auto right = parseExpression(next_min);
        if (!right) {
            errorAt(current(), "expected expression after binary operator");
            return left;
        }

        auto binary = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right));
        binary->span = spanFrom(binary->left->span, binary->right->span);
        left = std::move(binary);
    }

    return left;
}

ast::ExprPtr Parser::parsePrefix() {
    if (check(TokenKind::KwIf)) return parseIfExpression();

    if (isUnaryOperator(current().kind)) {
        const auto op = advance();
        auto operand = parseExpression(12);
        if (!operand) {
            errorAt(current(), "expected operand after unary operator");
            return nullptr;
        }
        auto unary = std::make_unique<ast::UnaryExpr>(op.kind, std::move(operand));
        unary->span = spanFrom(op.span, unary->operand->span);
        return unary;
    }

    return parsePrimary();
}

ast::ExprPtr Parser::parsePrimary() {
    const auto token = current();

    switch (token.kind) {
        case TokenKind::IntegerLiteral:
        case TokenKind::FloatLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::CharLiteral:
        case TokenKind::ByteLiteral:
        case TokenKind::ByteStringLiteral:
        case TokenKind::RawStringLiteral:
        case TokenKind::KwTrue:
        case TokenKind::KwFalse: {
            advance();
            auto expression = std::make_unique<ast::LiteralExpr>(token.kind, tokenText(token));
            expression->span = token.span;
            return expression;
        }
        case TokenKind::Identifier: {
            advance();
            auto expression = std::make_unique<ast::IdentifierExpr>(tokenText(token));
            expression->span = token.span;
            return expression;
        }
        case TokenKind::LeftParen: {
            const auto start = advance().span;
            auto expression = parseExpression();
            const auto end = expect(TokenKind::RightParen, "expected `)` after expression").span;
            if (expression) expression->span = spanFrom(start, end);
            return expression;
        }
        default:
            errorAt(token, "expected expression");
            if (!isAtEnd()) advance();
            return nullptr;
    }
}

ast::ExprPtr Parser::parsePostfix(ast::ExprPtr expression) {
    while (check(TokenKind::LeftParen)) {
        const auto start = expression->span;
        advance();

        auto call = std::make_unique<ast::CallExpr>(std::move(expression));
        if (!check(TokenKind::RightParen)) {
            do {
                auto argument = parseExpression();
                if (argument) call->arguments.push_back(std::move(argument));
            } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
        }

        const auto end = expect(TokenKind::RightParen, "expected `)` after arguments").span;
        call->span = spanFrom(start, end);
        expression = std::move(call);
    }

    return expression;
}

ast::ExprPtr Parser::parseIfExpression() {
    const auto start = expect(TokenKind::KwIf, "expected `if`").span;
    auto expression = std::make_unique<ast::IfExpr>();
    expression->condition = parseExpression();

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected `{` after if condition");
        return expression;
    }

    expression->then_branch = parseBlock();
    SourceSpan end = expression->then_branch->span;

    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::KwIf)) {
            expression->else_branch = parseIfExpression();
        } else if (check(TokenKind::LeftBrace)) {
            expression->else_branch = parseBlock();
        } else {
            errorAt(current(), "expected `if` or block after `else`");
        }
        if (expression->else_branch) end = expression->else_branch->span;
    }

    expression->span = spanFrom(start, end);
    return expression;
}

int Parser::binaryPrecedence(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Equal:
        case TokenKind::PlusEqual:
        case TokenKind::MinusEqual:
        case TokenKind::StarEqual:
        case TokenKind::SlashEqual:
        case TokenKind::PercentEqual:
        case TokenKind::AmpersandEqual:
        case TokenKind::PipeEqual:
        case TokenKind::CaretEqual:
        case TokenKind::ShiftLeftEqual:
        case TokenKind::ShiftRightEqual:
            return 1;
        case TokenKind::PipePipe: return 2;
        case TokenKind::AmpersandAmpersand: return 3;
        case TokenKind::Pipe: return 4;
        case TokenKind::Caret: return 5;
        case TokenKind::Ampersand: return 6;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
            return 7;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 8;
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            return 9;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 10;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return 11;
        default:
            return 0;
    }
}

bool Parser::isRightAssociative(TokenKind kind) noexcept {
    return binaryPrecedence(kind) == 1;
}

bool Parser::isUnaryOperator(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Bang:
        case TokenKind::Tilde:
        case TokenKind::Minus:
        case TokenKind::Ampersand:
        case TokenKind::Star:
        case TokenKind::KwAwait:
            return true;
        default:
            return false;
    }
}

bool Parser::isBlockLike(const ast::Expr& expression) noexcept {
    return expression.kind == ast::ExprKind::If || expression.kind == ast::ExprKind::Block;
}

SourceSpan Parser::spanFrom(SourceSpan first, SourceSpan last) const noexcept {
    return SourceSpan{.file = first.file, .start = first.start, .end = last.end};
}

std::string Parser::tokenText(const Token& token) const {
    return std::string(sources_.text(token.span));
}

} // namespace nus
