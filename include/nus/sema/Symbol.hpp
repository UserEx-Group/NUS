#pragma once

#include "nus/sema/Type.hpp"
#include "nus/source/SourceSpan.hpp"

#include <string>

namespace nus::sema {

enum class SymbolKind {
    Variable,
    Parameter,
    Function,
    BuiltinFunction,
};

struct Symbol {
    std::string name;
    SymbolKind kind{SymbolKind::Variable};
    Type type;
    bool is_mutable{false};
    SourceSpan span{};
};

} // namespace nus::sema
