#include "nus/codegen/LlvmEmitter.hpp"

#include "nus/lexer/TokenKind.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nus::codegen {
namespace {

struct Value {
    std::string type;
    std::string repr;
};

std::string sanitize(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 8U);
    for (const char ch : input) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') result.push_back(ch);
        else result.push_back('_');
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) result.insert(result.begin(), '_');
    return result;
}

std::string functionKey(const mir::Function& function) {
    return function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
}

std::string functionSymbol(std::string_view name) {
    if (name == "main") return "nus_main";
    return "nus_" + sanitize(name);
}

std::string structSymbol(std::string_view name) {
    return "%nus.struct." + sanitize(name);
}

std::string enumSymbol(std::string_view name) {
    return "%nus.enum." + sanitize(name);
}

std::string removeUnderscores(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    return text;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

std::string stripNumericSuffix(std::string text, const sema::Type& type) {
    text = removeUnderscores(std::move(text));
    const std::string suffix = type.name();
    if (!suffix.empty() && endsWith(text, suffix)) text.resize(text.size() - suffix.size());
    return text;
}

std::string integerLiteral(std::string text, const sema::Type& type) {
    text = stripNumericSuffix(std::move(text), type);
    try {
        std::size_t consumed = 0;
        unsigned long long value = 0;
        if (text.rfind("0b", 0) == 0 || text.rfind("0B", 0) == 0) {
            value = std::stoull(text.substr(2), &consumed, 2);
        } else if (text.rfind("0o", 0) == 0 || text.rfind("0O", 0) == 0) {
            value = std::stoull(text.substr(2), &consumed, 8);
        } else {
            value = std::stoull(text, &consumed, 0);
        }
        (void)consumed;
        return std::to_string(value);
    } catch (...) {
        throw std::runtime_error("LLVM codegen: invalid integer literal `" + text + "`");
    }
}

std::string floatLiteral(std::string text, const sema::Type& type) {
    text = stripNumericSuffix(std::move(text), type);
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}

std::string decodeString(std::string_view literal) {
    std::string body;
    if (literal.rfind("r\"", 0) == 0 && literal.size() >= 3) {
        body = std::string(literal.substr(2, literal.size() - 3));
        return body;
    }
    if (literal.rfind("r#\"", 0) == 0 && literal.size() >= 5) {
        const auto first_quote = literal.find('"');
        const auto last_quote = literal.rfind('"');
        if (first_quote != std::string_view::npos && last_quote > first_quote) {
            return std::string(literal.substr(first_quote + 1, last_quote - first_quote - 1));
        }
    }
    std::size_t begin = 0;
    if (!literal.empty() && literal.front() == 'b') begin = 1;
    if (begin >= literal.size() || literal[begin] != '"') return std::string(literal);
    const std::size_t end = literal.size() > begin + 1 && literal.back() == '"' ? literal.size() - 1 : literal.size();
    for (std::size_t i = begin + 1; i < end; ++i) {
        char ch = literal[i];
        if (ch != '\\' || i + 1 >= end) {
            body.push_back(ch);
            continue;
        }
        const char escaped = literal[++i];
        switch (escaped) {
            case 'n': body.push_back('\n'); break;
            case 'r': body.push_back('\r'); break;
            case 't': body.push_back('\t'); break;
            case '0': body.push_back('\0'); break;
            case '\\': body.push_back('\\'); break;
            case '"': body.push_back('"'); break;
            case '\'': body.push_back('\''); break;
            default: body.push_back(escaped); break;
        }
    }
    return body;
}

std::uint32_t decodeChar(std::string_view literal) {
    std::size_t begin = (!literal.empty() && literal.front() == 'b') ? 1U : 0U;
    if (begin >= literal.size() || literal[begin] != '\'') return 0;
    ++begin;
    if (begin >= literal.size()) return 0;
    if (literal[begin] != '\\') return static_cast<unsigned char>(literal[begin]);
    if (++begin >= literal.size()) return 0;
    switch (literal[begin]) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '0': return 0;
        case '\\': return '\\';
        case '\'': return '\'';
        default: return static_cast<unsigned char>(literal[begin]);
    }
}

std::string llvmBytes(std::string_view bytes, bool null_terminate = true) {
    std::ostringstream out;
    out << 'c' << '"';
    out << std::uppercase << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) out << '\\' << std::setw(2) << static_cast<unsigned>(byte);
    if (null_terminate) out << "\\00";
    out << '"';
    return out.str();
}

class ModuleEmitter;

class FunctionEmitter {
public:
    FunctionEmitter(ModuleEmitter& module, const mir::Function& function, std::ostringstream& out)
        : module_(module), function_(function), out_(out) {}

