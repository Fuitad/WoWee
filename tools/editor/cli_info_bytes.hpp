#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the disk-byte audit handlers - per-file size
// breakdowns grouped by category (open vs proprietary vs
// derived). Useful for capacity planning and tracking the
// open-format migration's progress against the proprietary
// .m2 / .wmo / .blp / .dbc baseline.
//   --info-zone-bytes      drill into one zone
//   --info-project-bytes   project-wide audit + open/prop split
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoBytes(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
