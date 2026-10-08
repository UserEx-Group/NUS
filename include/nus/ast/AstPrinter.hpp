#pragma once

#include "nus/ast/Ast.hpp"

#include <string>

namespace nus::ast {

class AstPrinter {
public:
    [[nodiscard]] std::string print(const SourceFile& file) const;

private:
    void printFunction(const FunctionDecl& function, std::string& out, int depth) const;
    void printBlock(const BlockExpr& block, std::string& out, int depth) const;
    void printStatement(const Stmt& statement, std::string& out, int depth) const;
    void printExpression(const Expr& expression, std::string& out, int depth) const;
    static void line(std::string& out, int depth, std::string_view text);
};

} // namespace nus::ast
