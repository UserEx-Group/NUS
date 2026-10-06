#pragma once

#include "nus/lexer/Token.hpp"
#include "nus/source/SourceManager.hpp"

#include <string_view>
#include <vector>

namespace nus {

class Lexer {
public:
    Lexer(const SourceManager& sources, FileId file);

    [[nodiscard]] std::vector<Token> tokenize();

private:
    [[nodiscard]] bool isAtEnd() const noexcept;
    [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;

    void skipTrivia();
    void skipLineComment();
    void skipBlockComment();

    [[nodiscard]] Token scanToken();
    [[nodiscard]] Token scanIdentifierOrKeyword();
    [[nodiscard]] Token scanNumber();
    [[nodiscard]] Token scanString();
    [[nodiscard]] Token scanChar();
    [[nodiscard]] Token scanByteLiteral();
    [[nodiscard]] Token scanRawString();

    [[nodiscard]] Token makeToken(TokenKind kind) const noexcept;
    [[nodiscard]] Token makeToken(TokenKind kind, std::size_t start, std::size_t end) const noexcept;

    [[nodiscard]] static bool isIdentifierStart(char c) noexcept;
    [[nodiscard]] static bool isIdentifierContinue(char c) noexcept;
    [[nodiscard]] static bool isDigitForBase(char c, int base) noexcept;
    [[nodiscard]] static TokenKind keywordKind(std::string_view text) noexcept;

    const SourceManager& sources_;
    FileId file_;
    std::string_view source_;
    std::size_t start_{0};
    std::size_t current_{0};
};

} // namespace nus
