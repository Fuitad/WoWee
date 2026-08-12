#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the proprietary-format inspection handlers - each
// reads a Blizzard-format file and prints its structure:
//   --info-png    --info-blp
//   --info-m2     --info-wmo
//   --info-adt    --info-jsondbc
//
// Returns true if matched; outRc holds the exit code.
bool handleFormatInfo(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
