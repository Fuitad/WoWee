#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Dispatch the four --gen-audio-* / --gen-zone-audio-pack handlers.
//
// Returns true if argv[i] matched one of these flags; in that case
// outRc holds the exit code (0 success, non-zero failure) and main()
// should `return outRc` immediately. Returns false if no match -
// caller should continue its dispatch chain.
//
// On match, advances `i` past the consumed arguments (same semantics
// as the in-line handlers it replaces).
bool handleGenAudio(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
