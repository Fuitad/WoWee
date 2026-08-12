#pragma once

// Portable subprocess launcher for the editor CLI. Replaces std::system()
// with direct posix_spawn / CreateProcess calls so we avoid invoking a
// shell - both for safety (CodeQL cpp/command-line-injection) and for
// correctness (paths with spaces, quotes, or metacharacters work
// without manual escaping).

#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

// Spawn `argv0` with `args` (excluding argv0 itself), wait for it to
// finish, return its exit code. No shell is invoked, so arguments are
// passed verbatim - quoting is unnecessary and forbidden.
//
// If `quiet` is true, the child's stdout and stderr are redirected to
// the platform null device (/dev/null on POSIX, NUL on Windows).
//
// On launch failure the function returns -1 and writes a diagnostic to
// stderr.
int runChild(const std::string& argv0,
             const std::vector<std::string>& args,
             bool quiet = false);

} // namespace cli
} // namespace editor
} // namespace wowee
