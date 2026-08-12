#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the clone-* duplicate handlers - deep-copy a quest /
// creature spawn / object placement by index, optionally
// renaming and offsetting the new copy. Useful for templating
// patterns: design once, clone N times.
//   --clone-quest      duplicate a quest with all objectives + rewards
//   --clone-creature   duplicate a spawn (default 5-yard X offset)
//   --clone-object     duplicate an object placement (5-yard X offset)
//
// Returns true if matched; outRc holds the exit code.
bool handleClone(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
