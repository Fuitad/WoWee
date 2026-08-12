#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone-data maintenance handlers - re-derive
// stored data files (collision, JSON sidecars) from sources
// after authoring changes. Distinct from cli_repair (which
// fixes manifest-vs-disk drift): these rebuild derived
// terrain data and clean up JSON files via load+save.
//   --fix-zone         re-parse + re-save every zone JSON to apply scrubs
//   --regen-collision  rebuild WOC for every WHM/WOT in a zone
//   --build-woc        single-tile WOC build from a WHM/WOT base
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneData(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
