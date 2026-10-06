#include "nus/lexer/TokenKind.hpp"

namespace nus {

std::string_view tokenKindName(TokenKind kind) noexcept {
#define UX_CASE(name) case TokenKind::name: return #name
    switch (kind) {
        UX_CASE(Invalid); UX_CASE(EndOfFile);
        UX_CASE(Identifier); UX_CASE(IntegerLiteral); UX_CASE(FloatLiteral);
        UX_CASE(StringLiteral); UX_CASE(CharLiteral); UX_CASE(ByteLiteral);
        UX_CASE(ByteStringLiteral); UX_CASE(RawStringLiteral);
        UX_CASE(KwAs); UX_CASE(KwAsync); UX_CASE(KwAwait); UX_CASE(KwBreak);
        UX_CASE(KwConst); UX_CASE(KwContinue); UX_CASE(KwDefer); UX_CASE(KwElse);
        UX_CASE(KwEnum); UX_CASE(KwExtern); UX_CASE(KwFalse); UX_CASE(KwFn);
        UX_CASE(KwFor); UX_CASE(KwIf); UX_CASE(KwImpl); UX_CASE(KwIn);
        UX_CASE(KwLet); UX_CASE(KwLoop); UX_CASE(KwMatch); UX_CASE(KwMut);
        UX_CASE(KwPub); UX_CASE(KwReturn); UX_CASE(KwScope); UX_CASE(KwSpawn);
        UX_CASE(KwStruct); UX_CASE(KwTrait); UX_CASE(KwTrue); UX_CASE(KwUnsafe);
        UX_CASE(KwUse); UX_CASE(KwWhile); UX_CASE(KwNetwork); UX_CASE(KwService);
        UX_CASE(KwProtocol); UX_CASE(KwSimulate); UX_CASE(KwNode); UX_CASE(KwLink);
        UX_CASE(KwWhere); UX_CASE(KwType); UX_CASE(KwMove); UX_CASE(KwRef);
        UX_CASE(KwStatic);
        UX_CASE(LeftParen); UX_CASE(RightParen); UX_CASE(LeftBrace); UX_CASE(RightBrace);
        UX_CASE(LeftBracket); UX_CASE(RightBracket); UX_CASE(Comma); UX_CASE(Semicolon);
        UX_CASE(Colon); UX_CASE(ColonColon); UX_CASE(Dot); UX_CASE(DotDot);
        UX_CASE(DotDotEqual); UX_CASE(At); UX_CASE(Question); UX_CASE(Plus);
        UX_CASE(Minus); UX_CASE(Star); UX_CASE(Slash); UX_CASE(Percent); UX_CASE(Equal);
        UX_CASE(EqualEqual); UX_CASE(Bang); UX_CASE(BangEqual); UX_CASE(Less);
        UX_CASE(LessEqual); UX_CASE(Greater); UX_CASE(GreaterEqual); UX_CASE(Ampersand);
        UX_CASE(AmpersandAmpersand); UX_CASE(Pipe); UX_CASE(PipePipe); UX_CASE(Caret);
        UX_CASE(Tilde); UX_CASE(ShiftLeft); UX_CASE(ShiftRight); UX_CASE(PlusEqual);
        UX_CASE(MinusEqual); UX_CASE(StarEqual); UX_CASE(SlashEqual); UX_CASE(PercentEqual);
        UX_CASE(AmpersandEqual); UX_CASE(PipeEqual); UX_CASE(CaretEqual);
        UX_CASE(ShiftLeftEqual); UX_CASE(ShiftRightEqual); UX_CASE(Arrow); UX_CASE(FatArrow);
    }
#undef UX_CASE
    return "Unknown";
}

} // namespace nus
