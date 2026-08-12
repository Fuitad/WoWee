#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the cross-reference / content sanity-check handlers.
// Each goes deeper than --validate (which only checks open-
// format file presence) - they verify that quest NPC IDs
// resolve to creatures.json entries, model paths resolve to
// on-disk files, spawn positions sit inside the zone's tile
// bounds, etc. All four support --json for CI pipelines.
//   --check-zone-refs        single-zone ref integrity
//   --check-zone-content     single-zone content sanity
//   --check-project-content  project-wide content sanity
//   --check-project-refs     project-wide ref integrity rollup
//
// Returns true if matched; outRc holds the exit code.
bool handleCheck(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
