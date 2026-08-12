#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the item-export handlers - render items.json as
// human-readable Markdown / CSV reports for design docs and
// pivot-table workflows.
//   --export-zone-items-md       per-zone Markdown by quality
//   --export-project-items-md    project-wide Markdown rollup
//   --export-project-items-csv   project-wide CSV (zone in col 1)
//
// Returns true if matched; outRc holds the exit code.
bool handleItemsExport(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
