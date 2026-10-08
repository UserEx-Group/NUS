#include "nus/sema/SemanticAnalyzer.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace nus::sema {
namespace {

Type errorType() { return Type::simple(TypeKind::Error); }
Type unitType() { return Type::simple(TypeKind::Unit); }
Type boolType() { return Type::simple(TypeKind::Bool); }
Type i32Type() { return Type::simple(TypeKind::I32); }
Type f64Type() { return Type::simple(TypeKind::F64); }

std::optional<TypeKind> simpleTypeKind(std::string_view name) {
    if (name == "unit" || name == "()") return TypeKind::Unit;
    if (name == "bool") return TypeKind::Bool;
    if (name == "i8") return TypeKind::I8;
    if (name == "i16") return TypeKind::I16;
    if (name == "i32") return TypeKind::I32;
    if (name == "i64") return TypeKind::I64;
    if (name == "isize") return TypeKind::ISize;
    if (name == "u8") return TypeKind::U8;
    if (name == "u16") return TypeKind::U16;
    if (name == "u32") return TypeKind::U32;
    if (name == "u64") return TypeKind::U64;
    if (name == "usize") return TypeKind::USize;
    if (name == "f32") return TypeKind::F32;
    if (name == "f64") return TypeKind::F64;
    if (name == "char") return TypeKind::Char;
    if (name == "string") return TypeKind::String;
    if (name == "bytes") return TypeKind::Bytes;
    return std::nullopt;
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool isDigitForBase(char c, int base) {
    if (c >= '0' && c <= '9') return (c - '0') < base;
    if (base == 16 && c >= 'a' && c <= 'f') return true;
    if (base == 16 && c >= 'A' && c <= 'F') return true;
    return false;
}

std::string_view rawIntegerSuffix(std::string_view text) {
    std::size_t index = 0;
    int base = 10;
    if (text.size() >= 2 && text[0] == '0') {
        if (text[1] == 'x' || text[1] == 'X') { base = 16; index = 2; }
        else if (text[1] == 'b' || text[1] == 'B') { base = 2; index = 2; }
        else if (text[1] == 'o' || text[1] == 'O') { base = 8; index = 2; }
    }
    while (index < text.size() && (isDigitForBase(text[index], base) || text[index] == '_')) ++index;
    return text.substr(index);
}

std::string_view rawFloatSuffix(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size() && (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '_')) ++index;
    if (index < text.size() && text[index] == '.') {
        ++index;
        while (index < text.size() && (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '_')) ++index;
    }
    if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
        ++index;
        if (index < text.size() && (text[index] == '+' || text[index] == '-')) ++index;
        while (index < text.size() && (std::isdigit(static_cast<unsigned char>(text[index])) || text[index] == '_')) ++index;
    }
    return text.substr(index);
}

std::string_view integerSuffix(std::string_view text) {
    static constexpr std::string_view suffixes[] = {
        "usize", "isize", "u64", "u32", "u16", "u8", "i64", "i32", "i16", "i8"
    };
    for (const auto suffix : suffixes) {
        if (endsWith(text, suffix)) return suffix;
    }
    return {};
}

std::string_view floatSuffix(std::string_view text) {
    if (endsWith(text, "f32")) return "f32";
    if (endsWith(text, "f64")) return "f64";
    return {};
}

Type typeForIntegerSuffix(std::string_view suffix) {
    if (suffix == "i8") return Type::simple(TypeKind::I8);
    if (suffix == "i16") return Type::simple(TypeKind::I16);
    if (suffix == "i32") return Type::simple(TypeKind::I32);
    if (suffix == "i64") return Type::simple(TypeKind::I64);
    if (suffix == "isize") return Type::simple(TypeKind::ISize);
    if (suffix == "u8") return Type::simple(TypeKind::U8);
    if (suffix == "u16") return Type::simple(TypeKind::U16);
    if (suffix == "u32") return Type::simple(TypeKind::U32);
    if (suffix == "u64") return Type::simple(TypeKind::U64);
    if (suffix == "usize") return Type::simple(TypeKind::USize);
    return i32Type();
}

std::optional<std::size_t> parseUnsignedLiteral(std::string text) {
    const auto suffix = integerSuffix(text);
    if (!suffix.empty()) text.resize(text.size() - suffix.size());
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());

    int base = 10;
    std::size_t offset = 0;
    if (text.size() > 2 && text[0] == '0') {
        if (text[1] == 'x' || text[1] == 'X') { base = 16; offset = 2; }
        else if (text[1] == 'b' || text[1] == 'B') { base = 2; offset = 2; }
        else if (text[1] == 'o' || text[1] == 'O') { base = 8; offset = 2; }
    }

    unsigned long long value = 0;
    const char* begin = text.data() + static_cast<std::ptrdiff_t>(offset);
    const char* end = text.data() + static_cast<std::ptrdiff_t>(text.size());
    const auto [ptr, ec] = std::from_chars(begin, end, value, base);
    if (ec != std::errc{} || ptr != end || value > std::numeric_limits<std::size_t>::max()) return std::nullopt;
    return static_cast<std::size_t>(value);
}

