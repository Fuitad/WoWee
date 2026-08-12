#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the four audit / dep-check handlers:
//   --validate-zone-pack
//   --validate-project-packs
//   --info-zone-deps
//   --info-project-deps
//
// Returns true if matched; outRc holds the exit code. Returns
// false if no match - caller should continue its dispatch chain.
bool handleAudits(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
