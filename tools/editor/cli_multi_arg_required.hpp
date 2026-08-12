#pragma once

#include <cstddef>

namespace wowee {
namespace editor {
namespace cli {

// Companion to kArgRequired for flags that take MORE than one
// positional argument. main.cpp uses this list for the early
// "missing argument" detector - for each entry we check whether
// argv[i+needed] would run off the end and, if so, print the
// synopsis and exit 1 instead of silently dropping into the GUI.
//
// `needed` is the count of *positional* args after the flag.
// `synopsis` is the full message shown to the user; it should
// embed both the flag and the slot list (e.g. "<zoneDir> <x> <y>")
// so a single std::fprintf reads naturally.
struct MultiArgFlag {
    const char* flag;
    int needed;
    const char* synopsis;
};

extern const MultiArgFlag kMultiArgRequired[];
extern const std::size_t kMultiArgRequiredSize;

} // namespace cli
} // namespace editor
} // namespace wowee