bool isLiteralOfKind(const ast::Expr* expression, TokenKind kind) {
    if (!expression || expression->kind != ast::ExprKind::Literal) return false;
    return static_cast<const ast::LiteralExpr*>(expression)->literal_kind == kind;
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer() = default;

void SemanticAnalyzer::reset() {
    symbols_ = SymbolTable{};
    diagnostics_.clear();
    expression_types_.clear();
    function_signatures_.clear();
    structs_.clear();
    current_return_type_ = unitType();
    loop_depth_ = 0;
    next_symbol_id_ = 1;
    borrow_scopes_.clear();
    borrow_scopes_.emplace_back();
    temporary_borrow_frames_.clear();
    persist_borrows_ = false;
}

void SemanticAnalyzer::analyze(const ast::SourceFile& file) {
    reset();
    registerBuiltins();
    collectStructNames(file);
    collectStructFields(file);
    collectFunctions(file);
    collectMethods(file);
    for (const auto& function : file.functions) analyzeFunction(function);
    for (const auto& implementation : file.impls) {
        const auto target_name = implementation.target.name();
        for (const auto& method : implementation.methods) analyzeMethod(target_name, method);
    }
}

const std::vector<Diagnostic>& SemanticAnalyzer::diagnostics() const noexcept {
    return diagnostics_;
}

bool SemanticAnalyzer::hasErrors() const noexcept {
    return !diagnostics_.empty();
}

const Type* SemanticAnalyzer::typeOf(const ast::Expr& expression) const noexcept {
    const auto it = expression_types_.find(&expression);
    return it == expression_types_.end() ? nullptr : &it->second;
}

void SemanticAnalyzer::registerBuiltins() {
    Symbol print;
    print.id = next_symbol_id_++;
    print.name = "print";
    print.kind = SymbolKind::BuiltinFunction;
    print.type = Type::function({}, unitType(), true);
    (void)symbols_.declare(std::move(print));

    Symbol assert_symbol;
    assert_symbol.id = next_symbol_id_++;
    assert_symbol.name = "assert";
    assert_symbol.kind = SymbolKind::BuiltinFunction;
    assert_symbol.type = Type::function({boolType()}, unitType());
    (void)symbols_.declare(std::move(assert_symbol));
}

void SemanticAnalyzer::collectStructNames(const ast::SourceFile& file) {
    for (const auto& structure : file.structs) {
        if (simpleTypeKind(structure.name)) {
            error(structure.span, "struct name `" + structure.name + "` conflicts with a built-in type");
            continue;
        }
        if (structs_.contains(structure.name)) {
            error(structure.span, "duplicate struct `" + structure.name + "`");
            continue;
        }
        StructInfo info;
        info.declaration = &structure;
        structs_.emplace(structure.name, std::move(info));
    }
}

void SemanticAnalyzer::collectStructFields(const ast::SourceFile& file) {
    for (const auto& structure : file.structs) {
        auto found = structs_.find(structure.name);
        if (found == structs_.end() || found->second.declaration != &structure) continue;
        auto& info = found->second;
        for (const auto& field : structure.fields) {
            if (info.fields.contains(field.name)) {
                error(field.span, "duplicate field `" + field.name + "` in struct `" + structure.name + "`");
                continue;
            }
            Type field_type = resolveType(field.type);
            if (field_type.containsReference()) {
                error(field.type.span, "reference fields require lifetime inference and are not supported yet");
            }
            info.fields.emplace(field.name, std::move(field_type));
        }
    }
}

void SemanticAnalyzer::collectFunctions(const ast::SourceFile& file) {
    for (const auto& function : file.functions) {
        std::vector<Type> parameters;
        parameters.reserve(function.parameters.size());
        for (const auto& parameter : function.parameters) parameters.push_back(resolveType(parameter.type));
        const Type result = function.return_type ? resolveType(*function.return_type) : unitType();
        if (function.return_type && result.containsReference()) {
            error(function.return_type->span, "returning references is not supported until lifetime inference is implemented");
        }

        Symbol symbol;
        symbol.id = next_symbol_id_++;
        symbol.name = function.name;
        symbol.kind = SymbolKind::Function;
        symbol.type = Type::function(std::move(parameters), result);
        function_signatures_.insert_or_assign(&function, symbol.type);
        symbol.span = function.span;
        if (!symbols_.declare(std::move(symbol))) {
            error(function.span, "duplicate function `" + function.name + "`");
        }
    }
}

void SemanticAnalyzer::collectMethods(const ast::SourceFile& file) {
    for (const auto& implementation : file.impls) {
        const auto target_name = implementation.target.name();
        auto struct_it = structs_.find(target_name);
        if (struct_it == structs_.end()) {
            error(implementation.target.span, "cannot implement unknown struct `" + target_name + "`");
            continue;
        }

        for (const auto& method : implementation.methods) {
            if (method.receiver == ast::ReceiverKind::None) {
                error(method.span, "method `" + method.name + "` must declare a `self` receiver");
            }
            if (struct_it->second.fields.contains(method.name) || struct_it->second.methods.contains(method.name)) {
                error(method.span, "duplicate member `" + method.name + "` in struct `" + target_name + "`");
                continue;
            }

            std::vector<Type> parameters;
            parameters.reserve(method.parameters.size());
            for (const auto& parameter : method.parameters) parameters.push_back(resolveType(parameter.type));
            const Type result = method.return_type ? resolveType(*method.return_type) : unitType();
            if (method.return_type && result.containsReference()) {
                error(method.return_type->span, "returning references is not supported until lifetime inference is implemented");
            }
            Type signature = Type::function(std::move(parameters), result);
            function_signatures_.insert_or_assign(&method, signature);
            struct_it->second.methods.emplace(method.name, MethodInfo{
                .declaration = &method,
                .receiver = method.receiver,
                .signature = std::move(signature),
            });
        }
    }
}

void SemanticAnalyzer::analyzeFunction(const ast::FunctionDecl& function) {
    const auto signature_it = function_signatures_.find(&function);
    const Type signature = signature_it != function_signatures_.end()
                               ? signature_it->second
                               : Type::function({}, unitType());
    current_return_type_ = signature.return_type ? *signature.return_type : unitType();
    loop_depth_ = 0;
    pushScope();

    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        Symbol symbol;
        symbol.id = next_symbol_id_++;
        symbol.name = parameter.name;
        symbol.kind = SymbolKind::Parameter;
        symbol.type = index < signature.parameters.size() ? signature.parameters[index] : errorType();
        symbol.is_mutable = false;
        symbol.span = parameter.span;
        if (!symbols_.declare(std::move(symbol))) {
            error(parameter.span, "duplicate parameter `" + parameter.name + "`");
        }
    }

    const Type body_type = checkBlock(*function.body, false);
    if (function.body->tail_expression && !compatible(current_return_type_, body_type, function.body->tail_expression.get())) {
        error(function.body->tail_expression->span,
              "function `" + function.name + "` returns `" + body_type.name() +
              "` but `" + current_return_type_.name() + "` is required");
    }

    popScope();
}

void SemanticAnalyzer::analyzeMethod(const std::string& target_name, const ast::FunctionDecl& method) {
    const auto signature_it = function_signatures_.find(&method);
    const Type signature = signature_it != function_signatures_.end()
                               ? signature_it->second
                               : Type::function({}, unitType());
    current_return_type_ = signature.return_type ? *signature.return_type : unitType();
    loop_depth_ = 0;
    pushScope();

    if (method.receiver != ast::ReceiverKind::None) {
        Type self_type = Type::structure(target_name);
        bool self_mutable = false;
        if (method.receiver == ast::ReceiverKind::Reference) self_type = Type::reference(self_type, false);
        if (method.receiver == ast::ReceiverKind::MutableReference) {
            self_type = Type::reference(self_type, true);
            self_mutable = true;
        }
        Symbol self_symbol;
        self_symbol.id = next_symbol_id_++;
        self_symbol.name = "self";
        self_symbol.kind = SymbolKind::Parameter;
        self_symbol.type = std::move(self_type);
        self_symbol.is_mutable = self_mutable;
        self_symbol.span = method.receiver_span;
        (void)symbols_.declare(std::move(self_symbol));
    }

    for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        const auto& parameter = method.parameters[index];
        Symbol symbol;
        symbol.id = next_symbol_id_++;
        symbol.name = parameter.name;
        symbol.kind = SymbolKind::Parameter;
        symbol.type = index < signature.parameters.size() ? signature.parameters[index] : errorType();
        symbol.is_mutable = false;
        symbol.span = parameter.span;
        if (!symbols_.declare(std::move(symbol))) {
            error(parameter.span, "duplicate parameter `" + parameter.name + "`");
        }
    }

    const Type body_type = checkBlock(*method.body, false);
    if (method.body->tail_expression && !compatible(current_return_type_, body_type, method.body->tail_expression.get())) {
        error(method.body->tail_expression->span,
              "method `" + method.name + "` returns `" + body_type.name() +
              "` but `" + current_return_type_.name() + "` is required");
    }

    popScope();
}