    void emit();

private:
    [[nodiscard]] std::string next(std::string_view hint = "v") {
        return "%" + std::string(hint) + std::to_string(next_value_++);
    }
    [[nodiscard]] const mir::Local& local(mir::LocalId id) const;
    [[nodiscard]] std::string slot(mir::LocalId id) const { return "%l" + std::to_string(id); }
    [[nodiscard]] std::string dropFlag(mir::LocalId id) const { return "%df" + std::to_string(id); }
    [[nodiscard]] std::string nextLabel(std::string_view hint) { return std::string(hint) + std::to_string(next_label_++); }
    [[nodiscard]] Value value(const mir::Operand& operand);
    [[nodiscard]] Value coerce(Value value, const sema::Type& source, const sema::Type& target);
    [[nodiscard]] sema::Type commonNumericType(const sema::Type& left, const sema::Type& right) const;
    [[nodiscard]] std::string objectPointer(const mir::Operand& operand, sema::Type& base_type);
    [[nodiscard]] std::string indexPointer(const mir::Operand& object, const mir::Operand& index, sema::Type& element_type);
    void store(mir::LocalId id, const Value& value);
    void instruction(const mir::Instruction& inst);
    void terminator(const mir::Terminator& term);
    void builtinPrint(const std::vector<mir::Operand>& arguments);
    void printOne(const mir::Operand& operand);
    void markMoved(const mir::Operand& operand);
    void emitDestroy(const sema::Type& type, const std::string& pointer);
    void emitDrop(const mir::Instruction& inst, bool conditional);
    void builtinStringClone(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinStringLen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinVecNew(const mir::Instruction& inst);
    void builtinVecPush(const std::vector<mir::Operand>& arguments);
    void builtinVecLen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinVecGet(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinVecIntoIter(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinVecBorrowIter(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments, bool is_mutable);
    void builtinIpv4(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinSocketAddr(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinSocketPort(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinNetworkErrorCode(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinNetworkErrorReason(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinDnsResolveIpv4(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpConnect(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpListen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpAccept(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpSetNoDelay(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpSetTimeout(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpSend(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinTcpReceive(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinUdpBind(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinUdpSendTo(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    void builtinUdpReceive(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments);
    [[nodiscard]] std::string emitSockaddrIn(const Value& address);
    [[nodiscard]] Value emitNetworkResult(const sema::Type& result_type, bool is_error, const Value& payload);
    [[nodiscard]] Value emitNetworkError(int code, std::optional<int> fixed_reason = std::nullopt);
    [[nodiscard]] std::string socketHandleForCall(const Value& stream);

    ModuleEmitter& module_;
    const mir::Function& function_;
    std::ostringstream& out_;
    std::size_t next_value_{0};
    std::size_t next_label_{0};
};

class ModuleEmitter {
public:
    ModuleEmitter(const mir::Program& program, TargetInfo target)
        : program_(program), target_(std::move(target)) {
        collectStrings();
    }

    std::string run() {
        std::ostringstream out;
        out << "; NUS LLVM IR - milestone 16\n";
        out << "; target: " << target_.description() << "\n";
        if (!target_.triple.empty()) out << "target triple = \"" << target_.triple << "\"\n";
        out << "\n";
        emitTypeDeclarations(out);
        emitRuntimeGlobals(out);
        emitStringGlobals(out);
        out << "declare i32 @printf(ptr, ...)\n";
        out << "declare void @abort()\n";
        out << "declare ptr @malloc(" << usizeType() << ")\n";
        out << "declare ptr @realloc(ptr, " << usizeType() << ")\n";
        out << "declare void @free(ptr)\n";
        out << "declare ptr @memcpy(ptr, ptr, " << usizeType() << ")\n";
        out << "declare i16 @htons(i16)\n";
        out << "declare i32 @htonl(i32)\n";
        out << "declare i32 @ntohl(i32)\n";
        out << "declare ptr @gethostbyname(ptr)\n";
        if (windowsTarget()) {
            out << "declare i32 @WSAStartup(i16, ptr)\n";
            out << "declare i32 @WSAGetLastError()\n";
            out << "declare i64 @socket(i32, i32, i32)\n";
            out << "declare i32 @connect(i64, ptr, i32)\n";
            out << "declare i32 @bind(i64, ptr, i32)\n";
            out << "declare i32 @listen(i64, i32)\n";
            out << "declare i64 @accept(i64, ptr, ptr)\n";
            out << "declare i32 @setsockopt(i64, i32, i32, ptr, i32)\n";
            out << "declare i32 @send(i64, ptr, i32, i32)\n";
            out << "declare i32 @sendto(i64, ptr, i32, i32, ptr, i32)\n";
            out << "declare i32 @recv(i64, ptr, i32, i32)\n";
            out << "declare i32 @closesocket(i64)\n";
        } else {
            out << "declare i32 @socket(i32, i32, i32)\n";
            out << "declare i32 @connect(i32, ptr, i32)\n";
            out << "declare i32 @bind(i32, ptr, i32)\n";
            out << "declare i32 @listen(i32, i32)\n";
            out << "declare i32 @accept(i32, ptr, ptr)\n";
            out << "declare i32 @setsockopt(i32, i32, i32, ptr, i32)\n";
            out << "declare i64 @send(i32, ptr, i64, i32)\n";
            out << "declare i64 @sendto(i32, ptr, i64, i32, ptr, i32)\n";
            out << "declare i64 @recv(i32, ptr, i64, i32)\n";
            out << "declare i32 @close(i32)\n";
            if (darwinTarget()) out << "declare ptr @__error()\n";
            else out << "declare ptr @__errno_location()\n";
        }
        out << "\n";
        emitNetworkInitHelper(out);
        emitNetworkErrorHelpers(out);
        emitAssertHelper(out);
        for (const auto& function : program_.functions) FunctionEmitter(*this, function, out).emit();
        emitMainWrapper(out);
        return out.str();
    }

    [[nodiscard]] std::string llvmType(const sema::Type& type) const {
        using sema::TypeKind;
        switch (type.kind) {
            case TypeKind::Unit: return "void";
            case TypeKind::Bool: return "i1";
            case TypeKind::I8:
            case TypeKind::U8: return "i8";
            case TypeKind::I16:
            case TypeKind::U16: return "i16";
            case TypeKind::I32:
            case TypeKind::U32:
            case TypeKind::Char: return "i32";
            case TypeKind::I64:
            case TypeKind::U64: return "i64";
            case TypeKind::ISize:
            case TypeKind::USize: return target_.pointer_bits == 32U ? "i32" : "i64";
            case TypeKind::F32: return "float";
            case TypeKind::F64: return "double";
            case TypeKind::String: return "%nus.string";
            case TypeKind::Bytes: return "%nus.bytes";
            case TypeKind::IpAddr: return "%nus.ipaddr";
            case TypeKind::SocketAddr: return "%nus.socketaddr";
            case TypeKind::NetworkError: return "%nus.neterr";
            case TypeKind::TcpStream: return "%nus.tcpstream";
            case TypeKind::TcpListener: return "%nus.tcplistener";
            case TypeKind::UdpSocket: return "%nus.udpsocket";
            case TypeKind::Vec:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed Vec type");
                return "{ ptr, " + usizeType() + ", " + usizeType() + " }";
            case TypeKind::VecIter:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed VecIter type");
                return "{ ptr, " + usizeType() + ", " + usizeType() + ", " + usizeType() + " }";
            case TypeKind::VecRefIter:
            case TypeKind::VecMutIter:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed borrowed Vec iterator type");
                return "{ ptr, " + usizeType() + ", " + usizeType() + " }";
            case TypeKind::Option:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed Option type");
                return "{ i1, " + llvmType(*type.element) + " }";
            case TypeKind::Result:
                if (type.generic_arguments.size() != 2) throw std::runtime_error("LLVM codegen: malformed Result type");
                return "{ i1, " + llvmType(type.generic_arguments[0]) + ", " + llvmType(type.generic_arguments[1]) + " }";
            case TypeKind::TypeParameter:
                throw std::runtime_error("LLVM codegen: unmonomorphized type parameter `" + type.name() + "`");
            case TypeKind::Struct: return structSymbol(type.nominal_name);
            case TypeKind::Enum: return enumSymbol(type.nominal_name);
            case TypeKind::Array:
                if (!type.element || !type.array_length) throw std::runtime_error("LLVM codegen: unsized array is not supported");
                return "[" + std::to_string(*type.array_length) + " x " + llvmType(*type.element) + "]";
            case TypeKind::Slice:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed slice type");
                return "{ ptr, " + usizeType() + " }";
            case TypeKind::Range:
                if (!type.element) throw std::runtime_error("LLVM codegen: malformed range type");
                return "{ " + llvmType(*type.element) + ", " + llvmType(*type.element) + ", i1 }";
            case TypeKind::Reference: return "ptr";
            case TypeKind::Function: return "ptr";
            case TypeKind::Error:
            case TypeKind::Unknown:
                throw std::runtime_error("LLVM codegen: unresolved type `" + type.name() + "`");
        }
        throw std::runtime_error("LLVM codegen: unknown type");
    }

    [[nodiscard]] std::string usizeType() const { return target_.pointer_bits == 32U ? "i32" : "i64"; }
    [[nodiscard]] bool windowsTarget() const { return target_.triple.find("windows") != std::string::npos; }
    [[nodiscard]] bool darwinTarget() const { return target_.triple.find("darwin") != std::string::npos || target_.triple.find("apple") != std::string::npos; }
    [[nodiscard]] const mir::Struct& structure(std::string_view name) const {
        for (const auto& structure : program_.structs) if (structure.name == name) return structure;
        throw std::runtime_error("LLVM codegen: unknown struct `" + std::string(name) + "`");
    }
    [[nodiscard]] std::size_t fieldIndex(std::string_view structure_name, std::string_view field) const {
        const auto& definition = structure(structure_name);
        for (std::size_t i = 0; i < definition.fields.size(); ++i) {
            if (definition.fields[i].name == field) return i;
        }
        throw std::runtime_error("LLVM codegen: struct `" + std::string(structure_name) + "` has no field `" + std::string(field) + "`");
    }
    [[nodiscard]] const mir::Enum& enumeration(std::string_view name) const {
        for (const auto& enumeration : program_.enums) if (enumeration.name == name) return enumeration;
        throw std::runtime_error("LLVM codegen: unknown enum `" + std::string(name) + "`");
    }
    [[nodiscard]] const mir::EnumVariant& enumVariant(std::string_view enum_name, std::string_view variant) const {
        const auto& definition = enumeration(enum_name);
        for (const auto& candidate : definition.variants) if (candidate.name == variant) return candidate;
        throw std::runtime_error("LLVM codegen: enum `" + std::string(enum_name) + "` has no variant `" + std::string(variant) + "`");
    }
    [[nodiscard]] std::size_t enumPayloadFieldIndex(std::string_view enum_name, std::string_view variant, std::size_t payload_index) const {
        const auto& definition = enumeration(enum_name);
        std::size_t field = 1;
        for (const auto& candidate : definition.variants) {
            if (candidate.name == variant) {
                if (payload_index >= candidate.payloads.size()) throw std::runtime_error("LLVM codegen: invalid enum payload index");
                return field + payload_index;
            }
            field += candidate.payloads.size();
        }
        throw std::runtime_error("LLVM codegen: enum variant not found");
    }
    [[nodiscard]] std::string symbol(std::string_view name) const { return functionSymbol(name); }
    [[nodiscard]] std::pair<std::string, std::size_t> stringGlobal(std::string_view literal) const {
        const auto found = strings_.find(std::string(literal));
        if (found == strings_.end()) throw std::runtime_error("LLVM codegen: string literal was not interned");
        return {found->second.name, found->second.length};
    }

private:
    friend class FunctionEmitter;
    struct InternedString { std::string name; std::string bytes; std::size_t length{0}; };

    void collectStrings() {
        std::size_t next_id = 0;
        auto collect = [&](const mir::Operand& operand) {
            if (operand.kind != mir::OperandKind::Literal || operand.type.kind != sema::TypeKind::String) return;
            const std::string key = operand.text;
            if (strings_.contains(key)) return;
            const std::string bytes = decodeString(key);
            strings_.emplace(key, InternedString{".nus.str." + std::to_string(next_id++), bytes, bytes.size()});
        };
        for (const auto& function : program_.functions) {
            for (const auto& block : function.blocks) {
                for (const auto& inst : block.instructions) for (const auto& operand : inst.operands) collect(operand);
                if (block.terminator) {
                    collect(block.terminator->condition);
                    collect(block.terminator->value);
                }
            }
        }
    }

    void emitTypeDeclarations(std::ostringstream& out) const {
        out << "%nus.string = type { ptr, " << usizeType() << ", " << usizeType() << " }\n";
        out << "%nus.bytes = type { ptr, " << usizeType() << " }\n";
        out << "%nus.ipaddr = type { i32 }\n";
        out << "%nus.socketaddr = type { %nus.ipaddr, i16 }\n";
        out << "%nus.neterr = type { i32, i32 }\n";
        out << "%nus.tcpstream = type { i64 }\n";
        out << "%nus.tcplistener = type { i64 }\n";
        out << "%nus.udpsocket = type { i64 }\n";
        out << "%nus.hostent = type { ptr, ptr, i32, i32, ptr }\n";
        out << "%nus.timeval = type { i64, i64 }\n";
        out << "%nus.sockaddr.in = type { i16, i16, i32, [8 x i8] }\n";
        for (const auto& structure : program_.structs) {
            out << structSymbol(structure.name) << " = type { ";
            for (std::size_t i = 0; i < structure.fields.size(); ++i) {
                if (i != 0) out << ", ";
                out << llvmType(structure.fields[i].type);
            }
            out << " }\n";
        }
        for (const auto& enumeration : program_.enums) {
            out << enumSymbol(enumeration.name) << " = type { i32";
            for (const auto& variant : enumeration.variants) {
                for (const auto& payload : variant.payloads) out << ", " << llvmType(payload);
            }
            out << " }\n";
        }
        out << '\n';
    }

    static void emitGlobalCString(std::ostringstream& out, std::string_view name, std::string_view value) {
        out << '@' << name << " = private unnamed_addr constant [" << (value.size() + 1U)
            << " x i8] " << llvmBytes(value) << "\n";
    }

    void emitRuntimeGlobals(std::ostringstream& out) const {
        emitGlobalCString(out, ".nus.fmt.i32", "%d");
        emitGlobalCString(out, ".nus.fmt.u32", "%u");
        emitGlobalCString(out, ".nus.fmt.i64", "%lld");
        emitGlobalCString(out, ".nus.fmt.u64", "%llu");
        emitGlobalCString(out, ".nus.fmt.f64", "%.17g");
        emitGlobalCString(out, ".nus.fmt.char", "%c");
        emitGlobalCString(out, ".nus.fmt.str", "%.*s");
        emitGlobalCString(out, ".nus.fmt.cstr", "%s");
        emitGlobalCString(out, ".nus.fmt.ptr", "%p");
        emitGlobalCString(out, ".nus.space", " ");
        emitGlobalCString(out, ".nus.newline", "\n");
        emitGlobalCString(out, ".nus.true", "true");
        emitGlobalCString(out, ".nus.false", "false");
        emitGlobalCString(out, ".nus.assert", "NUS assertion failed\n");
        out << '\n';
    }

    void emitStringGlobals(std::ostringstream& out) const {
        for (const auto& [_, value] : strings_) {
            emitGlobalCString(out, value.name, value.bytes);
        }
        if (!strings_.empty()) out << '\n';
    }

    void emitNetworkErrorHelpers(std::ostringstream& out) const {
        out << "define internal i32 @nus_net_last_error() {\n";
        out << "entry:\n";
        if (windowsTarget()) {
            out << "  %error = call i32 @WSAGetLastError()\n";
        } else if (darwinTarget()) {
            out << "  %ptr = call ptr @__error()\n";
            out << "  %error = load i32, ptr %ptr\n";
        } else {
            out << "  %ptr = call ptr @__errno_location()\n";
            out << "  %error = load i32, ptr %ptr\n";
        }
        out << "  ret i32 %error\n";
        out << "}\n\n";

        const int timeout = windowsTarget() ? 10060 : (darwinTarget() ? 60 : 110);
        const int again = windowsTarget() ? 10035 : (darwinTarget() ? 35 : 11);
        const int refused = windowsTarget() ? 10061 : (darwinTarget() ? 61 : 111);
        const int in_use = windowsTarget() ? 10048 : (darwinTarget() ? 48 : 98);
        out << "define internal i32 @nus_net_reason(i32 %native) {\n";
        out << "entry:\n";
        out << "  %is_timeout = icmp eq i32 %native, " << timeout << "\n";
        out << "  %is_again = icmp eq i32 %native, " << again << "\n";
        out << "  %timeout = or i1 %is_timeout, %is_again\n";
        out << "  %refused = icmp eq i32 %native, " << refused << "\n";
        out << "  %in_use = icmp eq i32 %native, " << in_use << "\n";
        out << "  %base = select i1 %in_use, i32 3, i32 0\n";
        out << "  %with_refused = select i1 %refused, i32 2, i32 %base\n";
        out << "  %reason = select i1 %timeout, i32 1, i32 %with_refused\n";
        out << "  ret i32 %reason\n";
        out << "}\n\n";
    }

    void emitNetworkInitHelper(std::ostringstream& out) const {
        if (!windowsTarget()) return;
        out << "@.nus.wsa.started = internal global i1 false\n\n";
        out << "define internal i32 @nus_net_init() {\n";
        out << "entry:\n";
        out << "  %started = load i1, ptr @.nus.wsa.started\n";
        out << "  br i1 %started, label %ready, label %start\n";
        out << "start:\n";
        out << "  %data = alloca [512 x i8]\n";
        out << "  %rc = call i32 @WSAStartup(i16 514, ptr %data)\n";
        out << "  %ok = icmp eq i32 %rc, 0\n";
        out << "  br i1 %ok, label %mark, label %failed\n";
        out << "mark:\n";
        out << "  store i1 true, ptr @.nus.wsa.started\n";
        out << "  br label %ready\n";
        out << "failed:\n";
        out << "  ret i32 %rc\n";
        out << "ready:\n";
        out << "  ret i32 0\n";
        out << "}\n\n";
    }

    void emitAssertHelper(std::ostringstream& out) const {
        out << "define internal void @nus_assert(i1 %condition) {\n";
        out << "entry:\n";
        out << "  br i1 %condition, label %ok, label %fail\n";
        out << "fail:\n";
        out << "  call i32 (ptr, ...) @printf(ptr @.nus.assert)\n";
        out << "  call void @abort()\n";
        out << "  unreachable\n";
        out << "ok:\n";
        out << "  ret void\n";
        out << "}\n\n";
    }

    void emitMainWrapper(std::ostringstream& out) const {
        const mir::Function* main = nullptr;
        for (const auto& function : program_.functions) {
            if (function.owner_type.empty() && function.name == "main") { main = &function; break; }
        }
        if (!main) return;
        for (const auto& local : main->locals) {
            if (local.is_parameter || local.is_receiver) {
                throw std::runtime_error("LLVM codegen: NUS `main` cannot have parameters in milestone 9");
            }
        }
        out << "define i32 @main() {\nentry:\n";
        if (main->return_type.isUnit()) {
            out << "  call void @nus_main()\n";
            out << "  ret i32 0\n";
        } else if (main->return_type.kind == sema::TypeKind::I32) {
            out << "  %code = call i32 @nus_main()\n";
            out << "  ret i32 %code\n";
        } else {
            throw std::runtime_error("LLVM codegen: milestone 9 supports only unit or i32 return type for `main`");
        }
        out << "}\n";
    }

    const mir::Program& program_;
    TargetInfo target_;
    std::unordered_map<std::string, InternedString> strings_;
};

const mir::Local& FunctionEmitter::local(mir::LocalId id) const {
    for (const auto& local : function_.locals) if (local.id == id) return local;
    throw std::runtime_error("LLVM codegen: unknown MIR local %" + std::to_string(id));
}

Value FunctionEmitter::value(const mir::Operand& operand) {
    using mir::OperandKind;
    switch (operand.kind) {
        case OperandKind::Unit: return {"void", ""};
        case OperandKind::Local: {
            const std::string ty = module_.llvmType(operand.type);
            const std::string result = next("load");
            out_ << "  " << result << " = load " << ty << ", ptr " << slot(operand.local) << "\n";
            return {ty, result};
        }
        case OperandKind::Literal: {
            using sema::TypeKind;
            switch (operand.type.kind) {
                case TypeKind::Bool: return {"i1", operand.text == "true" ? "1" : "0"};
                case TypeKind::I8: case TypeKind::I16: case TypeKind::I32: case TypeKind::I64: case TypeKind::ISize:
                case TypeKind::U8: case TypeKind::U16: case TypeKind::U32: case TypeKind::U64: case TypeKind::USize:
                    return {module_.llvmType(operand.type), integerLiteral(operand.text, operand.type)};
                case TypeKind::F32: case TypeKind::F64:
                    return {module_.llvmType(operand.type), floatLiteral(operand.text, operand.type)};
                case TypeKind::Char:
                    return {"i32", std::to_string(decodeChar(operand.text))};
                case TypeKind::String: {
                    const auto [global, length] = module_.stringGlobal(operand.text);
                    const std::string ty = module_.llvmType(operand.type);
                    const std::string first = next("str");
                    const std::string second = next("str");
                    const std::string third = next("str");
                    out_ << "  " << first << " = insertvalue " << ty << " poison, ptr @" << global << ", 0\n";
                    out_ << "  " << second << " = insertvalue " << ty << ' ' << first << ", " << module_.usizeType()
                         << ' ' << length << ", 1\n";
                    out_ << "  " << third << " = insertvalue " << ty << ' ' << second << ", " << module_.usizeType()
                         << " 0, 2\n";
                    return {ty, third};
                }
                default:
                    throw std::runtime_error("LLVM codegen: unsupported literal type `" + operand.type.name() + "`");
            }
        }
        case OperandKind::Global:
            return {"ptr", "@" + module_.symbol(operand.text)};
    }
    throw std::runtime_error("LLVM codegen: invalid operand");
}


Value FunctionEmitter::coerce(Value input, const sema::Type& source, const sema::Type& target) {
    const std::string target_ir = module_.llvmType(target);
    if (input.type == target_ir) return input;

    auto integerBits = [&](const sema::Type& type) -> unsigned {
        using sema::TypeKind;
        switch (type.kind) {
            case TypeKind::I8: case TypeKind::U8: return 8;
            case TypeKind::I16: case TypeKind::U16: return 16;
            case TypeKind::I32: case TypeKind::U32: case TypeKind::Char: return 32;
            case TypeKind::I64: case TypeKind::U64: return 64;
            case TypeKind::ISize: case TypeKind::USize: return static_cast<unsigned>(module_.target_.pointer_bits);
            default: return 0;
        }
    };

    if (source.isInteger() && target.isInteger()) {
        const unsigned from = integerBits(source);
        const unsigned to = integerBits(target);
        const std::string converted = next("cast");
        if (from < to) {
            const bool sign_extend = source.isSignedInteger() && target.isSignedInteger();
            out_ << "  " << converted << " = " << (sign_extend ? "sext" : "zext") << ' ' << input.type << ' ' << input.repr
                 << " to " << target_ir << "\n";
        } else if (from > to) {
            out_ << "  " << converted << " = trunc " << input.type << ' ' << input.repr << " to " << target_ir << "\n";
        } else {
            return {target_ir, input.repr};
        }
        return {target_ir, converted};
    }

    if (source.isFloat() && target.isFloat()) {
        const std::string converted = next("fcast");
        if (source.kind == sema::TypeKind::F32 && target.kind == sema::TypeKind::F64) {
            out_ << "  " << converted << " = fpext float " << input.repr << " to double\n";
        } else if (source.kind == sema::TypeKind::F64 && target.kind == sema::TypeKind::F32) {
            out_ << "  " << converted << " = fptrunc double " << input.repr << " to float\n";
        } else {
            return {target_ir, input.repr};
        }
        return {target_ir, converted};
    }

    throw std::runtime_error("LLVM codegen: missing coercion from `" + source.name() + "` to `" + target.name() + "`");
}

sema::Type FunctionEmitter::commonNumericType(const sema::Type& left, const sema::Type& right) const {
    if (left == right) return left;
    // Unsuffixed literals currently enter HIR as i32/f64. Prefer the explicitly
    // constrained peer when semantic analysis already proved compatibility.
    if (left.kind == sema::TypeKind::I32 && right.isInteger()) return right;
    if (right.kind == sema::TypeKind::I32 && left.isInteger()) return left;
    if (left.kind == sema::TypeKind::F64 && right.isFloat()) return right;
    if (right.kind == sema::TypeKind::F64 && left.isFloat()) return left;
    return left;
}

void FunctionEmitter::store(mir::LocalId id, const Value& stored) {
    const auto& destination = local(id);
    if (destination.type.isUnit()) return;
    const std::string expected = module_.llvmType(destination.type);
    if (expected != stored.type) {
        throw std::runtime_error("LLVM codegen: cannot store `" + stored.type + "` in local `" + destination.name + "` of type `" + expected + "`");
    }
    out_ << "  store " << stored.type << ' ' << stored.repr << ", ptr " << slot(id) << "\n";
    if (!destination.type.isCopy() && !destination.type.isReference()) {
        out_ << "  store i1 true, ptr " << dropFlag(id) << "\n";
    }
}

void FunctionEmitter::markMoved(const mir::Operand& operand) {
    if (operand.kind != mir::OperandKind::Local || operand.type.isCopy() || operand.type.isReference()) return;
    out_ << "  store i1 false, ptr " << dropFlag(operand.local) << "\n";
}

void FunctionEmitter::emitDestroy(const sema::Type& type, const std::string& pointer) {
    using sema::TypeKind;
    switch (type.kind) {
        case TypeKind::String: {
            const std::string loaded = next("dropstr");
            const std::string data = next("dropdata");
            const std::string cap = next("dropcap");
            const std::string owned = next("owned");
            const std::string do_label = nextLabel("drop.str.do.");
            const std::string end_label = nextLabel("drop.str.end.");
            out_ << "  " << loaded << " = load %nus.string, ptr " << pointer << "\n";
            out_ << "  " << data << " = extractvalue %nus.string " << loaded << ", 0\n";
            out_ << "  " << cap << " = extractvalue %nus.string " << loaded << ", 2\n";
            out_ << "  " << owned << " = icmp ne " << module_.usizeType() << ' ' << cap << ", 0\n";
            out_ << "  br i1 " << owned << ", label %" << do_label << ", label %" << end_label << "\n";
            out_ << do_label << ":\n";
            out_ << "  call void @free(ptr " << data << ")\n";
            out_ << "  br label %" << end_label << "\n";
            out_ << end_label << ":\n";
            break;
        }
        case TypeKind::TcpStream:
        case TypeKind::TcpListener:
        case TypeKind::UdpSocket: {
            const std::string aggregate = next("dropsocket");
            const std::string handle64 = next("drophandle");
            const std::string open = next("socketopen");
            const std::string close_label = nextLabel("drop.socket.close.");
            const std::string end_label = nextLabel("drop.socket.end.");
            out_ << "  " << aggregate << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << handle64 << " = extractvalue " << module_.llvmType(type) << ' ' << aggregate << ", 0\n";
            out_ << "  " << open << " = icmp ne i64 " << handle64 << ", -1\n";
            out_ << "  br i1 " << open << ", label %" << close_label << ", label %" << end_label << "\n";
            out_ << close_label << ":\n";
            if (module_.windowsTarget()) {
                out_ << "  call i32 @closesocket(i64 " << handle64 << ")\n";
            } else {
                const std::string handle32 = next("dropfd");
                out_ << "  " << handle32 << " = trunc i64 " << handle64 << " to i32\n";
                out_ << "  call i32 @close(i32 " << handle32 << ")\n";
            }
            out_ << "  br label %" << end_label << "\n";
            out_ << end_label << ":\n";
            break;
        }
        case TypeKind::Vec: {
            if (!type.element) throw std::runtime_error("LLVM codegen: malformed Vec drop type");
            const std::string loaded = next("dropvec");
            const std::string data = next("dropdata");
            const std::string length = next("droplen");
            out_ << "  " << loaded << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << data << " = extractvalue " << module_.llvmType(type) << ' ' << loaded << ", 0\n";
            out_ << "  " << length << " = extractvalue " << module_.llvmType(type) << ' ' << loaded << ", 1\n";
            if (!type.element->isCopy() && !type.element->isReference()) {
                const std::string index_slot = next("dropidxslot");
                const std::string head = nextLabel("drop.vec.head.");
                const std::string body = nextLabel("drop.vec.body.");
                const std::string done = nextLabel("drop.vec.done.");
                out_ << "  " << index_slot << " = alloca " << module_.usizeType() << "\n";
                out_ << "  store " << module_.usizeType() << " 0, ptr " << index_slot << "\n";
                out_ << "  br label %" << head << "\n";
                out_ << head << ":\n";
                const std::string index = next("dropidx");
                const std::string cond = next("dropcond");
                out_ << "  " << index << " = load " << module_.usizeType() << ", ptr " << index_slot << "\n";
                out_ << "  " << cond << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                out_ << "  br i1 " << cond << ", label %" << body << ", label %" << done << "\n";
                out_ << body << ":\n";
                const std::string element_ptr = next("dropvecelem");
                out_ << "  " << element_ptr << " = getelementptr inbounds " << module_.llvmType(*type.element)
                     << ", ptr " << data << ", " << module_.usizeType() << ' ' << index << "\n";
                emitDestroy(*type.element, element_ptr);
                const std::string next_index = next("dropidx");
                out_ << "  " << next_index << " = add " << module_.usizeType() << ' ' << index << ", 1\n";
                out_ << "  store " << module_.usizeType() << ' ' << next_index << ", ptr " << index_slot << "\n";
                out_ << "  br label %" << head << "\n";
                out_ << done << ":\n";
            }
            out_ << "  call void @free(ptr " << data << ")\n";
            break;
        }
        case TypeKind::VecIter: {
            if (!type.element) throw std::runtime_error("LLVM codegen: malformed VecIter drop type");
            const std::string loaded = next("dropiter");
            const std::string data = next("dropdata");
            const std::string length = next("droplen");
            const std::string start_index = next("dropstart");
            out_ << "  " << loaded << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << data << " = extractvalue " << module_.llvmType(type) << ' ' << loaded << ", 0\n";
            out_ << "  " << length << " = extractvalue " << module_.llvmType(type) << ' ' << loaded << ", 1\n";
            out_ << "  " << start_index << " = extractvalue " << module_.llvmType(type) << ' ' << loaded << ", 3\n";
            if (!type.element->isCopy() && !type.element->isReference()) {
                const std::string index_slot = next("dropidxslot");
                const std::string head = nextLabel("drop.iter.head.");
                const std::string body = nextLabel("drop.iter.body.");
                const std::string done = nextLabel("drop.iter.done.");
                out_ << "  " << index_slot << " = alloca " << module_.usizeType() << "\n";
                out_ << "  store " << module_.usizeType() << ' ' << start_index << ", ptr " << index_slot << "\n";
                out_ << "  br label %" << head << "\n";
                out_ << head << ":\n";
                const std::string index = next("dropidx");
                const std::string cond = next("dropcond");
                out_ << "  " << index << " = load " << module_.usizeType() << ", ptr " << index_slot << "\n";
                out_ << "  " << cond << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                out_ << "  br i1 " << cond << ", label %" << body << ", label %" << done << "\n";
                out_ << body << ":\n";
                const std::string element_ptr = next("dropiterelem");
                out_ << "  " << element_ptr << " = getelementptr inbounds " << module_.llvmType(*type.element)
                     << ", ptr " << data << ", " << module_.usizeType() << ' ' << index << "\n";
                emitDestroy(*type.element, element_ptr);
                const std::string next_index = next("dropidx");
                out_ << "  " << next_index << " = add " << module_.usizeType() << ' ' << index << ", 1\n";
                out_ << "  store " << module_.usizeType() << ' ' << next_index << ", ptr " << index_slot << "\n";
                out_ << "  br label %" << head << "\n";
                out_ << done << ":\n";
            }
            out_ << "  call void @free(ptr " << data << ")\n";
            break;
        }
        case TypeKind::VecRefIter:
        case TypeKind::VecMutIter:
            break;
        case TypeKind::Option: {
            if (!type.element || type.element->isCopy() || type.element->isReference()) break;
            const std::string aggregate = next("dropopt");
            const std::string tag = next("droptag");
            const std::string some_label = nextLabel("drop.option.some.");
            const std::string end_label = nextLabel("drop.option.end.");
            out_ << "  " << aggregate << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << tag << " = extractvalue " << module_.llvmType(type) << ' ' << aggregate << ", 0\n";
            out_ << "  br i1 " << tag << ", label %" << some_label << ", label %" << end_label << "\n";
            out_ << some_label << ":\n";
            const std::string payload_ptr = next("droppayload");
            out_ << "  " << payload_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer << ", i32 0, i32 1\n";
            emitDestroy(*type.element, payload_ptr);
            out_ << "  br label %" << end_label << "\n";
            out_ << end_label << ":\n";
            break;
        }
        case TypeKind::Result: {
            if (type.generic_arguments.size() != 2) break;
            const auto& ok_type = type.generic_arguments[0];
            const auto& err_type = type.generic_arguments[1];
            if ((ok_type.isCopy() || ok_type.isReference()) && (err_type.isCopy() || err_type.isReference())) break;
            const std::string aggregate = next("dropres");
            const std::string tag = next("droptag");
            const std::string err_label = nextLabel("drop.result.err.");
            const std::string ok_label = nextLabel("drop.result.ok.");
            const std::string end_label = nextLabel("drop.result.end.");
            out_ << "  " << aggregate << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << tag << " = extractvalue " << module_.llvmType(type) << ' ' << aggregate << ", 0\n";
            out_ << "  br i1 " << tag << ", label %" << err_label << ", label %" << ok_label << "\n";
            out_ << ok_label << ":\n";
            if (!ok_type.isCopy() && !ok_type.isReference()) {
                const std::string payload_ptr = next("dropok");
                out_ << "  " << payload_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer << ", i32 0, i32 1\n";
                emitDestroy(ok_type, payload_ptr);
            }
            out_ << "  br label %" << end_label << "\n";
            out_ << err_label << ":\n";
            if (!err_type.isCopy() && !err_type.isReference()) {
                const std::string payload_ptr = next("droperr");
                out_ << "  " << payload_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer << ", i32 0, i32 2\n";
                emitDestroy(err_type, payload_ptr);
            }
            out_ << "  br label %" << end_label << "\n";
            out_ << end_label << ":\n";
            break;
        }
        case TypeKind::Enum: {
            const auto& enumeration = module_.enumeration(type.nominal_name);
            bool needs_drop = false;
            for (const auto& variant : enumeration.variants) {
                for (const auto& payload : variant.payloads) {
                    if (!payload.isCopy() && !payload.isReference()) needs_drop = true;
                }
            }
            if (!needs_drop) break;
            const std::string aggregate = next("dropenum");
            const std::string tag = next("droptag");
            const std::string end_label = nextLabel("drop.enum.end.");
            out_ << "  " << aggregate << " = load " << module_.llvmType(type) << ", ptr " << pointer << "\n";
            out_ << "  " << tag << " = extractvalue " << module_.llvmType(type) << ' ' << aggregate << ", 0\n";
            for (std::size_t vi = 0; vi < enumeration.variants.size(); ++vi) {
                const auto& variant = enumeration.variants[vi];
                const std::string case_label = nextLabel("drop.enum.case.");
                const std::string next_label = vi + 1 == enumeration.variants.size() ? end_label : nextLabel("drop.enum.next.");
                const std::string condition = next("dropcase");
                out_ << "  " << condition << " = icmp eq i32 " << tag << ", " << variant.tag << "\n";
                out_ << "  br i1 " << condition << ", label %" << case_label << ", label %" << next_label << "\n";
                out_ << case_label << ":\n";
                for (std::size_t pi = 0; pi < variant.payloads.size(); ++pi) {
                    const auto& payload = variant.payloads[pi];
                    if (payload.isCopy() || payload.isReference()) continue;
                    const auto field = module_.enumPayloadFieldIndex(type.nominal_name, variant.name, pi);
                    const std::string payload_ptr = next("droppayload");
                    out_ << "  " << payload_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer
                         << ", i32 0, i32 " << field << "\n";
                    emitDestroy(payload, payload_ptr);
                }
                out_ << "  br label %" << end_label << "\n";
                if (next_label != end_label) out_ << next_label << ":\n";
            }
            out_ << end_label << ":\n";
            break;
        }
        case TypeKind::Struct: {
            const auto& definition = module_.structure(type.nominal_name);
            for (std::size_t i = 0; i < definition.fields.size(); ++i) {
                const auto& field = definition.fields[i];
                if (field.type.isCopy() || field.type.isReference()) continue;
                const std::string field_ptr = next("dropfield");
                out_ << "  " << field_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer
                     << ", i32 0, i32 " << i << "\n";
                emitDestroy(field.type, field_ptr);
            }
            break;
        }
        case TypeKind::Array: {
            if (!type.element || !type.array_length || type.element->isCopy() || type.element->isReference()) break;
            for (std::size_t i = 0; i < *type.array_length; ++i) {
                const std::string element_ptr = next("dropelem");
                out_ << "  " << element_ptr << " = getelementptr inbounds " << module_.llvmType(type) << ", ptr " << pointer
                     << ", i64 0, i64 " << i << "\n";
                emitDestroy(*type.element, element_ptr);
            }
            break;
        }
        default:
            break;
    }
}

void FunctionEmitter::emitDrop(const mir::Instruction& inst, bool conditional) {
    if (inst.operands.empty() || inst.operands.front().kind != mir::OperandKind::Local) return;
    const auto& operand = inst.operands.front();
    const auto& dropped = local(operand.local);
    if (conditional) {
        const std::string flag = next("dropflag");
        const std::string do_label = nextLabel("drop.do.");
        const std::string end_label = nextLabel("drop.end.");
        out_ << "  " << flag << " = load i1, ptr " << dropFlag(dropped.id) << "\n";
        out_ << "  br i1 " << flag << ", label %" << do_label << ", label %" << end_label << "\n";
        out_ << do_label << ":\n";
        emitDestroy(dropped.type, slot(dropped.id));
        out_ << "  store i1 false, ptr " << dropFlag(dropped.id) << "\n";
        out_ << "  br label %" << end_label << "\n";
        out_ << end_label << ":\n";
        return;
    }
    emitDestroy(dropped.type, slot(dropped.id));
    out_ << "  store i1 false, ptr " << dropFlag(dropped.id) << "\n";
}

std::string FunctionEmitter::objectPointer(const mir::Operand& operand, sema::Type& base_type) {
    if (operand.kind != mir::OperandKind::Local) throw std::runtime_error("LLVM codegen: member access requires addressable local");
    base_type = operand.type;
    if (base_type.kind == sema::TypeKind::Reference) {
        if (!base_type.element) throw std::runtime_error("LLVM codegen: malformed reference type");
        const Value pointer = value(operand);
        base_type = *base_type.element;
        return pointer.repr;
    }
    return slot(operand.local);
}

std::string FunctionEmitter::indexPointer(const mir::Operand& object, const mir::Operand& index, sema::Type& element_type) {
    if (object.kind != mir::OperandKind::Local) throw std::runtime_error("LLVM codegen: indexing requires addressable local");
    sema::Type base = object.type;
    std::string base_ptr;
    if (base.kind == sema::TypeKind::Reference) {
        if (!base.element) throw std::runtime_error("LLVM codegen: malformed reference type");
        base_ptr = value(object).repr;
        base = *base.element;
    } else {
        base_ptr = slot(object.local);
    }
    Value idx = value(index);
    const std::string index64 = next("idx");
    if (idx.type == "i64") {
        idx.repr = idx.repr;
    } else {
        const bool signed_index = index.type.isSignedInteger();
        out_ << "  " << index64 << " = " << (signed_index ? "sext" : "zext") << ' ' << idx.type << ' ' << idx.repr << " to i64\n";
        idx = {"i64", index64};
    }

    if (base.kind == sema::TypeKind::Array) {
        if (!base.element || !base.array_length) throw std::runtime_error("LLVM codegen: malformed array type");
        element_type = *base.element;
        const std::string pointer = next("elem");
        out_ << "  " << pointer << " = getelementptr inbounds " << module_.llvmType(base) << ", ptr " << base_ptr
             << ", i64 0, i64 " << idx.repr << "\n";
        return pointer;
    }
    if (base.kind == sema::TypeKind::Slice) {
        if (!base.element) throw std::runtime_error("LLVM codegen: malformed slice type");
        element_type = *base.element;
        const std::string aggregate = next("slice");
        out_ << "  " << aggregate << " = load " << module_.llvmType(base) << ", ptr " << base_ptr << "\n";
        const std::string data = next("data");
        out_ << "  " << data << " = extractvalue " << module_.llvmType(base) << ' ' << aggregate << ", 0\n";
        const std::string pointer = next("elem");
        out_ << "  " << pointer << " = getelementptr inbounds " << module_.llvmType(element_type) << ", ptr " << data
             << ", i64 " << idx.repr << "\n";
        return pointer;
    }
    throw std::runtime_error("LLVM codegen: indexing is supported only for arrays and slices in milestone 9");
}

void FunctionEmitter::printOne(const mir::Operand& operand) {
    Value printed = value(operand);
    using sema::TypeKind;
    switch (operand.type.kind) {
        case TypeKind::Bool: {
            const std::string selected = next("boolstr");
            out_ << "  " << selected << " = select i1 " << printed.repr << ", ptr @.nus.true, ptr @.nus.false\n";
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.cstr, ptr " << selected << ")\n";
            break;
        }
        case TypeKind::I8: case TypeKind::I16: {
            const std::string promoted = next("sext");
            out_ << "  " << promoted << " = sext " << printed.type << ' ' << printed.repr << " to i32\n";
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.i32, i32 " << promoted << ")\n";
            break;
        }
        case TypeKind::U8: case TypeKind::U16: {
            const std::string promoted = next("zext");
            out_ << "  " << promoted << " = zext " << printed.type << ' ' << printed.repr << " to i32\n";
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.u32, i32 " << promoted << ")\n";
            break;
        }
        case TypeKind::I32:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.i32, i32 " << printed.repr << ")\n";
            break;
        case TypeKind::U32:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.u32, i32 " << printed.repr << ")\n";
            break;
        case TypeKind::NetworkError: {
            const std::string code = next("netcode");
            out_ << "  " << code << " = extractvalue %nus.neterr " << printed.repr << ", 0\n";
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.i32, i32 " << code << ")\n";
            break;
        }
        case TypeKind::I64:
        case TypeKind::ISize:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.i64, " << printed.type << ' ' << printed.repr << ")\n";
            break;
        case TypeKind::U64:
        case TypeKind::USize:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.u64, " << printed.type << ' ' << printed.repr << ")\n";
            break;
        case TypeKind::F32: {
            const std::string promoted = next("fpext");
            out_ << "  " << promoted << " = fpext float " << printed.repr << " to double\n";
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.f64, double " << promoted << ")\n";
            break;
        }
        case TypeKind::F64:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.f64, double " << printed.repr << ")\n";
            break;
        case TypeKind::Char:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.char, i32 " << printed.repr << ")\n";
            break;
        case TypeKind::String: {
            const std::string data = next("strdata");
            const std::string length = next("strlen");
            const std::string length32 = next("strlen32");
            out_ << "  " << data << " = extractvalue %nus.string " << printed.repr << ", 0\n";
            out_ << "  " << length << " = extractvalue %nus.string " << printed.repr << ", 1\n";
            if (module_.usizeType() == "i64") {
                out_ << "  " << length32 << " = trunc i64 " << length << " to i32\n";
                out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.str, i32 " << length32 << ", ptr " << data << ")\n";
            } else {
                out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.str, i32 " << length << ", ptr " << data << ")\n";
            }
            break;
        }
        case TypeKind::Reference:
            out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.fmt.ptr, ptr " << printed.repr << ")\n";
            break;
        default:
            throw std::runtime_error("LLVM codegen: `print` does not yet support type `" + operand.type.name() + "`");
    }
}

void FunctionEmitter::builtinPrint(const std::vector<mir::Operand>& arguments) {
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i != 0) out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.space)\n";
        printOne(arguments[i]);
    }
    out_ << "  call i32 (ptr, ...) @printf(ptr @.nus.newline)\n";
}

