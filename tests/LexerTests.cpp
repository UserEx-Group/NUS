#include "nus/lexer/Lexer.hpp"
#include "nus/lexer/TokenKind.hpp"
#include "nus/source/SourceManager.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using nus::TokenKind;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expectKinds(std::string_view source, const std::vector<TokenKind>& expected) {
    nus::SourceManager sources;
    const auto file = sources.addSource("<test>", std::string(source));
    nus::Lexer lexer(sources, file);
    const auto tokens = lexer.tokenize();

    if (tokens.size() != expected.size()) {
        std::cerr << "expected " << expected.size() << " tokens, got " << tokens.size() << '\n';
        for (const auto& token : tokens) {
            std::cerr << "  " << nus::tokenKindName(token.kind)
                      << " `" << sources.text(token.span) << "`\n";
        }
        fail("token count mismatch");
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (tokens[i].kind != expected[i]) {
            std::cerr << "token " << i << ": expected " << nus::tokenKindName(expected[i])
                      << ", got " << nus::tokenKindName(tokens[i].kind)
                      << " `" << sources.text(tokens[i].span) << "`\n";
            fail("token kind mismatch");
        }
    }
}

void testFirstProgram() {
    expectKinds(
        R"(fn main() {
            let x = 10 + 20 * 3;
        })",
        {
            TokenKind::KwFn,
            TokenKind::Identifier,
            TokenKind::LeftParen,
            TokenKind::RightParen,
            TokenKind::LeftBrace,
            TokenKind::KwLet,
            TokenKind::Identifier,
            TokenKind::Equal,
            TokenKind::IntegerLiteral,
            TokenKind::Plus,
            TokenKind::IntegerLiteral,
            TokenKind::Star,
            TokenKind::IntegerLiteral,
            TokenKind::Semicolon,
            TokenKind::RightBrace,
            TokenKind::EndOfFile,
        }
    );
}

void testOperators() {
    expectKinds(
        "== != <= >= && || :: .. ..= -> => += -= *= /= %= &= |= ^= <<= >>=",
        {
            TokenKind::EqualEqual, TokenKind::BangEqual,
            TokenKind::LessEqual, TokenKind::GreaterEqual,
            TokenKind::AmpersandAmpersand, TokenKind::PipePipe,
            TokenKind::ColonColon, TokenKind::DotDot, TokenKind::DotDotEqual,
            TokenKind::Arrow, TokenKind::FatArrow,
            TokenKind::PlusEqual, TokenKind::MinusEqual,
            TokenKind::StarEqual, TokenKind::SlashEqual, TokenKind::PercentEqual,
            TokenKind::AmpersandEqual, TokenKind::PipeEqual, TokenKind::CaretEqual,
            TokenKind::ShiftLeftEqual, TokenKind::ShiftRightEqual,
            TokenKind::EndOfFile,
        }
    );
}

void testLiterals() {
    expectKinds(
        R"(42 1_000 0xff 0b1010 0o755 3.14 1e6 2.0f32 "hello" 'x' b'A' b"HTTP" r#"raw "text""#)",
        {
            TokenKind::IntegerLiteral,
            TokenKind::IntegerLiteral,
            TokenKind::IntegerLiteral,
            TokenKind::IntegerLiteral,
            TokenKind::IntegerLiteral,
            TokenKind::FloatLiteral,
            TokenKind::FloatLiteral,
            TokenKind::FloatLiteral,
            TokenKind::StringLiteral,
            TokenKind::CharLiteral,
            TokenKind::ByteLiteral,
            TokenKind::ByteStringLiteral,
            TokenKind::RawStringLiteral,
            TokenKind::EndOfFile,
        }
    );
}

void testNestedComments() {
    expectKinds(
        "let /* outer /* inner */ still outer */ value = 1; // end\nreturn;",
        {
            TokenKind::KwLet,
            TokenKind::Identifier,
            TokenKind::Equal,
            TokenKind::IntegerLiteral,
            TokenKind::Semicolon,
            TokenKind::KwReturn,
            TokenKind::Semicolon,
            TokenKind::EndOfFile,
        }
    );
}

void testSourceLocations() {
    nus::SourceManager sources;
    const auto file = sources.addSource("<test>", "fn\n  main");
    nus::Lexer lexer(sources, file);
    const auto tokens = lexer.tokenize();

    const auto location = sources.location(file, tokens[1].span.start);
    if (location.line != 2 || location.column != 3) {
        fail("incorrect source location");
    }
}

} // namespace

int main() {
    testFirstProgram();
    testOperators();
    testLiterals();
    testNestedComments();
    testSourceLocations();
    std::cout << "all lexer tests passed\n";
    return 0;
}
