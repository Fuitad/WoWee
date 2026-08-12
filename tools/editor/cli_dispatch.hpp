#pragma once

namespace wowee {
namespace editor {
namespace cli {

// Try every registered handler family in turn against argv[i].
// Returns true if a handler claimed the flag (sets outRc); the
// caller should return outRc immediately. Returns false if no
// handler matched - the caller falls through to its own
// inline-handler chain (for handlers needing extra parameters
// like dataPath, or for the GUI-state args --data / --adt).
//
// The handler-family table lives in cli_dispatch.cpp; adding a
// new module means adding one #include + one row there, no
// touching of main.cpp's argv loop required.
bool tryDispatchAll(int& i, int argc, char** argv, int& outRc);

} // namespace cli
} // namespace editor
} // namespace wowee
