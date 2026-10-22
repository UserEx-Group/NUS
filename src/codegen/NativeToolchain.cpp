#include "nus/codegen/NativeToolchain.hpp"

#include "nus/codegen/TargetInfo.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

namespace nus::codegen {
namespace {

std::string quote(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) return value;
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '\"') escaped += "\\\"";
        else escaped.push_back(ch);
    }
    escaped += '\"';
    return escaped;
}

std::string displayCommand(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out << ' ';
        out << quote(args[i]);
    }
    return out.str();
}

#ifdef _WIN32
int runProcess(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1U);
    for (const auto& arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    return static_cast<int>(_spawnvp(_P_WAIT, argv.front(), argv.data()));
}
#else
int runProcess(const std::vector<std::string>& args) {
    return std::system(displayCommand(args).c_str());
}
#endif

} // namespace

std::string NativeToolchain::clangExecutable() {
    if (const char* configured = std::getenv("NUS_CLANG")) {
        if (*configured != '\0') return configured;
    }
    return "clang";
}

std::string NativeToolchain::mingwRoot() {
#ifdef _WIN32
    if (const char* configured = std::getenv("NUS_MINGW")) {
        if (*configured != '\0') return configured;
    }
    const std::filesystem::path standalone{"C:\\mingw64"};
    if (std::filesystem::exists(standalone)) return standalone.string();
    const std::filesystem::path msys2{"C:\\msys64\\mingw64"};
    if (std::filesystem::exists(msys2)) return msys2.string();
#endif
    return {};
}

bool NativeToolchain::clangAvailable() {
    const std::vector<std::string> args{clangExecutable(), "--version"};
    return runProcess(args) == 0;
}

NativeBuildResult NativeToolchain::build(const std::string& llvm_ir,
                                         const std::filesystem::path& output) const {
    NativeBuildResult result;
    result.output = output;

    std::error_code ec;
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path(), ec);

    const auto stem = output.stem().string();
    const auto ir_path = output.parent_path() / (stem + ".nus.ll");
    {
        std::ofstream stream(ir_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            result.message = "cannot write temporary LLVM IR file `" + ir_path.string() + "`";
            return result;
        }
        stream << llvm_ir;
    }

    std::vector<std::string> args;
    args.push_back(clangExecutable());
    const auto target = TargetInfo::host();
    if (!target.triple.empty()) args.push_back("--target=" + target.triple);
#ifdef _WIN32
    const auto mingw = mingwRoot();
    if (mingw.empty()) {
        result.message = "MinGW-w64 toolchain was not found; set NUS_MINGW (for example C:\\mingw64)";
        result.command = displayCommand(args);
        return result;
    }
    args.push_back("--gcc-toolchain=" + mingw);
#endif
    args.insert(args.end(), {"-Wno-override-module", "-O0", "-x", "ir", ir_path.string(), "-o", output.string()});

    result.command = displayCommand(args);
    result.exit_code = runProcess(args);
    result.success = result.exit_code == 0;
    if (!result.success) {
        result.message = "clang failed while linking native executable; verify NUS_CLANG and the native GNU toolchain";
        return result;
    }

    std::filesystem::remove(ir_path, ec);
    result.message = "native executable generated";
    return result;
}

} // namespace nus::codegen
