#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the structural validators for INTEROP file formats -
// the formats that flow into and out of wowee from third-party
// tools, where the open-format validators in cli_format_validate
// don't apply. Each does a deep structural check (chunks, CRCs,
// magic numbers, schema constraints) beyond what --info-* shows.
//   --validate-stl       ASCII STL (matches --export-stl output)
//   --validate-png       PNG signature, chunks, CRCs
//   --validate-blp       BLP1/BLP2 header + mip table bounds
//   --validate-jsondbc   JSON DBC sidecar schema + record shape
//
// All four support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleValidateInterop(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
