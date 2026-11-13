// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/process.hpp"

#include "annokit/core/file.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace annokit::update {

namespace fs = std::filesystem;
using core::ErrorCode;
using core::make_error;

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}

/// One argument quoted as CommandLineToArgvW reads it back.
std::wstring quoted(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

}  // namespace

core::Result<fs::path> executable_path() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return make_error(ErrorCode::PlatformError, "cannot find the program's own file");
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

core::Status start_program(const fs::path& program, const std::vector<std::string>& arguments) {
    std::wstring command = quoted(program.wstring());
    for (const std::string& a : arguments) {
        command += L" " + quoted(wide(a));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring folder = program.parent_path().wstring();
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                        folder.c_str(), &startup, &process)) {
        return make_error(ErrorCode::PlatformError, "cannot start " + core::path_to_utf8(program) + " (error " +
                                                        std::to_string(GetLastError()) + ")");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return core::Ok{};
}

#else

core::Result<fs::path> executable_path() {
    std::error_code ec;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return make_error(ErrorCode::PlatformError, "cannot find the program's own file");
    }
    buffer.resize(std::strlen(buffer.c_str()));
    fs::path path = fs::canonical(buffer, ec);
#else
    fs::path path = fs::read_symlink("/proc/self/exe", ec);
#endif
    if (ec) {
        return make_error(ErrorCode::PlatformError, "cannot find the program's own file: " + ec.message());
    }
    return path;
}

core::Status start_program(const fs::path& program, const std::vector<std::string>& arguments) {
    const std::string path = program.string();
    std::vector<std::string> owned;
    owned.reserve(arguments.size() + 1);
    owned.push_back(path);
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& a : owned) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
#if defined(POSIX_SPAWN_SETSID)
    // Its own session: it is not ended with this one's terminal or process group.
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
#endif
    pid_t child = 0;
    const int error = posix_spawn(&child, path.c_str(), nullptr, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    if (error != 0) {
        return make_error(ErrorCode::PlatformError, "cannot start " + path + ": " + std::strerror(error));
    }
    return core::Ok{};
}

#endif

}  // namespace annokit::update
