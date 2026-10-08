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
        if (check(TokenKind::KwFn) || check(TokenKind::KwStruct) || check(TokenKind::KwImpl)) return;
        advance();
    }
}

void Parser::synchronizeStatement() {
    while (!isAtEnd()) {
        if (previous().kind == TokenKind::Semicolon) return;
        switch (current().kind) {
            case TokenKind::KwLet:
            case TokenKind::KwReturn:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwLoop:
            case TokenKind::KwFor:
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
        if (check(TokenKind::KwFn)) {
            if (auto function = parseFunction()) file.functions.push_back(std::move(*function));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwStruct)) {
            if (auto structure = parseStruct()) file.structs.push_back(std::move(*structure));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwImpl)) {
            if (auto implementation = parseImpl()) file.impls.push_back(std::move(*implementation));
            else synchronizeTopLevel();
            continue;
        }

        errorAt(current(), "expected top-level declaration (`fn`, `struct`, or `impl`)");
        synchronizeTopLevel();
    }

    return file;
}

std::optional<ast::FunctionDecl> Parser::parseFunction(bool allow_receiver) {
    const auto start = expect(TokenKind::KwFn, "expected `fn`").span;
    const auto& name = expect(TokenKind::Identifier, "expected function name");
    expect(TokenKind::LeftParen, "expected `(` after function name");

    ast::FunctionDecl function;
    function.name = tokenText(name);

    if (!check(TokenKind::RightParen)) {
        bool parsed_receiver = false;
        if (allow_receiver) parsed_receiver = parseReceiver(function);

        if (parsed_receiver) {
            if (match(TokenKind::Comma) && !check(TokenKind::RightParen)) {
                do {
                    function.parameters.push_back(parseParameter());
                } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
            }
        } else {
            do {
                function.parameters.push_back(parseParameter());
            } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
        }
    }

    expect(TokenKind::RightParen, "expected `)` after parameters");

    if (match(TokenKind::Arrow)) function.return_type = parseType();

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected function body");
        return std::nullopt;
    }

    function.body = parseBlock();
    function.span = spanFrom(start, function.body->span);
    return function;
}

bool Parser::parseReceiver(ast::FunctionDecl& function) {
    if (check(TokenKind::Identifier) && tokenText(current()) == "self") {
        const auto token = advance();
        function.receiver = ast::ReceiverKind::Value;
        function.receiver_span = token.span;
        return true;
    }

    if (!check(TokenKind::Ampersand)) return false;
    const auto start = advance().span;
    const bool is_mutable = match(TokenKind::KwMut);
    if (!check(TokenKind::Identifier) || tokenText(current()) != "self") {
        errorAt(current(), "expected `self` after receiver reference");
        return true;
    }
    const auto self_token = advance();
    function.receiver = is_mutable ? ast::ReceiverKind::MutableReference : ast::ReceiverKind::Reference;
    function.receiver_span = spanFrom(start, self_token.span);
    return true;
}

std::optional<ast::StructDecl> Parser::parseStruct() {
    const auto start = expect(TokenKind::KwStruct, "expected `struct`").span;
    const auto& name = expect(TokenKind::Identifier, "expected struct name");
    expect(TokenKind::LeftBrace, "expected `{` after struct name");

    ast::StructDecl declaration;
    declaration.name = tokenText(name);

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        const auto& field_name = expect(TokenKind::Identifier, "expected field name");
        expect(TokenKind::Colon, "expected `:` after field name");
        auto type = parseType();

        ast::StructFieldDecl field;
        field.name = tokenText(field_name);
        field.span = spanFrom(field_name.span, type.span);
        field.type = std::move(type);
        declaration.fields.push_back(std::move(field));

        if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon) && !check(TokenKind::RightBrace)) {
            errorAt(current(), "expected `,`, `;`, or `}` after struct field");
            synchronizeStatement();
        }
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after struct declaration").span;
    declaration.span = spanFrom(start, end);
    return declaration;
}