void FunctionEmitter::builtinStringClone(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: string_clone expects one argument");
    const Value reference = value(arguments.front());
    const std::string source = next("strsrc");
    const std::string data = next("strdata");
    const std::string length = next("strlen");
    const std::string bytes = next("strbytes");
    const std::string allocated = next("stralloc");
    const std::string terminator = next("strterm");
    out_ << "  " << source << " = load %nus.string, ptr " << reference.repr << "\n";
    out_ << "  " << data << " = extractvalue %nus.string " << source << ", 0\n";
    out_ << "  " << length << " = extractvalue %nus.string " << source << ", 1\n";
    out_ << "  " << bytes << " = add " << module_.usizeType() << ' ' << length << ", 1\n";
    out_ << "  " << allocated << " = call ptr @malloc(" << module_.usizeType() << ' ' << bytes << ")\n";
    out_ << "  call ptr @memcpy(ptr " << allocated << ", ptr " << data << ", " << module_.usizeType() << ' ' << length << ")\n";
    out_ << "  " << terminator << " = getelementptr inbounds i8, ptr " << allocated << ", " << module_.usizeType() << ' ' << length << "\n";
    out_ << "  store i8 0, ptr " << terminator << "\n";
    const std::string one = next("string");
    const std::string two = next("string");
    const std::string three = next("string");
    out_ << "  " << one << " = insertvalue %nus.string poison, ptr " << allocated << ", 0\n";
    out_ << "  " << two << " = insertvalue %nus.string " << one << ", " << module_.usizeType() << ' ' << length << ", 1\n";
    out_ << "  " << three << " = insertvalue %nus.string " << two << ", " << module_.usizeType() << ' ' << bytes << ", 2\n";
    store(*inst.destination, {"%nus.string", three});
}

void FunctionEmitter::builtinStringLen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: string_len expects one argument");
    const Value reference = value(arguments.front());
    const std::string source = next("strsrc");
    const std::string length = next("strlen");
    out_ << "  " << source << " = load %nus.string, ptr " << reference.repr << "\n";
    out_ << "  " << length << " = extractvalue %nus.string " << source << ", 1\n";
    store(*inst.destination, {module_.usizeType(), length});
}

