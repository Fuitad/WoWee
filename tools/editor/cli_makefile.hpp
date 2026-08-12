#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the Makefile-generation handlers - emit GNU make
// recipes that rebuild every derived output (.glb / .obj /
// .stl / .html / .csv / .md) from sources via wowee_editor
// flags. Designers can `make -j` to rebuild after editing
// without remembering which CLI flag does what.
//   --gen-makefile           per-zone Makefile inside the zone dir
//   --gen-project-makefile   project-wide Makefile delegating per-zone
//
// Returns true if matched; outRc holds the exit code.
bool handleMakefile(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
