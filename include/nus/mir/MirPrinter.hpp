#pragma once

#include "nus/mir/Mir.hpp"

#include <string>

namespace nus::mir {

class MirPrinter {
public:
    [[nodiscard]] std::string print(const Program& program) const;
};

} // namespace nus::mir
