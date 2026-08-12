#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the four zone/project pack-orchestrator handlers:
//   --gen-zone-texture-pack
//   --gen-zone-mesh-pack
//   --gen-zone-starter-pack
//   --gen-project-starter-pack
//
// Each fans out to per-asset commands via subprocess, so this
// module has no dependency on the procedural generators it calls.
//
// Returns true if matched; outRc holds the exit code. Returns
// false if no match - caller should continue its dispatch chain.
bool handleZonePacks(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
