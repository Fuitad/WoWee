#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the simple texture-helper handlers - placeholder
// generation and PNG import workflows that complement the
// procedural pattern generators in cli_gen_texture.
//   --gen-texture           solid hex / checker / grid PNG
//   --add-texture-to-zone   import an existing PNG into a zone
//
// Returns true if matched; outRc holds the exit code.
bool handleTextureHelpers(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
