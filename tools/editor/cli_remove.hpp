#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the remove-* by-index handlers - strip a single
// entry out of a zone's creatures/objects/quests/items list
// by 0-based index. All four use bounds-checked load-erase-save
// and report what was removed for audit trails.
//   --remove-creature
//   --remove-object
//   --remove-quest
//   --remove-item
//
// Returns true if matched; outRc holds the exit code.
bool handleRemove(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
