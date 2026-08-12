#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the content-density audit handlers - count creatures
// / objects / quests per tile to surface sparse zones (boring)
// and over-stuffed ones (frame-rate bombs). Useful for content
// pacing reviews and balance audits.
//   --info-zone-density     per-tile bucket within one zone
//   --info-project-density  per-zone rollup with project-wide averages
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoDensity(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
