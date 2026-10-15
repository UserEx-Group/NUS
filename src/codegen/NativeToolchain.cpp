#include "nus/codegen/NativeToolchain.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace nus::codegen {
namespace {

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string quoteExecutable(const std::string& executable) {
    return "\"" + executable + "\"";
}

} // namespace

std::string NativeToolchain::clangExecutable() {
    if (const char* configured = std::getenv("NUS_CLANG")) {
        if (*configured != '\0') return configured;
    }
    return "clang";
}

bool NativeToolchain::clangAvailable() {
#ifdef _WIN32
    const std::string redirect = " >NUL 2>&1";
#else
    const std::string redirect = " >/dev/null 2>&1";
#endif
    const std::string command = quoteExecutable(clangExecutable()) + " --version" + redirect;
    return std::system(command.c_str()) == 0;
}

NativeBuildResult NativeToolchain::build(const std::string& llvm_ir,
                                         const std::filesystem::path& output) const {
    NativeBuildResult result;
    result.output = output;

    std::error_code ec;
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path(), ec);

    auto ir_path = output;
    ir_path += ".nus.ll";
    {
        std::ofstream stream(ir_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            result.message = "cannot write temporary LLVM IR file `" + ir_path.string() + "`";
            return result;
        }
        stream << llvm_ir;
    }

    std::ostringstream command;
    command << quoteExecutable(clangExecutable())
            << " -Wno-override-module -O0 -x ir " << quote(ir_path)
            << " -o " << quote(output);
    result.command = command.str();
    result.exit_code = std::system(result.command.c_str());
    result.success = result.exit_code == 0;
    if (!result.success) {
        result.message = "clang failed while linking native executable; set NUS_CLANG to the clang executable if it is not on PATH";
        return result;
    }

    std::filesystem::remove(ir_path, ec);
    result.message = "native executable generated";
    return result;
}

} // namespace nus::codegen
