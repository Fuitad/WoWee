#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the strip-* cleanup handlers - remove derived
// outputs (.glb / .obj / .stl / .html / .dot / .csv / .png /
// ZONE.md / DEPS.md) leaving only source files. Useful before
// --pack-wcp so archives don't carry redundant exports, or
// before committing to git so derived blobs don't bloat
// history. Both honor --dry-run for safe previews.
//   --strip-zone     clean one zone's top-level derived files
//   --strip-project  walk every zone in a project, per-zone report
//
// Returns true if matched; outRc holds the exit code.
bool handleStrip(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
