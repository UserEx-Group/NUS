#include "nus/lexer/Lexer.hpp"

#include <array>
#include <cctype>
#include <utility>

namespace nus {
namespace {

using Keyword = std::pair<std::string_view, TokenKind>;

constexpr std::array keywords{
    Keyword{"as", TokenKind::KwAs},
    Keyword{"async", TokenKind::KwAsync},
    Keyword{"await", TokenKind::KwAwait},
    Keyword{"break", TokenKind::KwBreak},
    Keyword{"const", TokenKind::KwConst},
    Keyword{"continue", TokenKind::KwContinue},
    Keyword{"defer", TokenKind::KwDefer},
    Keyword{"else", TokenKind::KwElse},
    Keyword{"enum", TokenKind::KwEnum},
    Keyword{"extern", TokenKind::KwExtern},
    Keyword{"false", TokenKind::KwFalse},
    Keyword{"fn", TokenKind::KwFn},
    Keyword{"for", TokenKind::KwFor},
    Keyword{"if", TokenKind::KwIf},
    Keyword{"impl", TokenKind::KwImpl},
    Keyword{"in", TokenKind::KwIn},
    Keyword{"let", TokenKind::KwLet},
    Keyword{"link", TokenKind::KwLink},
    Keyword{"loop", TokenKind::KwLoop},
    Keyword{"match", TokenKind::KwMatch},
    Keyword{"move", TokenKind::KwMove},
    Keyword{"mut", TokenKind::KwMut},
    Keyword{"network", TokenKind::KwNetwork},
    Keyword{"node", TokenKind::KwNode},
    Keyword{"protocol", TokenKind::KwProtocol},
    Keyword{"pub", TokenKind::KwPub},
    Keyword{"ref", TokenKind::KwRef},
    Keyword{"return", TokenKind::KwReturn},
    Keyword{"scope", TokenKind::KwScope},
    Keyword{"service", TokenKind::KwService},
    Keyword{"simulate", TokenKind::KwSimulate},
    Keyword{"spawn", TokenKind::KwSpawn},
    Keyword{"static", TokenKind::KwStatic},
    Keyword{"struct", TokenKind::KwStruct},
    Keyword{"trait", TokenKind::KwTrait},
    Keyword{"true", TokenKind::KwTrue},
    Keyword{"type", TokenKind::KwType},
    Keyword{"unsafe", TokenKind::KwUnsafe},
    Keyword{"use", TokenKind::KwUse},
    Keyword{"where", TokenKind::KwWhere},
    Keyword{"while", TokenKind::KwWhile},
};

} // namespace

Lexer::Lexer(const SourceManager& sources, FileId file)
    : sources_(sources), file_(file), source_(sources.content(file)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skipTrivia();
        start_ = current_;

        if (isAtEnd()) {
            tokens.push_back(makeToken(TokenKind::EndOfFile));
            break;
        }

        tokens.push_back(scanToken());
    }

    return tokens;
}

bool Lexer::isAtEnd() const noexcept {
    return current_ >= source_.size();
}

