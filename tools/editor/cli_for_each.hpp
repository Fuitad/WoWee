#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the batch-runner handlers - iterate over zones (or
// tiles within a zone) and execute a shell command for each
// one, with `{}` substitution like find -exec.
//   --for-each-zone <projectDir> -- <cmd>
//   --for-each-tile <zoneDir>    -- <cmd>
//
// Useful for batch-validating, rebuilding, or processing every
// zone / tile without hand-typing the loop. Exit code is the
// failure count, capped at 255 so the shell can still see it.
//
// Returns true if matched; outRc holds the exit code.
bool handleForEach(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
