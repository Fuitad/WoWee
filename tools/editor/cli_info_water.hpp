#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the water-layer audit handlers - aggregate liquid
// data (water/ocean/magma/slime) across a zone or project.
// Useful for confirming a 'lake zone' actually carries water,
// or for budget planning when many zones share liquid types.
//   --info-zone-water     drill into one zone's MH2O layers
//   --info-project-water  project-wide rollup with per-zone rows
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoWater(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
