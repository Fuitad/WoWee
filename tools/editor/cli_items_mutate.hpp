#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the items.json mutation handlers - edit / duplicate /
// transfer item records in a zone's items.json.
//   --set-item          edit fields on an existing item by id or #idx
//   --copy-zone-items   copy items between zones (replace or merge)
//   --clone-item        duplicate an item with a fresh id
//
// Returns true if matched; outRc holds the exit code.
bool handleItemsMutate(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
