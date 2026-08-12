#include "cli_subprocess.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
// Guarded: the build already defines it, and redefining is an error under
// -Werror.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <string>
#else
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace wowee {
namespace editor {
namespace cli {

#if defined(_WIN32)

// Quote a single argument per CommandLineToArgvW rules so CreateProcessA
// reconstructs the same argv on the child side. Required because
// CreateProcess takes a single command-line string, unlike POSIX exec.
static std::string winQuoteArg(const std::string& s) {
    if (!s.empty() &&
        s.find_first_of(" \t\n\v\"") == std::string::npos) {
        return s;
    }
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (size_t i = 0; i < s.size(); ++i) {
        size_t backslashes = 0;
        while (i < s.size() && s[i] == '\\') {
            ++backslashes;
            ++i;
        }
        if (i == s.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (s[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
        } else {
            out.append(backslashes, '\\');
        }
        out.push_back(s[i]);
    }
    out.push_back('"');
    return out;
}

int runChild(const std::string& argv0,
             const std::vector<std::string>& args,
             bool quiet) {
    std::string cmdLine = winQuoteArg(argv0);
    for (const auto& a : args) {
        cmdLine.push_back(' ');
        cmdLine += winQuoteArg(a);
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    HANDLE nullHandle = INVALID_HANDLE_VALUE;
    if (quiet) {
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        nullHandle = CreateFileA(
            "NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (nullHandle != INVALID_HANDLE_VALUE) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = nullHandle;
            si.hStdError = nullHandle;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }
    }

    PROCESS_INFORMATION pi = {};
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    BOOL ok = CreateProcessA(
        argv0.c_str(),
        mutableCmd.data(),
        NULL, NULL,
        quiet && nullHandle != INVALID_HANDLE_VALUE ? TRUE : FALSE,
        0, NULL, NULL, &si, &pi);

    if (nullHandle != INVALID_HANDLE_VALUE) CloseHandle(nullHandle);

    if (!ok) {
        std::fprintf(stderr,
            "runChild: CreateProcess('%s') failed (err=%lu)\n",
            argv0.c_str(), GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

#else  // POSIX

int runChild(const std::string& argv0,
             const std::vector<std::string>& args,
             bool quiet) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(argv0.c_str()));
    for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (quiet) {
        posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv0.c_str(), &actions, nullptr,
                         argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (rc != 0) {
        std::fprintf(stderr,
            "runChild: posix_spawn('%s') failed (rc=%d)\n",
            argv0.c_str(), rc);
        return -1;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

#endif

} // namespace cli
} // namespace editor
} // namespace wowee
