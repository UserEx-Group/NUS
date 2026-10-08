#pragma once

#include "nus/hir/Hir.hpp"

#include <string>

namespace nus::hir {

class HirPrinter {
public:
    [[nodiscard]] std::string print(const Program& program) const;

private:
    void printFunction(const Function& function, std::string& out, int depth) const;
    void printExpr(const Expr& expression, std::string& out, int depth) const;
    void printStmt(const Stmt& statement, std::string& out, int depth) const;
    static void line(std::string& out, int depth, const std::string& text);
};

} // namespace nus::hir
