#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the spatial-bounds info handlers - compute world-space
// XYZ bounding boxes from manifest tile coords + per-chunk
// height samples. Useful for sizing camera frustums, planning
// new tile placement, and sanity-checking project layouts.
//   --info-zone-extents      one zone's bounding box
//   --info-project-extents   union across every zone in a project
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoExtents(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
