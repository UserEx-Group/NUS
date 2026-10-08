#include "nus/sema/SymbolTable.hpp"

#include <utility>

namespace nus::sema {

SymbolTable::SymbolTable() {
    scopes_.emplace_back();
}

void SymbolTable::pushScope() {
    scopes_.emplace_back();
}

void SymbolTable::popScope() {
    if (scopes_.size() > 1) scopes_.pop_back();
}

bool SymbolTable::declare(Symbol symbol) {
    auto& scope = scopes_.back();
    if (scope.contains(symbol.name)) return false;
    scope.emplace(symbol.name, std::move(symbol));
    return true;
}

const Symbol* SymbolTable::lookup(std::string_view name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(std::string(name));
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookupMutable(std::string_view name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(std::string(name));
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupCurrent(std::string_view name) const {
    const auto& scope = scopes_.back();
    const auto found = scope.find(std::string(name));
    return found == scope.end() ? nullptr : &found->second;
}

Symbol* SymbolTable::lookupById(std::size_t id) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        for (auto& [name, symbol] : *it) {
            (void)name;
            if (symbol.id == id) return &symbol;
        }
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupById(std::size_t id) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        for (const auto& [name, symbol] : *it) {
            (void)name;
            if (symbol.id == id) return &symbol;
        }
    }
    return nullptr;
}

std::size_t SymbolTable::depth() const noexcept {
    return scopes_.size();
}

} // namespace nus::sema
