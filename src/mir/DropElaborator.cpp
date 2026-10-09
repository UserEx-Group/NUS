#include "nus/mir/DropElaborator.hpp"

#include <algorithm>

namespace nus::mir {
namespace {

std::string functionKey(const Function& function) {
    return function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
}

const Local* findLocal(const Function& function, LocalId id) {
    for (const auto& local : function.locals) if (local.id == id) return &local;
    return nullptr;
}

Operand localOperand(const Local& local) {
    return Operand{.kind = OperandKind::Local, .local = local.id, .type = local.type};
}

} // namespace

void DropElaborator::run(Program& program, const FlowResult& flow) const {
    for (auto& function : program.functions) {
        const auto found = flow.functions.find(functionKey(function));
        run(function, found == flow.functions.end() ? nullptr : &found->second);
    }
}

void DropElaborator::run(Function& function, const FunctionFlowInfo* flow) const {
    if (!flow) return;
    for (auto& block : function.blocks) {
        if (!block.terminator || block.terminator->kind != TerminatorKind::Return) continue;
        const auto definite_it = flow->definitely_available_before_terminator.find(block.id);
        const auto maybe_it = flow->maybe_available_before_terminator.find(block.id);
        if (definite_it == flow->definitely_available_before_terminator.end() ||
            maybe_it == flow->maybe_available_before_terminator.end()) continue;

        LocalId returned = InvalidLocalId;
        if (block.terminator->value.kind == OperandKind::Local && !block.terminator->value.type.isCopy()) {
            returned = block.terminator->value.local;
        }

        std::vector<const Local*> drops;
        for (const auto id : maybe_it->second) {
            if (id == returned) continue;
            const Local* local = findLocal(function, id);
            if (!local || local->type.isCopy() || local->type.isReference()) continue;
            drops.push_back(local);
        }
        std::sort(drops.begin(), drops.end(), [](const Local* a, const Local* b) { return a->id > b->id; });
        for (const auto* local : drops) {
            const bool definite = definite_it->second.contains(local->id);
            block.instructions.push_back(Instruction{
                .kind = definite ? InstructionKind::Drop : InstructionKind::DropIf,
                .name = local->name,
                .operands = {localOperand(*local)},
                .span = block.terminator->span,
            });
        }
    }
}

} // namespace nus::mir
