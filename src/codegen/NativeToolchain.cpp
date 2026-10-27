#include "nus/codegen/NativeToolchain.hpp"

#include "nus/codegen/TargetInfo.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <cstdio>
#include <sys/wait.h>
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


std::string normalizeNewlines(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r' && i + 1U < value.size() && value[i + 1U] == '\n') continue;
        normalized.push_back(value[i]);
    }
    return normalized;
}

#ifdef _WIN32
NativeExecutionResult runCaptured(const std::filesystem::path& executable, const std::filesystem::path& runtime_root) {
    NativeExecutionResult result;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        result.message = "CreatePipe failed while preparing native executable capture";
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        result.message = "SetHandleInformation failed while preparing native executable capture";
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    const std::wstring application = std::filesystem::absolute(executable).wstring();
    std::wstring command_line = L"\"" + application + L"\"";
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    std::wstring previous_path;
    bool path_changed = false;
    if (!runtime_root.empty()) {
        const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (required != 0) {
            std::vector<wchar_t> buffer(required);
            const DWORD copied = GetEnvironmentVariableW(L"PATH", buffer.data(), required);
            if (copied != 0) previous_path.assign(buffer.data(), copied);
        }
        const std::wstring runtime_bin = (runtime_root / "bin").wstring();
        std::wstring child_path = runtime_bin;
        if (!previous_path.empty()) child_path += L";" + previous_path;
        path_changed = SetEnvironmentVariableW(L"PATH", child_path.c_str()) != FALSE;
    }

    const BOOL created = CreateProcessW(
        application.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &startup,
        &process);

    if (path_changed) {
        SetEnvironmentVariableW(L"PATH", previous_path.empty() ? nullptr : previous_path.c_str());
    }
    CloseHandle(write_pipe);

    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(read_pipe);
        result.message = "CreateProcessW failed for native executable (Windows error " + std::to_string(error) + ")";
        return result;
    }

    std::string output;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
        output.append(buffer, buffer + read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = static_cast<DWORD>(-1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    result.exit_code = static_cast<int>(exit_code);
    result.success = exit_code == 0;
    result.stdout_text = normalizeNewlines(std::move(output));
    if (!result.success) {
        if (exit_code == 0xC0000135UL) {
            result.message = "native executable could not load a runtime DLL; verify NUS_MINGW\\bin";
        } else {
            result.message = "native executable returned a non-zero exit code";
        }
    }
    return result;
}
#else
NativeExecutionResult runCaptured(const std::filesystem::path& executable, const std::filesystem::path&) {
    NativeExecutionResult result;
    const std::string command = quote(executable.string());
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.message = "popen failed while executing native program";
        return result;
    }
    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe)) output += buffer;
    const int status = pclose(pipe);
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = status;
    }
    result.success = result.exit_code == 0;
    result.stdout_text = normalizeNewlines(std::move(output));
    if (!result.success) result.message = "native executable returned a non-zero exit code";
    return result;
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
#ifdef _WIN32
    // Winsock is part of the native NUS networking runtime on Windows/MinGW-w64.
    args.push_back("-lws2_32");
#endif

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

NativeExecutionResult NativeToolchain::run(const std::filesystem::path& executable) const {
    if (!std::filesystem::exists(executable)) {
        NativeExecutionResult result;
        result.message = "native executable does not exist: `" + executable.string() + "`";
        return result;
    }
#ifdef _WIN32
    return runCaptured(executable, mingwRoot());
#else
    return runCaptured(executable, {});
#endif
}

} // namespace nus::codegen