Type SemanticAnalyzer::resolveType(const ast::TypeRef& type_ref) {
    std::string base_name;
    for (std::size_t i = 0; i < type_ref.path.size(); ++i) {
        if (i != 0) base_name += "::";
        base_name += type_ref.path[i];
    }

    Type base = errorType();
    if (const auto kind = simpleTypeKind(base_name)) {
        base = Type::simple(*kind);
    } else if (structs_.contains(base_name)) {
        base = Type::structure(base_name);
    } else {
        error(type_ref.span, "unknown type `" + base_name + "`");
        return errorType();
    }

    if (type_ref.is_reference) return Type::reference(std::move(base), type_ref.is_mutable_reference);
    return base;
}

Type SemanticAnalyzer::checkBlock(const ast::BlockExpr& block, bool create_scope) {
    if (create_scope) pushScope();

    for (const auto& statement : block.statements) checkStatement(*statement);

    Type result = unitType();
    if (block.tail_expression) {
        beginTemporaryBorrowFrame();
        result = checkExpr(*block.tail_expression);
        if (create_scope && persist_borrows_ && result.containsReference()) {
            error(block.tail_expression->span,
                  "borrowed values cannot escape a nested block until lifetime inference is implemented");
        }
        consumeValue(*block.tail_expression, result, "block result");
        endTemporaryBorrowFrame();
    }

    if (create_scope) popScope();
    return result;
}

void SemanticAnalyzer::checkStatement(const ast::Stmt& statement) {
    beginTemporaryBorrowFrame();

    switch (statement.kind) {
        case ast::StmtKind::Let: {
            const auto& let = static_cast<const ast::LetStmt&>(statement);
            const bool previous_persistence = persist_borrows_;
            persist_borrows_ = true;
            const Type initializer_type = checkExpr(*let.initializer);
            persist_borrows_ = previous_persistence;

            Type declared_type = initializer_type;
            if (let.type) {
                declared_type = resolveType(*let.type);
                if (!compatible(declared_type, initializer_type, let.initializer.get())) {
                    error(let.initializer->span,
                          "cannot initialize `" + let.name + "` of type `" + declared_type.name() +
                          "` with value of type `" + initializer_type.name() + "`");
                }
            }

            consumeValue(*let.initializer, initializer_type, "initializer");

            Symbol symbol;
            symbol.id = next_symbol_id_++;
            symbol.name = let.name;
            symbol.kind = SymbolKind::Variable;
            symbol.type = declared_type;
            symbol.is_mutable = let.is_mutable;
            symbol.span = let.span;
            if (!symbols_.declare(std::move(symbol))) {
                error(let.span, "duplicate declaration of `" + let.name + "` in the same scope");
            }
            break;
        }
        case ast::StmtKind::Return: {
            const auto& ret = static_cast<const ast::ReturnStmt&>(statement);
            if (!ret.value) {
                if (!current_return_type_.isUnit() && !current_return_type_.isError()) {
                    error(ret.span, "return statement requires a value of type `" + current_return_type_.name() + "`");
                }
                break;
            }
            const Type actual = checkExpr(*ret.value);
            if (!compatible(current_return_type_, actual, ret.value.get())) {
                error(ret.value->span,
                      "return type mismatch: expected `" + current_return_type_.name() +
                      "`, found `" + actual.name() + "`");
            } else {
                consumeValue(*ret.value, actual, "return value");
            }
            break;
        }
        case ast::StmtKind::Break: {
            const auto& brk = static_cast<const ast::BreakStmt&>(statement);
            if (loop_depth_ == 0) error(brk.span, "`break` can only be used inside a loop");
            if (brk.value) {
                (void)checkExpr(*brk.value);
                error(brk.value->span, "break values are not supported yet");
            }
            break;
        }
        case ast::StmtKind::Continue: {
            if (loop_depth_ == 0) error(statement.span, "`continue` can only be used inside a loop");
            break;
        }
        case ast::StmtKind::Expression: {
            const auto& expr = static_cast<const ast::ExprStmt&>(statement);
            const Type type = checkExpr(*expr.expression);
            consumeValue(*expr.expression, type, "expression result");
            break;
        }
    }

    endTemporaryBorrowFrame();
}

Type SemanticAnalyzer::checkExpr(const ast::Expr& expression) {
    Type result = errorType();
    switch (expression.kind) {
        case ast::ExprKind::Literal:
            result = checkLiteral(static_cast<const ast::LiteralExpr&>(expression));
            break;
        case ast::ExprKind::Identifier: {
            const auto& identifier = static_cast<const ast::IdentifierExpr&>(expression);
            if (const auto* symbol = symbols_.lookup(identifier.name)) {
                result = symbol->type;
                if (symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::BuiltinFunction) {
                    if (symbol->is_moved) {
                        error(expression.span, "use of moved value `" + identifier.name + "`");
                    } else if (symbol->mutable_borrowed) {
                        error(expression.span, "cannot use `" + identifier.name + "` while it is mutably borrowed");
                    }
                }
            } else {
                error(expression.span, "undefined name `" + identifier.name + "`");
            }
            break;
        }
        case ast::ExprKind::Unary:
            result = checkUnary(static_cast<const ast::UnaryExpr&>(expression));
            break;
        case ast::ExprKind::Binary:
            result = checkBinary(static_cast<const ast::BinaryExpr&>(expression));
            break;
        case ast::ExprKind::Assignment:
            result = checkAssignment(static_cast<const ast::AssignmentExpr&>(expression));
            break;
        case ast::ExprKind::Call:
            result = checkCall(static_cast<const ast::CallExpr&>(expression));
            break;
        case ast::ExprKind::Member:
            result = checkMember(static_cast<const ast::MemberExpr&>(expression));
            break;
        case ast::ExprKind::Index:
            result = checkIndex(static_cast<const ast::IndexExpr&>(expression));
            break;
        case ast::ExprKind::Array:
            result = checkArray(static_cast<const ast::ArrayExpr&>(expression));
            break;
        case ast::ExprKind::Range:
            result = checkRange(static_cast<const ast::RangeExpr&>(expression));
            break;
        case ast::ExprKind::StructLiteral:
            result = checkStructLiteral(static_cast<const ast::StructLiteralExpr&>(expression));
            break;
        case ast::ExprKind::Block:
            result = checkBlock(static_cast<const ast::BlockExpr&>(expression));
            break;
        case ast::ExprKind::If:
            result = checkIf(static_cast<const ast::IfExpr&>(expression));
            break;
        case ast::ExprKind::While:
            result = checkWhile(static_cast<const ast::WhileExpr&>(expression));
            break;
        case ast::ExprKind::Loop:
            result = checkLoop(static_cast<const ast::LoopExpr&>(expression));
            break;
        case ast::ExprKind::For:
            result = checkFor(static_cast<const ast::ForExpr&>(expression));
            break;
    }
    recordType(expression, result);
    return result;
}