std::optional<ast::ImplDecl> Parser::parseImpl() {
    const auto start = expect(TokenKind::KwImpl, "expected `impl`").span;
    ast::ImplDecl declaration;
    declaration.target = parseType();
    expect(TokenKind::LeftBrace, "expected `{` after impl target");

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (!check(TokenKind::KwFn)) {
            errorAt(current(), "expected method declaration inside `impl`");
            synchronizeTopLevel();
            if (check(TokenKind::RightBrace)) break;
            continue;
        }
        if (auto method = parseFunction(true)) declaration.methods.push_back(std::move(*method));
        else synchronizeTopLevel();
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after impl block").span;
    declaration.span = spanFrom(start, end);
    return declaration;
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
        if (check(TokenKind::KwLet) || check(TokenKind::KwReturn) ||
            check(TokenKind::KwBreak) || check(TokenKind::KwContinue)) {
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
    if (check(TokenKind::KwBreak)) return parseBreakStatement();
    if (check(TokenKind::KwContinue)) return parseContinueStatement();
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

ast::StmtPtr Parser::parseBreakStatement() {
    const auto start = expect(TokenKind::KwBreak, "expected `break`").span;
    auto statement = std::make_unique<ast::BreakStmt>();

    if (!check(TokenKind::Semicolon)) {
        statement->value = parseExpression();
    }

    const auto end = expect(TokenKind::Semicolon, "expected `;` after break statement").span;
    statement->span = spanFrom(start, end);
    return statement;
}

ast::StmtPtr Parser::parseContinueStatement() {
    const auto start = expect(TokenKind::KwContinue, "expected `continue`").span;
    const auto end = expect(TokenKind::Semicolon, "expected `;` after continue statement").span;

    auto statement = std::make_unique<ast::ContinueStmt>();
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

        const auto op_token = advance();
        const int next_min = isRightAssociative(op) ? precedence : precedence + 1;
        auto right = parseExpression(next_min);
        if (!right) {
            errorAt(current(), "expected expression after operator");
            return left;
        }

        if (isAssignmentOperator(op)) {
            if (!isAssignable(*left)) {
                errorAt(op_token, "left side of assignment is not assignable");
            }
            auto assignment = std::make_unique<ast::AssignmentExpr>(op, std::move(left), std::move(right));
            assignment->span = spanFrom(assignment->target->span, assignment->value->span);
            left = std::move(assignment);
            continue;
        }

        if (isRangeOperator(op)) {
            if (left->kind == ast::ExprKind::Range) {
                errorAt(op_token, "range operators are not associative");
            }
            auto range = std::make_unique<ast::RangeExpr>(
                std::move(left), std::move(right), op == TokenKind::DotDotEqual);
            range->span = spanFrom(range->start->span, range->end->span);
            left = std::move(range);
            continue;
        }

        auto binary = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right));
        binary->span = spanFrom(binary->left->span, binary->right->span);
        left = std::move(binary);
    }

    return left;
}

ast::ExprPtr Parser::parsePrefix() {
    if (check(TokenKind::KwIf)) return parseIfExpression();
    if (check(TokenKind::KwWhile)) return parseWhileExpression();
    if (check(TokenKind::KwLoop)) return parseLoopExpression();
    if (check(TokenKind::KwFor)) return parseForExpression();

    if (isUnaryOperator(current().kind)) {
        const auto op = advance();
        auto operand = parseExpression(13);
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
            if (looksLikeStructLiteral()) return parseStructLiteral(token);
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
        case TokenKind::LeftBracket:
            return parseArrayExpression();
        default:
            errorAt(token, "expected expression");
            if (!isAtEnd()) advance();
            return nullptr;
    }
}

ast::ExprPtr Parser::parseArrayExpression() {
    const auto start = expect(TokenKind::LeftBracket, "expected `[`").span;
    auto array = std::make_unique<ast::ArrayExpr>();

    if (match(TokenKind::RightBracket)) {
        array->span = spanFrom(start, previous().span);
        return array;
    }

    auto first = parseExpression();
    if (!first) {
        expect(TokenKind::RightBracket, "expected `]` after array expression");
        array->span = spanFrom(start, previous().span);
        return array;
    }

    if (match(TokenKind::Semicolon)) {
        array->repeat_value = std::move(first);
        array->repeat_count = parseExpression();
        const auto end = expect(TokenKind::RightBracket, "expected `]` after repeated array").span;
        array->span = spanFrom(start, end);
        return array;
    }

    array->elements.push_back(std::move(first));
    while (match(TokenKind::Comma)) {
        if (check(TokenKind::RightBracket)) break;
        auto element = parseExpression();
        if (!element) break;
        array->elements.push_back(std::move(element));
    }

    const auto end = expect(TokenKind::RightBracket, "expected `]` after array literal").span;
    array->span = spanFrom(start, end);
    return array;
}

bool Parser::looksLikeStructLiteral() const noexcept {
    if (!check(TokenKind::LeftBrace)) return false;
    const auto first = current_index_ + 1;
    const auto second = current_index_ + 2;
    return second < tokens_.size() && tokens_[first].kind == TokenKind::Identifier &&
           tokens_[second].kind == TokenKind::Colon;
}

