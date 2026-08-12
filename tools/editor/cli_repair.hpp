#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the repair-* manifest-drift fix handlers - auto-fix
// the common manifest-vs-disk inconsistencies that accumulate
// when zones are hand-edited or partially copied. Both honor
// --dry-run for safe previews.
//   --repair-zone     fix one zone (sync tiles, hasCreatures)
//   --repair-project  per-zone wrapper with aggregate tally
//
// Returns true if matched; outRc holds the exit code.
bool handleRepair(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
