#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the add-* append-by-coords handlers - append a
// single creature spawn / object placement / item record to a
// zone's JSON files. Useful for batch-populating zones via
// shell scripts without launching the GUI placement tool.
//   --add-object       <zone> <m2|wmo> <gamePath> <x> <y> <z> [scale]
//   --add-creature     <zone> <name> <x> <y> <z> [displayId] [level]
//   --add-item         <zone> <name> [id] [quality] [displayId] [itemLevel]
//
// Returns true if matched; outRc holds the exit code.
bool handleAdd(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
