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
        if (check(TokenKind::KwFn) || check(TokenKind::KwStruct) || check(TokenKind::KwEnum) || check(TokenKind::KwTrait) || check(TokenKind::KwImpl)) return;
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
        if (check(TokenKind::KwFn) || (check(TokenKind::KwAsync) && checkNext(TokenKind::KwFn))) {
            if (auto function = parseFunction()) file.functions.push_back(std::move(*function));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwStruct)) {
            if (auto structure = parseStruct()) file.structs.push_back(std::move(*structure));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwEnum)) {
            if (auto enumeration = parseEnum()) file.enums.push_back(std::move(*enumeration));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwTrait)) {
            if (auto trait = parseTrait()) file.traits.push_back(std::move(*trait));
            else synchronizeTopLevel();
            continue;
        }
        if (check(TokenKind::KwImpl)) {
            if (auto implementation = parseImpl()) file.impls.push_back(std::move(*implementation));
            else synchronizeTopLevel();
            continue;
        }

        errorAt(current(), "expected top-level declaration (`fn`, `async fn`, `struct`, `enum`, `trait`, or `impl`)");
        synchronizeTopLevel();
    }

    return file;
}

std::optional<ast::FunctionDecl> Parser::parseFunction(bool allow_receiver, bool require_body) {
    SourceSpan start{};
    bool is_async = false;
    if (match(TokenKind::KwAsync)) {
        start = previous().span;
        is_async = true;
        expect(TokenKind::KwFn, "expected `fn` after `async`");
    } else {
        start = expect(TokenKind::KwFn, "expected `fn`").span;
    }
    const auto& name = expect(TokenKind::Identifier, "expected function name");

    ast::FunctionDecl function;
    function.name = tokenText(name);
    function.is_async = is_async;
    function.generic_parameters = parseGenericParameters();

    expect(TokenKind::LeftParen, "expected `(` after function name");

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

    if (!require_body) {
        const auto end = expect(TokenKind::Semicolon, "expected `;` after trait method signature").span;
        function.span = spanFrom(start, end);
        return function;
    }

    if (!check(TokenKind::LeftBrace)) {
        errorAt(current(), "expected function body");
        return std::nullopt;
    }

    function.body = parseBlock();
    function.span = spanFrom(start, function.body->span);
    return function;
}

std::vector<ast::GenericParameter> Parser::parseGenericParameters() {
    std::vector<ast::GenericParameter> parameters;
    if (!match(TokenKind::Less)) return parameters;
    const auto start = previous().span;
    if (check(TokenKind::Greater)) {
        errorAt(current(), "generic parameter list cannot be empty");
        advance();
        return parameters;
    }
    do {
        const auto& name = expect(TokenKind::Identifier, "expected generic parameter name");
        ast::GenericParameter parameter;
        parameter.name = tokenText(name);
        parameter.span = name.span;
        if (match(TokenKind::Colon)) {
            do {
                const auto& bound = expect(TokenKind::Identifier, "expected trait bound");
                parameter.bounds.push_back(tokenText(bound));
                parameter.span = spanFrom(parameter.span, bound.span);
            } while (match(TokenKind::Plus));
        }
        parameters.push_back(std::move(parameter));
    } while (match(TokenKind::Comma));
    const auto end = expect(TokenKind::Greater, "expected `>` after generic parameters").span;
    (void)start; (void)end;
    return parameters;
}

