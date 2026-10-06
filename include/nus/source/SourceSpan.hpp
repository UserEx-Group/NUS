#pragma once

#include <cstddef>
#include <cstdint>

namespace nus {

using FileId = std::uint32_t;
inline constexpr FileId InvalidFileId = static_cast<FileId>(-1);

struct SourceSpan {
    FileId file{InvalidFileId};
    std::size_t start{0};
    std::size_t end{0}; // half-open: [start, end)

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return end >= start ? end - start : 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size() == 0;
    }
};

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};
};

} // namespace nus
