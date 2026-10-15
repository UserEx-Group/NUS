#pragma once

#include <cstddef>
#include <string>

namespace nus::codegen {

struct TargetInfo {
    std::size_t pointer_bits{sizeof(void*) * 8U};
    std::string triple;

    [[nodiscard]] static TargetInfo host();
    [[nodiscard]] std::string description() const;
};

} // namespace nus::codegen