Type SemanticAnalyzer::checkLiteral(const ast::LiteralExpr& expression) {
    switch (expression.literal_kind) {
        case TokenKind::IntegerLiteral: {
            const auto raw_suffix = rawIntegerSuffix(expression.text);
            if (!raw_suffix.empty()) {
                const auto suffix = integerSuffix(expression.text);
                if (suffix.empty() || suffix != raw_suffix) {
                    error(expression.span, "invalid integer literal suffix `" + std::string(raw_suffix) + "`");
                    return errorType();
                }
                return typeForIntegerSuffix(suffix);
            }
            return i32Type();
        }
        case TokenKind::FloatLiteral: {
            const auto raw_suffix = rawFloatSuffix(expression.text);
            if (!raw_suffix.empty()) {
                const auto suffix = floatSuffix(expression.text);
                if (suffix.empty() || suffix != raw_suffix) {
                    error(expression.span, "invalid floating-point literal suffix `" + std::string(raw_suffix) + "`");
                    return errorType();
                }
                return suffix == "f32" ? Type::simple(TypeKind::F32) : f64Type();
            }
            return f64Type();
        }
        case TokenKind::StringLiteral:
        case TokenKind::RawStringLiteral:
            return Type::simple(TypeKind::String);
        case TokenKind::CharLiteral:
            return Type::simple(TypeKind::Char);
        case TokenKind::ByteLiteral:
            return Type::simple(TypeKind::U8);
        case TokenKind::ByteStringLiteral:
            return Type::simple(TypeKind::Bytes);
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            return boolType();
        default:
            error(expression.span, "unsupported literal in semantic analysis");
            return errorType();
    }
}

Type SemanticAnalyzer::checkUnary(const ast::UnaryExpr& expression) {
    if (expression.op == TokenKind::Ampersand) {
        const LValueInfo place = checkLValue(*expression.operand);
        if (place.type.isError()) return errorType();
        if (expression.mutable_borrow && !place.is_mutable) {
            error(expression.span, "cannot mutably borrow an immutable value");
            return Type::reference(place.type, true);
        }
        (void)borrowPlace(*expression.operand, expression.mutable_borrow, expression.span);
        return Type::reference(place.type, expression.mutable_borrow);
    }

    const Type operand = checkExpr(*expression.operand);
    if (operand.isError()) return operand;

    switch (expression.op) {
        case TokenKind::Bang:
            if (!operand.isBool()) error(expression.span, "operator `!` requires `bool`, found `" + operand.name() + "`");
            return boolType();
        case TokenKind::Tilde:
            if (!operand.isInteger()) error(expression.span, "operator `~` requires an integer, found `" + operand.name() + "`");
            return operand;
        case TokenKind::Minus:
            if (!(operand.isSignedInteger() || operand.isFloat())) {
                error(expression.span, "unary `-` requires a signed numeric type, found `" + operand.name() + "`");
                return errorType();
            }
            return operand;
        case TokenKind::Star:
            if (operand.kind != TypeKind::Reference || !operand.element) {
                error(expression.span, "cannot dereference value of type `" + operand.name() + "`");
                return errorType();
            }
            return *operand.element;
        case TokenKind::KwAwait:
            error(expression.span, "`await` semantic analysis is not implemented yet");
            return errorType();
        default:
            error(expression.span, "unsupported unary operator");
            return errorType();
    }
}

Type SemanticAnalyzer::checkBinary(const ast::BinaryExpr& expression) {
    const Type left = checkExpr(*expression.left);
    const Type right = checkExpr(*expression.right);
    if (left.isError() || right.isError()) return errorType();

    if (isLogicalOperator(expression.op)) {
        if (!left.isBool() || !right.isBool()) {
            error(expression.span, "logical operators require `bool` operands");
            return errorType();
        }
        return boolType();
    }

    if (isEqualityOperator(expression.op)) {
        if (!compatible(left, right, expression.right.get()) && !compatible(right, left, expression.left.get())) {
            error(expression.span, "cannot compare `" + left.name() + "` with `" + right.name() + "`");
        }
        return boolType();
    }

    if (isComparisonOperator(expression.op)) {
        if (!left.isNumeric() || !right.isNumeric() ||
            (!compatible(left, right, expression.right.get()) && !compatible(right, left, expression.left.get()))) {
            error(expression.span, "comparison requires compatible numeric operands, found `" +
                                   left.name() + "` and `" + right.name() + "`");
        }
        return boolType();
    }

    if (isShiftOperator(expression.op)) {
        if (!left.isInteger() || !right.isInteger()) {
            error(expression.span, "shift operators require integer operands");
            return errorType();
        }
        return left;
    }

    if (isBitwiseOperator(expression.op)) {
        const bool left_result = compatible(left, right, expression.right.get());
        const bool right_result = compatible(right, left, expression.left.get());
        if (!left.isInteger() || !right.isInteger() || (!left_result && !right_result)) {
            error(expression.span, "bitwise operators require compatible integer operands");
            return errorType();
        }
        return left_result ? left : right;
    }

    if (isArithmeticOperator(expression.op)) {
        const bool left_result = compatible(left, right, expression.right.get());
        const bool right_result = compatible(right, left, expression.left.get());
        if (!left.isNumeric() || !right.isNumeric() || (!left_result && !right_result)) {
            error(expression.span, "arithmetic operator requires compatible numeric operands, found `" +
                                   left.name() + "` and `" + right.name() + "`");
            return errorType();
        }
        if (expression.op == TokenKind::Percent && (!left.isInteger() || !right.isInteger())) {
            error(expression.span, "operator `%` requires integer operands");
            return errorType();
        }
        return left_result ? left : right;
    }

    error(expression.span, "unsupported binary operator in semantic analysis");
    return errorType();
}

