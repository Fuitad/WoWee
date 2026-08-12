#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the WOM model inspection handlers:
//   --info                (bare WOM summary)
//   --info-batches        (per-batch material info)
//   --info-textures       (texture path list)
//   --info-doodads        (doodad set / instance list)
//   --info-attachments    } combined handler under the hood -
//   --info-particles      } same M2 load + skin merge,
//   --info-sequences      } different sub-array iteration
//   --info-bones          (bone hierarchy)
//   --export-bones-dot    (Graphviz DOT output)
//
// Returns true if matched; outRc holds the exit code.
bool handleWomInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