ast::ExprPtr Parser::parseStructLiteral(const Token& type_name) {
    ast::TypeRef type;
    type.path.push_back(tokenText(type_name));
    type.span = type_name.span;
    auto literal = std::make_unique<ast::StructLiteralExpr>(std::move(type));
    const auto start = type_name.span;
    expect(TokenKind::LeftBrace, "expected `{` after struct type");

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        const auto& field_name = expect(TokenKind::Identifier, "expected struct field name");
        expect(TokenKind::Colon, "expected `:` after struct field name");
        auto value = parseExpression();

        ast::StructFieldInit field;
        field.name = tokenText(field_name);
        field.span = value ? spanFrom(field_name.span, value->span) : field_name.span;
        field.value = std::move(value);
        literal->fields.push_back(std::move(field));

        if (!match(TokenKind::Comma) && !check(TokenKind::RightBrace)) {
            errorAt(current(), "expected `,` or `}` after struct field initializer");
            break;
        }
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after struct literal").span;
    literal->span = spanFrom(start, end);
    return literal;
}

ast::ExprPtr Parser::parsePostfix(ast::ExprPtr expression) {
    while (true) {
        if (match(TokenKind::LeftParen)) {
            const auto start = expression->span;
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
            continue;
        }

        if (match(TokenKind::Dot)) {
            const auto start = expression->span;
            const auto& member = expect(TokenKind::Identifier, "expected member name after `.`");
            auto member_expr = std::make_unique<ast::MemberExpr>(std::move(expression), tokenText(member));
            member_expr->span = spanFrom(start, member.span);
            expression = std::move(member_expr);
            continue;
        }

        if (match(TokenKind::LeftBracket)) {
            const auto start = expression->span;
            auto index = parseExpression();
            const auto end = expect(TokenKind::RightBracket, "expected `]` after index expression").span;
            auto index_expr = std::make_unique<ast::IndexExpr>(std::move(expression), std::move(index));
            index_expr->span = spanFrom(start, end);
            expression = std::move(index_expr);
            continue;
        }

        break;
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

ast::ExprPtr Parser::parseWhileExpression() {
    const auto start = expect(TokenKind::KwWhile, "expected `while`").span;
    auto expression = std::make_unique<ast::WhileExpr>();
    expression->condition = parseExpression();

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected `{` after while condition");
        return expression;
    }

    expression->body = parseBlock();
    expression->span = spanFrom(start, expression->body->span);
    return expression;
}

ast::ExprPtr Parser::parseLoopExpression() {
    const auto start = expect(TokenKind::KwLoop, "expected `loop`").span;
    auto expression = std::make_unique<ast::LoopExpr>();

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected `{` after `loop`");
        return expression;
    }

    expression->body = parseBlock();
    expression->span = spanFrom(start, expression->body->span);
    return expression;
}

ast::ExprPtr Parser::parseForExpression() {
    const auto start = expect(TokenKind::KwFor, "expected `for`").span;
    const auto& binding = expect(TokenKind::Identifier, "expected loop binding after `for`");
    expect(TokenKind::KwIn, "expected `in` after for-loop binding");

    auto expression = std::make_unique<ast::ForExpr>();
    expression->binding = tokenText(binding);
    expression->iterable = parseExpression();

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected `{` after for-loop iterable");
        return expression;
    }

    expression->body = parseBlock();
    expression->span = spanFrom(start, expression->body->span);
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
        case TokenKind::DotDot:
        case TokenKind::DotDotEqual:
            return 2;
        case TokenKind::PipePipe: return 3;
        case TokenKind::AmpersandAmpersand: return 4;
        case TokenKind::Pipe: return 5;
        case TokenKind::Caret: return 6;
        case TokenKind::Ampersand: return 7;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
            return 8;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 9;
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            return 10;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 11;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return 12;
        default:
            return 0;
    }
}

bool Parser::isRightAssociative(TokenKind kind) noexcept {
    return isAssignmentOperator(kind);
}

bool Parser::isAssignmentOperator(TokenKind kind) noexcept {
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
            return true;
        default:
            return false;
    }
}

bool Parser::isRangeOperator(TokenKind kind) noexcept {
    return kind == TokenKind::DotDot || kind == TokenKind::DotDotEqual;
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

bool Parser::isAssignable(const ast::Expr& expression) noexcept {
    return expression.kind == ast::ExprKind::Identifier ||
           expression.kind == ast::ExprKind::Member ||
           expression.kind == ast::ExprKind::Index;
}

bool Parser::isBlockLike(const ast::Expr& expression) noexcept {
    switch (expression.kind) {
        case ast::ExprKind::If:
        case ast::ExprKind::Block:
        case ast::ExprKind::While:
        case ast::ExprKind::Loop:
        case ast::ExprKind::For:
            return true;
        default:
            return false;
    }
}

SourceSpan Parser::spanFrom(SourceSpan first, SourceSpan last) const noexcept {
    return SourceSpan{.file = first.file, .start = first.start, .end = last.end};
}

std::string Parser::tokenText(const Token& token) const {
    return std::string(sources_.text(token.span));
}

} // namespace nus
