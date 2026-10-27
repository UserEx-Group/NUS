#pragma once

#include "nus/codegen/TargetInfo.hpp"
#include "nus/mir/Mir.hpp"
#include "nus/sema/Type.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace nus::codegen {

struct TypeLayout {
    std::size_t size{0};
    std::size_t alignment{1};
};

struct FieldLayout {
    std::string name;
    std::size_t offset{0};
    TypeLayout layout;
};

struct StructLayout {
    std::string name;
    TypeLayout layout;
    std::vector<FieldLayout> fields;
};

class LayoutEngine {
public:
    LayoutEngine(const mir::Program& program, TargetInfo target = TargetInfo::host())
        : program_(program), target_(std::move(target)) {}

    [[nodiscard]] TypeLayout layoutOf(const sema::Type& type) const;
    [[nodiscard]] StructLayout layoutOfStruct(const std::string& name) const;

private:
    [[nodiscard]] const mir::Struct& findStruct(const std::string& name) const;
    [[nodiscard]] const mir::Enum& findEnum(const std::string& name) const;
    [[nodiscard]] static std::size_t alignTo(std::size_t value, std::size_t alignment);

    const mir::Program& program_;
    TargetInfo target_;
};

} // namespace nus::codegen
