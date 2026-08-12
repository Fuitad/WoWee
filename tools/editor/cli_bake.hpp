#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the zone & project mesh-bake handlers - these stitch
// every WHM heightfield + WOM/WOB asset in a zone into a single
// 3D file (.obj / .stl / .glb) for external DCC import.
//   --bake-zone-glb       --bake-zone-stl       --bake-zone-obj
//   --bake-project-obj    --bake-project-stl    --bake-project-glb
//
// Returns true if matched; outRc holds the exit code.
bool handleBake(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