Type SemanticAnalyzer::checkAssignment(const ast::AssignmentExpr& expression) {
    const LValueInfo target = checkLValue(*expression.target);
    const Type value = checkExpr(*expression.value);
    if (target.type.isError() || value.isError()) return errorType();

    Symbol* root = rootSymbol(*expression.target);
    if (root && (root->shared_borrows > 0 || root->mutable_borrowed)) {
        error(expression.target->span, "cannot assign to `" + root->name + "` while it is borrowed");
    }

    if (!target.is_mutable) {
        error(expression.target->span, "cannot assign to immutable value");
    }

    bool compatible_assignment = true;
    if (expression.op == TokenKind::Equal) {
        if (!compatible(target.type, value, expression.value.get())) {
            compatible_assignment = false;
            error(expression.value->span,
                  "cannot assign value of type `" + value.name() + "` to `" + target.type.name() + "`");
        }
    } else {
        const TokenKind base = compoundBaseOperator(expression.op);
        if (isArithmeticOperator(base)) {
            if (!target.type.isNumeric() || !value.isNumeric() || !compatible(target.type, value, expression.value.get())) {
                compatible_assignment = false;
                error(expression.span, "compound arithmetic assignment requires compatible numeric operands");
            }
        } else if (isBitwiseOperator(base)) {
            if (!target.type.isInteger() || !value.isInteger() || !compatible(target.type, value, expression.value.get())) {
                compatible_assignment = false;
                error(expression.span, "compound bitwise assignment requires compatible integer operands");
            }
        } else if (isShiftOperator(base)) {
            if (!target.type.isInteger() || !value.isInteger()) {
                compatible_assignment = false;
                error(expression.span, "compound shift assignment requires integer operands");
            }
        }
    }

    if (compatible_assignment) {
        consumeValue(*expression.value, value, "assignment");
        if (expression.op == TokenKind::Equal) reinitializeIfDirectIdentifier(*expression.target);
    }
    return unitType();
}

Type SemanticAnalyzer::checkCall(const ast::CallExpr& expression) {
    if (expression.callee->kind == ast::ExprKind::Member) {
        return checkMethodCall(expression, static_cast<const ast::MemberExpr&>(*expression.callee));
    }

    const Type callee = checkExpr(*expression.callee);
    if (callee.kind != TypeKind::Function) {
        beginTemporaryBorrowFrame();
        const bool old_persistence = persist_borrows_;
        persist_borrows_ = false;
        for (const auto& argument : expression.arguments) (void)checkExpr(*argument);
        persist_borrows_ = old_persistence;
        endTemporaryBorrowFrame();
        if (!callee.isError()) error(expression.callee->span, "value of type `" + callee.name() + "` is not callable");
        return errorType();
    }

    if (!callee.variadic_any && expression.arguments.size() != callee.parameters.size()) {
        error(expression.span,
              "function expects " + std::to_string(callee.parameters.size()) + " argument(s), found " +
              std::to_string(expression.arguments.size()));
    }

    beginTemporaryBorrowFrame();
    const bool old_persistence = persist_borrows_;
    persist_borrows_ = false;

    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        const Type actual = checkExpr(*expression.arguments[i]);
        if (callee.variadic_any || i >= callee.parameters.size()) continue;
        const Type& expected = callee.parameters[i];
        if (!compatible(expected, actual, expression.arguments[i].get())) {
            error(expression.arguments[i]->span,
                  "argument " + std::to_string(i + 1) + " expects `" + expected.name() +
                  "`, found `" + actual.name() + "`");
            continue;
        }
        if (!expected.isReference()) {
            consumeValue(*expression.arguments[i], actual, "function argument");
        }
    }

    persist_borrows_ = old_persistence;
    endTemporaryBorrowFrame();
    return callee.return_type ? *callee.return_type : unitType();
}

Type SemanticAnalyzer::checkMethodCall(const ast::CallExpr& expression, const ast::MemberExpr& member) {
    const Type object_type = checkExpr(*member.object);
    const MethodInfo* method = findMethod(object_type, member.member);
    if (!method) {
        if (findField(object_type, member.member)) {
            const Type field = checkMember(member);
            for (const auto& argument : expression.arguments) (void)checkExpr(*argument);
            error(member.span, "field `" + member.member + "` of type `" + field.name() + "` is not callable");
        } else if (!object_type.isError()) {
            error(member.span, "type `" + dereferenceForMember(object_type).name() + "` has no method `" + member.member + "`");
            for (const auto& argument : expression.arguments) (void)checkExpr(*argument);
        }
        return errorType();
    }

    recordType(member, method->signature);
    beginTemporaryBorrowFrame();
    const bool old_persistence = persist_borrows_;
    persist_borrows_ = false;

    if (method->receiver == ast::ReceiverKind::Value) {
        if (object_type.isReference()) {
            error(member.object->span, "cannot call consuming method `" + member.member + "` through a borrowed reference");
        } else {
            consumeValue(*member.object, object_type, "method receiver");
        }
    } else if (method->receiver == ast::ReceiverKind::Reference) {
        if (!object_type.isReference()) (void)borrowPlace(*member.object, false, member.object->span);
    } else if (method->receiver == ast::ReceiverKind::MutableReference) {
        if (object_type.isReference()) {
            if (!object_type.mutable_reference) {
                error(member.object->span, "method `" + member.member + "` requires a mutable receiver");
            }
        } else {
            const auto receiver = checkLValue(*member.object);
            if (!receiver.is_mutable) {
                error(member.object->span, "method `" + member.member + "` requires a mutable receiver");
            } else {
                (void)borrowPlace(*member.object, true, member.object->span);
            }
        }
    }

    const Type& signature = method->signature;
    if (expression.arguments.size() != signature.parameters.size()) {
        error(expression.span,
              "method `" + member.member + "` expects " + std::to_string(signature.parameters.size()) +
              " argument(s), found " + std::to_string(expression.arguments.size()));
    }
    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        const Type actual = checkExpr(*expression.arguments[i]);
        if (i >= signature.parameters.size()) continue;
        const Type& expected = signature.parameters[i];
        if (!compatible(expected, actual, expression.arguments[i].get())) {
            error(expression.arguments[i]->span,
                  "argument " + std::to_string(i + 1) + " of method `" + member.member +
                  "` expects `" + expected.name() + "`, found `" + actual.name() + "`");
            continue;
        }
        if (!expected.isReference()) consumeValue(*expression.arguments[i], actual, "method argument");
    }

    persist_borrows_ = old_persistence;
    endTemporaryBorrowFrame();
    return signature.return_type ? *signature.return_type : unitType();
}

Type SemanticAnalyzer::checkMember(const ast::MemberExpr& expression) {
    const Type object = checkExpr(*expression.object);
    if (const Type* field = findField(object, expression.member)) return *field;
    if (const MethodInfo* method = findMethod(object, expression.member)) return method->signature;
    if (!object.isError()) {
        error(expression.span, "type `" + dereferenceForMember(object).name() + "` has no member `" + expression.member + "`");
    }
    return errorType();
}

