#pragma once

// Cross-platform subprocess helpers for spawning ffplay (audio playback).
// Linux: fork/exec/kill/waitpid.  Windows: CreateProcess/TerminateProcess.

#include <string>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>

  using ProcessHandle = HANDLE;
  inline const ProcessHandle INVALID_PROCESS = INVALID_HANDLE_VALUE;

#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <csignal>

  using ProcessHandle = pid_t;
  inline constexpr ProcessHandle INVALID_PROCESS = -1;

#endif

#include <filesystem>

namespace wowee {
namespace platform {

// Return a platform-appropriate temp file path for the given filename.
inline std::string getTempFilePath(const std::string& filename) {
    auto tmp = std::filesystem::temp_directory_path() / filename;
    return tmp.string();
}

// Kill a subprocess (and its children on Linux).
inline void killProcess(ProcessHandle& handle) {
    if (handle == INVALID_PROCESS) return;

#ifdef _WIN32
    TerminateProcess(handle, 0);
    WaitForSingleObject(handle, 2000);
    CloseHandle(handle);
#else
    kill(-handle, SIGTERM);  // kill process group
    kill(handle, SIGTERM);
    int status = 0;
    // Non-blocking wait with SIGKILL fallback after ~200ms
    for (int i = 0; i < 20; ++i) {
        pid_t ret = waitpid(handle, &status, WNOHANG);
        if (ret != 0) break;  // exited or error
        usleep(10000);         // 10ms
    }
    // If still alive, force kill
    if (waitpid(handle, &status, WNOHANG) == 0) {
        kill(-handle, SIGKILL);
        kill(handle, SIGKILL);
        waitpid(handle, &status, 0);
    }
#endif

    handle = INVALID_PROCESS;
}

// Check if a process has exited. If so, clean up and set handle to INVALID_PROCESS.
// Returns true if the process is still running.
inline bool isProcessRunning(ProcessHandle& handle) {
    if (handle == INVALID_PROCESS) return false;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(handle, 0);
    if (result == WAIT_OBJECT_0) {
        // Process has exited
        CloseHandle(handle);
        handle = INVALID_PROCESS;
        return false;
    }
    return true;
#else
    int status = 0;
    pid_t result = waitpid(handle, &status, WNOHANG);
    if (result == handle) {
        handle = INVALID_PROCESS;
        return false;
    }
    return true;
#endif
}

} // namespace platform
} // namespace wowee
