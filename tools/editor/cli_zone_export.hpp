#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone-level visual / report export handlers -
// render PNG previews of terrain, spawn distributions, and
// Markdown dependency reports. All three operate per-zone.
//   --export-png             heightmap + normals + zone-map PNG triplet
//   --export-zone-deps-md    GitHub-renderable model-dependency table
//   --export-zone-spawn-png  top-down spawn distribution PNG
//
// Returns true if matched; outRc holds the exit code.
bool handleZoneExport(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
