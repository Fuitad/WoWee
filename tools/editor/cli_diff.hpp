#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the diff handlers - each compares two files of the
// same format and reports differences:
//   --diff-wcp        --diff-zone
//   --diff-glb        --diff-wom
//   --diff-wob        --diff-whm
//   --diff-woc        --diff-jsondbc
//   --diff-extract    --diff-checksum
//
// Returns true if matched; outRc holds the exit code.
bool handleDiff(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