Type SemanticAnalyzer::checkIndex(const ast::IndexExpr& expression) {
    const Type object = checkExpr(*expression.object);
    const Type index = checkExpr(*expression.index);
    if (object.isError() || index.isError()) return errorType();

    if (object.kind != TypeKind::Array && object.kind != TypeKind::Slice && object.kind != TypeKind::Bytes && object.kind != TypeKind::String) {
        error(expression.object->span, "value of type `" + object.name() + "` is not indexable");
        return errorType();
    }

    if (index.kind == TypeKind::Range) {
        if (object.kind == TypeKind::String) return Type::simple(TypeKind::String);
        if (object.kind == TypeKind::Bytes) return Type::simple(TypeKind::Bytes);
        return object.element ? Type::slice(*object.element) : errorType();
    }

    if (!index.isInteger()) {
        error(expression.index->span, "index must be an integer or range, found `" + index.name() + "`");
        return errorType();
    }

    if (object.kind == TypeKind::String) return Type::simple(TypeKind::Char);
    if (object.kind == TypeKind::Bytes) return Type::simple(TypeKind::U8);
    return object.element ? *object.element : errorType();
}

Type SemanticAnalyzer::checkArray(const ast::ArrayExpr& expression) {
    if (expression.isRepeated()) {
        const Type value = checkExpr(*expression.repeat_value);
        const Type count = checkExpr(*expression.repeat_count);
        if (!count.isInteger()) error(expression.repeat_count->span, "array repeat count must be an integer");
        if (!value.isCopy() && !value.isError()) {
            error(expression.repeat_value->span,
                  "array repetition requires a Copy value, found `" + value.name() + "`");
        }
        return Type::array(value, repeatedArrayLength(*expression.repeat_count));
    }

    if (expression.elements.empty()) {
        error(expression.span, "cannot infer the element type of an empty array");
        return errorType();
    }

    Type element = checkExpr(*expression.elements.front());
    const ast::Expr* representative = expression.elements.front().get();
    for (std::size_t i = 1; i < expression.elements.size(); ++i) {
        const Type current = checkExpr(*expression.elements[i]);
        if (compatible(element, current, expression.elements[i].get())) continue;
        if (compatible(current, element, representative)) {
            element = current;
            representative = expression.elements[i].get();
            continue;
        }
        error(expression.elements[i]->span,
              "array element has type `" + current.name() + "`, expected `" + element.name() + "`");
    }

    for (const auto& item : expression.elements) {
        const Type* item_type = typeOf(*item);
        if (item_type) consumeValue(*item, *item_type, "array element");
    }
    return Type::array(element, expression.elements.size());
}

Type SemanticAnalyzer::checkRange(const ast::RangeExpr& expression) {
    const Type start = checkExpr(*expression.start);
    const Type end = checkExpr(*expression.end);
    if (start.isError() || end.isError()) return errorType();
    const bool start_result = compatible(start, end, expression.end.get());
    const bool end_result = compatible(end, start, expression.start.get());
    if (!start.isInteger() || !end.isInteger() || (!start_result && !end_result)) {
        error(expression.span, "range endpoints must have compatible integer types");
        return errorType();
    }
    return Type::range(start_result ? start : end);
}

Type SemanticAnalyzer::checkStructLiteral(const ast::StructLiteralExpr& expression) {
    const Type type = resolveType(expression.type);
    if (!type.isStruct()) {
        if (!type.isError()) error(expression.span, "`" + expression.type.name() + "` is not a struct type");
        for (const auto& field : expression.fields) if (field.value) (void)checkExpr(*field.value);
        return errorType();
    }

    const auto* info = findStruct(type);
    if (!info) return errorType();
    std::unordered_map<std::string, bool> seen;
    for (const auto& field : expression.fields) {
        if (seen.contains(field.name)) {
            error(field.span, "duplicate initializer for field `" + field.name + "`");
            if (field.value) (void)checkExpr(*field.value);
            continue;
        }
        seen.emplace(field.name, true);
        const auto expected = info->fields.find(field.name);
        if (expected == info->fields.end()) {
            error(field.span, "struct `" + type.name() + "` has no field `" + field.name + "`");
            if (field.value) (void)checkExpr(*field.value);
            continue;
        }
        const Type actual = field.value ? checkExpr(*field.value) : errorType();
        if (field.value && !compatible(expected->second, actual, field.value.get())) {
            error(field.value->span,
                  "field `" + field.name + "` expects `" + expected->second.name() +
                  "`, found `" + actual.name() + "`");
        } else if (field.value) {
            consumeValue(*field.value, actual, "struct field initializer");
        }
    }
    for (const auto& [name, field_type] : info->fields) {
        (void)field_type;
        if (!seen.contains(name)) error(expression.span, "missing initializer for field `" + name + "`");
    }
    return type;
}

Type SemanticAnalyzer::checkIf(const ast::IfExpr& expression) {
    (void)requireBool(*expression.condition, "if condition");
    const Type then_type = checkBlock(*expression.then_branch);
    if (!expression.else_branch) return unitType();

    const Type else_type = checkExpr(*expression.else_branch);
    const bool then_result = compatible(then_type, else_type, expression.else_branch.get());
    const bool else_result = compatible(else_type, then_type, expression.then_branch.get());
    if (!then_result && !else_result) {
        error(expression.span,
              "if branches have incompatible types `" + then_type.name() + "` and `" + else_type.name() + "`");
        return errorType();
    }
    return then_result ? then_type : else_type;
}

Type SemanticAnalyzer::checkWhile(const ast::WhileExpr& expression) {
    (void)requireBool(*expression.condition, "while condition");
    ++loop_depth_;
    (void)checkBlock(*expression.body);
    --loop_depth_;
    return unitType();
}

Type SemanticAnalyzer::checkLoop(const ast::LoopExpr& expression) {
    ++loop_depth_;
    (void)checkBlock(*expression.body);
    --loop_depth_;
    return unitType();
}

Type SemanticAnalyzer::checkFor(const ast::ForExpr& expression) {
    const Type iterable = checkExpr(*expression.iterable);
    Type binding_type = errorType();
    if (iterable.kind == TypeKind::Range || iterable.kind == TypeKind::Array || iterable.kind == TypeKind::Slice) {
        if (iterable.element) binding_type = *iterable.element;
    } else if (iterable.kind == TypeKind::Bytes) {
        binding_type = Type::simple(TypeKind::U8);
    } else if (!iterable.isError()) {
        error(expression.iterable->span, "value of type `" + iterable.name() + "` is not iterable");
    }

    consumeValue(*expression.iterable, iterable, "for-loop iterable");

    pushScope();
    Symbol binding;
    binding.id = next_symbol_id_++;
    binding.name = expression.binding;
    binding.kind = SymbolKind::Variable;
    binding.type = binding_type;
    binding.is_mutable = false;
    binding.span = expression.span;
    (void)symbols_.declare(std::move(binding));

    ++loop_depth_;
    (void)checkBlock(*expression.body, false);
    --loop_depth_;
    popScope();
    return unitType();
}

Type SemanticAnalyzer::dereferenceForMember(Type type) {
    while (type.kind == TypeKind::Reference && type.element) type = *type.element;
    return type;
}

