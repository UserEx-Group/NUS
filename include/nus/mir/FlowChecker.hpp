#pragma once

#include "nus/diagnostics/Diagnostic.hpp"
#include "nus/mir/Dataflow.hpp"
#include "nus/mir/Mir.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace nus::mir {

struct Loan {
    std::size_t id{0};
    LocalId root{InvalidLocalId};
    LocalId reference{InvalidLocalId};
    bool is_mutable{false};
    SourceSpan span{};
};

struct FunctionFlowInfo {
    LivenessResult liveness;
    std::vector<Loan> loans;
    std::unordered_map<BlockId, LocalSet> definitely_available_before_terminator;
    std::unordered_map<BlockId, LocalSet> maybe_available_before_terminator;
};

struct FlowResult {
    std::vector<Diagnostic> diagnostics;
    std::unordered_map<std::string, FunctionFlowInfo> functions;

    [[nodiscard]] bool hasErrors() const noexcept { return !diagnostics.empty(); }
};

class FlowChecker {
public:
    [[nodiscard]] FlowResult check(const Program& program) const;

private:
    [[nodiscard]] FunctionFlowInfo analyzeFunction(const Function& function,
                                                   std::vector<Diagnostic>& diagnostics) const;
};

} // namespace nus::mir
