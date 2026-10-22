#include "nus/codegen/Layout.hpp"

#include <algorithm>
#include <stdexcept>

namespace nus::codegen {

std::size_t LayoutEngine::alignTo(std::size_t value, std::size_t alignment) {
    if (alignment <= 1) return value;
    const auto remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

const mir::Struct& LayoutEngine::findStruct(const std::string& name) const {
    for (const auto& structure : program_.structs) {
        if (structure.name == name) return structure;
    }
    throw std::runtime_error("layout: unknown struct `" + name + "`");
}

TypeLayout LayoutEngine::layoutOf(const sema::Type& type) const {
    using sema::TypeKind;
    const std::size_t pointer = target_.pointer_bits / 8U;
    switch (type.kind) {
        case TypeKind::Error:
        case TypeKind::Unknown:
            throw std::runtime_error("layout: unresolved type `" + type.name() + "`");
        case TypeKind::Unit: return {0, 1};
        case TypeKind::Bool:
        case TypeKind::I8:
        case TypeKind::U8: return {1, 1};
        case TypeKind::I16:
        case TypeKind::U16: return {2, 2};
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::F32:
        case TypeKind::Char: return {4, 4};
        case TypeKind::I64:
        case TypeKind::U64:
        case TypeKind::F64: return {8, 8};
        case TypeKind::ISize:
        case TypeKind::USize:
        case TypeKind::Reference:
        case TypeKind::Function: return {pointer, pointer};
        case TypeKind::String:
        case TypeKind::Vec: return {pointer * 3U, pointer};
        case TypeKind::Option: {
            if (!type.element) throw std::runtime_error("layout: malformed Option");
            const auto payload = layoutOf(*type.element);
            const std::size_t offset = alignTo(1, payload.alignment);
            return {alignTo(offset + payload.size, payload.alignment), payload.alignment};
        }
        case TypeKind::Result: {
            if (type.generic_arguments.size() != 2) throw std::runtime_error("layout: malformed Result");
            const auto ok = layoutOf(type.generic_arguments[0]);
            const auto err = layoutOf(type.generic_arguments[1]);
            const std::size_t alignment = std::max({std::size_t{1}, ok.alignment, err.alignment});
            std::size_t size = 1;
            size = alignTo(size, ok.alignment) + ok.size;
            size = alignTo(size, err.alignment) + err.size;
            return {alignTo(size, alignment), alignment};
        }
        case TypeKind::TypeParameter:
            throw std::runtime_error("layout: unmonomorphized type parameter `" + type.name() + "`");
        case TypeKind::Bytes:
        case TypeKind::Slice: return {pointer * 2U, pointer};
        case TypeKind::Range: {
            if (!type.element) throw std::runtime_error("layout: malformed range");
            const auto item = layoutOf(*type.element);
            std::size_t size = 0;
            size = alignTo(size, item.alignment) + item.size;
            size = alignTo(size, item.alignment) + item.size;
            size = alignTo(size, 1) + 1;
            return {alignTo(size, item.alignment), item.alignment};
        }
        case TypeKind::Array: {
            if (!type.element || !type.array_length) throw std::runtime_error("layout: unsized array");
            const auto item = layoutOf(*type.element);
            const auto stride = alignTo(item.size, item.alignment);
            return {stride * *type.array_length, item.alignment};
        }
        case TypeKind::Struct:
            return layoutOfStruct(type.nominal_name).layout;
    }
    throw std::runtime_error("layout: unsupported type");
}

StructLayout LayoutEngine::layoutOfStruct(const std::string& name) const {
    const auto& structure = findStruct(name);
    StructLayout result;
    result.name = name;
    std::size_t offset = 0;
    std::size_t max_alignment = 1;
    for (const auto& field : structure.fields) {
        const auto field_layout = layoutOf(field.type);
        offset = alignTo(offset, field_layout.alignment);
        result.fields.push_back(FieldLayout{field.name, offset, field_layout});
        offset += field_layout.size;
        max_alignment = std::max(max_alignment, field_layout.alignment);
    }
    result.layout = {alignTo(offset, max_alignment), max_alignment};
    return result;
}

} // namespace nus::codegen
