#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the tree-style content browser handlers - Unix
// `tree`-style hierarchical views of zone and project contents
// (manifest, tiles, creatures, objects, quests, files, baked
// artifacts). Designed for at-a-glance comprehension of what
// lives in a given directory without opening the JSON files.
//   --info-zone-tree     drill into one zone's contents
//   --info-project-tree  bird's-eye view of every zone in a project
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoTree(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
