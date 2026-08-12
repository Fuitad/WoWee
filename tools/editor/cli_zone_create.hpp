#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone-creation handlers. Both kickstart a new
// authoring session by generating a new zone directory under
// custom_zones/ - empty for --scaffold-zone, populated with
// one of each content type for --mvp-zone.
//   --scaffold-zone   minimal valid empty zone (terrain + manifest)
//   --mvp-zone        scaffold + 1 creature + 1 object + 1 quest
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneCreate(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
