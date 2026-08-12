#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone-level lifecycle handlers - copy, rename,
// remove, and clear-content operations on whole zone
// directories. All slug-aware (file rename and manifest
// mapName stay in sync) so the result is self-consistent.
//   --copy-zone           duplicate a zone with a fresh slug
//   --rename-zone         change a zone's slug + rename files
//   --remove-zone         delete a zone directory (with confirmation)
//   --clear-zone-content  empty creatures/objects/quests/items
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneMgmt(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
