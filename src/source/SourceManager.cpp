#include "nus/source/SourceManager.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace nus {

FileId SourceManager::addSource(std::filesystem::path path, std::string content) {
    if (files_.size() >= static_cast<std::size_t>(InvalidFileId)) {
        throw std::runtime_error("too many source files");
    }

    SourceFile source_file{
        .path = std::move(path),
        .content = std::move(content),
        .line_starts = {},
    };
    source_file.line_starts = computeLineStarts(source_file.content);

    files_.push_back(std::move(source_file));
    return static_cast<FileId>(files_.size() - 1);
}

FileId SourceManager::loadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open source file: " + path.string());
    }

    std::string content(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );

    return addSource(path, std::move(content));
}

std::string_view SourceManager::content(FileId id) const {
    return file(id).content;
}

std::string_view SourceManager::text(SourceSpan span) const {
    const auto source = content(span.file);
    if (span.start > span.end || span.end > source.size()) {
        throw std::out_of_range("invalid source span");
    }
    return source.substr(span.start, span.size());
}

const std::filesystem::path& SourceManager::path(FileId id) const {
    return file(id).path;
}

SourceLocation SourceManager::location(FileId id, std::size_t offset) const {
    const auto& source_file = file(id);
    if (offset > source_file.content.size()) {
        throw std::out_of_range("source offset out of range");
    }

    const auto it = std::upper_bound(
        source_file.line_starts.begin(),
        source_file.line_starts.end(),
        offset
    );

    const auto line_index = static_cast<std::size_t>(
        std::distance(source_file.line_starts.begin(), it) - 1
    );
    const auto line_start = source_file.line_starts[line_index];

    return SourceLocation{
        .line = line_index + 1,
        .column = offset - line_start + 1,
    };
}

std::size_t SourceManager::fileCount() const noexcept {
    return files_.size();
}

const SourceManager::SourceFile& SourceManager::file(FileId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (id == InvalidFileId || index >= files_.size()) {
        throw std::out_of_range("invalid file id");
    }
    return files_[index];
}

std::vector<std::size_t> SourceManager::computeLineStarts(std::string_view source) {
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

} // namespace nus
