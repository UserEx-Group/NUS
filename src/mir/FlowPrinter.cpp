#include "nus/mir/FlowPrinter.hpp"

#include <sstream>

namespace nus::mir {
namespace {

std::string key(const Function& function) {
    return function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
}

std::string setText(const LocalSet& set) {
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto local : set) {
        if (!first) out << ", ";
        first = false;
        out << '%' << local;
    }
    out << '}';
    return out.str();
}

} // namespace

std::string FlowPrinter::print(const Program& program, const FlowResult& result) const {
    std::ostringstream out;
    out << "MIR FLOW\n";
    for (const auto& function : program.functions) {
        const auto name = key(function);
        out << "  fn " << name << "\n";
        const auto found = result.functions.find(name);
        if (found == result.functions.end()) continue;
        const auto& info = found->second;
        out << "    loans:\n";
        for (const auto& loan : info.loans) {
            out << "      L" << loan.id << ": %" << loan.reference << " -> %" << loan.root
                << (loan.is_mutable ? " mutable" : " shared") << '\n';
        }
        for (const auto& block : function.blocks) {
            const auto live = info.liveness.blocks.find(block.id);
            if (live == info.liveness.blocks.end()) continue;
            out << "    bb" << block.id << ": live_in=" << setText(live->second.live_in)
                << " live_out=" << setText(live->second.live_out) << '\n';
            for (std::size_t i = 0; i < block.instructions.size(); ++i) {
                out << "      #" << i << " before=" << setText(live->second.live_before[i])
                    << " after=" << setText(live->second.live_after[i]) << '\n';
            }
        }
    }
    return out.str();
}

} // namespace nus::mir
