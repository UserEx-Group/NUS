#pragma once

#include "nus/sema/Symbol.hpp"

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nus::sema {

class SymbolTable {
public:
    SymbolTable();

    void pushScope();
    void popScope();

    [[nodiscard]] bool declare(Symbol symbol);
    [[nodiscard]] const Symbol* lookup(std::string_view name) const;
    [[nodiscard]] const Symbol* lookupCurrent(std::string_view name) const;
    [[nodiscard]] std::size_t depth() const noexcept;

private:
    using Scope = std::unordered_map<std::string, Symbol>;
    std::vector<Scope> scopes_;
};

} // namespace nus::sema