void FunctionEmitter::builtinVecNew(const mir::Instruction& inst) {
    if (!inst.destination) throw std::runtime_error("LLVM codegen: vec_new requires a destination");
    const std::string type = module_.llvmType(local(*inst.destination).type);
    const std::string one = next("vec");
    const std::string two = next("vec");
    const std::string three = next("vec");
    out_ << "  " << one << " = insertvalue " << type << " poison, ptr null, 0\n";
    out_ << "  " << two << " = insertvalue " << type << ' ' << one << ", " << module_.usizeType() << " 0, 1\n";
    out_ << "  " << three << " = insertvalue " << type << ' ' << two << ", " << module_.usizeType() << " 0, 2\n";
    store(*inst.destination, {type, three});
}

void FunctionEmitter::builtinVecPush(const std::vector<mir::Operand>& arguments) {
    if (arguments.size() != 2) throw std::runtime_error("LLVM codegen: vec_push expects two arguments");
    const Value reference = value(arguments[0]);
    if (arguments[0].type.kind != sema::TypeKind::Reference || !arguments[0].type.element ||
        arguments[0].type.element->kind != sema::TypeKind::Vec || !arguments[0].type.element->element) {
        throw std::runtime_error("LLVM codegen: vec_push requires &mut Vec<T>");
    }
    const sema::Type element_type = *arguments[0].type.element->element;
    const std::string element_llvm = module_.llvmType(element_type);
    const std::string type = module_.llvmType(*arguments[0].type.element);
    const std::string aggregate = next("vec");
    const std::string data = next("vecdata");
    const std::string length = next("veclen");
    const std::string new_length = next("veclen");
    const std::string size_ptr = next("vecsizeptr");
    const std::string element_size = next("vecsize");
    const std::string bytes = next("vecbytes");
    const std::string allocated = next("vecalloc");
    const std::string element = next("vecelem");
    out_ << "  " << aggregate << " = load " << type << ", ptr " << reference.repr << "\n";
    out_ << "  " << data << " = extractvalue " << type << ' ' << aggregate << ", 0\n";
    out_ << "  " << length << " = extractvalue " << type << ' ' << aggregate << ", 1\n";
    out_ << "  " << new_length << " = add " << module_.usizeType() << ' ' << length << ", 1\n";
    out_ << "  " << size_ptr << " = getelementptr " << element_llvm << ", ptr null, i32 1\n";
    out_ << "  " << element_size << " = ptrtoint ptr " << size_ptr << " to " << module_.usizeType() << "\n";
    out_ << "  " << bytes << " = mul " << module_.usizeType() << ' ' << new_length << ", " << element_size << "\n";
    out_ << "  " << allocated << " = call ptr @realloc(ptr " << data << ", " << module_.usizeType() << ' ' << bytes << ")\n";
    out_ << "  " << element << " = getelementptr inbounds " << element_llvm << ", ptr " << allocated << ", " << module_.usizeType() << ' ' << length << "\n";
    const Value inserted = coerce(value(arguments[1]), arguments[1].type, element_type);
    out_ << "  store " << element_llvm << ' ' << inserted.repr << ", ptr " << element << "\n";
    const std::string one = next("vec");
    const std::string two = next("vec");
    const std::string three = next("vec");
    out_ << "  " << one << " = insertvalue " << type << ' ' << aggregate << ", ptr " << allocated << ", 0\n";
    out_ << "  " << two << " = insertvalue " << type << ' ' << one << ", " << module_.usizeType() << ' ' << new_length << ", 1\n";
    out_ << "  " << three << " = insertvalue " << type << ' ' << two << ", " << module_.usizeType() << ' ' << new_length << ", 2\n";
    out_ << "  store " << type << ' ' << three << ", ptr " << reference.repr << "\n";
}

void FunctionEmitter::builtinVecLen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: vec_len expects one argument");
    const Value reference = value(arguments.front());
    if (arguments[0].type.kind != sema::TypeKind::Reference || !arguments[0].type.element || arguments[0].type.element->kind != sema::TypeKind::Vec) {
        throw std::runtime_error("LLVM codegen: vec_len requires &Vec<T>");
    }
    const std::string type = module_.llvmType(*arguments[0].type.element);
    const std::string aggregate = next("vec");
    const std::string length = next("veclen");
    out_ << "  " << aggregate << " = load " << type << ", ptr " << reference.repr << "\n";
    out_ << "  " << length << " = extractvalue " << type << ' ' << aggregate << ", 1\n";
    store(*inst.destination, {module_.usizeType(), length});
}

void FunctionEmitter::builtinVecGet(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: vec_get expects two arguments");
    const Value reference = value(arguments[0]);
    const Value index = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::USize));
    if (arguments[0].type.kind != sema::TypeKind::Reference || !arguments[0].type.element ||
        arguments[0].type.element->kind != sema::TypeKind::Vec || !arguments[0].type.element->element) {
        throw std::runtime_error("LLVM codegen: vec_get requires &Vec<T>");
    }
    const sema::Type element_type = *arguments[0].type.element->element;
    const std::string element_llvm = module_.llvmType(element_type);
    const std::string type = module_.llvmType(*arguments[0].type.element);
    const std::string aggregate = next("vec");
    const std::string data = next("vecdata");
    const std::string length = next("veclen");
    const std::string in_bounds = next("bounds");
    const std::string element = next("vecelem");
    const std::string loaded = next("vecvalue");
    out_ << "  " << aggregate << " = load " << type << ", ptr " << reference.repr << "\n";
    out_ << "  " << data << " = extractvalue " << type << ' ' << aggregate << ", 0\n";
    out_ << "  " << length << " = extractvalue " << type << ' ' << aggregate << ", 1\n";
    out_ << "  " << in_bounds << " = icmp ult " << module_.usizeType() << ' ' << index.repr << ", " << length << "\n";
    out_ << "  call void @nus_assert(i1 " << in_bounds << ")\n";
    out_ << "  " << element << " = getelementptr inbounds " << element_llvm << ", ptr " << data << ", " << module_.usizeType() << ' ' << index.repr << "\n";
    out_ << "  " << loaded << " = load " << element_llvm << ", ptr " << element << "\n";
    store(*inst.destination, {element_llvm, loaded});
}

void FunctionEmitter::builtinVecIntoIter(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: Vec::into_iter expects one receiver");
    if (arguments[0].type.kind != sema::TypeKind::Vec || !arguments[0].type.element) {
        throw std::runtime_error("LLVM codegen: Vec::into_iter requires Vec<T> by value");
    }
    const auto& destination = local(*inst.destination);
    if (destination.type.kind != sema::TypeKind::VecIter || !destination.type.element) {
        throw std::runtime_error("LLVM codegen: Vec::into_iter destination must be VecIter<T>");
    }
    const Value source = value(arguments[0]);
    const std::string target_type = module_.llvmType(destination.type);
    const std::string data = next("iterdata");
    const std::string length = next("iterlen");
    const std::string capacity = next("itercap");
    out_ << "  " << data << " = extractvalue " << source.type << ' ' << source.repr << ", 0\n";
    out_ << "  " << length << " = extractvalue " << source.type << ' ' << source.repr << ", 1\n";
    out_ << "  " << capacity << " = extractvalue " << source.type << ' ' << source.repr << ", 2\n";
    const std::string one = next("iter");
    const std::string two = next("iter");
    const std::string three = next("iter");
    const std::string four = next("iter");
    out_ << "  " << one << " = insertvalue " << target_type << " poison, ptr " << data << ", 0\n";
    out_ << "  " << two << " = insertvalue " << target_type << ' ' << one << ", " << module_.usizeType() << ' ' << length << ", 1\n";
    out_ << "  " << three << " = insertvalue " << target_type << ' ' << two << ", " << module_.usizeType() << ' ' << capacity << ", 2\n";
    out_ << "  " << four << " = insertvalue " << target_type << ' ' << three << ", " << module_.usizeType() << " 0, 3\n";
    store(*inst.destination, {target_type, four});
    markMoved(arguments[0]);
}

void FunctionEmitter::builtinVecBorrowIter(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments, bool is_mutable) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: Vec borrowed iterator expects one receiver");
    if (arguments[0].type.kind != sema::TypeKind::Reference || !arguments[0].type.element ||
        arguments[0].type.element->kind != sema::TypeKind::Vec || !arguments[0].type.element->element) {
        throw std::runtime_error("LLVM codegen: Vec borrowed iterator requires &Vec<T> receiver");
    }
    if (is_mutable && !arguments[0].type.mutable_reference) {
        throw std::runtime_error("LLVM codegen: Vec::iter_mut requires &mut Vec<T>");
    }
    const auto& destination = local(*inst.destination);
    const auto expected_kind = is_mutable ? sema::TypeKind::VecMutIter : sema::TypeKind::VecRefIter;
    if (destination.type.kind != expected_kind || !destination.type.element) {
        throw std::runtime_error("LLVM codegen: borrowed iterator destination has wrong type");
    }
    const Value reference = value(arguments[0]);
    const std::string vec_type = module_.llvmType(*arguments[0].type.element);
    const std::string aggregate = next("vec");
    const std::string data = next("iterdata");
    const std::string length = next("iterlen");
    out_ << "  " << aggregate << " = load " << vec_type << ", ptr " << reference.repr << "\n";
    out_ << "  " << data << " = extractvalue " << vec_type << ' ' << aggregate << ", 0\n";
    out_ << "  " << length << " = extractvalue " << vec_type << ' ' << aggregate << ", 1\n";
    const std::string target_type = module_.llvmType(destination.type);
    const std::string one = next("iter");
    const std::string two = next("iter");
    const std::string three = next("iter");
    out_ << "  " << one << " = insertvalue " << target_type << " poison, ptr " << data << ", 0\n";
    out_ << "  " << two << " = insertvalue " << target_type << ' ' << one << ", " << module_.usizeType() << ' ' << length << ", 1\n";
    out_ << "  " << three << " = insertvalue " << target_type << ' ' << two << ", " << module_.usizeType() << " 0, 2\n";
    store(*inst.destination, {target_type, three});
}

Value FunctionEmitter::emitNetworkResult(const sema::Type& result_type, bool is_error, const Value& payload) {
    if (result_type.kind != sema::TypeKind::Result || result_type.generic_arguments.size() != 2) {
        throw std::runtime_error("LLVM codegen: networking builtin destination must be Result<T, NetworkError>");
    }
    const std::string type = module_.llvmType(result_type);
    const std::string tagged = next("netresult");
    out_ << "  " << tagged << " = insertvalue " << type << " zeroinitializer, i1 " << (is_error ? "1" : "0") << ", 0\n";
    const std::string inserted = next("netresult");
    out_ << "  " << inserted << " = insertvalue " << type << ' ' << tagged << ", " << payload.type << ' ' << payload.repr
         << ", " << (is_error ? 2 : 1) << "\n";
    return {type, inserted};
}

Value FunctionEmitter::emitNetworkError(int code, std::optional<int> fixed_reason) {
    const std::string first = next("neterr");
    out_ << "  " << first << " = insertvalue %nus.neterr zeroinitializer, i32 " << code << ", 0\n";
    std::string reason;
    if (fixed_reason) {
        reason = std::to_string(*fixed_reason);
    } else {
        const std::string native = next("nativeerr");
        reason = next("netreason");
        out_ << "  " << native << " = call i32 @nus_net_last_error()\n";
        out_ << "  " << reason << " = call i32 @nus_net_reason(i32 " << native << ")\n";
    }
    const std::string second = next("neterr");
    out_ << "  " << second << " = insertvalue %nus.neterr " << first << ", i32 " << reason << ", 1\n";
    return {"%nus.neterr", second};
}

std::string FunctionEmitter::emitSockaddrIn(const Value& address) {
    if (address.type != "%nus.socketaddr") throw std::runtime_error("LLVM codegen: expected SocketAddr");
    const std::string ip = next("sockip");
    const std::string ip_host = next("sockiphost");
    const std::string ip_net = next("sockipnet");
    const std::string port_host = next("sockport");
    const std::string port_net = next("sockportnet");
    const std::string storage = next("sockaddr");
    const std::string family_ptr = next("sockfamily");
    const std::string port_ptr = next("sockportptr");
    const std::string addr_ptr = next("sockaddrptr");
    out_ << "  " << ip << " = extractvalue %nus.socketaddr " << address.repr << ", 0\n";
    out_ << "  " << ip_host << " = extractvalue %nus.ipaddr " << ip << ", 0\n";
    out_ << "  " << port_host << " = extractvalue %nus.socketaddr " << address.repr << ", 1\n";
    out_ << "  " << ip_net << " = call i32 @htonl(i32 " << ip_host << ")\n";
    out_ << "  " << port_net << " = call i16 @htons(i16 " << port_host << ")\n";
    out_ << "  " << storage << " = alloca %nus.sockaddr.in\n";
    out_ << "  store %nus.sockaddr.in zeroinitializer, ptr " << storage << "\n";
    out_ << "  " << family_ptr << " = getelementptr inbounds %nus.sockaddr.in, ptr " << storage << ", i32 0, i32 0\n";
    out_ << "  store i16 2, ptr " << family_ptr << "\n";
    out_ << "  " << port_ptr << " = getelementptr inbounds %nus.sockaddr.in, ptr " << storage << ", i32 0, i32 1\n";
    out_ << "  store i16 " << port_net << ", ptr " << port_ptr << "\n";
    out_ << "  " << addr_ptr << " = getelementptr inbounds %nus.sockaddr.in, ptr " << storage << ", i32 0, i32 2\n";
    out_ << "  store i32 " << ip_net << ", ptr " << addr_ptr << "\n";
    return storage;
}

std::string FunctionEmitter::socketHandleForCall(const Value& stream) {
    const std::string handle64 = next("sockhandle");
    out_ << "  " << handle64 << " = extractvalue " << stream.type << ' ' << stream.repr << ", 0\n";
    if (module_.windowsTarget()) return handle64;
    const std::string handle32 = next("sockfd");
    out_ << "  " << handle32 << " = trunc i64 " << handle64 << " to i32\n";
    return handle32;
}

void FunctionEmitter::builtinIpv4(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 4) throw std::runtime_error("LLVM codegen: ipv4 expects four octets");
    std::vector<std::string> octets;
    for (const auto& argument : arguments) {
        const Value raw = coerce(value(argument), argument.type, sema::Type::simple(sema::TypeKind::U8));
        const std::string extended = next("octet");
        out_ << "  " << extended << " = zext i8 " << raw.repr << " to i32\n";
        octets.push_back(extended);
    }
    const std::string a = next("ip");
    const std::string b = next("ip");
    const std::string c = next("ip");
    const std::string ab = next("ip");
    const std::string abc = next("ip");
    const std::string packed = next("ip");
    out_ << "  " << a << " = shl i32 " << octets[0] << ", 24\n";
    out_ << "  " << b << " = shl i32 " << octets[1] << ", 16\n";
    out_ << "  " << c << " = shl i32 " << octets[2] << ", 8\n";
    out_ << "  " << ab << " = or i32 " << a << ", " << b << "\n";
    out_ << "  " << abc << " = or i32 " << ab << ", " << c << "\n";
    out_ << "  " << packed << " = or i32 " << abc << ", " << octets[3] << "\n";
    const std::string aggregate = next("ipaddr");
    out_ << "  " << aggregate << " = insertvalue %nus.ipaddr poison, i32 " << packed << ", 0\n";
    store(*inst.destination, {"%nus.ipaddr", aggregate});
}

void FunctionEmitter::builtinSocketAddr(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: socket_addr expects IpAddr and port");
    const Value ip = coerce(value(arguments[0]), arguments[0].type, sema::Type::simple(sema::TypeKind::IpAddr));
    const Value port = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::U16));
    const std::string one = next("socketaddr");
    const std::string two = next("socketaddr");
    out_ << "  " << one << " = insertvalue %nus.socketaddr poison, %nus.ipaddr " << ip.repr << ", 0\n";
    out_ << "  " << two << " = insertvalue %nus.socketaddr " << one << ", i16 " << port.repr << ", 1\n";
    store(*inst.destination, {"%nus.socketaddr", two});
}

void FunctionEmitter::builtinSocketPort(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: socket_port expects &SocketAddr");
    const Value ref = value(arguments[0]);
    const std::string address = next("socketaddr");
    const std::string port = next("socketport");
    out_ << "  " << address << " = load %nus.socketaddr, ptr " << ref.repr << "\n";
    out_ << "  " << port << " = extractvalue %nus.socketaddr " << address << ", 1\n";
    store(*inst.destination, {"i16", port});
}

void FunctionEmitter::builtinNetworkErrorCode(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: network_error_code expects &NetworkError");
    const Value ref = value(arguments[0]);
    const std::string error = next("neterr");
    const std::string code = next("netcode");
    out_ << "  " << error << " = load %nus.neterr, ptr " << ref.repr << "\n";
    out_ << "  " << code << " = extractvalue %nus.neterr " << error << ", 0\n";
    store(*inst.destination, {"i32", code});
}

void FunctionEmitter::builtinNetworkErrorReason(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: network_error_reason expects &NetworkError");
    const Value ref = value(arguments[0]);
    const std::string error = next("neterr");
    const std::string reason = next("netreason");
    out_ << "  " << error << " = load %nus.neterr, ptr " << ref.repr << "\n";
    out_ << "  " << reason << " = extractvalue %nus.neterr " << error << ", 1\n";
    store(*inst.destination, {"i32", reason});
}

