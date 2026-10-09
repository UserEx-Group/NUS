#pragma once

#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/Mir.hpp"

#include <string>

namespace nus::mir {

class FlowPrinter {
public:
    [[nodiscard]] std::string print(const Program& program, const FlowResult& result) const;
};

} // namespace nus::mir
