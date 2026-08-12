#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the audio-config audit handlers - print the
// music / day-ambience / night-ambience / volume settings
// stored in zone.json. Useful for spot-checking that a zone
// is wired up to the right audio assets, and for catching
// zones still missing audio assignment before a release pass.
//   --info-zone-audio     one zone's audio config
//   --info-project-audio  table across every zone in a project
//
// Both support an optional trailing `--json` flag for
// machine-readable reports.
//
// Returns true if matched; outRc holds the exit code.
bool handleInfoAudio(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