char Lexer::peek(std::size_t lookahead) const noexcept {
    const auto index = current_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() noexcept {
    return isAtEnd() ? '\0' : source_[current_++];
}

bool Lexer::match(char expected) noexcept {
    if (peek() != expected) {
        return false;
    }
    ++current_;
    return true;
}

void Lexer::skipTrivia() {
    while (!isAtEnd()) {
        switch (peek()) {
            case ' ': case '\t': case '\r': case '\n':
                advance();
                break;
            case '/':
                if (peek(1) == '/') {
                    skipLineComment();
                } else if (peek(1) == '*') {
                    skipBlockComment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

void Lexer::skipLineComment() {
    advance();
    advance();
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

void Lexer::skipBlockComment() {
    advance();
    advance();
    std::size_t depth = 1;

    while (!isAtEnd() && depth > 0) {
        if (peek() == '/' && peek(1) == '*') {
            advance();
            advance();
            ++depth;
        } else if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            --depth;
        } else {
            advance();
        }
    }
}

Token Lexer::scanToken() {
    const char c = advance();

    if (isIdentifierStart(c)) {
        if (c == 'b' && peek() == '"') {
            return scanByteLiteral();
        }
        if (c == 'b' && peek() == '\'') {
            return scanByteLiteral();
        }
        if (c == 'r' && (peek() == '"' || peek() == '#')) {
            return scanRawString();
        }
        return scanIdentifierOrKeyword();
    }

    if (c >= '0' && c <= '9') {
        return scanNumber();
    }

    switch (c) {
        case '(': return makeToken(TokenKind::LeftParen);
        case ')': return makeToken(TokenKind::RightParen);
        case '{': return makeToken(TokenKind::LeftBrace);
        case '}': return makeToken(TokenKind::RightBrace);
        case '[': return makeToken(TokenKind::LeftBracket);
        case ']': return makeToken(TokenKind::RightBracket);
        case ',': return makeToken(TokenKind::Comma);
        case ';': return makeToken(TokenKind::Semicolon);
        case '@': return makeToken(TokenKind::At);
        case '?': return makeToken(TokenKind::Question);
        case '~': return makeToken(TokenKind::Tilde);
        case ':': return makeToken(match(':') ? TokenKind::ColonColon : TokenKind::Colon);
        case '.':
            if (match('.')) {
                return makeToken(match('=') ? TokenKind::DotDotEqual : TokenKind::DotDot);
            }
            return makeToken(TokenKind::Dot);
        case '+': return makeToken(match('=') ? TokenKind::PlusEqual : TokenKind::Plus);
        case '-':
            if (match('>')) return makeToken(TokenKind::Arrow);
            return makeToken(match('=') ? TokenKind::MinusEqual : TokenKind::Minus);
        case '*': return makeToken(match('=') ? TokenKind::StarEqual : TokenKind::Star);
        case '/': return makeToken(match('=') ? TokenKind::SlashEqual : TokenKind::Slash);
        case '%': return makeToken(match('=') ? TokenKind::PercentEqual : TokenKind::Percent);
        case '=':
            if (match('=')) return makeToken(TokenKind::EqualEqual);
            if (match('>')) return makeToken(TokenKind::FatArrow);
            return makeToken(TokenKind::Equal);
        case '!': return makeToken(match('=') ? TokenKind::BangEqual : TokenKind::Bang);
        case '&':
            if (match('&')) return makeToken(TokenKind::AmpersandAmpersand);
            return makeToken(match('=') ? TokenKind::AmpersandEqual : TokenKind::Ampersand);
        case '|':
            if (match('|')) return makeToken(TokenKind::PipePipe);
            return makeToken(match('=') ? TokenKind::PipeEqual : TokenKind::Pipe);
        case '^': return makeToken(match('=') ? TokenKind::CaretEqual : TokenKind::Caret);
        case '<':
            if (match('<')) {
                return makeToken(match('=') ? TokenKind::ShiftLeftEqual : TokenKind::ShiftLeft);
            }
            return makeToken(match('=') ? TokenKind::LessEqual : TokenKind::Less);
        case '>':
            if (match('>')) {
                return makeToken(match('=') ? TokenKind::ShiftRightEqual : TokenKind::ShiftRight);
            }
            return makeToken(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);
        case '"': return scanString();
        case '\'': return scanChar();
        default: return makeToken(TokenKind::Invalid);
    }
}

Token Lexer::scanIdentifierOrKeyword() {
    while (isIdentifierContinue(peek())) {
        advance();
    }

    const auto text = source_.substr(start_, current_ - start_);
    return makeToken(keywordKind(text));
}

Token Lexer::scanNumber() {
    int base = 10;
    bool is_float = false;

    if (source_[start_] == '0') {
        if (peek() == 'x' || peek() == 'X') {
            base = 16;
            advance();
        } else if (peek() == 'b' || peek() == 'B') {
            base = 2;
            advance();
        } else if (peek() == 'o' || peek() == 'O') {
            base = 8;
            advance();
        }
    }

    while (isDigitForBase(peek(), base) || peek() == '_') {
        advance();
    }

    if (base == 10 && peek() == '.' && peek(1) != '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
            advance();
        }
    }

    if (base == 10 && (peek() == 'e' || peek() == 'E')) {
        const auto save = current_;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            is_float = true;
            while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
                advance();
            }
        } else {
            current_ = save;
        }
    }

    // Numeric suffixes are kept as part of the literal token. Semantic analysis validates them.
    while (isIdentifierContinue(peek())) {
        advance();
    }

    return makeToken(is_float ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral);
}

Token Lexer::scanString() {
    bool escaped = false;
    while (!isAtEnd()) {
        const char c = advance();
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return makeToken(TokenKind::StringLiteral);
        }
        if (c == '\n') {
            return makeToken(TokenKind::Invalid);
        }
    }
    return makeToken(TokenKind::Invalid);
}

Token Lexer::scanChar() {
    bool escaped = false;
    while (!isAtEnd()) {
        const char c = advance();
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '\'') {
            return makeToken(TokenKind::CharLiteral);
        }
        if (c == '\n') {
            return makeToken(TokenKind::Invalid);
        }
    }
    return makeToken(TokenKind::Invalid);
}

