#pragma once

#include "nus/mir/FlowChecker.hpp"
#include "nus/mir/Mir.hpp"

namespace nus::mir {

class DropElaborator {
public:
    void run(Program& program, const FlowResult& flow) const;

private:
    void run(Function& function, const FunctionFlowInfo* flow) const;
};

} // namespace nus::mir
