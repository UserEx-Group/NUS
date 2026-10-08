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
    current_return_type_ = unitType();
    loop_depth_ = 0;
}

void SemanticAnalyzer::analyze(const ast::SourceFile& file) {
    reset();
    registerBuiltins();
    collectFunctions(file);
    for (const auto& function : file.functions) analyzeFunction(function);
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
    print.name = "print";
    print.kind = SymbolKind::BuiltinFunction;
    print.type = Type::function({}, unitType(), true);
    (void)symbols_.declare(std::move(print));

    Symbol assert_symbol;
    assert_symbol.name = "assert";
    assert_symbol.kind = SymbolKind::BuiltinFunction;
    assert_symbol.type = Type::function({boolType()}, unitType());
    (void)symbols_.declare(std::move(assert_symbol));
}

void SemanticAnalyzer::collectFunctions(const ast::SourceFile& file) {
    for (const auto& function : file.functions) {
        std::vector<Type> parameters;
        parameters.reserve(function.parameters.size());
        for (const auto& parameter : function.parameters) parameters.push_back(resolveType(parameter.type));
        const Type result = function.return_type ? resolveType(*function.return_type) : unitType();

        Symbol symbol;
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

void SemanticAnalyzer::analyzeFunction(const ast::FunctionDecl& function) {
    const auto signature_it = function_signatures_.find(&function);
    const Type signature = signature_it != function_signatures_.end()
                               ? signature_it->second
                               : Type::function({}, unitType());
    current_return_type_ = signature.return_type ? *signature.return_type : unitType();
    loop_depth_ = 0;
    symbols_.pushScope();

    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        Symbol symbol;
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

    symbols_.popScope();
}

Type SemanticAnalyzer::resolveType(const ast::TypeRef& type_ref) {
    const auto name = type_ref.name();
    if (const auto kind = simpleTypeKind(name)) return Type::simple(*kind);
    error(type_ref.span, "unknown type `" + name + "`");
    return errorType();
}

Type SemanticAnalyzer::checkBlock(const ast::BlockExpr& block, bool create_scope) {
    if (create_scope) symbols_.pushScope();

    for (const auto& statement : block.statements) checkStatement(*statement);
    Type result = block.tail_expression ? checkExpr(*block.tail_expression) : unitType();

    if (create_scope) symbols_.popScope();
    return result;
}

void SemanticAnalyzer::checkStatement(const ast::Stmt& statement) {
    switch (statement.kind) {
        case ast::StmtKind::Let: {
            const auto& let = static_cast<const ast::LetStmt&>(statement);
            const Type initializer_type = checkExpr(*let.initializer);
            Type declared_type = initializer_type;
            if (let.type) {
                declared_type = resolveType(*let.type);
                if (!compatible(declared_type, initializer_type, let.initializer.get())) {
                    error(let.initializer->span,
                          "cannot initialize `" + let.name + "` of type `" + declared_type.name() +
                          "` with value of type `" + initializer_type.name() + "`");
                }
            }

            Symbol symbol;
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
            (void)checkExpr(*expr.expression);
            break;
        }
    }
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
        case TokenKind::Ampersand:
            return Type::reference(operand);
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

    if (!target.is_mutable) {
        error(expression.target->span, "cannot assign to immutable value");
    }

    if (expression.op == TokenKind::Equal) {
        if (!compatible(target.type, value, expression.value.get())) {
            error(expression.value->span,
                  "cannot assign value of type `" + value.name() + "` to `" + target.type.name() + "`");
        }
        return unitType();
    }

    const TokenKind base = compoundBaseOperator(expression.op);
    if (isArithmeticOperator(base)) {
        if (!target.type.isNumeric() || !value.isNumeric() || !compatible(target.type, value, expression.value.get())) {
            error(expression.span, "compound arithmetic assignment requires compatible numeric operands");
        }
    } else if (isBitwiseOperator(base)) {
        if (!target.type.isInteger() || !value.isInteger() || !compatible(target.type, value, expression.value.get())) {
            error(expression.span, "compound bitwise assignment requires compatible integer operands");
        }
    } else if (isShiftOperator(base)) {
        if (!target.type.isInteger() || !value.isInteger()) {
            error(expression.span, "compound shift assignment requires integer operands");
        }
    }
    return unitType();
}

Type SemanticAnalyzer::checkCall(const ast::CallExpr& expression) {
    const Type callee = checkExpr(*expression.callee);
    if (callee.kind != TypeKind::Function) {
        for (const auto& argument : expression.arguments) (void)checkExpr(*argument);
        if (!callee.isError()) error(expression.callee->span, "value of type `" + callee.name() + "` is not callable");
        return errorType();
    }

    if (!callee.variadic_any && expression.arguments.size() != callee.parameters.size()) {
        error(expression.span,
              "function expects " + std::to_string(callee.parameters.size()) + " argument(s), found " +
              std::to_string(expression.arguments.size()));
    }

    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        const Type actual = checkExpr(*expression.arguments[i]);
        if (callee.variadic_any || i >= callee.parameters.size()) continue;
        const Type& expected = callee.parameters[i];
        if (!compatible(expected, actual, expression.arguments[i].get())) {
            error(expression.arguments[i]->span,
                  "argument " + std::to_string(i + 1) + " expects `" + expected.name() +
                  "`, found `" + actual.name() + "`");
        }
    }

    return callee.return_type ? *callee.return_type : unitType();
}

Type SemanticAnalyzer::checkMember(const ast::MemberExpr& expression) {
    (void)checkExpr(*expression.object);
    error(expression.span,
          "member access `." + expression.member + "` requires struct/type metadata, which is not implemented yet");
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

    symbols_.pushScope();
    Symbol binding;
    binding.name = expression.binding;
    binding.kind = SymbolKind::Variable;
    binding.type = binding_type;
    binding.is_mutable = false;
    binding.span = expression.span;
    (void)symbols_.declare(std::move(binding));

    ++loop_depth_;
    (void)checkBlock(*expression.body, false);
    --loop_depth_;
    symbols_.popScope();
    return unitType();
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
        return LValueInfo{.type = symbol->type, .is_mutable = symbol->is_mutable};
    }

    if (expression.kind == ast::ExprKind::Index) {
        const auto& index = static_cast<const ast::IndexExpr&>(expression);
        const Type result = checkIndex(index);
        LValueInfo base = checkLValue(*index.object);
        return LValueInfo{.type = result, .is_mutable = base.is_mutable};
    }

    if (expression.kind == ast::ExprKind::Member) {
        (void)checkMember(static_cast<const ast::MemberExpr&>(expression));
        return {};
    }

    error(expression.span, "expression is not assignable");
    return {};
}

bool SemanticAnalyzer::compatible(const Type& expected, const Type& actual, const ast::Expr* value) const {
    if (expected.isError() || actual.isError()) return true;
    if (expected == actual) return true;

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