Token Lexer::scanByteLiteral() {
    const char delimiter = advance();
    bool escaped = false;

    while (!isAtEnd()) {
        const char c = advance();
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == delimiter) {
            return makeToken(delimiter == '"' ? TokenKind::ByteStringLiteral : TokenKind::ByteLiteral);
        }
        if (c == '\n') {
            return makeToken(TokenKind::Invalid);
        }
    }

    return makeToken(TokenKind::Invalid);
}

Token Lexer::scanRawString() {
    std::size_t hashes = 0;
    while (peek() == '#') {
        ++hashes;
        advance();
    }

    if (!match('"')) {
        while (isIdentifierContinue(peek())) {
            advance();
        }
        return makeToken(TokenKind::Identifier);
    }

    while (!isAtEnd()) {
        if (peek() != '"') {
            advance();
            continue;
        }

        const auto quote_position = current_;
        advance();

        std::size_t matched_hashes = 0;
        while (matched_hashes < hashes && peek() == '#') {
            advance();
            ++matched_hashes;
        }

        if (matched_hashes == hashes) {
            return makeToken(TokenKind::RawStringLiteral);
        }

        current_ = quote_position + 1;
    }

    return makeToken(TokenKind::Invalid);
}

Token Lexer::makeToken(TokenKind kind) const noexcept {
    return makeToken(kind, start_, current_);
}

Token Lexer::makeToken(TokenKind kind, std::size_t start, std::size_t end) const noexcept {
    return Token{
        .kind = kind,
        .span = SourceSpan{.file = file_, .start = start, .end = end},
    };
}

bool Lexer::isIdentifierStart(char c) noexcept {
    const auto uc = static_cast<unsigned char>(c);
    return c == '_' || std::isalpha(uc) != 0;
}

bool Lexer::isIdentifierContinue(char c) noexcept {
    const auto uc = static_cast<unsigned char>(c);
    return c == '_' || std::isalnum(uc) != 0;
}

bool Lexer::isDigitForBase(char c, int base) noexcept {
    if (c >= '0' && c <= '9') {
        return (c - '0') < base;
    }
    if (base == 16) {
        return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    return false;
}

TokenKind Lexer::keywordKind(std::string_view text) noexcept {
    for (const auto& [keyword, kind] : keywords) {
        if (text == keyword) {
            return kind;
        }
    }
    return TokenKind::Identifier;
}

} // namespace nus
