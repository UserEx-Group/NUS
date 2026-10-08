#include "nus/mir/CfgVerifier.hpp"

#include <unordered_set>

namespace nus::mir {

std::vector<std::string> CfgVerifier::verify(const Program& program) const {
    std::vector<std::string> errors;
    for (const auto& function : program.functions) {
        const std::string name = function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
        if (function.blocks.empty()) {
            errors.push_back("function `" + name + "` has no basic blocks");
            continue;
        }
        std::unordered_set<BlockId> ids;
        for (const auto& block : function.blocks) {
            if (!ids.insert(block.id).second) errors.push_back("function `" + name + "` has duplicate basic block id");
        }
        if (!ids.contains(function.entry)) errors.push_back("function `" + name + "` has an invalid entry block");

        auto validTarget = [&](BlockId id, std::string_view context) {
            if (id == InvalidBlockId || !ids.contains(id)) {
                errors.push_back("function `" + name + "` has invalid " + std::string(context) + " target");
            }
        };

        for (const auto& block : function.blocks) {
            if (!block.terminator) {
                errors.push_back("bb" + std::to_string(block.id) + " in `" + name + "` has no terminator");
                continue;
            }
            switch (block.terminator->kind) {
                case TerminatorKind::Goto:
                    validTarget(block.terminator->target, "goto");
                    break;
                case TerminatorKind::Branch:
                    validTarget(block.terminator->then_block, "branch-then");
                    validTarget(block.terminator->else_block, "branch-else");
                    break;
                case TerminatorKind::Return:
                case TerminatorKind::Unreachable:
                    break;
            }
        }
    }
    return errors;
}

} // namespace nus::mir
