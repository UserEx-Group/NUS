#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nus::sema {

enum class TypeKind {
    Error,
    Unknown,
    Unit,
    Bool,
    I8,
    I16,
    I32,
    I64,
    ISize,
    U8,
    U16,
    U32,
    U64,
    USize,
    F32,
    F64,
    Char,
    String,
    Bytes,
    Struct,
    Array,
    Slice,
    Range,
    Reference,
    Function,
};

struct Type {
    TypeKind kind{TypeKind::Unknown};
    std::string nominal_name;
    std::shared_ptr<Type> element;
    std::optional<std::size_t> array_length;
    std::vector<Type> parameters;
    std::shared_ptr<Type> return_type;
    bool variadic_any{false};
    bool mutable_reference{false};

    [[nodiscard]] static Type simple(TypeKind kind);
    [[nodiscard]] static Type structure(std::string name);
    [[nodiscard]] static Type array(Type element, std::optional<std::size_t> length = std::nullopt);
    [[nodiscard]] static Type slice(Type element);
    [[nodiscard]] static Type range(Type element);
    [[nodiscard]] static Type reference(Type element, bool is_mutable = false);
    [[nodiscard]] static Type function(std::vector<Type> parameters, Type result, bool variadic_any = false);

    [[nodiscard]] bool isError() const noexcept;
    [[nodiscard]] bool isNumeric() const noexcept;
    [[nodiscard]] bool isInteger() const noexcept;
    [[nodiscard]] bool isSignedInteger() const noexcept;
    [[nodiscard]] bool isFloat() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isUnit() const noexcept;
    [[nodiscard]] bool isStruct() const noexcept;
    [[nodiscard]] std::string name() const;
};

[[nodiscard]] bool operator==(const Type& lhs, const Type& rhs);
[[nodiscard]] bool operator!=(const Type& lhs, const Type& rhs);

} // namespace nus::sema
