#pragma once

#include <filesystem>
#include <string>

namespace nus::codegen {

struct NativeExecutionResult {
    bool success{false};
    int exit_code{-1};
    std::string stdout_text;
    std::string message;
};

struct NativeBuildResult {
    bool success{false};
    int exit_code{-1};
    std::filesystem::path output;
    std::string command;
    std::string message;
};

class NativeToolchain {
public:
    [[nodiscard]] static std::string clangExecutable();
    [[nodiscard]] static bool clangAvailable();
    [[nodiscard]] static std::string mingwRoot();
    [[nodiscard]] NativeBuildResult build(const std::string& llvm_ir,
                                          const std::filesystem::path& output) const;
    [[nodiscard]] NativeExecutionResult run(const std::filesystem::path& executable) const;
};

} // namespace nus::codegen
