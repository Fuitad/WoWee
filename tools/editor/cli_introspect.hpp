#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the CLI introspection / discoverability handlers -
// auto-discover commands by parsing printUsage's output so the
// surface stays self-describing as new flags are added. Useful
// for shell completion, IDE plugins, and 'is there a flag for X?'
// search workflows.
//   --list-commands         flat sorted/deduped flag list
//   --info-cli-stats        per-category counts (--info-* / --validate-*)
//   --info-cli-categories   per-category command listing
//   --info-cli-help <q>     substring search through help text
//   --gen-completion <bash|zsh>   re-exec'd at completion time
//
// Returns true if matched; outRc holds the exit code.
bool handleIntrospect(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
