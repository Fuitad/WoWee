#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone discovery / aggregation handlers - list
// every zone in the standard locations, or compute project-
// wide tile / creature / quest / byte totals.
//   --list-zones    quick name+dir listing across custom_zones/output
//   --zone-stats    project-wide aggregate with per-zone breakdown
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneList(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
