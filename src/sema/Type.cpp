#include "nus/sema/Type.hpp"

#include <sstream>
#include <utility>

namespace nus::sema {

Type Type::simple(TypeKind kind) {
    Type type;
    type.kind = kind;
    return type;
}

Type Type::structure(std::string name) {
    Type type;
    type.kind = TypeKind::Struct;
    type.nominal_name = std::move(name);
    return type;
}

Type Type::array(Type element_type, std::optional<std::size_t> length) {
    Type type;
    type.kind = TypeKind::Array;
    type.element = std::make_shared<Type>(std::move(element_type));
    type.array_length = length;
    return type;
}

Type Type::slice(Type element_type) {
    Type type;
    type.kind = TypeKind::Slice;
    type.element = std::make_shared<Type>(std::move(element_type));
    return type;
}

Type Type::range(Type element_type) {
    Type type;
    type.kind = TypeKind::Range;
    type.element = std::make_shared<Type>(std::move(element_type));
    return type;
}

Type Type::reference(Type element_type, bool is_mutable) {
    Type type;
    type.kind = TypeKind::Reference;
    type.element = std::make_shared<Type>(std::move(element_type));
    type.mutable_reference = is_mutable;
    return type;
}

Type Type::function(std::vector<Type> parameter_types, Type result, bool is_variadic_any) {
    Type type;
    type.kind = TypeKind::Function;
    type.parameters = std::move(parameter_types);
    type.return_type = std::make_shared<Type>(std::move(result));
    type.variadic_any = is_variadic_any;
    return type;
}

bool Type::isError() const noexcept {
    return kind == TypeKind::Error;
}

bool Type::isNumeric() const noexcept {
    return isInteger() || isFloat();
}

bool Type::isInteger() const noexcept {
    switch (kind) {
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::ISize:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
        case TypeKind::USize:
            return true;
        default:
            return false;
    }
}

bool Type::isSignedInteger() const noexcept {
    switch (kind) {
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::ISize:
            return true;
        default:
            return false;
    }
}

bool Type::isFloat() const noexcept {
    return kind == TypeKind::F32 || kind == TypeKind::F64;
}

bool Type::isBool() const noexcept {
    return kind == TypeKind::Bool;
}

bool Type::isUnit() const noexcept {
    return kind == TypeKind::Unit;
}

bool Type::isStruct() const noexcept {
    return kind == TypeKind::Struct;
}

bool Type::isReference() const noexcept {
    return kind == TypeKind::Reference;
}

bool Type::isCopy() const noexcept {
    switch (kind) {
        case TypeKind::Error:
        case TypeKind::Unknown:
        case TypeKind::Unit:
        case TypeKind::Bool:
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::ISize:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
        case TypeKind::USize:
        case TypeKind::F32:
        case TypeKind::F64:
        case TypeKind::Char:
        case TypeKind::Slice:
        case TypeKind::Function:
            return true;
        case TypeKind::Reference:
            return !mutable_reference;
        case TypeKind::Range:
            return !element || element->isCopy();
        case TypeKind::Array:
            return element && element->isCopy();
        case TypeKind::String:
        case TypeKind::Bytes:
        case TypeKind::Struct:
            return false;
    }
    return false;
}

bool Type::containsReference() const noexcept {
    if (kind == TypeKind::Reference) return true;
    if ((kind == TypeKind::Array || kind == TypeKind::Slice || kind == TypeKind::Range) && element) {
        return element->containsReference();
    }
    if (kind == TypeKind::Function) {
        for (const auto& parameter : parameters) {
            if (parameter.containsReference()) return true;
        }
        return return_type && return_type->containsReference();
    }
    return false;
}

std::string Type::name() const {
    switch (kind) {
        case TypeKind::Error: return "<error>";
        case TypeKind::Unknown: return "<unknown>";
        case TypeKind::Unit: return "unit";
        case TypeKind::Bool: return "bool";
        case TypeKind::I8: return "i8";
        case TypeKind::I16: return "i16";
        case TypeKind::I32: return "i32";
        case TypeKind::I64: return "i64";
        case TypeKind::ISize: return "isize";
        case TypeKind::U8: return "u8";
        case TypeKind::U16: return "u16";
        case TypeKind::U32: return "u32";
        case TypeKind::U64: return "u64";
        case TypeKind::USize: return "usize";
        case TypeKind::F32: return "f32";
        case TypeKind::F64: return "f64";
        case TypeKind::Char: return "char";
        case TypeKind::String: return "string";
        case TypeKind::Bytes: return "bytes";
        case TypeKind::Struct: return nominal_name.empty() ? "<anonymous struct>" : nominal_name;
        case TypeKind::Array: {
            const std::string element_name = element ? element->name() : "<unknown>";
            if (array_length) return "[" + element_name + "; " + std::to_string(*array_length) + "]";
            return "[" + element_name + "; ?]";
        }
        case TypeKind::Slice:
            return "[" + (element ? element->name() : std::string("<unknown>")) + "]";
        case TypeKind::Range:
            return "Range<" + (element ? element->name() : std::string("<unknown>")) + ">";
        case TypeKind::Reference:
            return std::string("&") + (mutable_reference ? "mut " : "") +
                   (element ? element->name() : std::string("<unknown>"));
        case TypeKind::Function: {
            std::ostringstream out;
            out << "fn(";
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                if (i != 0) out << ", ";
                out << parameters[i].name();
            }
            if (variadic_any) {
                if (!parameters.empty()) out << ", ";
                out << "...";
            }
            out << ") -> " << (return_type ? return_type->name() : "unit");
            return out.str();
        }
    }
    return "<unknown>";
}

bool operator==(const Type& lhs, const Type& rhs) {
    if (lhs.kind != rhs.kind) return false;

    switch (lhs.kind) {
        case TypeKind::Struct:
            return lhs.nominal_name == rhs.nominal_name;
        case TypeKind::Array:
            if (lhs.array_length != rhs.array_length) return false;
            [[fallthrough]];
        case TypeKind::Slice:
        case TypeKind::Range:
            if (!lhs.element || !rhs.element) return lhs.element == rhs.element;
            return *lhs.element == *rhs.element;
        case TypeKind::Reference:
            if (lhs.mutable_reference != rhs.mutable_reference) return false;
            if (!lhs.element || !rhs.element) return lhs.element == rhs.element;
            return *lhs.element == *rhs.element;
        case TypeKind::Function:
            if (lhs.variadic_any != rhs.variadic_any || lhs.parameters != rhs.parameters) return false;
            if (!lhs.return_type || !rhs.return_type) return lhs.return_type == rhs.return_type;
            return *lhs.return_type == *rhs.return_type;
        default:
            return true;
    }
}

bool operator!=(const Type& lhs, const Type& rhs) {
    return !(lhs == rhs);
}

} // namespace nus::sema
