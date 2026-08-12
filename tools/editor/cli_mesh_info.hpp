#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the mesh-aggregate info handlers - surface
// vertex/triangle/bone/animation totals across WOM/WOB
// files, plus per-mesh detail views.
//   --info-zone-models-total      aggregate WOM/WOB across a zone
//   --list-zone-meshes-detail     per-mesh table sorted by tris
//   --info-mesh                   single-WOM detail dump
//   --info-mesh-storage-budget    per-category byte breakdown of a WOM
//   --info-project-models-total   aggregate across every zone in a project
//
// All five support --json output.
//
// Returns true if matched; outRc holds the exit code.
bool handleMeshInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
