#include "nus/mir/FlowChecker.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <unordered_set>

namespace nus::mir {
namespace {

constexpr unsigned Uninitialized = 1U << 0U;
constexpr unsigned Available = 1U << 1U;
constexpr unsigned Moved = 1U << 2U;
using StateVector = std::vector<unsigned>;

std::string functionKey(const Function& function) {
    return function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
}

const Local* findLocal(const Function& function, LocalId id) {
    for (const auto& local : function.locals) if (local.id == id) return &local;
    return nullptr;
}

std::size_t stateSize(const Function& function) {
    LocalId max = 0;
    bool any = false;
    for (const auto& local : function.locals) {
        max = std::max(max, local.id);
        any = true;
    }
    return any ? static_cast<std::size_t>(max) + 1U : 0U;
}

StateVector entryState(const Function& function) {
    StateVector state(stateSize(function), Uninitialized);
    for (const auto& local : function.locals) {
        if (local.is_parameter || local.is_receiver) state[local.id] = Available;
    }
    return state;
}

StateVector joinStates(const std::vector<StateVector>& inputs, std::size_t count) {
    if (inputs.empty()) return StateVector(count, Uninitialized);
    StateVector result(count, 0U);
    for (const auto& input : inputs) {
        for (std::size_t i = 0; i < count; ++i) result[i] |= input[i];
    }
    return result;
}

std::unordered_map<BlockId, std::vector<BlockId>> predecessors(const Function& function) {
    std::unordered_map<BlockId, std::vector<BlockId>> result;
    for (const auto& block : function.blocks) result[block.id];
    for (const auto& block : function.blocks) {
        for (const auto successor : LivenessAnalysis::successors(block)) result[successor].push_back(block.id);
    }
    return result;
}

bool unavailable(unsigned state) {
    return (state & Available) == 0U || (state & (Moved | Uninitialized)) != 0U;
}

bool maybeUnavailable(unsigned state) {
    return (state & Available) != 0U && (state & (Moved | Uninitialized)) != 0U;
}

void defineDestination(StateVector& state, const Instruction& instruction) {
    if (instruction.destination && *instruction.destination < state.size()) state[*instruction.destination] = Available;
}

std::vector<LocalId> consumedByInstruction(const Instruction& instruction) {
    std::vector<LocalId> result;
    auto consume = [&](const Operand& operand) {
        if (operand.kind == OperandKind::Local && !operand.type.isCopy()) result.push_back(operand.local);
    };

    switch (instruction.kind) {
        case InstructionKind::Assign:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
            break;
        case InstructionKind::Call: {
            if (instruction.operands.empty()) break;
            const auto& callee = instruction.operands.front();
            if (callee.type.kind != sema::TypeKind::Function) break;
            if (callee.type.variadic_any) break;
            for (std::size_t i = 1; i < instruction.operands.size(); ++i) {
                const std::size_t parameter = i - 1;
                if (parameter >= callee.type.parameters.size()) break;
                if (!callee.type.parameters[parameter].isReference()) consume(instruction.operands[i]);
            }
            break;
        }
        case InstructionKind::MakeStruct:
        case InstructionKind::MakeArray:
            for (const auto& operand : instruction.operands) consume(operand);
            break;
        case InstructionKind::StoreMember:
        case InstructionKind::StoreIndex:
            if (!instruction.operands.empty()) consume(instruction.operands.back());
            break;
        case InstructionKind::IterInit:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
            break;
        case InstructionKind::Unary:
        case InstructionKind::Binary:
        case InstructionKind::Borrow:
        case InstructionKind::Member:
        case InstructionKind::Index:
        case InstructionKind::MakeRange:
        case InstructionKind::IterHasNext:
        case InstructionKind::IterNext:
        case InstructionKind::Drop:
        case InstructionKind::DropIf:
            break;
    }
    return result;
}

std::vector<LocalId> consumedByTerminator(const Terminator& terminator) {
    if (terminator.kind == TerminatorKind::Return && terminator.value.kind == OperandKind::Local &&
        !terminator.value.type.isCopy()) {
        return {terminator.value.local};
    }
    return {};
}

void transferInstruction(StateVector& state, const Instruction& instruction) {
    for (const auto local : consumedByInstruction(instruction)) {
        if (local < state.size()) state[local] = Moved;
    }
    defineDestination(state, instruction);
}

void transferTerminator(StateVector& state, const Terminator& terminator) {
    for (const auto local : consumedByTerminator(terminator)) {
        if (local < state.size()) state[local] = Moved;
    }
}

bool stateEqual(const StateVector& a, const StateVector& b) { return a == b; }

std::vector<const Loan*> activeLoans(const FunctionFlowInfo& info,
                                     const LocalSet& live,
                                     const std::unordered_map<std::size_t, std::set<LocalId>>& carriers) {
    std::vector<const Loan*> result;
    for (const auto& loan : info.loans) {
        const auto found = carriers.find(loan.id);
        if (found == carriers.end()) continue;
        bool active = false;
        for (const auto carrier : found->second) {
            if (live.contains(carrier)) {
                active = true;
                break;
            }
        }
        if (active) result.push_back(&loan);
    }
    return result;
}

bool hasLoanForRoot(const std::vector<const Loan*>& loans, LocalId root, bool require_mutable = false) {
    return std::any_of(loans.begin(), loans.end(), [&](const Loan* loan) {
        return loan->root == root && (!require_mutable || loan->is_mutable);
    });
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, SourceSpan span, std::string message) {
    diagnostics.push_back(Diagnostic{.message = std::move(message), .span = span});
}

void checkAvailableUse(const Function& function, const StateVector& state, const Operand& operand,
                       SourceSpan span, std::vector<Diagnostic>& diagnostics) {
    if (operand.kind != OperandKind::Local || operand.local >= state.size()) return;
    const Local* local = findLocal(function, operand.local);
    if (!local) return;
    const unsigned value = state[operand.local];
    if (!unavailable(value)) return;
    if (maybeUnavailable(value)) {
        if ((value & Moved) != 0U && (value & Uninitialized) == 0U) {
            addDiagnostic(diagnostics, span, "value `" + local->name + "` may have been moved on another control-flow path");
        } else {
            addDiagnostic(diagnostics, span, "value `" + local->name + "` may be unavailable on another control-flow path");
        }
    } else if ((value & Moved) != 0U) {
        addDiagnostic(diagnostics, span, "use of moved value `" + local->name + "`");
    } else {
        addDiagnostic(diagnostics, span, "use of uninitialized value `" + local->name + "`");
    }
}

} // namespace

FlowResult FlowChecker::check(const Program& program) const {
    FlowResult result;
    for (const auto& function : program.functions) {
        result.functions.emplace(functionKey(function), analyzeFunction(function, result.diagnostics));
    }
    return result;
}

FunctionFlowInfo FlowChecker::analyzeFunction(const Function& function,
                                              std::vector<Diagnostic>& diagnostics) const {
    FunctionFlowInfo info;
    info.liveness = LivenessAnalysis{}.analyze(function);

    // Discover loans created by MIR borrow instructions.
    std::size_t next_loan = 0;
    std::unordered_map<LocalId, std::set<std::size_t>> local_loans;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind != InstructionKind::Borrow || !instruction.destination || instruction.operands.empty()) continue;
            const auto& source = instruction.operands.front();
            if (source.kind != OperandKind::Local) continue;
            Loan loan{
                .id = next_loan++,
                .root = source.local,
                .reference = *instruction.destination,
                .is_mutable = instruction.name == "mut",
                .span = instruction.span,
            };
            local_loans[*instruction.destination].insert(loan.id);
            info.loans.push_back(loan);
        }
    }

    // Propagate loan identity through assignments of references.
    bool loan_changed = true;
    while (loan_changed) {
        loan_changed = false;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.kind != InstructionKind::Assign || !instruction.destination || instruction.operands.empty()) continue;
                const Local* destination = findLocal(function, *instruction.destination);
                const auto& source = instruction.operands.front();
                if (!destination || !destination->type.isReference() || source.kind != OperandKind::Local) continue;
                const auto source_found = local_loans.find(source.local);
                if (source_found == local_loans.end()) continue;
                auto& target = local_loans[*instruction.destination];
                const auto old_size = target.size();
                target.insert(source_found->second.begin(), source_found->second.end());
                if (target.size() != old_size) loan_changed = true;
            }
        }
    }

    std::unordered_map<std::size_t, std::set<LocalId>> carriers;
    for (const auto& [local, loans] : local_loans) {
        for (const auto loan : loans) carriers[loan].insert(local);
    }

    // Move/uninitialized forward dataflow.
    const std::size_t count = stateSize(function);
    const auto preds = predecessors(function);
    std::unordered_map<BlockId, StateVector> in_state;
    std::unordered_map<BlockId, StateVector> out_state;
    for (const auto& block : function.blocks) {
        in_state[block.id] = StateVector(count, Uninitialized);
        out_state[block.id] = StateVector(count, Uninitialized);
    }
    in_state[function.entry] = entryState(function);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : function.blocks) {
            StateVector incoming;
            if (block.id == function.entry) {
                incoming = entryState(function);
            } else {
                std::vector<StateVector> inputs;
                if (const auto it = preds.find(block.id); it != preds.end()) {
                    for (const auto pred : it->second) inputs.push_back(out_state[pred]);
                }
                incoming = joinStates(inputs, count);
            }
            StateVector state = incoming;
            for (const auto& instruction : block.instructions) transferInstruction(state, instruction);
            if (block.terminator) transferTerminator(state, *block.terminator);
            if (!stateEqual(in_state[block.id], incoming) || !stateEqual(out_state[block.id], state)) {
                in_state[block.id] = std::move(incoming);
                out_state[block.id] = std::move(state);
                changed = true;
            }
        }
    }

    // Diagnostic pass, now using stable move states and liveness-derived NLL loan activity.
    for (const auto& block : function.blocks) {
        StateVector state = in_state[block.id];
        const auto live_it = info.liveness.blocks.find(block.id);
        if (live_it == info.liveness.blocks.end()) continue;
        const auto& live = live_it->second;

        for (std::size_t i = 0; i < block.instructions.size(); ++i) {
            const auto& instruction = block.instructions[i];
            const auto active = activeLoans(info, live.live_before[i], carriers);

            // All ordinary operand reads require the source value to be available.
            for (const auto& operand : instruction.operands) checkAvailableUse(function, state, operand, instruction.span, diagnostics);

            if (instruction.kind == InstructionKind::Borrow && !instruction.operands.empty() &&
                instruction.operands.front().kind == OperandKind::Local) {
                const auto root = instruction.operands.front().local;
                const bool mutable_borrow = instruction.name == "mut";
                if (root < state.size() && unavailable(state[root])) {
                    const Local* local = findLocal(function, root);
                    if (local) addDiagnostic(diagnostics, instruction.span, "cannot borrow unavailable value `" + local->name + "`");
                }
                if (mutable_borrow ? hasLoanForRoot(active, root) : hasLoanForRoot(active, root, true)) {
                    const Local* local = findLocal(function, root);
                    if (local) {
                        addDiagnostic(diagnostics, instruction.span,
                                      mutable_borrow
                                          ? "cannot mutably borrow `" + local->name + "` because it is already borrowed"
                                          : "cannot immutably borrow `" + local->name + "` while it is mutably borrowed");
                    }
                }
            }

            // Direct writes to an owner conflict with any live loan of that owner.
            if (instruction.kind == InstructionKind::Assign && instruction.destination &&
                hasLoanForRoot(active, *instruction.destination)) {
                if (const Local* local = findLocal(function, *instruction.destination)) {
                    addDiagnostic(diagnostics, instruction.span, "cannot assign to `" + local->name + "` while it is borrowed");
                }
            }
            if ((instruction.kind == InstructionKind::StoreMember || instruction.kind == InstructionKind::StoreIndex) &&
                !instruction.operands.empty() && instruction.operands.front().kind == OperandKind::Local) {
                const auto root = instruction.operands.front().local;
                if (hasLoanForRoot(active, root)) {
                    if (const Local* local = findLocal(function, root)) {
                        addDiagnostic(diagnostics, instruction.span, "cannot assign to `" + local->name + "` while it is borrowed");
                    }
                }
            }

            // Reading the owner directly is forbidden only while an exclusive loan is live.
            if (instruction.kind != InstructionKind::Borrow) {
                for (const auto& operand : instruction.operands) {
                    if (operand.kind == OperandKind::Local && hasLoanForRoot(active, operand.local, true)) {
                        if (const Local* local = findLocal(function, operand.local)) {
                            addDiagnostic(diagnostics, instruction.span,
                                          "cannot use `" + local->name + "` while it is mutably borrowed");
                        }
                    }
                }
            }

            for (const auto moved : consumedByInstruction(instruction)) {
                if (hasLoanForRoot(active, moved)) {
                    if (const Local* local = findLocal(function, moved)) {
                        addDiagnostic(diagnostics, instruction.span, "cannot move `" + local->name + "` while it is borrowed");
                    }
                }
            }

            transferInstruction(state, instruction);
        }

        info.definitely_available_before_terminator[block.id] = LocalSet{};
        info.maybe_available_before_terminator[block.id] = LocalSet{};
        for (const auto& local : function.locals) {
            if (local.id >= state.size()) continue;
            if (state[local.id] == Available) {
                info.definitely_available_before_terminator[block.id].insert(local.id);
            }
            if ((state[local.id] & Available) != 0U) {
                info.maybe_available_before_terminator[block.id].insert(local.id);
            }
        }

        if (block.terminator) {
            const auto active = activeLoans(info, live.terminator_live_before, carriers);
            if (block.terminator->kind == TerminatorKind::Return) {
                checkAvailableUse(function, state, block.terminator->value, block.terminator->span, diagnostics);
                for (const auto moved : consumedByTerminator(*block.terminator)) {
                    if (hasLoanForRoot(active, moved)) {
                        if (const Local* local = findLocal(function, moved)) {
                            addDiagnostic(diagnostics, block.terminator->span,
                                          "cannot move `" + local->name + "` while it is borrowed");
                        }
                    }
                }
            } else if (block.terminator->kind == TerminatorKind::Branch) {
                checkAvailableUse(function, state, block.terminator->condition, block.terminator->span, diagnostics);
            }
            transferTerminator(state, *block.terminator);
        }
    }

    return info;
}

} // namespace nus::mir
