#include "nus/mir/Dataflow.hpp"

#include <algorithm>

namespace nus::mir {
namespace {

void addOperand(LocalSet& set, const Operand& operand) {
    if (operand.kind == OperandKind::Local && operand.local != InvalidLocalId) set.insert(operand.local);
}

LocalSet setMinus(const LocalSet& lhs, const LocalSet& rhs) {
    LocalSet result;
    std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                        std::inserter(result, result.begin()));
    return result;
}

void setUnionInto(LocalSet& target, const LocalSet& source) {
    target.insert(source.begin(), source.end());
}

} // namespace

LocalSet LivenessAnalysis::instructionUses(const Instruction& instruction) {
    LocalSet uses;
    for (const auto& operand : instruction.operands) addOperand(uses, operand);
    return uses;
}

LocalSet LivenessAnalysis::instructionDefs(const Instruction& instruction) {
    LocalSet defs;
    if (instruction.destination) defs.insert(*instruction.destination);
    return defs;
}

LocalSet LivenessAnalysis::terminatorUses(const Terminator& terminator) {
    LocalSet uses;
    if (terminator.kind == TerminatorKind::Branch) addOperand(uses, terminator.condition);
    if (terminator.kind == TerminatorKind::Return) addOperand(uses, terminator.value);
    return uses;
}

std::vector<BlockId> LivenessAnalysis::successors(const BasicBlock& block) {
    std::vector<BlockId> result;
    if (!block.terminator) return result;
    switch (block.terminator->kind) {
        case TerminatorKind::Goto:
            result.push_back(block.terminator->target);
            break;
        case TerminatorKind::Branch:
            result.push_back(block.terminator->then_block);
            if (block.terminator->else_block != block.terminator->then_block) {
                result.push_back(block.terminator->else_block);
            }
            break;
        case TerminatorKind::Return:
        case TerminatorKind::Unreachable:
            break;
    }
    return result;
}

LivenessResult LivenessAnalysis::analyze(const Function& function) const {
    LivenessResult result;
    std::unordered_map<BlockId, LocalSet> block_use;
    std::unordered_map<BlockId, LocalSet> block_def;

    for (const auto& block : function.blocks) {
        LocalSet use;
        LocalSet def;
        for (const auto& instruction : block.instructions) {
            const auto uses = instructionUses(instruction);
            for (const auto local : uses) {
                if (!def.contains(local)) use.insert(local);
            }
            const auto defs = instructionDefs(instruction);
            def.insert(defs.begin(), defs.end());
        }
        if (block.terminator) {
            const auto uses = terminatorUses(*block.terminator);
            for (const auto local : uses) {
                if (!def.contains(local)) use.insert(local);
            }
        }
        block_use.emplace(block.id, std::move(use));
        block_def.emplace(block.id, std::move(def));
        result.blocks.emplace(block.id, BlockLiveness{});
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = function.blocks.rbegin(); it != function.blocks.rend(); ++it) {
            const auto& block = *it;
            LocalSet new_out;
            for (const auto successor : successors(block)) {
                if (const auto found = result.blocks.find(successor); found != result.blocks.end()) {
                    setUnionInto(new_out, found->second.live_in);
                }
            }
            LocalSet new_in = block_use[block.id];
            auto out_minus_def = setMinus(new_out, block_def[block.id]);
            setUnionInto(new_in, out_minus_def);

            auto& info = result.blocks[block.id];
            if (info.live_out != new_out || info.live_in != new_in) {
                info.live_out = std::move(new_out);
                info.live_in = std::move(new_in);
                changed = true;
            }
        }
    }

    for (const auto& block : function.blocks) {
        auto& info = result.blocks[block.id];
        info.live_before.assign(block.instructions.size(), LocalSet{});
        info.live_after.assign(block.instructions.size(), LocalSet{});

        LocalSet live = info.live_out;
        if (block.terminator) {
            const auto term_uses = terminatorUses(*block.terminator);
            setUnionInto(live, term_uses);
        }
        info.terminator_live_before = live;

        for (std::size_t index = block.instructions.size(); index > 0; --index) {
            const std::size_t i = index - 1;
            info.live_after[i] = live;
            const auto defs = instructionDefs(block.instructions[i]);
            live = setMinus(live, defs);
            const auto uses = instructionUses(block.instructions[i]);
            setUnionInto(live, uses);
            info.live_before[i] = live;
        }
    }

    return result;
}

} // namespace nus::mir
