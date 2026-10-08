#pragma once

#include "nus/hir/Hir.hpp"
#include "nus/mir/Mir.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace nus::mir {

class MirBuilder {
public:
    [[nodiscard]] Program lower(const hir::Program& program);

private:
    [[nodiscard]] Function lowerFunction(const hir::Function& function);
    [[nodiscard]] Operand lowerExpr(const hir::Expr& expression);
    void lowerStmt(const hir::Stmt& statement);
    [[nodiscard]] Operand lowerBlock(const hir::Expr& block);

    [[nodiscard]] LocalId createTemp(sema::Type type);
    [[nodiscard]] BlockId createBlock();
    [[nodiscard]] BasicBlock& currentBlock();
    void switchTo(BlockId id);
    void emit(Instruction instruction);
    void terminate(Terminator terminator);
    void gotoIfOpen(BlockId target, SourceSpan span = {});
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] Operand localOperand(LocalId id) const;
    [[nodiscard]] Operand unitOperand() const;

    Function* function_{nullptr};
    BlockId current_{0};
    std::vector<std::pair<BlockId, BlockId>> loop_targets_; // break, continue
};

} // namespace nus::mir
