#pragma once

#include "nus/sema/Type.hpp"
#include "nus/source/SourceSpan.hpp"

#include <cstddef>
#include <string>

namespace nus::sema {

enum class SymbolKind {
    Variable,
    Parameter,
    Function,
    BuiltinFunction,
};

struct Symbol {
    std::size_t id{0};
    std::string name;
    SymbolKind kind{SymbolKind::Variable};
    Type type;
    bool is_mutable{false};
    bool is_moved{false};
    std::size_t shared_borrows{0};
    bool mutable_borrowed{false};
    SourceSpan span{};
};

} // namespace nus::sema
