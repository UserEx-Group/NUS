#pragma once

#include "nus/mir/Mir.hpp"

#include <set>
#include <unordered_map>
#include <vector>

namespace nus::mir {

using LocalSet = std::set<LocalId>;

struct BlockLiveness {
    LocalSet live_in;
    LocalSet live_out;
    LocalSet terminator_live_before;
    std::vector<LocalSet> live_before;
    std::vector<LocalSet> live_after;
};

struct LivenessResult {
    std::unordered_map<BlockId, BlockLiveness> blocks;
};

class LivenessAnalysis {
public:
    [[nodiscard]] LivenessResult analyze(const Function& function) const;

    [[nodiscard]] static LocalSet instructionUses(const Instruction& instruction);
    [[nodiscard]] static LocalSet instructionDefs(const Instruction& instruction);
    [[nodiscard]] static LocalSet terminatorUses(const Terminator& terminator);
    [[nodiscard]] static std::vector<BlockId> successors(const BasicBlock& block);
};

} // namespace nus::mir
