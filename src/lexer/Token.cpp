#include "nus/lexer/TokenKind.hpp"

namespace nus {

std::string_view tokenKindName(TokenKind kind) noexcept {
#define NUS_CASE(name) case TokenKind::name: return #name
    switch (kind) {
        NUS_CASE(Invalid); NUS_CASE(EndOfFile);
        NUS_CASE(Identifier); NUS_CASE(IntegerLiteral); NUS_CASE(FloatLiteral);
        NUS_CASE(StringLiteral); NUS_CASE(CharLiteral); NUS_CASE(ByteLiteral);
        NUS_CASE(ByteStringLiteral); NUS_CASE(RawStringLiteral);
        NUS_CASE(KwAs); NUS_CASE(KwAsync); NUS_CASE(KwAwait); NUS_CASE(KwBreak);
        NUS_CASE(KwConst); NUS_CASE(KwContinue); NUS_CASE(KwDefer); NUS_CASE(KwElse);
        NUS_CASE(KwEnum); NUS_CASE(KwExtern); NUS_CASE(KwFalse); NUS_CASE(KwFn);
        NUS_CASE(KwFor); NUS_CASE(KwIf); NUS_CASE(KwImpl); NUS_CASE(KwIn);
        NUS_CASE(KwLet); NUS_CASE(KwLoop); NUS_CASE(KwMatch); NUS_CASE(KwMut);
        NUS_CASE(KwPub); NUS_CASE(KwReturn); NUS_CASE(KwScope); NUS_CASE(KwSpawn);
        NUS_CASE(KwStruct); NUS_CASE(KwTrait); NUS_CASE(KwTrue); NUS_CASE(KwUnsafe);
        NUS_CASE(KwUse); NUS_CASE(KwWhile); NUS_CASE(KwNetwork); NUS_CASE(KwService);
        NUS_CASE(KwProtocol); NUS_CASE(KwSimulate); NUS_CASE(KwNode); NUS_CASE(KwLink);
        NUS_CASE(KwWhere); NUS_CASE(KwType); NUS_CASE(KwMove); NUS_CASE(KwRef);
        NUS_CASE(KwStatic);
        NUS_CASE(LeftParen); NUS_CASE(RightParen); NUS_CASE(LeftBrace); NUS_CASE(RightBrace);
        NUS_CASE(LeftBracket); NUS_CASE(RightBracket); NUS_CASE(Comma); NUS_CASE(Semicolon);
        NUS_CASE(Colon); NUS_CASE(ColonColon); NUS_CASE(Dot); NUS_CASE(DotDot);
        NUS_CASE(DotDotEqual); NUS_CASE(At); NUS_CASE(Question); NUS_CASE(Plus);
        NUS_CASE(Minus); NUS_CASE(Star); NUS_CASE(Slash); NUS_CASE(Percent); NUS_CASE(Equal);
        NUS_CASE(EqualEqual); NUS_CASE(Bang); NUS_CASE(BangEqual); NUS_CASE(Less);
        NUS_CASE(LessEqual); NUS_CASE(Greater); NUS_CASE(GreaterEqual); NUS_CASE(Ampersand);
        NUS_CASE(AmpersandAmpersand); NUS_CASE(Pipe); NUS_CASE(PipePipe); NUS_CASE(Caret);
        NUS_CASE(Tilde); NUS_CASE(ShiftLeft); NUS_CASE(ShiftRight); NUS_CASE(PlusEqual);
        NUS_CASE(MinusEqual); NUS_CASE(StarEqual); NUS_CASE(SlashEqual); NUS_CASE(PercentEqual);
        NUS_CASE(AmpersandEqual); NUS_CASE(PipeEqual); NUS_CASE(CaretEqual);
        NUS_CASE(ShiftLeftEqual); NUS_CASE(ShiftRightEqual); NUS_CASE(Arrow); NUS_CASE(FatArrow);
    }
#undef NUS_CASE
    return "Unknown";
}

} // namespace nus
