#pragma once

#include "nus/source/SourceSpan.hpp"

#include <string>

namespace nus {

struct Diagnostic {
    std::string message;
    SourceSpan span{};
};

} // namespace nus