void FunctionEmitter::builtinDnsResolveIpv4(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: dns_resolve_ipv4 expects &string");
    const auto& result_type = local(*inst.destination).type;
    const Value host_ref = value(arguments[0]);
    const std::string host_string = next("hostname");
    const std::string host_data = next("hostdata");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << host_string << " = load %nus.string, ptr " << host_ref.repr << "\n";
    out_ << "  " << host_data << " = extractvalue %nus.string " << host_string << ", 0\n";
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    const std::string resolve_label = nextLabel("dns.resolve.");
    const std::string fail_label = nextLabel("dns.fail.");
    const std::string ok_label = nextLabel("dns.ok.");
    const std::string done = nextLabel("dns.done.");
    if (module_.windowsTarget()) {
        const std::string init = next("wsainit");
        const std::string init_ok = next("wsaok");
        const std::string init_fail = nextLabel("dns.init.fail.");
        out_ << "  " << init << " = call i32 @nus_net_init()\n";
        out_ << "  " << init_ok << " = icmp eq i32 " << init << ", 0\n";
        out_ << "  br i1 " << init_ok << ", label %" << resolve_label << ", label %" << init_fail << "\n";
        out_ << init_fail << ":\n";
        const Value error = emitNetworkError(1, 0);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    } else {
        out_ << "  br label %" << resolve_label << "\n";
    }
    out_ << resolve_label << ":\n";
    const std::string entry = next("hostent");
    const std::string found = next("hostfound");
    out_ << "  " << entry << " = call ptr @gethostbyname(ptr " << host_data << ")\n";
    out_ << "  " << found << " = icmp ne ptr " << entry << ", null\n";
    out_ << "  br i1 " << found << ", label %" << ok_label << ", label %" << fail_label << "\n";
    out_ << fail_label << ":\n";
    {
        const Value error = emitNetworkError(9, 4);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << ok_label << ":\n";
    const std::string list_ptr = next("addrlistptr");
    const std::string list = next("addrlist");
    const std::string first = next("firstaddr");
    const std::string net_ip = next("netip");
    const std::string host_ip = next("hostip");
    const std::string ip = next("ipaddr");
    out_ << "  " << list_ptr << " = getelementptr inbounds %nus.hostent, ptr " << entry << ", i32 0, i32 4\n";
    out_ << "  " << list << " = load ptr, ptr " << list_ptr << "\n";
    out_ << "  " << first << " = load ptr, ptr " << list << "\n";
    out_ << "  " << net_ip << " = load i32, ptr " << first << "\n";
    out_ << "  " << host_ip << " = call i32 @ntohl(i32 " << net_ip << ")\n";
    out_ << "  " << ip << " = insertvalue %nus.ipaddr poison, i32 " << host_ip << ", 0\n";
    {
        const Value result = emitNetworkResult(result_type, false, {"%nus.ipaddr", ip});
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpListen(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 3) throw std::runtime_error("LLVM codegen: tcp_listen expects address, backlog and reuse_addr");
    const auto& result_type = local(*inst.destination).type;
    const Value address_ref = value(arguments[0]);
    const Value backlog = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::I32));
    const Value reuse = coerce(value(arguments[2]), arguments[2].type, sema::Type::simple(sema::TypeKind::Bool));
    const std::string address = next("socketaddr");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << address << " = load %nus.socketaddr, ptr " << address_ref.repr << "\n";
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";

    const std::string start = nextLabel("listen.start.");
    const std::string socket_fail = nextLabel("listen.socket.fail.");
    const std::string have_socket = nextLabel("listen.have.socket.");
    const std::string option_fail = nextLabel("listen.option.fail.");
    const std::string bind_stage = nextLabel("listen.bind.");
    const std::string bind_fail = nextLabel("listen.bind.fail.");
    const std::string listen_stage = nextLabel("listen.listen.");
    const std::string listen_fail = nextLabel("listen.listen.fail.");
    const std::string success = nextLabel("listen.success.");
    const std::string done = nextLabel("listen.done.");

    if (module_.windowsTarget()) {
        const std::string init = next("wsainit");
        const std::string ok = next("wsaok");
        const std::string fail = nextLabel("listen.init.fail.");
        out_ << "  " << init << " = call i32 @nus_net_init()\n";
        out_ << "  " << ok << " = icmp eq i32 " << init << ", 0\n";
        out_ << "  br i1 " << ok << ", label %" << start << ", label %" << fail << "\n";
        out_ << fail << ":\n";
        const Value error = emitNetworkError(1, 0);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    } else out_ << "  br label %" << start << "\n";

    out_ << start << ":\n";
    const std::string raw = next("socket");
    std::string handle64;
    if (module_.windowsTarget()) { out_ << "  " << raw << " = call i64 @socket(i32 2, i32 1, i32 6)\n"; handle64 = raw; }
    else { out_ << "  " << raw << " = call i32 @socket(i32 2, i32 1, i32 6)\n"; handle64 = next("socket64"); out_ << "  " << handle64 << " = sext i32 " << raw << " to i64\n"; }
    const std::string socket_ok = next("socketok");
    out_ << "  " << socket_ok << " = icmp ne " << (module_.windowsTarget() ? "i64" : "i32") << ' ' << raw << ", -1\n";
    out_ << "  br i1 " << socket_ok << ", label %" << have_socket << ", label %" << socket_fail << "\n";
    out_ << socket_fail << ":\n";
    {
        const Value error = emitNetworkError(2);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << have_socket << ":\n";
    const std::string reuse32 = next("reuse");
    const std::string reuse_slot = next("reuseptr");
    const int sol_socket = (module_.windowsTarget() || module_.darwinTarget()) ? 0xffff : 1;
    const int so_reuseaddr = (module_.windowsTarget() || module_.darwinTarget()) ? 4 : 2;
    out_ << "  " << reuse32 << " = zext i1 " << reuse.repr << " to i32\n";
    out_ << "  " << reuse_slot << " = alloca i32\n";
    out_ << "  store i32 " << reuse32 << ", ptr " << reuse_slot << "\n";
    const std::string opt_rc = next("setsockopt");
    if (module_.windowsTarget()) out_ << "  " << opt_rc << " = call i32 @setsockopt(i64 " << raw << ", i32 " << sol_socket << ", i32 " << so_reuseaddr << ", ptr " << reuse_slot << ", i32 4)\n";
    else out_ << "  " << opt_rc << " = call i32 @setsockopt(i32 " << raw << ", i32 " << sol_socket << ", i32 " << so_reuseaddr << ", ptr " << reuse_slot << ", i32 4)\n";
    const std::string opt_ok = next("optionok");
    out_ << "  " << opt_ok << " = icmp eq i32 " << opt_rc << ", 0\n";
    out_ << "  br i1 " << opt_ok << ", label %" << bind_stage << ", label %" << option_fail << "\n";
    out_ << option_fail << ":\n";
    {
        const Value error = emitNetworkError(10);
        if (module_.windowsTarget()) out_ << "  call i32 @closesocket(i64 " << raw << ")\n"; else out_ << "  call i32 @close(i32 " << raw << ")\n";
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << bind_stage << ":\n";
    const std::string sockaddr = emitSockaddrIn({"%nus.socketaddr", address});
    const std::string bind_rc = next("bind");
    if (module_.windowsTarget()) out_ << "  " << bind_rc << " = call i32 @bind(i64 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    else out_ << "  " << bind_rc << " = call i32 @bind(i32 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    const std::string bound = next("bound");
    out_ << "  " << bound << " = icmp eq i32 " << bind_rc << ", 0\n";
    out_ << "  br i1 " << bound << ", label %" << listen_stage << ", label %" << bind_fail << "\n";
    out_ << bind_fail << ":\n";
    {
        const Value error = emitNetworkError(4);
        if (module_.windowsTarget()) out_ << "  call i32 @closesocket(i64 " << raw << ")\n"; else out_ << "  call i32 @close(i32 " << raw << ")\n";
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << listen_stage << ":\n";
    const std::string listen_rc = next("listen");
    if (module_.windowsTarget()) out_ << "  " << listen_rc << " = call i32 @listen(i64 " << raw << ", i32 " << backlog.repr << ")\n";
    else out_ << "  " << listen_rc << " = call i32 @listen(i32 " << raw << ", i32 " << backlog.repr << ")\n";
    const std::string listening = next("listening");
    out_ << "  " << listening << " = icmp eq i32 " << listen_rc << ", 0\n";
    out_ << "  br i1 " << listening << ", label %" << success << ", label %" << listen_fail << "\n";
    out_ << listen_fail << ":\n";
    {
        const Value error = emitNetworkError(7);
        if (module_.windowsTarget()) out_ << "  call i32 @closesocket(i64 " << raw << ")\n"; else out_ << "  call i32 @close(i32 " << raw << ")\n";
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << success << ":\n";
    const std::string listener = next("tcplistener");
    out_ << "  " << listener << " = insertvalue %nus.tcplistener poison, i64 " << handle64 << ", 0\n";
    {
        const Value result = emitNetworkResult(result_type, false, {"%nus.tcplistener", listener});
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpAccept(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: tcp_accept expects &mut TcpListener");
    const auto& result_type = local(*inst.destination).type;
    const Value listener_ref = value(arguments[0]);
    const std::string listener = next("tcplistener");
    out_ << "  " << listener << " = load %nus.tcplistener, ptr " << listener_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.tcplistener", listener});
    const std::string raw = next("accepted");
    if (module_.windowsTarget()) out_ << "  " << raw << " = call i64 @accept(i64 " << handle << ", ptr null, ptr null)\n";
    else out_ << "  " << raw << " = call i32 @accept(i32 " << handle << ", ptr null, ptr null)\n";
    const std::string ok = next("acceptok");
    out_ << "  " << ok << " = icmp ne " << (module_.windowsTarget() ? "i64" : "i32") << ' ' << raw << ", -1\n";
    const std::string ok_label = nextLabel("accept.ok.");
    const std::string err_label = nextLabel("accept.err.");
    const std::string done = nextLabel("accept.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    out_ << "  br i1 " << ok << ", label %" << ok_label << ", label %" << err_label << "\n";
    out_ << err_label << ":\n";
    {
        const Value error = emitNetworkError(8);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << ok_label << ":\n";
    std::string handle64 = raw;
    if (!module_.windowsTarget()) { handle64 = next("accepted64"); out_ << "  " << handle64 << " = sext i32 " << raw << " to i64\n"; }
    const std::string stream = next("tcpstream");
    out_ << "  " << stream << " = insertvalue %nus.tcpstream poison, i64 " << handle64 << ", 0\n";
    {
        const Value result = emitNetworkResult(result_type, false, {"%nus.tcpstream", stream});
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    }
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpSetNoDelay(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: tcp_set_nodelay expects stream and bool");
    const auto& result_type = local(*inst.destination).type;
    const Value stream_ref = value(arguments[0]);
    const Value enabled = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::Bool));
    const std::string stream = next("tcpstream");
    out_ << "  " << stream << " = load %nus.tcpstream, ptr " << stream_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.tcpstream", stream});
    const std::string enabled32 = next("nodelay");
    const std::string option = next("option");
    out_ << "  " << enabled32 << " = zext i1 " << enabled.repr << " to i32\n";
    out_ << "  " << option << " = alloca i32\n  store i32 " << enabled32 << ", ptr " << option << "\n";
    const std::string rc = next("setsockopt");
    if (module_.windowsTarget()) out_ << "  " << rc << " = call i32 @setsockopt(i64 " << handle << ", i32 6, i32 1, ptr " << option << ", i32 4)\n";
    else out_ << "  " << rc << " = call i32 @setsockopt(i32 " << handle << ", i32 6, i32 1, ptr " << option << ", i32 4)\n";
    const std::string ok = next("optionok");
    out_ << "  " << ok << " = icmp eq i32 " << rc << ", 0\n";
    const std::string ok_label = nextLabel("option.ok.");
    const std::string err_label = nextLabel("option.err.");
    const std::string done = nextLabel("option.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n  br i1 " << ok << ", label %" << ok_label << ", label %" << err_label << "\n";
    out_ << err_label << ":\n";
    Value result = emitNetworkResult(result_type, true, emitNetworkError(10));
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    result = emitNetworkResult(result_type, false, {"i32", "0"});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpSetTimeout(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: tcp_set_timeout expects stream and milliseconds");
    const auto& result_type = local(*inst.destination).type;
    const Value stream_ref = value(arguments[0]);
    const Value millis = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::U32));
    const std::string stream = next("tcpstream");
    out_ << "  " << stream << " = load %nus.tcpstream, ptr " << stream_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.tcpstream", stream});
    std::string option_ptr;
    int sol_socket = (module_.windowsTarget() || module_.darwinTarget()) ? 0xffff : 1;
    int recv_timeout = (module_.windowsTarget() || module_.darwinTarget()) ? 0x1006 : 20;
    int send_timeout = (module_.windowsTarget() || module_.darwinTarget()) ? 0x1005 : 21;
    int option_size = 0;
    if (module_.windowsTarget()) {
        option_ptr = next("timeoutptr");
        out_ << "  " << option_ptr << " = alloca i32\n  store i32 " << millis.repr << ", ptr " << option_ptr << "\n";
        option_size = 4;
    } else {
        const std::string ms64 = next("ms64");
        const std::string sec = next("seconds");
        const std::string rem = next("millirem");
        const std::string usec = next("microseconds");
        const std::string sec_ptr = next("secptr");
        const std::string usec_ptr = next("usecptr");
        option_ptr = next("timeoutptr");
        out_ << "  " << ms64 << " = zext i32 " << millis.repr << " to i64\n";
        out_ << "  " << sec << " = udiv i64 " << ms64 << ", 1000\n";
        out_ << "  " << rem << " = urem i64 " << ms64 << ", 1000\n";
        out_ << "  " << usec << " = mul i64 " << rem << ", 1000\n";
        out_ << "  " << option_ptr << " = alloca %nus.timeval\n";
        out_ << "  " << sec_ptr << " = getelementptr inbounds %nus.timeval, ptr " << option_ptr << ", i32 0, i32 0\n";
        out_ << "  " << usec_ptr << " = getelementptr inbounds %nus.timeval, ptr " << option_ptr << ", i32 0, i32 1\n";
        out_ << "  store i64 " << sec << ", ptr " << sec_ptr << "\n  store i64 " << usec << ", ptr " << usec_ptr << "\n";
        option_size = 16;
    }
    const std::string recv_rc = next("setrecvtimeout");
    if (module_.windowsTarget()) out_ << "  " << recv_rc << " = call i32 @setsockopt(i64 " << handle << ", i32 " << sol_socket << ", i32 " << recv_timeout << ", ptr " << option_ptr << ", i32 " << option_size << ")\n";
    else out_ << "  " << recv_rc << " = call i32 @setsockopt(i32 " << handle << ", i32 " << sol_socket << ", i32 " << recv_timeout << ", ptr " << option_ptr << ", i32 " << option_size << ")\n";
    const std::string recv_ok = next("timeoutok");
    out_ << "  " << recv_ok << " = icmp eq i32 " << recv_rc << ", 0\n";
    const std::string send_stage = nextLabel("timeout.send.");
    const std::string err_label = nextLabel("timeout.err.");
    const std::string ok_label = nextLabel("timeout.ok.");
    const std::string done = nextLabel("timeout.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n  br i1 " << recv_ok << ", label %" << send_stage << ", label %" << err_label << "\n";
    out_ << send_stage << ":\n";
    const std::string send_rc = next("setsendtimeout");
    if (module_.windowsTarget()) out_ << "  " << send_rc << " = call i32 @setsockopt(i64 " << handle << ", i32 " << sol_socket << ", i32 " << send_timeout << ", ptr " << option_ptr << ", i32 " << option_size << ")\n";
    else out_ << "  " << send_rc << " = call i32 @setsockopt(i32 " << handle << ", i32 " << sol_socket << ", i32 " << send_timeout << ", ptr " << option_ptr << ", i32 " << option_size << ")\n";
    const std::string send_ok = next("timeoutok");
    out_ << "  " << send_ok << " = icmp eq i32 " << send_rc << ", 0\n  br i1 " << send_ok << ", label %" << ok_label << ", label %" << err_label << "\n";
    out_ << err_label << ":\n";
    Value result = emitNetworkResult(result_type, true, emitNetworkError(10));
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    result = emitNetworkResult(result_type, false, {"i32", "0"});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}


void FunctionEmitter::builtinTcpConnect(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: tcp_connect expects &SocketAddr");
    const auto& result_type = local(*inst.destination).type;
    const Value address_ref = value(arguments[0]);
    const std::string address = next("socketaddr");
    out_ << "  " << address << " = load %nus.socketaddr, ptr " << address_ref.repr << "\n";
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";

    const std::string start_label = nextLabel("tcp.start.");
    const std::string socket_fail = nextLabel("tcp.socket.fail.");
    const std::string connect_fail = nextLabel("tcp.connect.fail.");
    const std::string success = nextLabel("tcp.success.");
    const std::string done = nextLabel("tcp.done.");
    if (module_.windowsTarget()) {
        const std::string init = next("wsainit");
        const std::string init_ok = next("wsaok");
        const std::string init_fail = nextLabel("tcp.init.fail.");
        out_ << "  " << init << " = call i32 @nus_net_init()\n";
        out_ << "  " << init_ok << " = icmp eq i32 " << init << ", 0\n";
        out_ << "  br i1 " << init_ok << ", label %" << start_label << ", label %" << init_fail << "\n";
        out_ << init_fail << ":\n";
        const Value error = emitNetworkError(1, 0);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    } else {
        out_ << "  br label %" << start_label << "\n";
    }
    out_ << start_label << ":\n";
    const std::string raw = next("socket");
    std::string handle64;
    if (module_.windowsTarget()) {
        out_ << "  " << raw << " = call i64 @socket(i32 2, i32 1, i32 6)\n";
        handle64 = raw;
    } else {
        out_ << "  " << raw << " = call i32 @socket(i32 2, i32 1, i32 6)\n";
        handle64 = next("socket64");
        out_ << "  " << handle64 << " = sext i32 " << raw << " to i64\n";
    }
    const std::string socket_ok = next("socketok");
    out_ << "  " << socket_ok << " = icmp ne " << (module_.windowsTarget() ? "i64" : "i32") << ' ' << raw << ", -1\n";
    const std::string have_socket = nextLabel("tcp.have.socket.");
    out_ << "  br i1 " << socket_ok << ", label %" << have_socket << ", label %" << socket_fail << "\n";
    out_ << socket_fail << ":\n";
    {
        const Value error = emitNetworkError(2);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << have_socket << ":\n";
    const std::string sockaddr = emitSockaddrIn({"%nus.socketaddr", address});
    const std::string rc = next("connect");
    if (module_.windowsTarget()) out_ << "  " << rc << " = call i32 @connect(i64 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    else out_ << "  " << rc << " = call i32 @connect(i32 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    const std::string connected = next("connected");
    out_ << "  " << connected << " = icmp eq i32 " << rc << ", 0\n";
    out_ << "  br i1 " << connected << ", label %" << success << ", label %" << connect_fail << "\n";
    out_ << connect_fail << ":\n";
    {
        const Value error = emitNetworkError(3);
        if (module_.windowsTarget()) out_ << "  call i32 @closesocket(i64 " << raw << ")\n";
        else out_ << "  call i32 @close(i32 " << raw << ")\n";
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << success << ":\n";
    const std::string stream = next("tcpstream");
    out_ << "  " << stream << " = insertvalue %nus.tcpstream poison, i64 " << handle64 << ", 0\n";
    {
        const Value result = emitNetworkResult(result_type, false, {"%nus.tcpstream", stream});
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinUdpBind(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 1) throw std::runtime_error("LLVM codegen: udp_bind expects &SocketAddr");
    const auto& result_type = local(*inst.destination).type;
    const Value address_ref = value(arguments[0]);
    const std::string address = next("socketaddr");
    out_ << "  " << address << " = load %nus.socketaddr, ptr " << address_ref.repr << "\n";
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    const std::string start_label = nextLabel("udp.start.");
    const std::string socket_fail = nextLabel("udp.socket.fail.");
    const std::string bind_fail = nextLabel("udp.bind.fail.");
    const std::string success = nextLabel("udp.success.");
    const std::string done = nextLabel("udp.done.");
    if (module_.windowsTarget()) {
        const std::string init = next("wsainit");
        const std::string init_ok = next("wsaok");
        const std::string init_fail = nextLabel("udp.init.fail.");
        out_ << "  " << init << " = call i32 @nus_net_init()\n";
        out_ << "  " << init_ok << " = icmp eq i32 " << init << ", 0\n";
        out_ << "  br i1 " << init_ok << ", label %" << start_label << ", label %" << init_fail << "\n";
        out_ << init_fail << ":\n";
        const Value error = emitNetworkError(1, 0);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    } else out_ << "  br label %" << start_label << "\n";
    out_ << start_label << ":\n";
    const std::string raw = next("socket");
    std::string handle64;
    if (module_.windowsTarget()) { out_ << "  " << raw << " = call i64 @socket(i32 2, i32 2, i32 17)\n"; handle64 = raw; }
    else { out_ << "  " << raw << " = call i32 @socket(i32 2, i32 2, i32 17)\n"; handle64 = next("socket64"); out_ << "  " << handle64 << " = sext i32 " << raw << " to i64\n"; }
    const std::string socket_ok = next("socketok");
    out_ << "  " << socket_ok << " = icmp ne " << (module_.windowsTarget() ? "i64" : "i32") << ' ' << raw << ", -1\n";
    const std::string have_socket = nextLabel("udp.have.socket.");
    out_ << "  br i1 " << socket_ok << ", label %" << have_socket << ", label %" << socket_fail << "\n";
    out_ << socket_fail << ":\n";
    {
        const Value error = emitNetworkError(2);
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << have_socket << ":\n";
    const std::string sockaddr = emitSockaddrIn({"%nus.socketaddr", address});
    const std::string rc = next("bind");
    if (module_.windowsTarget()) out_ << "  " << rc << " = call i32 @bind(i64 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    else out_ << "  " << rc << " = call i32 @bind(i32 " << raw << ", ptr " << sockaddr << ", i32 16)\n";
    const std::string bound = next("bound");
    out_ << "  " << bound << " = icmp eq i32 " << rc << ", 0\n";
    out_ << "  br i1 " << bound << ", label %" << success << ", label %" << bind_fail << "\n";
    out_ << bind_fail << ":\n";
    {
        const Value error = emitNetworkError(4);
        if (module_.windowsTarget()) out_ << "  call i32 @closesocket(i64 " << raw << ")\n";
        else out_ << "  call i32 @close(i32 " << raw << ")\n";
        const Value result = emitNetworkResult(result_type, true, error);
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << success << ":\n";
    const std::string socket = next("udpsocket");
    out_ << "  " << socket << " = insertvalue %nus.udpsocket poison, i64 " << handle64 << ", 0\n";
    {
        const Value result = emitNetworkResult(result_type, false, {"%nus.udpsocket", socket});
        out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n";
        out_ << "  br label %" << done << "\n";
    }
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpSend(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: tcp_send expects stream and buffer");
    const auto& result_type = local(*inst.destination).type;
    const Value stream_ref = value(arguments[0]);
    const Value buffer_ref = value(arguments[1]);
    const std::string stream = next("tcpstream");
    const std::string vec = next("buffer");
    out_ << "  " << stream << " = load %nus.tcpstream, ptr " << stream_ref.repr << "\n";
    out_ << "  " << vec << " = load { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " }, ptr " << buffer_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.tcpstream", stream});
    const std::string data = next("bufdata");
    const std::string length = next("buflen");
    out_ << "  " << data << " = extractvalue { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " } " << vec << ", 0\n";
    out_ << "  " << length << " = extractvalue { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " } " << vec << ", 1\n";
    const std::string rc = next("send");
    if (module_.windowsTarget()) {
        const std::string len32 = next("sendlen");
        out_ << "  " << len32 << " = trunc " << module_.usizeType() << ' ' << length << " to i32\n";
        out_ << "  " << rc << " = call i32 @send(i64 " << handle << ", ptr " << data << ", i32 " << len32 << ", i32 0)\n";
    } else out_ << "  " << rc << " = call i64 @send(i32 " << handle << ", ptr " << data << ", i64 " << length << ", i32 0)\n";
    const std::string failed = next("sendfail");
    out_ << "  " << failed << " = icmp slt " << (module_.windowsTarget() ? "i32" : "i64") << ' ' << rc << ", 0\n";
    const std::string err_label = nextLabel("send.err.");
    const std::string ok_label = nextLabel("send.ok.");
    const std::string done = nextLabel("send.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    out_ << "  br i1 " << failed << ", label %" << err_label << ", label %" << ok_label << "\n";
    out_ << err_label << ":\n";
    const Value error = emitNetworkError(5);
    Value result = emitNetworkResult(result_type, true, error);
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    std::string count = rc;
    if (module_.windowsTarget()) { count = next("sent"); out_ << "  " << count << " = zext i32 " << rc << " to " << module_.usizeType() << "\n"; }
    result = emitNetworkResult(result_type, false, {module_.usizeType(), count});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinUdpSendTo(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 3) throw std::runtime_error("LLVM codegen: udp_send_to expects socket, address, buffer");
    const auto& result_type = local(*inst.destination).type;
    const Value socket_ref = value(arguments[0]);
    const Value address_ref = value(arguments[1]);
    const Value buffer_ref = value(arguments[2]);
    const std::string socket = next("udpsocket");
    const std::string address = next("socketaddr");
    const std::string vec = next("buffer");
    out_ << "  " << socket << " = load %nus.udpsocket, ptr " << socket_ref.repr << "\n";
    out_ << "  " << address << " = load %nus.socketaddr, ptr " << address_ref.repr << "\n";
    out_ << "  " << vec << " = load { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " }, ptr " << buffer_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.udpsocket", socket});
    const std::string sockaddr = emitSockaddrIn({"%nus.socketaddr", address});
    const std::string data = next("bufdata");
    const std::string length = next("buflen");
    out_ << "  " << data << " = extractvalue { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " } " << vec << ", 0\n";
    out_ << "  " << length << " = extractvalue { ptr, " << module_.usizeType() << ", " << module_.usizeType() << " } " << vec << ", 1\n";
    const std::string rc = next("sendto");
    if (module_.windowsTarget()) {
        const std::string len32 = next("sendlen");
        out_ << "  " << len32 << " = trunc " << module_.usizeType() << ' ' << length << " to i32\n";
        out_ << "  " << rc << " = call i32 @sendto(i64 " << handle << ", ptr " << data << ", i32 " << len32 << ", i32 0, ptr " << sockaddr << ", i32 16)\n";
    } else out_ << "  " << rc << " = call i64 @sendto(i32 " << handle << ", ptr " << data << ", i64 " << length << ", i32 0, ptr " << sockaddr << ", i32 16)\n";
    const std::string failed = next("sendfail");
    out_ << "  " << failed << " = icmp slt " << (module_.windowsTarget() ? "i32" : "i64") << ' ' << rc << ", 0\n";
    const std::string err_label = nextLabel("sendto.err.");
    const std::string ok_label = nextLabel("sendto.ok.");
    const std::string done = nextLabel("sendto.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    out_ << "  br i1 " << failed << ", label %" << err_label << ", label %" << ok_label << "\n";
    out_ << err_label << ":\n";
    const Value error = emitNetworkError(5);
    Value result = emitNetworkResult(result_type, true, error);
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    std::string count = rc;
    if (module_.windowsTarget()) { count = next("sent"); out_ << "  " << count << " = zext i32 " << rc << " to " << module_.usizeType() << "\n"; }
    result = emitNetworkResult(result_type, false, {module_.usizeType(), count});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinTcpReceive(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: tcp_receive expects stream and maximum length");
    const auto& result_type = local(*inst.destination).type;
    const Value stream_ref = value(arguments[0]);
    const Value requested = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::USize));
    const std::string stream = next("tcpstream");
    out_ << "  " << stream << " = load %nus.tcpstream, ptr " << stream_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.tcpstream", stream});
    const std::string data = next("recvbuf");
    out_ << "  " << data << " = call ptr @malloc(" << module_.usizeType() << ' ' << requested.repr << ")\n";
    const std::string rc = next("recv");
    if (module_.windowsTarget()) {
        const std::string len32 = next("recvlen");
        out_ << "  " << len32 << " = trunc " << module_.usizeType() << ' ' << requested.repr << " to i32\n";
        out_ << "  " << rc << " = call i32 @recv(i64 " << handle << ", ptr " << data << ", i32 " << len32 << ", i32 0)\n";
    } else out_ << "  " << rc << " = call i64 @recv(i32 " << handle << ", ptr " << data << ", i64 " << requested.repr << ", i32 0)\n";
    const std::string failed = next("recvfail");
    out_ << "  " << failed << " = icmp slt " << (module_.windowsTarget() ? "i32" : "i64") << ' ' << rc << ", 0\n";
    const std::string err_label = nextLabel("recv.err.");
    const std::string ok_label = nextLabel("recv.ok.");
    const std::string done = nextLabel("recv.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    out_ << "  br i1 " << failed << ", label %" << err_label << ", label %" << ok_label << "\n";
    out_ << err_label << ":\n";
    const Value error = emitNetworkError(6);
    out_ << "  call void @free(ptr " << data << ")\n";
    Value result = emitNetworkResult(result_type, true, error);
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    std::string count = rc;
    if (module_.windowsTarget()) { count = next("received"); out_ << "  " << count << " = zext i32 " << rc << " to " << module_.usizeType() << "\n"; }
    const std::string v1 = next("buffer");
    const std::string v2 = next("buffer");
    const std::string v3 = next("buffer");
    const std::string vec_type = module_.llvmType(sema::Type::vector(sema::Type::simple(sema::TypeKind::U8)));
    out_ << "  " << v1 << " = insertvalue " << vec_type << " poison, ptr " << data << ", 0\n";
    out_ << "  " << v2 << " = insertvalue " << vec_type << ' ' << v1 << ", " << module_.usizeType() << ' ' << count << ", 1\n";
    out_ << "  " << v3 << " = insertvalue " << vec_type << ' ' << v2 << ", " << module_.usizeType() << ' ' << requested.repr << ", 2\n";
    result = emitNetworkResult(result_type, false, {vec_type, v3});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}

void FunctionEmitter::builtinUdpReceive(const mir::Instruction& inst, const std::vector<mir::Operand>& arguments) {
    if (!inst.destination || arguments.size() != 2) throw std::runtime_error("LLVM codegen: udp_receive expects socket and maximum length");
    const auto& result_type = local(*inst.destination).type;
    const Value socket_ref = value(arguments[0]);
    const Value requested = coerce(value(arguments[1]), arguments[1].type, sema::Type::simple(sema::TypeKind::USize));
    const std::string socket = next("udpsocket");
    out_ << "  " << socket << " = load %nus.udpsocket, ptr " << socket_ref.repr << "\n";
    const std::string handle = socketHandleForCall({"%nus.udpsocket", socket});
    const std::string data = next("recvbuf");
    out_ << "  " << data << " = call ptr @malloc(" << module_.usizeType() << ' ' << requested.repr << ")\n";
    const std::string rc = next("recv");
    if (module_.windowsTarget()) {
        const std::string len32 = next("recvlen");
        out_ << "  " << len32 << " = trunc " << module_.usizeType() << ' ' << requested.repr << " to i32\n";
        out_ << "  " << rc << " = call i32 @recv(i64 " << handle << ", ptr " << data << ", i32 " << len32 << ", i32 0)\n";
    } else out_ << "  " << rc << " = call i64 @recv(i32 " << handle << ", ptr " << data << ", i64 " << requested.repr << ", i32 0)\n";
    const std::string failed = next("recvfail");
    out_ << "  " << failed << " = icmp slt " << (module_.windowsTarget() ? "i32" : "i64") << ' ' << rc << ", 0\n";
    const std::string err_label = nextLabel("udprecv.err.");
    const std::string ok_label = nextLabel("udprecv.ok.");
    const std::string done = nextLabel("udprecv.done.");
    const std::string result_slot = next("netresultslot");
    out_ << "  " << result_slot << " = alloca " << module_.llvmType(result_type) << "\n";
    out_ << "  br i1 " << failed << ", label %" << err_label << ", label %" << ok_label << "\n";
    out_ << err_label << ":\n";
    const Value error = emitNetworkError(6);
    out_ << "  call void @free(ptr " << data << ")\n";
    Value result = emitNetworkResult(result_type, true, error);
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << ok_label << ":\n";
    std::string count = rc;
    if (module_.windowsTarget()) { count = next("received"); out_ << "  " << count << " = zext i32 " << rc << " to " << module_.usizeType() << "\n"; }
    const std::string v1 = next("buffer");
    const std::string v2 = next("buffer");
    const std::string v3 = next("buffer");
    const std::string vec_type = module_.llvmType(sema::Type::vector(sema::Type::simple(sema::TypeKind::U8)));
    out_ << "  " << v1 << " = insertvalue " << vec_type << " poison, ptr " << data << ", 0\n";
    out_ << "  " << v2 << " = insertvalue " << vec_type << ' ' << v1 << ", " << module_.usizeType() << ' ' << count << ", 1\n";
    out_ << "  " << v3 << " = insertvalue " << vec_type << ' ' << v2 << ", " << module_.usizeType() << ' ' << requested.repr << ", 2\n";
    result = emitNetworkResult(result_type, false, {vec_type, v3});
    out_ << "  store " << result.type << ' ' << result.repr << ", ptr " << result_slot << "\n  br label %" << done << "\n";
    out_ << done << ":\n";
    const std::string loaded = next("netresult");
    out_ << "  " << loaded << " = load " << module_.llvmType(result_type) << ", ptr " << result_slot << "\n";
    store(*inst.destination, {module_.llvmType(result_type), loaded});
}


void FunctionEmitter::instruction(const mir::Instruction& inst) {
    using mir::InstructionKind;
    switch (inst.kind) {
        case InstructionKind::Assign: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto& source = inst.operands.front();
            const auto& destination = local(*inst.destination);
            const Value moved = coerce(value(source), source.type, destination.type);
            store(*inst.destination, moved);
            if (source.kind == mir::OperandKind::Local && source.local != *inst.destination && !source.type.isCopy() && !source.type.isReference()) {
                markMoved(source);
            }
            break;
        }
        case InstructionKind::Borrow: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto& source = inst.operands.front();
            if (source.kind != mir::OperandKind::Local) throw std::runtime_error("LLVM codegen: borrow source must be a local");
            std::string pointer;
            if (source.type.kind == sema::TypeKind::Reference) pointer = value(source).repr;
            else pointer = slot(source.local);
            store(*inst.destination, {"ptr", pointer});
            break;
        }
        case InstructionKind::Unary: {
            if (!inst.destination || inst.operands.empty()) return;
            const Value source = value(inst.operands.front());
            const auto& dest = local(*inst.destination);
            Value result{module_.llvmType(dest.type), {}};
            result.repr = next("un");
            if (inst.op == TokenKind::Bang) {
                out_ << "  " << result.repr << " = xor i1 " << source.repr << ", true\n";
            } else if (inst.op == TokenKind::Tilde) {
                out_ << "  " << result.repr << " = xor " << source.type << ' ' << source.repr << ", -1\n";
            } else if (inst.op == TokenKind::Minus && dest.type.isFloat()) {
                out_ << "  " << result.repr << " = fneg " << source.type << ' ' << source.repr << "\n";
            } else if (inst.op == TokenKind::Minus) {
                out_ << "  " << result.repr << " = sub " << source.type << " 0, " << source.repr << "\n";
            } else if (inst.op == TokenKind::Star && inst.operands.front().type.isReference()) {
                out_ << "  " << result.repr << " = load " << result.type << ", ptr " << source.repr << "\n";
            } else {
                throw std::runtime_error("LLVM codegen: unsupported unary operator `" + std::string(tokenKindName(inst.op)) + "`");
            }
            store(*inst.destination, result);
            break;
        }
        case InstructionKind::Binary: {
            if (!inst.destination || inst.operands.size() < 2) return;
            Value lhs = value(inst.operands[0]);
            Value rhs = value(inst.operands[1]);
            const auto& dest = local(*inst.destination);
            sema::Type operand_type = inst.operands[0].type;
            if (inst.operands[0].type.isNumeric() && inst.operands[1].type.isNumeric()) {
                if (dest.type.isNumeric()) operand_type = dest.type;
                else operand_type = commonNumericType(inst.operands[0].type, inst.operands[1].type);
                lhs = coerce(std::move(lhs), inst.operands[0].type, operand_type);
                rhs = coerce(std::move(rhs), inst.operands[1].type, operand_type);
            }
            const std::string result_name = next("bin");
            const bool floating = operand_type.isFloat();
            const bool signed_integer = operand_type.isSignedInteger();
            std::string operation;
            switch (inst.op) {
                case TokenKind::Plus: operation = floating ? "fadd" : "add"; break;
                case TokenKind::Minus: operation = floating ? "fsub" : "sub"; break;
                case TokenKind::Star: operation = floating ? "fmul" : "mul"; break;
                case TokenKind::Slash: operation = floating ? "fdiv" : (signed_integer ? "sdiv" : "udiv"); break;
                case TokenKind::Percent: operation = floating ? "frem" : (signed_integer ? "srem" : "urem"); break;
                case TokenKind::Ampersand: case TokenKind::AmpersandAmpersand: operation = "and"; break;
                case TokenKind::Pipe: case TokenKind::PipePipe: operation = "or"; break;
                case TokenKind::Caret: operation = "xor"; break;
                case TokenKind::ShiftLeft: operation = "shl"; break;
                case TokenKind::ShiftRight: operation = signed_integer ? "ashr" : "lshr"; break;
                case TokenKind::EqualEqual: operation = floating ? "fcmp oeq" : "icmp eq"; break;
                case TokenKind::BangEqual: operation = floating ? "fcmp one" : "icmp ne"; break;
                case TokenKind::Less: operation = floating ? "fcmp olt" : (signed_integer ? "icmp slt" : "icmp ult"); break;
                case TokenKind::LessEqual: operation = floating ? "fcmp ole" : (signed_integer ? "icmp sle" : "icmp ule"); break;
                case TokenKind::Greater: operation = floating ? "fcmp ogt" : (signed_integer ? "icmp sgt" : "icmp ugt"); break;
                case TokenKind::GreaterEqual: operation = floating ? "fcmp oge" : (signed_integer ? "icmp sge" : "icmp uge"); break;
                default: throw std::runtime_error("LLVM codegen: unsupported binary operator `" + std::string(tokenKindName(inst.op)) + "`");
            }
            out_ << "  " << result_name << " = " << operation << ' ' << lhs.type << ' ' << lhs.repr << ", " << rhs.repr << "\n";
            store(*inst.destination, {module_.llvmType(dest.type), result_name});
            break;
        }
        case InstructionKind::Call: {
            if (inst.operands.empty() || inst.operands.front().kind != mir::OperandKind::Global) {
                throw std::runtime_error("LLVM codegen: only direct calls are supported in milestone 9");
            }
            const auto& callee = inst.operands.front();
            std::vector<mir::Operand> arguments(inst.operands.begin() + 1, inst.operands.end());
            if (callee.text == "print") {
                builtinPrint(arguments);
                return;
            }
            if (callee.text == "assert") {
                if (arguments.size() != 1) throw std::runtime_error("LLVM codegen: assert expects one argument");
                const Value condition = value(arguments.front());
                out_ << "  call void @nus_assert(i1 " << condition.repr << ")\n";
                return;
            }
            if (callee.text == "string_clone") { builtinStringClone(inst, arguments); return; }
            if (callee.text == "string_len") { builtinStringLen(inst, arguments); return; }
            if (callee.text == "ipv4") { builtinIpv4(inst, arguments); return; }
            if (callee.text == "socket_addr") { builtinSocketAddr(inst, arguments); return; }
            if (callee.text == "socket_port") { builtinSocketPort(inst, arguments); return; }
            if (callee.text == "network_error_code") { builtinNetworkErrorCode(inst, arguments); return; }
            if (callee.text == "network_error_reason") { builtinNetworkErrorReason(inst, arguments); return; }
            if (callee.text == "dns_resolve_ipv4") { builtinDnsResolveIpv4(inst, arguments); return; }
            if (callee.text == "tcp_connect") { builtinTcpConnect(inst, arguments); return; }
            if (callee.text == "tcp_listen") { builtinTcpListen(inst, arguments); return; }
            if (callee.text == "tcp_accept") { builtinTcpAccept(inst, arguments); return; }
            if (callee.text == "tcp_set_nodelay") { builtinTcpSetNoDelay(inst, arguments); return; }
            if (callee.text == "tcp_set_timeout") { builtinTcpSetTimeout(inst, arguments); return; }
            if (callee.text == "tcp_send") { builtinTcpSend(inst, arguments); return; }
            if (callee.text == "tcp_receive") { builtinTcpReceive(inst, arguments); return; }
            if (callee.text == "udp_bind") { builtinUdpBind(inst, arguments); return; }
            if (callee.text == "udp_send_to") { builtinUdpSendTo(inst, arguments); return; }
            if (callee.text == "udp_receive") { builtinUdpReceive(inst, arguments); return; }
            if (callee.text == "vec_new_i32" || callee.text.rfind("__builtin_vec_new$", 0) == 0) { builtinVecNew(inst); return; }
            if (callee.text == "vec_push_i32" || callee.text.rfind("__builtin_vec_push$", 0) == 0) { builtinVecPush(arguments); return; }
            if (callee.text == "vec_len_i32" || callee.text.rfind("__builtin_vec_len$", 0) == 0) { builtinVecLen(inst, arguments); return; }
            if (callee.text == "vec_get_i32" || callee.text.rfind("__builtin_vec_get$", 0) == 0) { builtinVecGet(inst, arguments); return; }
            if (callee.text.rfind("__builtin_vec_into_iter$", 0) == 0) { builtinVecIntoIter(inst, arguments); return; }
            if (callee.text.rfind("__builtin_vec_iter_mut$", 0) == 0) { builtinVecBorrowIter(inst, arguments, true); return; }
            if (callee.text.rfind("__builtin_vec_iter$", 0) == 0) { builtinVecBorrowIter(inst, arguments, false); return; }
            if (callee.text.rfind("__builtin_some$", 0) == 0 || callee.text.rfind("__builtin_none$", 0) == 0 ||
                callee.text.rfind("__builtin_ok$", 0) == 0 || callee.text.rfind("__builtin_err$", 0) == 0) {
                if (!inst.destination) throw std::runtime_error("LLVM codegen: tagged constructor requires destination");
                const auto& destination = local(*inst.destination);
                const std::string aggregate_type = module_.llvmType(destination.type);
                std::string aggregate = "zeroinitializer";
                const bool some = callee.text.rfind("__builtin_some$", 0) == 0;
                const bool none = callee.text.rfind("__builtin_none$", 0) == 0;
                const bool err = callee.text.rfind("__builtin_err$", 0) == 0;
                const bool tag = some || err;
                const std::string tagged = next("tagged");
                out_ << "  " << tagged << " = insertvalue " << aggregate_type << ' ' << aggregate << ", i1 " << (tag ? "1" : "0") << ", 0\n";
                aggregate = tagged;
                if (!none && !arguments.empty()) {
                    std::size_t field = 1;
                    sema::Type expected = arguments.front().type;
                    if (destination.type.kind == sema::TypeKind::Option && destination.type.element) expected = *destination.type.element;
                    if (destination.type.kind == sema::TypeKind::Result && destination.type.generic_arguments.size() == 2) {
                        expected = err ? destination.type.generic_arguments[1] : destination.type.generic_arguments[0];
                        field = err ? 2 : 1;
                    }
                    const Value payload = coerce(value(arguments.front()), arguments.front().type, expected);
                    const std::string inserted = next("payload");
                    out_ << "  " << inserted << " = insertvalue " << aggregate_type << ' ' << aggregate << ", "
                         << payload.type << ' ' << payload.repr << ", " << field << "\n";
                    aggregate = inserted;
                    if (!arguments.front().type.isCopy() && !arguments.front().type.isReference()) markMoved(arguments.front());
                }
                store(*inst.destination, {aggregate_type, aggregate});
                return;
            }
            std::vector<Value> values;
            values.reserve(arguments.size());
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                Value argument_value = value(arguments[i]);
                if (callee.type.kind == sema::TypeKind::Function && i < callee.type.parameters.size()) {
                    argument_value = coerce(std::move(argument_value), arguments[i].type, callee.type.parameters[i]);
                }
                values.push_back(std::move(argument_value));
            }
            const sema::Type return_type = (callee.type.kind == sema::TypeKind::Function && callee.type.return_type)
                ? *callee.type.return_type : sema::Type::simple(sema::TypeKind::Unit);
            const std::string llvm_return = module_.llvmType(return_type);
            std::ostringstream call;
            call << "call " << llvm_return << " @" << module_.symbol(callee.text) << '(';
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) call << ", ";
                call << values[i].type << ' ' << values[i].repr;
            }
            call << ')';
            if (return_type.isUnit()) {
                out_ << "  " << call.str() << "\n";
            } else {
                if (!inst.destination) throw std::runtime_error("LLVM codegen: non-unit call has no destination");
                const std::string result = next("call");
                out_ << "  " << result << " = " << call.str() << "\n";
                store(*inst.destination, {llvm_return, result});
            }
            if (callee.type.kind == sema::TypeKind::Function) {
                for (std::size_t i = 0; i < arguments.size() && i < callee.type.parameters.size(); ++i) {
                    if (!callee.type.parameters[i].isReference() && !arguments[i].type.isCopy()) markMoved(arguments[i]);
                }
            }
            break;
        }
        case InstructionKind::MakeVariant: {
            if (!inst.destination) return;
            const auto& destination = local(*inst.destination);
            const std::string type = module_.llvmType(destination.type);
            std::string variant = inst.name;
            const auto sep = variant.rfind("::");
            if (sep != std::string::npos) variant = variant.substr(sep + 2);
            std::string aggregate = "zeroinitializer";
            std::size_t tag = 0;
            std::vector<std::size_t> fields;
            std::vector<sema::Type> payload_types;
            if (destination.type.kind == sema::TypeKind::Option && destination.type.element) {
                tag = variant == "Some" ? 1U : 0U;
                if (variant == "Some") { fields = {1}; payload_types = {*destination.type.element}; }
            } else if (destination.type.kind == sema::TypeKind::Result && destination.type.generic_arguments.size() == 2) {
                tag = variant == "Err" ? 1U : 0U;
                if (variant == "Err") { fields = {2}; payload_types = {destination.type.generic_arguments[1]}; }
                else { fields = {1}; payload_types = {destination.type.generic_arguments[0]}; }
            } else if (destination.type.kind == sema::TypeKind::Enum) {
                const auto& v = module_.enumVariant(destination.type.nominal_name, variant);
                tag = v.tag;
                payload_types = v.payloads;
                for (std::size_t i = 0; i < v.payloads.size(); ++i) fields.push_back(module_.enumPayloadFieldIndex(destination.type.nominal_name, variant, i));
            } else {
                throw std::runtime_error("LLVM codegen: MakeVariant destination is not an enum-like type");
            }
            const std::string tagged = next("variant");
            const std::string tag_type = destination.type.kind == sema::TypeKind::Enum ? "i32" : "i1";
            out_ << "  " << tagged << " = insertvalue " << type << ' ' << aggregate << ", " << tag_type << ' ' << tag << ", 0\n";
            aggregate = tagged;
            for (std::size_t i = 0; i < inst.operands.size() && i < fields.size(); ++i) {
                const Value payload = coerce(value(inst.operands[i]), inst.operands[i].type, payload_types[i]);
                const std::string inserted = next("variant");
                out_ << "  " << inserted << " = insertvalue " << type << ' ' << aggregate << ", "
                     << payload.type << ' ' << payload.repr << ", " << fields[i] << "\n";
                aggregate = inserted;
            }
            store(*inst.destination, {type, aggregate});
            for (const auto& operand : inst.operands) if (!operand.type.isCopy() && !operand.type.isReference()) markMoved(operand);
            break;
        }
        case InstructionKind::GetDiscriminant: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto source = value(inst.operands.front());
            const bool user_enum = inst.operands.front().type.kind == sema::TypeKind::Enum;
            const std::string raw = next("tag");
            out_ << "  " << raw << " = extractvalue " << source.type << ' ' << source.repr << ", 0\n";
            if (user_enum) store(*inst.destination, {"i32", raw});
            else {
                const std::string extended = next("tag32");
                out_ << "  " << extended << " = zext i1 " << raw << " to i32\n";
                store(*inst.destination, {"i32", extended});
            }
            break;
        }
        case InstructionKind::GetVariantField: {
            if (!inst.destination || inst.operands.size() < 2) return;
            const auto& source_operand = inst.operands.front();
            const Value source = value(source_operand);
            const std::size_t payload_index = static_cast<std::size_t>(std::stoull(inst.operands[1].text));
            std::size_t field = 1;
            if (source_operand.type.kind == sema::TypeKind::Option) field = 1;
            else if (source_operand.type.kind == sema::TypeKind::Result) field = inst.name == "Err" ? 2 : 1;
            else if (source_operand.type.kind == sema::TypeKind::Enum) field = module_.enumPayloadFieldIndex(source_operand.type.nominal_name, inst.name, payload_index);
            else throw std::runtime_error("LLVM codegen: variant field source is not enum-like");
            const std::string extracted = next("payload");
            const auto& destination = local(*inst.destination);
            out_ << "  " << extracted << " = extractvalue " << source.type << ' ' << source.repr << ", " << field << "\n";
            store(*inst.destination, {module_.llvmType(destination.type), extracted});
            break;
        }
        case InstructionKind::Consume: {
            if (!inst.operands.empty()) markMoved(inst.operands.front());
            break;
        }
        case InstructionKind::Member: {
            if (!inst.destination || inst.operands.empty()) return;
            sema::Type base;
            const std::string object = objectPointer(inst.operands.front(), base);
            if (base.kind != sema::TypeKind::Struct) throw std::runtime_error("LLVM codegen: member base is not a struct");
            const auto& definition = module_.structure(base.nominal_name);
            const std::size_t index = module_.fieldIndex(base.nominal_name, inst.name);
            const sema::Type field_type = definition.fields[index].type;
            const std::string pointer = next("field");
            out_ << "  " << pointer << " = getelementptr inbounds " << module_.llvmType(base) << ", ptr " << object
                 << ", i32 0, i32 " << index << "\n";
            const std::string loaded = next("fieldv");
            out_ << "  " << loaded << " = load " << module_.llvmType(field_type) << ", ptr " << pointer << "\n";
            store(*inst.destination, {module_.llvmType(field_type), loaded});
            break;
        }
        case InstructionKind::StoreMember: {
            if (inst.operands.size() < 2) return;
            sema::Type base;
            const std::string object = objectPointer(inst.operands[0], base);
            if (base.kind != sema::TypeKind::Struct) throw std::runtime_error("LLVM codegen: member store base is not a struct");
            const auto& definition = module_.structure(base.nominal_name);
            const std::size_t index = module_.fieldIndex(base.nominal_name, inst.name);
            const sema::Type field_type = definition.fields[index].type;
            const Value assigned = coerce(value(inst.operands[1]), inst.operands[1].type, field_type);
            const std::string pointer = next("field");
            out_ << "  " << pointer << " = getelementptr inbounds " << module_.llvmType(base) << ", ptr " << object
                 << ", i32 0, i32 " << index << "\n";
            out_ << "  store " << module_.llvmType(field_type) << ' ' << assigned.repr << ", ptr " << pointer << "\n";
            break;
        }
        case InstructionKind::Index: {
            if (!inst.destination || inst.operands.size() < 2) return;
            sema::Type element;
            const std::string pointer = indexPointer(inst.operands[0], inst.operands[1], element);
            const std::string loaded = next("indexv");
            out_ << "  " << loaded << " = load " << module_.llvmType(element) << ", ptr " << pointer << "\n";
            store(*inst.destination, {module_.llvmType(element), loaded});
            break;
        }
        case InstructionKind::StoreIndex: {
            if (inst.operands.size() < 3) return;
            sema::Type element;
            const std::string pointer = indexPointer(inst.operands[0], inst.operands[1], element);
            const Value assigned = coerce(value(inst.operands[2]), inst.operands[2].type, element);
            out_ << "  store " << module_.llvmType(element) << ' ' << assigned.repr << ", ptr " << pointer << "\n";
            break;
        }
        case InstructionKind::StoreDeref: {
            if (inst.operands.size() < 2) return;
            const auto& reference = inst.operands[0];
            if (reference.type.kind != sema::TypeKind::Reference || !reference.type.element || !reference.type.mutable_reference) {
                throw std::runtime_error("LLVM codegen: store.deref requires &mut T");
            }
            const Value pointer = value(reference);
            const Value assigned = coerce(value(inst.operands[1]), inst.operands[1].type, *reference.type.element);
            out_ << "  store " << module_.llvmType(*reference.type.element) << ' ' << assigned.repr << ", ptr " << pointer.repr << "\n";
            if (!inst.operands[1].type.isCopy() && !inst.operands[1].type.isReference()) markMoved(inst.operands[1]);
            break;
        }
        case InstructionKind::MakeStruct: {
            if (!inst.destination) return;
            const auto& destination = local(*inst.destination);
            if (destination.type.kind != sema::TypeKind::Struct) throw std::runtime_error("LLVM codegen: struct instruction has non-struct destination");
            const auto& definition = module_.structure(destination.type.nominal_name);
            std::vector<std::string> field_names;
            const auto left = inst.name.find('{');
            const auto right = inst.name.rfind('}');
            if (left != std::string::npos && right != std::string::npos && right > left) {
                std::stringstream fields(inst.name.substr(left + 1, right - left - 1));
                std::string field;
                while (std::getline(fields, field, ',')) if (!field.empty()) field_names.push_back(field);
            }
            std::string aggregate = "poison";
            for (std::size_t i = 0; i < inst.operands.size(); ++i) {
                const std::string field_name = i < field_names.size() ? field_names[i] : definition.fields.at(i).name;
                const std::size_t index = module_.fieldIndex(destination.type.nominal_name, field_name);
                const sema::Type field_type = definition.fields.at(index).type;
                const Value field_value = coerce(value(inst.operands[i]), inst.operands[i].type, field_type);
                const std::string inserted = next("struct");
                out_ << "  " << inserted << " = insertvalue " << module_.llvmType(destination.type) << ' ' << aggregate
                     << ", " << field_value.type << ' ' << field_value.repr << ", " << index << "\n";
                aggregate = inserted;
            }
            store(*inst.destination, {module_.llvmType(destination.type), aggregate});
            for (const auto& operand : inst.operands) if (!operand.type.isCopy() && !operand.type.isReference()) markMoved(operand);
            break;
        }
        case InstructionKind::MakeArray: {
            if (!inst.destination) return;
            const auto& destination = local(*inst.destination);
            if (destination.type.kind != sema::TypeKind::Array || !destination.type.element || !destination.type.array_length) {
                throw std::runtime_error("LLVM codegen: malformed array destination");
            }
            const std::string array_type = module_.llvmType(destination.type);
            if (inst.name == "repeat") {
                if (inst.operands.empty()) return;
                const Value repeated = value(inst.operands.front());
                if (repeated.repr == "0") {
                    store(*inst.destination, {array_type, "zeroinitializer"});
                    break;
                }
                throw std::runtime_error("LLVM codegen: non-zero array repetition is not yet supported");
            }
            std::string aggregate = "poison";
            for (std::size_t i = 0; i < inst.operands.size(); ++i) {
                const Value item = coerce(value(inst.operands[i]), inst.operands[i].type, *destination.type.element);
                const std::string inserted = next("array");
                out_ << "  " << inserted << " = insertvalue " << array_type << ' ' << aggregate << ", "
                     << item.type << ' ' << item.repr << ", " << i << "\n";
                aggregate = inserted;
            }
            store(*inst.destination, {array_type, aggregate});
            for (const auto& operand : inst.operands) if (!operand.type.isCopy() && !operand.type.isReference()) markMoved(operand);
            break;
        }
        case InstructionKind::MakeRange: {
            if (!inst.destination || inst.operands.size() < 2) return;
            const auto& destination = local(*inst.destination);
            if (!destination.type.element) throw std::runtime_error("LLVM codegen: malformed range type");
            const Value start = coerce(value(inst.operands[0]), inst.operands[0].type, *destination.type.element);
            const Value end = coerce(value(inst.operands[1]), inst.operands[1].type, *destination.type.element);
            const std::string range_type = module_.llvmType(destination.type);
            const std::string one = next("range");
            const std::string two = next("range");
            const std::string three = next("range");
            out_ << "  " << one << " = insertvalue " << range_type << " poison, " << start.type << ' ' << start.repr << ", 0\n";
            out_ << "  " << two << " = insertvalue " << range_type << ' ' << one << ", " << end.type << ' ' << end.repr << ", 1\n";
            out_ << "  " << three << " = insertvalue " << range_type << ' ' << two << ", i1 " << (inst.name == "inclusive" ? "1" : "0") << ", 2\n";
            store(*inst.destination, {range_type, three});
            break;
        }
        case InstructionKind::IterInit: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto& destination = local(*inst.destination);
            const auto& source_operand = inst.operands.front();
            if (destination.type.kind == sema::TypeKind::VecIter && source_operand.type.kind == sema::TypeKind::Vec) {
                if (!destination.type.element || !source_operand.type.element) throw std::runtime_error("LLVM codegen: malformed Vec iterator");
                const Value source = value(source_operand);
                const std::string target_type = module_.llvmType(destination.type);
                const std::string data = next("iterdata");
                const std::string length = next("iterlen");
                const std::string capacity = next("itercap");
                out_ << "  " << data << " = extractvalue " << source.type << ' ' << source.repr << ", 0\n";
                out_ << "  " << length << " = extractvalue " << source.type << ' ' << source.repr << ", 1\n";
                out_ << "  " << capacity << " = extractvalue " << source.type << ' ' << source.repr << ", 2\n";
                const std::string one = next("iter");
                const std::string two = next("iter");
                const std::string three = next("iter");
                const std::string four = next("iter");
                out_ << "  " << one << " = insertvalue " << target_type << " poison, ptr " << data << ", 0\n";
                out_ << "  " << two << " = insertvalue " << target_type << ' ' << one << ", " << module_.usizeType() << ' ' << length << ", 1\n";
                out_ << "  " << three << " = insertvalue " << target_type << ' ' << two << ", " << module_.usizeType() << ' ' << capacity << ", 2\n";
                out_ << "  " << four << " = insertvalue " << target_type << ' ' << three << ", " << module_.usizeType() << " 0, 3\n";
                store(*inst.destination, {target_type, four});
                markMoved(source_operand);
            } else {
                store(*inst.destination, value(source_operand));
                if (!source_operand.type.isCopy() && !source_operand.type.isReference()) markMoved(source_operand);
            }
            break;
        }
        case InstructionKind::IterHasNext: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto& iterator = inst.operands.front();
            const Value range = value(iterator);
            if (iterator.type.kind == sema::TypeKind::Range && iterator.type.element) {
                const std::string element_type = module_.llvmType(*iterator.type.element);
                const std::string start = next("start");
                const std::string end = next("end");
                const std::string inclusive = next("inclusive");
                const std::string less = next("less");
                const std::string less_equal = next("lesseq");
                const std::string result = next("hasnext");
                out_ << "  " << start << " = extractvalue " << range.type << ' ' << range.repr << ", 0\n";
                out_ << "  " << end << " = extractvalue " << range.type << ' ' << range.repr << ", 1\n";
                out_ << "  " << inclusive << " = extractvalue " << range.type << ' ' << range.repr << ", 2\n";
                const bool signed_element = iterator.type.element->isSignedInteger();
                out_ << "  " << less << " = icmp " << (signed_element ? "slt" : "ult") << ' ' << element_type << ' ' << start << ", " << end << "\n";
                out_ << "  " << less_equal << " = icmp " << (signed_element ? "sle" : "ule") << ' ' << element_type << ' ' << start << ", " << end << "\n";
                out_ << "  " << result << " = select i1 " << inclusive << ", i1 " << less_equal << ", i1 " << less << "\n";
                store(*inst.destination, {"i1", result});
                break;
            }
            if (iterator.type.kind == sema::TypeKind::VecIter && iterator.type.element) {
                const std::string length = next("iterlen");
                const std::string index = next("iteridx");
                const std::string result = next("hasnext");
                out_ << "  " << length << " = extractvalue " << range.type << ' ' << range.repr << ", 1\n";
                out_ << "  " << index << " = extractvalue " << range.type << ' ' << range.repr << ", 3\n";
                out_ << "  " << result << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                store(*inst.destination, {"i1", result});
                break;
            }
            if ((iterator.type.kind == sema::TypeKind::VecRefIter || iterator.type.kind == sema::TypeKind::VecMutIter) && iterator.type.element) {
                const std::string length = next("iterlen");
                const std::string index = next("iteridx");
                const std::string result = next("hasnext");
                out_ << "  " << length << " = extractvalue " << range.type << ' ' << range.repr << ", 1\n";
                out_ << "  " << index << " = extractvalue " << range.type << ' ' << range.repr << ", 2\n";
                out_ << "  " << result << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                store(*inst.destination, {"i1", result});
                break;
            }
            throw std::runtime_error("LLVM codegen: unsupported iterator type `" + iterator.type.name() + "`");
        }
        case InstructionKind::IterNext: {
            if (!inst.destination || inst.operands.empty()) return;
            const auto& iterator = inst.operands.front();
            if (iterator.kind != mir::OperandKind::Local) throw std::runtime_error("LLVM codegen: iter.next requires a local iterator");
            const Value aggregate = value(iterator);
            if (iterator.type.kind == sema::TypeKind::Range && iterator.type.element) {
                const std::string element_type = module_.llvmType(*iterator.type.element);
                const std::string start = next("next");
                const std::string incremented = next("inc");
                const std::string updated = next("range");
                out_ << "  " << start << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 0\n";
                out_ << "  " << incremented << " = add " << element_type << ' ' << start << ", 1\n";
                out_ << "  " << updated << " = insertvalue " << aggregate.type << ' ' << aggregate.repr << ", " << element_type << ' ' << incremented << ", 0\n";
                out_ << "  store " << aggregate.type << ' ' << updated << ", ptr " << slot(iterator.local) << "\n";
                store(*inst.destination, {element_type, start});
                break;
            }
            if (iterator.type.kind == sema::TypeKind::VecIter && iterator.type.element) {
                const std::string element_type = module_.llvmType(*iterator.type.element);
                const std::string data = next("iterdata");
                const std::string length = next("iterlen");
                const std::string index = next("iteridx");
                const std::string in_bounds = next("iterbounds");
                const std::string element_ptr = next("iterelem");
                const std::string loaded = next("itervalue");
                const std::string incremented = next("iteridx");
                const std::string updated = next("iter");
                out_ << "  " << data << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 0\n";
                out_ << "  " << length << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 1\n";
                out_ << "  " << index << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 3\n";
                out_ << "  " << in_bounds << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                out_ << "  call void @nus_assert(i1 " << in_bounds << ")\n";
                out_ << "  " << element_ptr << " = getelementptr inbounds " << element_type << ", ptr " << data << ", " << module_.usizeType() << ' ' << index << "\n";
                out_ << "  " << loaded << " = load " << element_type << ", ptr " << element_ptr << "\n";
                out_ << "  " << incremented << " = add " << module_.usizeType() << ' ' << index << ", 1\n";
                out_ << "  " << updated << " = insertvalue " << aggregate.type << ' ' << aggregate.repr << ", " << module_.usizeType() << ' ' << incremented << ", 3\n";
                out_ << "  store " << aggregate.type << ' ' << updated << ", ptr " << slot(iterator.local) << "\n";
                store(*inst.destination, {element_type, loaded});
                break;
            }
            if ((iterator.type.kind == sema::TypeKind::VecRefIter || iterator.type.kind == sema::TypeKind::VecMutIter) && iterator.type.element) {
                const std::string element_type = module_.llvmType(*iterator.type.element);
                const std::string data = next("iterdata");
                const std::string length = next("iterlen");
                const std::string index = next("iteridx");
                const std::string in_bounds = next("iterbounds");
                const std::string element_ptr = next("iteref");
                const std::string incremented = next("iteridx");
                const std::string updated = next("iter");
                out_ << "  " << data << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 0\n";
                out_ << "  " << length << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 1\n";
                out_ << "  " << index << " = extractvalue " << aggregate.type << ' ' << aggregate.repr << ", 2\n";
                out_ << "  " << in_bounds << " = icmp ult " << module_.usizeType() << ' ' << index << ", " << length << "\n";
                out_ << "  call void @nus_assert(i1 " << in_bounds << ")\n";
                out_ << "  " << element_ptr << " = getelementptr inbounds " << element_type << ", ptr " << data << ", " << module_.usizeType() << ' ' << index << "\n";
                out_ << "  " << incremented << " = add " << module_.usizeType() << ' ' << index << ", 1\n";
                out_ << "  " << updated << " = insertvalue " << aggregate.type << ' ' << aggregate.repr << ", " << module_.usizeType() << ' ' << incremented << ", 2\n";
                out_ << "  store " << aggregate.type << ' ' << updated << ", ptr " << slot(iterator.local) << "\n";
                store(*inst.destination, {"ptr", element_ptr});
                break;
            }
            throw std::runtime_error("LLVM codegen: unsupported iterator type `" + iterator.type.name() + "`");
        }
        case InstructionKind::Drop:
            emitDrop(inst, false);
            break;
        case InstructionKind::DropIf:
            emitDrop(inst, true);
            break;
    }
}

void FunctionEmitter::terminator(const mir::Terminator& term) {
    using mir::TerminatorKind;
    switch (term.kind) {
        case TerminatorKind::Goto:
            out_ << "  br label %bb" << term.target << "\n";
            break;
        case TerminatorKind::Branch: {
            const Value condition = value(term.condition);
            out_ << "  br i1 " << condition.repr << ", label %bb" << term.then_block << ", label %bb" << term.else_block << "\n";
            break;
        }
        case TerminatorKind::Return:
            if (function_.return_type.isUnit()) out_ << "  ret void\n";
            else {
                const Value returned = coerce(value(term.value), term.value.type, function_.return_type);
                out_ << "  ret " << returned.type << ' ' << returned.repr << "\n";
            }
            break;
        case TerminatorKind::Unreachable:
            out_ << "  unreachable\n";
            break;
    }
}

void FunctionEmitter::emit() {
    const std::string qualified = functionKey(function_);
    const std::string return_type = module_.llvmType(function_.return_type);
    std::vector<const mir::Local*> parameters;
    for (const auto& local : function_.locals) if (local.is_receiver || local.is_parameter) parameters.push_back(&local);

    out_ << "define " << return_type << " @" << module_.symbol(qualified) << '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i != 0) out_ << ", ";
        out_ << module_.llvmType(parameters[i]->type) << " %arg" << i;
    }
    out_ << ") {\nentry:\n";

    for (const auto& local : function_.locals) {
        if (local.type.isUnit()) continue;
        out_ << "  " << slot(local.id) << " = alloca " << module_.llvmType(local.type) << "\n";
        if (!local.type.isCopy() && !local.type.isReference()) {
            out_ << "  " << dropFlag(local.id) << " = alloca i1\n";
            out_ << "  store i1 false, ptr " << dropFlag(local.id) << "\n";
        }
    }
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        out_ << "  store " << module_.llvmType(parameters[i]->type) << " %arg" << i << ", ptr " << slot(parameters[i]->id) << "\n";
        if (!parameters[i]->type.isCopy() && !parameters[i]->type.isReference()) {
            out_ << "  store i1 true, ptr " << dropFlag(parameters[i]->id) << "\n";
        }
    }
    out_ << "  br label %bb" << function_.entry << "\n";

    for (const auto& block : function_.blocks) {
        out_ << "bb" << block.id << ":\n";
        for (const auto& inst : block.instructions) instruction(inst);
        if (!block.terminator) throw std::runtime_error("LLVM codegen: basic block is missing a terminator");
        terminator(*block.terminator);
    }
    out_ << "}\n\n";
}

} // namespace

std::string LlvmEmitter::emit(const mir::Program& program, TargetInfo target) {
    return ModuleEmitter(program, std::move(target)).run();
}

} // namespace nus::codegen
