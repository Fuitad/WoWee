#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the project-level action handlers - top-level
// project utilities that don't fit any of the more specific
// modules. Each operates on a project directory rather than
// a single zone.
//   --copy-project        recursive copy of an entire project tree
//   --zone-summary        one-shot validate + content rollup for a zone
//   --bench-bake-project  per-zone WHM/WOT load timing benchmark
//
// Returns true if matched; outRc holds the exit code.
bool handleProjectActions(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
