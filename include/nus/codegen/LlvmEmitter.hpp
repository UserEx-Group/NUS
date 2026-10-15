#pragma once

#include "nus/codegen/TargetInfo.hpp"
#include "nus/mir/Mir.hpp"

#include <string>

namespace nus::codegen {

class LlvmEmitter {
public:
    [[nodiscard]] std::string emit(const mir::Program& program,
                                   TargetInfo target = TargetInfo::host());
};

} // namespace nus::codegen
