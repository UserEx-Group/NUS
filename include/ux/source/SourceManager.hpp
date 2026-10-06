#pragma once

#include "nus/source/SourceSpan.hpp"

#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nus {

class SourceManager {
public:
    FileId addSource(std::filesystem::path path, std::string content);
    FileId loadFile(const std::filesystem::path& path);

    [[nodiscard]] std::string_view content(FileId id) const;
    [[nodiscard]] std::string_view text(SourceSpan span) const;
    [[nodiscard]] const std::filesystem::path& path(FileId id) const;
    [[nodiscard]] SourceLocation location(FileId id, std::size_t offset) const;
    [[nodiscard]] std::size_t fileCount() const noexcept;

private:
    struct SourceFile {
        std::filesystem::path path;
        std::string content;
        std::vector<std::size_t> line_starts;
    };

    [[nodiscard]] const SourceFile& file(FileId id) const;
    static std::vector<std::size_t> computeLineStarts(std::string_view source);

    std::deque<SourceFile> files_;
};

} // namespace nus
