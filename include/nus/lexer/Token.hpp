#pragma once

#include "nus/lexer/TokenKind.hpp"
#include "nus/source/SourceSpan.hpp"

namespace nus {

struct Token {
    TokenKind kind{TokenKind::Invalid};
    SourceSpan span{};
};

} // namespace nus