const SemanticAnalyzer::StructInfo* SemanticAnalyzer::findStruct(const Type& type) const {
    const Type base = dereferenceForMember(type);
    if (!base.isStruct()) return nullptr;
    const auto it = structs_.find(base.nominal_name);
    return it == structs_.end() ? nullptr : &it->second;
}

SemanticAnalyzer::StructInfo* SemanticAnalyzer::findStruct(const Type& type) {
    const Type base = dereferenceForMember(type);
    if (!base.isStruct()) return nullptr;
    const auto it = structs_.find(base.nominal_name);
    return it == structs_.end() ? nullptr : &it->second;
}

const SemanticAnalyzer::MethodInfo* SemanticAnalyzer::findMethod(const Type& type, std::string_view name) const {
    const auto* info = findStruct(type);
    if (!info) return nullptr;
    const auto it = info->methods.find(std::string(name));
    return it == info->methods.end() ? nullptr : &it->second;
}

const Type* SemanticAnalyzer::findField(const Type& type, std::string_view name) const {
    const auto* info = findStruct(type);
    if (!info) return nullptr;
    const auto it = info->fields.find(std::string(name));
    return it == info->fields.end() ? nullptr : &it->second;
}

SemanticAnalyzer::LValueInfo SemanticAnalyzer::checkLValue(const ast::Expr& expression) {
    if (expression.kind == ast::ExprKind::Identifier) {
        const auto& identifier = static_cast<const ast::IdentifierExpr&>(expression);
        const auto* symbol = symbols_.lookup(identifier.name);
        if (!symbol) {
            error(expression.span, "undefined name `" + identifier.name + "`");
            return {};
        }
        if (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::BuiltinFunction) {
            error(expression.span, "function `" + identifier.name + "` is not assignable");
            return {};
        }
        recordType(expression, symbol->type);
        return LValueInfo{.type = symbol->type, .is_mutable = symbol->is_mutable};
    }

    if (expression.kind == ast::ExprKind::Unary) {
        const auto& unary = static_cast<const ast::UnaryExpr&>(expression);
        if (unary.op == TokenKind::Star) {
            const Type pointer = checkExpr(*unary.operand);
            if (pointer.kind != TypeKind::Reference || !pointer.element) {
                if (!pointer.isError()) error(expression.span, "cannot assign through non-reference type `" + pointer.name() + "`");
                return {};
            }
            recordType(expression, *pointer.element);
            return LValueInfo{.type = *pointer.element, .is_mutable = pointer.mutable_reference};
        }
    }

    if (expression.kind == ast::ExprKind::Index) {
        const auto& index = static_cast<const ast::IndexExpr&>(expression);
        const Type object_type = checkExpr(*index.object);
        const Type result = checkIndex(index);
        LValueInfo base = checkLValue(*index.object);
        if (object_type.kind == TypeKind::Reference && object_type.mutable_reference) base.is_mutable = true;
        recordType(expression, result);
        return LValueInfo{.type = result, .is_mutable = base.is_mutable};
    }

    if (expression.kind == ast::ExprKind::Member) {
        const auto& member = static_cast<const ast::MemberExpr&>(expression);
        const Type object_type = checkExpr(*member.object);
        const Type* field = findField(object_type, member.member);
        if (!field) {
            if (findMethod(object_type, member.member)) {
                error(expression.span, "method `" + member.member + "` is not assignable");
            } else if (!object_type.isError()) {
                error(expression.span, "type `" + dereferenceForMember(object_type).name() + "` has no field `" + member.member + "`");
            }
            return {};
        }
        LValueInfo base = checkLValue(*member.object);
        if (object_type.kind == TypeKind::Reference && object_type.mutable_reference) base.is_mutable = true;
        recordType(expression, *field);
        return LValueInfo{.type = *field, .is_mutable = base.is_mutable};
    }

    error(expression.span, "expression is not assignable");
    return {};
}

Symbol* SemanticAnalyzer::rootSymbol(const ast::Expr& expression) {
    switch (expression.kind) {
        case ast::ExprKind::Identifier: {
            const auto& identifier = static_cast<const ast::IdentifierExpr&>(expression);
            Symbol* symbol = symbols_.lookupMutable(identifier.name);
            if (!symbol || symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::BuiltinFunction) return nullptr;
            return symbol;
        }
        case ast::ExprKind::Member:
            return rootSymbol(*static_cast<const ast::MemberExpr&>(expression).object);
        case ast::ExprKind::Index:
            return rootSymbol(*static_cast<const ast::IndexExpr&>(expression).object);
        default:
            return nullptr;
    }
}

const Symbol* SemanticAnalyzer::rootSymbol(const ast::Expr& expression) const {
    switch (expression.kind) {
        case ast::ExprKind::Identifier: {
            const auto& identifier = static_cast<const ast::IdentifierExpr&>(expression);
            const Symbol* symbol = symbols_.lookup(identifier.name);
            if (!symbol || symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::BuiltinFunction) return nullptr;
            return symbol;
        }
        case ast::ExprKind::Member:
            return rootSymbol(*static_cast<const ast::MemberExpr&>(expression).object);
        case ast::ExprKind::Index:
            return rootSymbol(*static_cast<const ast::IndexExpr&>(expression).object);
        default:
            return nullptr;
    }
}

bool SemanticAnalyzer::borrowPlace(const ast::Expr& expression, bool is_mutable, SourceSpan span) {
    const LValueInfo place = checkLValue(expression);
    if (place.type.isError()) return false;
    if (is_mutable && !place.is_mutable) {
        error(span, "cannot mutably borrow an immutable value");
        return false;
    }

    Symbol* root = rootSymbol(expression);
    if (!root) {
        // Reborrows through an existing reference are already constrained by the reference type.
        return true;
    }
    if (root->is_moved) {
        error(span, "cannot borrow moved value `" + root->name + "`");
        return false;
    }
    if (is_mutable) {
        if (root->mutable_borrowed || root->shared_borrows > 0) {
            error(span, "cannot mutably borrow `" + root->name + "` because it is already borrowed");
            return false;
        }
    } else if (root->mutable_borrowed) {
        error(span, "cannot immutably borrow `" + root->name + "` while it is mutably borrowed");
        return false;
    }

    registerBorrow(*root, is_mutable);
    return true;
}

void SemanticAnalyzer::consumeValue(const ast::Expr& expression, const Type& type, std::string_view context) {
    if (type.isError() || type.isCopy()) return;

    if (expression.kind == ast::ExprKind::Unary) {
        const auto& unary = static_cast<const ast::UnaryExpr&>(expression);
        if (unary.op == TokenKind::Star) {
            error(expression.span, "cannot move out of borrowed content in " + std::string(context));
            return;
        }
    }

    Symbol* root = rootSymbol(expression);
    if (!root) return; // Temporary/rvalue: ownership is already local to the expression.

    if (root->is_moved) return; // The read path already emitted the primary diagnostic.
    if (root->mutable_borrowed || root->shared_borrows > 0) {
        error(expression.span, "cannot move `" + root->name + "` while it is borrowed");
        return;
    }
    root->is_moved = true;
}

