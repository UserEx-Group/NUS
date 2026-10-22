#include "nus/codegen/TargetInfo.hpp"

#include <sstream>

namespace nus::codegen {

TargetInfo TargetInfo::host() {
    TargetInfo target;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    target.triple = "x86_64-w64-windows-gnu";
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    target.triple = "aarch64-w64-windows-gnu";
#elif defined(__APPLE__) && defined(__aarch64__)
    target.triple = "arm64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
    target.triple = "x86_64-apple-darwin";
#elif defined(__linux__) && defined(__aarch64__)
    target.triple = "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
    target.triple = "x86_64-unknown-linux-gnu";
#else
    target.triple.clear();
#endif
    return target;
}

std::string TargetInfo::description() const {
    std::ostringstream out;
    out << (triple.empty() ? "host" : triple) << " (pointer width: " << pointer_bits << " bits)";
    return out.str();
}

} // namespace nus::codegen
