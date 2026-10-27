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
        case InstructionKind::MakeVariant:
            for (const auto& operand : instruction.operands) consume(operand);
            break;
        case InstructionKind::StoreMember:
        case InstructionKind::StoreIndex:
        case InstructionKind::StoreDeref:
            if (!instruction.operands.empty()) consume(instruction.operands.back());
            break;
        case InstructionKind::IterInit:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
            break;
        case InstructionKind::MakeReadyFuture:
        case InstructionKind::Await:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
            break;
        case InstructionKind::Consume:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
            break;
        case InstructionKind::Unary:
        case InstructionKind::Binary:
        case InstructionKind::Borrow:
        case InstructionKind::Member:
        case InstructionKind::Index:
        case InstructionKind::MakeRange:
        case InstructionKind::GetDiscriminant:
        case InstructionKind::GetVariantField:
        case InstructionKind::IterHasNext:
        case InstructionKind::IterNext:
            break;
        case InstructionKind::Drop:
        case InstructionKind::DropIf:
            if (!instruction.operands.empty()) consume(instruction.operands.front());
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
                                     const std::unordered_map<std::size_t, std::set<LocalId>>& carriers,
                                     const std::set<std::size_t>& activated_pins) {
    std::vector<const Loan*> result;
    for (const auto& loan : info.loans) {
        if (loan.is_pin && !activated_pins.contains(loan.id)) continue;
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
        if (loan->is_pin || loan->root != root) return false;
        return !require_mutable || loan->is_mutable;
    });
}

bool hasProtectionForRoot(const std::vector<const Loan*>& loans, LocalId root) {
    return std::any_of(loans.begin(), loans.end(), [&](const Loan* loan) {
        return loan->root == root;
    });
}

