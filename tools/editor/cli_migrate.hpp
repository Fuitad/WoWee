#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the schema-migration handlers - these upgrade older
// on-disk asset versions in-place so newer tooling stops rejecting
// or silently misreading them. All migrators are idempotent:
// already-modern files become no-ops.
//   --migrate-wom       single WOM v1/v2 → WOM3 (single-batch)
//   --migrate-zone      every WOM in a zone directory
//   --migrate-project   every zone in a project directory
//   --migrate-jsondbc   JSON DBC sidecar schema fixes
//
// Returns true if matched; outRc holds the exit code.
bool handleMigrate(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