void SemanticAnalyzer::reinitializeIfDirectIdentifier(const ast::Expr& expression) {
    if (expression.kind != ast::ExprKind::Identifier) return;
    const auto& identifier = static_cast<const ast::IdentifierExpr&>(expression);
    if (Symbol* symbol = symbols_.lookupMutable(identifier.name)) symbol->is_moved = false;
}

void SemanticAnalyzer::pushScope() {
    symbols_.pushScope();
    borrow_scopes_.emplace_back();
}

void SemanticAnalyzer::popScope() {
    if (!borrow_scopes_.empty()) {
        for (auto it = borrow_scopes_.back().rbegin(); it != borrow_scopes_.back().rend(); ++it) {
            releaseBorrow(*it);
        }
        borrow_scopes_.pop_back();
    }
    symbols_.popScope();
}

void SemanticAnalyzer::beginTemporaryBorrowFrame() {
    temporary_borrow_frames_.emplace_back();
}

void SemanticAnalyzer::endTemporaryBorrowFrame() {
    if (temporary_borrow_frames_.empty()) return;
    for (auto it = temporary_borrow_frames_.back().rbegin(); it != temporary_borrow_frames_.back().rend(); ++it) {
        releaseBorrow(*it);
    }
    temporary_borrow_frames_.pop_back();
}

void SemanticAnalyzer::releaseBorrow(const BorrowRecord& borrow) {
    Symbol* symbol = symbols_.lookupById(borrow.symbol_id);
    if (!symbol) return;
    if (borrow.is_mutable) {
        symbol->mutable_borrowed = false;
    } else if (symbol->shared_borrows > 0) {
        --symbol->shared_borrows;
    }
}

void SemanticAnalyzer::registerBorrow(Symbol& symbol, bool is_mutable) {
    if (is_mutable) symbol.mutable_borrowed = true;
    else ++symbol.shared_borrows;

    BorrowRecord record{.symbol_id = symbol.id, .is_mutable = is_mutable};
    if (persist_borrows_) {
        if (borrow_scopes_.empty()) borrow_scopes_.emplace_back();
        borrow_scopes_.back().push_back(record);
    } else {
        if (temporary_borrow_frames_.empty()) temporary_borrow_frames_.emplace_back();
        temporary_borrow_frames_.back().push_back(record);
    }
}

bool SemanticAnalyzer::compatible(const Type& expected, const Type& actual, const ast::Expr* value) const {
    if (expected.isError() || actual.isError()) return true;
    if (expected == actual) return true;

    if (expected.kind == TypeKind::Reference && actual.kind == TypeKind::Reference &&
        !expected.mutable_reference && actual.mutable_reference && expected.element && actual.element &&
        *expected.element == *actual.element) {
        return true;
    }

    if (value && isLiteralOfKind(value, TokenKind::IntegerLiteral) && expected.isInteger() && actual.isInteger()) {
        return true;
    }
    if (value && isLiteralOfKind(value, TokenKind::FloatLiteral) && expected.isFloat() && actual.isFloat()) {
        return true;
    }
    return false;
}

bool SemanticAnalyzer::requireBool(const ast::Expr& expression, std::string_view context) {
    const Type type = checkExpr(expression);
    if (type.isError()) return false;
    if (!type.isBool()) {
        error(expression.span, std::string(context) + " must be `bool`, found `" + type.name() + "`");
        return false;
    }
    return true;
}

bool SemanticAnalyzer::requireInteger(const ast::Expr& expression, std::string_view context) {
    const Type type = checkExpr(expression);
    if (type.isError()) return false;
    if (!type.isInteger()) {
        error(expression.span, std::string(context) + " must be an integer, found `" + type.name() + "`");
        return false;
    }
    return true;
}

bool SemanticAnalyzer::isComparisonOperator(TokenKind kind) noexcept {
    return kind == TokenKind::Less || kind == TokenKind::LessEqual ||
           kind == TokenKind::Greater || kind == TokenKind::GreaterEqual;
}

bool SemanticAnalyzer::isEqualityOperator(TokenKind kind) noexcept {
    return kind == TokenKind::EqualEqual || kind == TokenKind::BangEqual;
}

bool SemanticAnalyzer::isLogicalOperator(TokenKind kind) noexcept {
    return kind == TokenKind::AmpersandAmpersand || kind == TokenKind::PipePipe;
}

bool SemanticAnalyzer::isBitwiseOperator(TokenKind kind) noexcept {
    return kind == TokenKind::Ampersand || kind == TokenKind::Pipe || kind == TokenKind::Caret;
}

bool SemanticAnalyzer::isShiftOperator(TokenKind kind) noexcept {
    return kind == TokenKind::ShiftLeft || kind == TokenKind::ShiftRight;
}

bool SemanticAnalyzer::isArithmeticOperator(TokenKind kind) noexcept {
    return kind == TokenKind::Plus || kind == TokenKind::Minus || kind == TokenKind::Star ||
           kind == TokenKind::Slash || kind == TokenKind::Percent;
}

TokenKind SemanticAnalyzer::compoundBaseOperator(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::PlusEqual: return TokenKind::Plus;
        case TokenKind::MinusEqual: return TokenKind::Minus;
        case TokenKind::StarEqual: return TokenKind::Star;
        case TokenKind::SlashEqual: return TokenKind::Slash;
        case TokenKind::PercentEqual: return TokenKind::Percent;
        case TokenKind::AmpersandEqual: return TokenKind::Ampersand;
        case TokenKind::PipeEqual: return TokenKind::Pipe;
        case TokenKind::CaretEqual: return TokenKind::Caret;
        case TokenKind::ShiftLeftEqual: return TokenKind::ShiftLeft;
        case TokenKind::ShiftRightEqual: return TokenKind::ShiftRight;
        default: return kind;
    }
}

std::optional<std::size_t> SemanticAnalyzer::repeatedArrayLength(const ast::Expr& expression) {
    if (expression.kind != ast::ExprKind::Literal) return std::nullopt;
    const auto& literal = static_cast<const ast::LiteralExpr&>(expression);
    if (literal.literal_kind != TokenKind::IntegerLiteral) return std::nullopt;
    return parseUnsignedLiteral(literal.text);
}

void SemanticAnalyzer::recordType(const ast::Expr& expression, Type type) {
    expression_types_.insert_or_assign(&expression, std::move(type));
}

void SemanticAnalyzer::error(SourceSpan span, std::string message) {
    diagnostics_.push_back(Diagnostic{.message = std::move(message), .span = span});
}

} // namespace nus::sema
