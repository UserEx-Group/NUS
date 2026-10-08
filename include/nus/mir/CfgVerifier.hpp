#pragma once

#include "nus/mir/Mir.hpp"

#include <string>
#include <vector>

namespace nus::mir {

class CfgVerifier {
public:
    [[nodiscard]] std::vector<std::string> verify(const Program& program) const;
};

} // namespace nus::mir