bool isReactorRegistration(const Instruction& instruction) {
    if (instruction.kind != InstructionKind::Call || instruction.operands.empty()) return false;
    const auto& callee = instruction.operands.front();
    if (callee.kind != OperandKind::Global) return false;
    return callee.text == "reactor_register_tcp" || callee.text == "reactor_register_listener" ||
           callee.text == "reactor_register_udp";
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
                .is_pin = false,
                .span = instruction.span,
            };
            local_loans[*instruction.destination].insert(loan.id);
            info.loans.push_back(loan);
        }
    }

    // Propagate loan identity through reference values and borrow-carrying values.
    // This is the MIR-level lifetime provenance used by NLL: a call such as
    // `values.iter()` receives a temporary &Vec<T>, but the resulting iterator
    // keeps that same loan alive until the iterator's last use. Reactor values
    // also carry synthetic resource pins created below.
    auto propagateLoans = [&]() {
        bool loan_changed = true;
        while (loan_changed) {
            loan_changed = false;
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (!instruction.destination) continue;
                    const Local* destination = findLocal(function, *instruction.destination);
                    if (!destination || !destination->type.carriesBorrow()) continue;

                    std::size_t first_source = 0;
                    bool propagates = instruction.kind == InstructionKind::Assign ||
                                      instruction.kind == InstructionKind::IterInit ||
                                      instruction.kind == InstructionKind::IterNext;
                    if (instruction.kind == InstructionKind::Call) {
                        propagates = true;
                        first_source = 1; // operand 0 is the callee
                    }
                    if (!propagates) continue;

                    auto& target = local_loans[*instruction.destination];
                    const auto old_size = target.size();
                    for (std::size_t i = first_source; i < instruction.operands.size(); ++i) {
                        const auto& source = instruction.operands[i];
                        if (source.kind != OperandKind::Local) continue;
                        const auto source_found = local_loans.find(source.local);
                        if (source_found == local_loans.end()) continue;
                        target.insert(source_found->second.begin(), source_found->second.end());
                    }
                    if (target.size() != old_size) loan_changed = true;
                }
            }
        }
    };

    propagateLoans();

    // A Reactor stores operating-system handles, but it must not impose a Rust-style
    // shared borrow on a registered socket: callers still need `&mut TcpStream` for
    // send/receive after readiness. Instead registration creates an internal resource
    // pin. A pin prevents move/drop/reassignment of the registered owner while the
    // Reactor is live, but it deliberately does not participate in ordinary borrow
    // conflicts.
    struct PinSite {
        BlockId block{InvalidBlockId};
        std::size_t instruction{0};
        std::size_t loan{0};
    };
    std::vector<PinSite> pin_sites;

    auto rootsForCarrier = [&](LocalId local, bool pins_allowed) {
        std::set<LocalId> roots;
        const auto found = local_loans.find(local);
        if (found == local_loans.end()) return roots;
        for (const auto loan_id : found->second) {
            if (loan_id >= info.loans.size()) continue;
            const auto& loan = info.loans[loan_id];
            if (!pins_allowed && loan.is_pin) continue;
            roots.insert(loan.root);
        }
        return roots;
    };

    for (const auto& block : function.blocks) {
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            if (!isReactorRegistration(instruction) || instruction.operands.size() < 3) continue;
            const auto& reactor_ref = instruction.operands[1];
            const auto& resource_ref = instruction.operands[2];
            if (reactor_ref.kind != OperandKind::Local || resource_ref.kind != OperandKind::Local) continue;

            const auto reactor_roots = rootsForCarrier(reactor_ref.local, false);
            const auto resource_roots = rootsForCarrier(resource_ref.local, false);
            if (reactor_roots.empty() || resource_roots.empty()) {
                addDiagnostic(diagnostics, instruction.span,
                              "reactor registration requires directly traceable borrowed resources in milestone 17");
                continue;
            }

            for (const auto reactor_root : reactor_roots) {
                const Local* reactor_local = findLocal(function, reactor_root);
                if (!reactor_local || reactor_local->type.kind != sema::TypeKind::Reactor) continue;
                for (const auto resource_root : resource_roots) {
                    Loan pin{
                        .id = next_loan++,
                        .root = resource_root,
                        .reference = reactor_root,
                        .is_mutable = false,
                        .is_pin = true,
                        .span = instruction.span,
                    };
                    info.loans.push_back(pin);
                    local_loans[reactor_root].insert(pin.id);
                    pin_sites.push_back(PinSite{
                        .block = block.id,
                        .instruction = instruction_index,
                        .loan = pin.id,
                    });
                }
            }
        }
    }

    // A Reactor can itself move after registration; propagate its pins to the new owner.
    propagateLoans();

    std::unordered_map<std::size_t, std::set<LocalId>> carriers;
    for (const auto& [local, loans] : local_loans) {
        for (const auto loan : loans) carriers[loan].insert(local);
    }

    // Pins begin only after a successful registration call is attempted. Track that
    // program point with a may-dataflow so a Reactor that crosses branches/loops keeps
    // protecting every handle that may already have been registered.
    std::unordered_map<BlockId, std::set<std::size_t>> pins_in;
    std::unordered_map<BlockId, std::set<std::size_t>> pins_out;
    std::unordered_map<BlockId, std::unordered_map<std::size_t, std::set<std::size_t>>> pins_created_at;
    for (const auto& block : function.blocks) {
        pins_in[block.id] = {};
        pins_out[block.id] = {};
    }
    for (const auto& site : pin_sites) pins_created_at[site.block][site.instruction].insert(site.loan);

    const auto pin_preds = predecessors(function);
    bool pins_changed = true;
    while (pins_changed) {
        pins_changed = false;
        for (const auto& block : function.blocks) {
            std::set<std::size_t> incoming;
            if (block.id != function.entry) {
                if (const auto it = pin_preds.find(block.id); it != pin_preds.end()) {
                    for (const auto pred : it->second) {
                        const auto& predecessor_pins = pins_out[pred];
                        incoming.insert(predecessor_pins.begin(), predecessor_pins.end());
                    }
                }
            }
            auto outgoing = incoming;
            if (const auto created_block = pins_created_at.find(block.id); created_block != pins_created_at.end()) {
                for (const auto& [_, created] : created_block->second) {
                    outgoing.insert(created.begin(), created.end());
                }
            }
            if (pins_in[block.id] != incoming || pins_out[block.id] != outgoing) {
                pins_in[block.id] = std::move(incoming);
                pins_out[block.id] = std::move(outgoing);
                pins_changed = true;
            }
        }
    }

    // Move/uninitialized forward dataflow.
    const std::size_t count = stateSize(function);
    const auto preds = predecessors(function);
    std::unordered_map<BlockId, StateVector> in_state;
    std::unordered_map<BlockId, StateVector> out_state;
    // Start non-entry blocks at the dataflow bottom. Initializing loop predecessors as
    // `Uninitialized` permanently polluted joins with a path that does not actually
    // exist yet, producing false "may be unavailable" diagnostics for loop-carried
    // non-Copy iterators.
    for (const auto& block : function.blocks) {
        in_state[block.id] = StateVector(count, 0U);
        out_state[block.id] = StateVector(count, 0U);
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
        auto activated_pins = pins_in[block.id];

        for (std::size_t i = 0; i < block.instructions.size(); ++i) {
            const auto& instruction = block.instructions[i];
            const auto active = activeLoans(info, live.live_before[i], carriers, activated_pins);

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
                hasProtectionForRoot(active, *instruction.destination)) {
                if (const Local* local = findLocal(function, *instruction.destination)) {
                    addDiagnostic(diagnostics, instruction.span, "cannot assign to `" + local->name + "` while it is borrowed or reactor-pinned");
                }
            }
            if ((instruction.kind == InstructionKind::StoreMember || instruction.kind == InstructionKind::StoreIndex || instruction.kind == InstructionKind::StoreDeref) &&
                !instruction.operands.empty() && instruction.operands.front().kind == OperandKind::Local) {
                const auto root = instruction.operands.front().local;
                if (hasProtectionForRoot(active, root)) {
                    if (const Local* local = findLocal(function, root)) {
                        addDiagnostic(diagnostics, instruction.span, "cannot assign to `" + local->name + "` while it is borrowed or reactor-pinned");
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
                if (hasProtectionForRoot(active, moved)) {
                    if (const Local* local = findLocal(function, moved)) {
                        addDiagnostic(diagnostics, instruction.span, "cannot move `" + local->name + "` while it is borrowed or reactor-pinned");
                    }
                }
            }

            transferInstruction(state, instruction);
            if (const auto block_created = pins_created_at.find(block.id); block_created != pins_created_at.end()) {
                if (const auto at = block_created->second.find(i); at != block_created->second.end()) {
                    activated_pins.insert(at->second.begin(), at->second.end());
                }
            }
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
            const auto active = activeLoans(info, live.terminator_live_before, carriers, activated_pins);
            if (block.terminator->kind == TerminatorKind::Return) {
                checkAvailableUse(function, state, block.terminator->value, block.terminator->span, diagnostics);
                for (const auto moved : consumedByTerminator(*block.terminator)) {
                    if (hasProtectionForRoot(active, moved)) {
                        if (const Local* local = findLocal(function, moved)) {
                            addDiagnostic(diagnostics, block.terminator->span,
                                          "cannot move `" + local->name + "` while it is borrowed or reactor-pinned");
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
