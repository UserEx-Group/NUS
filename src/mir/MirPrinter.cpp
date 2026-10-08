#include "nus/mir/MirPrinter.hpp"

#include "nus/lexer/TokenKind.hpp"

#include <sstream>

namespace nus::mir {
namespace {

std::string operand(const Operand& value) {
    switch (value.kind) {
        case OperandKind::Local: return "%" + std::to_string(value.local);
        case OperandKind::Literal: return "`" + value.text + "`";
        case OperandKind::Global: return "@" + value.text;
        case OperandKind::Unit: return "unit";
    }
    return "<?>";
}

std::string instructionName(InstructionKind kind) {
    switch (kind) {
        case InstructionKind::Assign: return "assign";
        case InstructionKind::Unary: return "unary";
        case InstructionKind::Binary: return "binary";
        case InstructionKind::Borrow: return "borrow";
        case InstructionKind::Call: return "call";
        case InstructionKind::Member: return "member";
        case InstructionKind::Index: return "index";
        case InstructionKind::MakeArray: return "array";
        case InstructionKind::MakeRange: return "range";
        case InstructionKind::MakeStruct: return "struct";
        case InstructionKind::IterInit: return "iter.init";
        case InstructionKind::IterHasNext: return "iter.has_next";
        case InstructionKind::IterNext: return "iter.next";
        case InstructionKind::StoreMember: return "store.member";
        case InstructionKind::StoreIndex: return "store.index";
    }
    return "<?>";
}

void appendInstruction(std::ostringstream& out, const Instruction& inst) {
    out << "    ";
    if (inst.destination) out << '%' << *inst.destination << " = ";
    out << instructionName(inst.kind);
    if (inst.op != TokenKind::Invalid) out << ' ' << tokenKindName(inst.op);
    if (!inst.name.empty()) out << " [" << inst.name << ']';
    if (!inst.operands.empty()) {
        out << ' ';
        for (std::size_t i = 0; i < inst.operands.size(); ++i) {
            if (i != 0) out << ", ";
            out << operand(inst.operands[i]);
        }
    }
    out << '\n';
}

void appendTerminator(std::ostringstream& out, const Terminator& term) {
    out << "    -> ";
    switch (term.kind) {
        case TerminatorKind::Goto:
            out << "goto bb" << term.target;
            break;
        case TerminatorKind::Branch:
            out << "branch " << operand(term.condition) << " ? bb" << term.then_block << " : bb" << term.else_block;
            break;
        case TerminatorKind::Return:
            out << "return " << operand(term.value);
            break;
        case TerminatorKind::Unreachable:
            out << "unreachable";
            break;
    }
    out << '\n';
}

} // namespace

std::string MirPrinter::print(const Program& program) const {
    std::ostringstream out;
    out << "MIR\n";
    for (const auto& structure : program.structs) {
        out << "  struct " << structure.name << " {\n";
        for (const auto& field : structure.fields) out << "    " << field.name << ": " << field.type.name() << "\n";
        out << "  }\n";
    }
    for (const auto& function : program.functions) {
        const std::string qualified = function.owner_type.empty() ? function.name : function.owner_type + "::" + function.name;
        out << "  fn " << qualified << " -> " << function.return_type.name() << " {\n";
        out << "    locals:\n";
        for (const auto& local : function.locals) {
            out << "      %" << local.id << " " << local.name << ": " << local.type.name();
            if (local.is_mutable) out << " mut";
            if (local.is_temporary) out << " temp";
            out << '\n';
        }
        for (const auto& block : function.blocks) {
            out << "    bb" << block.id;
            if (block.id == function.entry) out << " [entry]";
            out << ":\n";
            for (const auto& inst : block.instructions) appendInstruction(out, inst);
            if (block.terminator) appendTerminator(out, *block.terminator);
            else out << "    -> <missing terminator>\n";
        }
        out << "  }\n";
    }
    return out.str();
}

} // namespace nus::mir