std::vector<ast::TypeRef> Parser::parseExplicitGenericArguments() {
    std::vector<ast::TypeRef> arguments;
    expect(TokenKind::ColonColon, "expected `::` before explicit generic arguments");
    expect(TokenKind::Less, "expected `<` after `::`");
    if (!check(TokenKind::Greater)) {
        do {
            arguments.push_back(parseType());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::Greater, "expected `>` after explicit generic arguments");
    return arguments;
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

std::optional<ast::EnumDecl> Parser::parseEnum() {
    const auto start = expect(TokenKind::KwEnum, "expected `enum`").span;
    const auto& name = expect(TokenKind::Identifier, "expected enum name");
    expect(TokenKind::LeftBrace, "expected `{` after enum name");

    ast::EnumDecl declaration;
    declaration.name = tokenText(name);

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        const auto& variant_name = expect(TokenKind::Identifier, "expected enum variant name");
        ast::EnumVariantDecl variant;
        variant.name = tokenText(variant_name);
        variant.span = variant_name.span;
        if (match(TokenKind::LeftParen)) {
            if (!check(TokenKind::RightParen)) {
                do {
                    variant.payload_types.push_back(parseType());
                } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
            }
            const auto end = expect(TokenKind::RightParen, "expected `)` after enum variant payload").span;
            variant.span = spanFrom(variant.span, end);
        }
        declaration.variants.push_back(std::move(variant));
        if (!match(TokenKind::Comma) && !check(TokenKind::RightBrace)) {
            errorAt(current(), "expected `,` or `}` after enum variant");
            synchronizeStatement();
        }
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after enum declaration").span;
    declaration.span = spanFrom(start, end);
    return declaration;
}

std::optional<ast::TraitDecl> Parser::parseTrait() {
    const auto start = expect(TokenKind::KwTrait, "expected `trait`").span;
    const auto& name = expect(TokenKind::Identifier, "expected trait name");
    ast::TraitDecl declaration;
    declaration.name = tokenText(name);
    expect(TokenKind::LeftBrace, "expected `{` after trait name");

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (match(TokenKind::KwType)) {
            const auto type_start = previous().span;
            const auto& assoc = expect(TokenKind::Identifier, "expected associated type name");
            ast::AssociatedTypeDecl item;
            item.name = tokenText(assoc);
            const auto finish = expect(TokenKind::Semicolon, "expected `;` after associated type declaration").span;
            item.span = spanFrom(type_start, finish);
            declaration.associated_types.push_back(std::move(item));
            continue;
        }
        if (check(TokenKind::KwFn)) {
            if (auto method = parseFunction(true, false)) declaration.methods.push_back(std::move(*method));
            continue;
        }
        errorAt(current(), "expected associated type or method signature inside trait");
        synchronizeStatement();
    }

    const auto end = expect(TokenKind::RightBrace, "expected `}` after trait declaration").span;
    declaration.span = spanFrom(start, end);
    return declaration;
}

std::optional<ast::ImplDecl> Parser::parseImpl() {
    const auto start = expect(TokenKind::KwImpl, "expected `impl`").span;
    ast::ImplDecl declaration;
    declaration.generic_parameters = parseGenericParameters();
    auto first = parseType();
    if (match(TokenKind::KwFor)) {
        declaration.trait = std::move(first);
        declaration.target = parseType();
    } else {
        declaration.target = std::move(first);
    }
    expect(TokenKind::LeftBrace, "expected `{` after impl target");

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (match(TokenKind::KwType)) {
            const auto type_start = previous().span;
            const auto& assoc = expect(TokenKind::Identifier, "expected associated type name");
            expect(TokenKind::Equal, "expected `=` in associated type binding");
            ast::AssociatedTypeDecl binding;
            binding.name = tokenText(assoc);
            binding.value = parseType();
            const auto finish = expect(TokenKind::Semicolon, "expected `;` after associated type binding").span;
            binding.span = spanFrom(type_start, finish);
            declaration.associated_types.push_back(std::move(binding));
            continue;
        }
        if (!check(TokenKind::KwFn)) {
            errorAt(current(), "expected associated type binding or method declaration inside `impl`");
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
    SourceSpan start{};

    if (match(TokenKind::Ampersand)) {
        start = previous().span;
        type.is_reference = true;
        type.is_mutable_reference = match(TokenKind::KwMut);
    }

    if (match(TokenKind::LeftParen)) {
        const auto unit_start = previous().span;
        const auto end = expect(TokenKind::RightParen, "expected `)` for unit type").span;
        type.path.push_back("()");
        type.span = type.is_reference ? spanFrom(start, end) : spanFrom(unit_start, end);
        return type;
    }

    const auto& first = expect(TokenKind::Identifier, "expected type name");
    type.path.push_back(tokenText(first));
    type.span = type.is_reference ? spanFrom(start, first.span) : first.span;

    while (match(TokenKind::ColonColon)) {
        const auto& part = expect(TokenKind::Identifier, "expected type path segment after `::`");
        type.path.push_back(tokenText(part));
        type.span = spanFrom(type.span, part.span);
    }

    if (match(TokenKind::Less)) {
        do {
            type.generic_arguments.push_back(parseType());
        } while (match(TokenKind::Comma));
        const auto end = expect(TokenKind::Greater, "expected `>` after generic type arguments").span;
        type.span = spanFrom(type.span, end);
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
    if (check(TokenKind::KwMatch)) return parseMatchExpression();

    if (isUnaryOperator(current().kind)) {
        const auto op = advance();
        const bool mutable_borrow = op.kind == TokenKind::Ampersand && match(TokenKind::KwMut);
        auto operand = parseExpression(13);
        if (!operand) {
            errorAt(current(), "expected operand after unary operator");
            return nullptr;
        }
        auto unary = std::make_unique<ast::UnaryExpr>(op.kind, std::move(operand), mutable_borrow);
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
            if (check(TokenKind::ColonColon) && !checkNext(TokenKind::Less)) {
                advance();
                const auto& variant_token = expect(TokenKind::Identifier, "expected variant name after `::`");
                auto expression = std::make_unique<ast::VariantExpr>(tokenText(token), tokenText(variant_token));
                SourceSpan end = variant_token.span;
                if (match(TokenKind::LeftParen)) {
                    if (!check(TokenKind::RightParen)) {
                        do {
                            auto payload = parseExpression();
                            if (payload) expression->payloads.push_back(std::move(payload));
                        } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
                    }
                    end = expect(TokenKind::RightParen, "expected `)` after enum variant payload").span;
                }
                expression->span = spanFrom(token.span, end);
                return expression;
            }
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
        if (check(TokenKind::ColonColon) && checkNext(TokenKind::Less)) {
            const auto start = expression->span;
            auto generic_arguments = parseExplicitGenericArguments();
            expect(TokenKind::LeftParen, "expected `(` after explicit generic arguments");
            auto call = std::make_unique<ast::CallExpr>(std::move(expression));
            call->generic_arguments = std::move(generic_arguments);
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

        if (match(TokenKind::Question)) {
            const auto end = previous().span;
            auto try_expr = std::make_unique<ast::TryExpr>(std::move(expression));
            try_expr->span = spanFrom(try_expr->operand->span, end);
            expression = std::move(try_expr);
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

ast::Pattern Parser::parsePattern() {
    ast::Pattern pattern;
    const auto start = current().span;
    const auto& first = expect(TokenKind::Identifier, "expected pattern");
    const std::string first_name = tokenText(first);
    pattern.span = first.span;
    if (first_name == "_") {
        pattern.kind = ast::PatternKind::Wildcard;
        pattern.name = "_";
        return pattern;
    }

    if (match(TokenKind::ColonColon)) {
        const auto& variant = expect(TokenKind::Identifier, "expected variant name after `::`");
        pattern.kind = ast::PatternKind::Variant;
        pattern.enum_name = first_name;
        pattern.variant = tokenText(variant);
        pattern.span = spanFrom(start, variant.span);
    } else if (check(TokenKind::LeftParen)) {
        pattern.kind = ast::PatternKind::Variant;
        pattern.variant = first_name;
    } else {
        pattern.kind = ast::PatternKind::Name;
        pattern.name = first_name;
        return pattern;
    }

    if (match(TokenKind::LeftParen)) {
        if (!check(TokenKind::RightParen)) {
            do {
                pattern.elements.push_back(parsePattern());
            } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
        }
        const auto end = expect(TokenKind::RightParen, "expected `)` after variant pattern").span;
        pattern.span = spanFrom(start, end);
    }
    return pattern;
}

ast::ExprPtr Parser::parseMatchExpression() {
    const auto start = expect(TokenKind::KwMatch, "expected `match`").span;
    auto expression = std::make_unique<ast::MatchExpr>();
    expression->scrutinee = parseExpression();
    expect(TokenKind::LeftBrace, "expected `{` after match value");

    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        ast::MatchArm arm;
        arm.pattern = parsePattern();
        expect(TokenKind::FatArrow, "expected `=>` after match pattern");
        arm.value = parseExpression();
        arm.span = arm.value ? spanFrom(arm.pattern.span, arm.value->span) : arm.pattern.span;
        expression->arms.push_back(std::move(arm));
        if (!match(TokenKind::Comma) && !check(TokenKind::RightBrace)) {
            errorAt(current(), "expected `,` or `}` after match arm");
            break;
        }
    }
    const auto end = expect(TokenKind::RightBrace, "expected `}` after match expression").span;
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
    if (expression.kind == ast::ExprKind::Identifier ||
        expression.kind == ast::ExprKind::Member ||
        expression.kind == ast::ExprKind::Index) {
        return true;
    }
    if (expression.kind == ast::ExprKind::Unary) {
        return static_cast<const ast::UnaryExpr&>(expression).op == TokenKind::Star;
    }
    return false;
}

bool Parser::isBlockLike(const ast::Expr& expression) noexcept {
    switch (expression.kind) {
        case ast::ExprKind::If:
        case ast::ExprKind::Block:
        case ast::ExprKind::While:
        case ast::ExprKind::Loop:
        case ast::ExprKind::For:
        case ast::ExprKind::Match:
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
