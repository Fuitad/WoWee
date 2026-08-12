#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

// Common pattern across cli_gen_texture and cli_gen_mesh handlers:
// "if there's another arg AND it doesn't look like a switch, parse
// it into <var>; otherwise leave <var> at its default". 465+ copies
// across the two files were each writing this 3-line block manually.
//
// Each helper silently no-ops on parse failure so the caller's
// default value is preserved - matches the prior try/catch
// behavior exactly.

inline bool parseOptArg(int& i, int argc, char** argv) {
    return i + 1 < argc && argv[i + 1][0] != '-';
}

inline void parseOptInt(int& i, int argc, char** argv, int& value) {
    if (parseOptArg(i, argc, argv)) {
        try { value = std::stoi(argv[++i]); } catch (...) {}
    }
}

inline void parseOptFloat(int& i, int argc, char** argv, float& value) {
    if (parseOptArg(i, argc, argv)) {
        try { value = std::stof(argv[++i]); } catch (...) {}
    }
}

inline void parseOptUint(int& i, int argc, char** argv, uint32_t& value) {
    if (parseOptArg(i, argc, argv)) {
        try { value = static_cast<uint32_t>(std::stoul(argv[++i])); }
        catch (...) {}
    }
}

// Common --json-output flag pattern: every --info-* / --validate-*
// handler (~50 sites across the editor) writes the same three lines
// to detect and consume an optional `--json` follower. Hoisted here
// so future handlers can do `bool jsonOut = consumeJsonFlag(i, argc, argv);`
// instead of the open-coded peek-and-advance.
inline bool consumeJsonFlag(int& i, int argc, char** argv) {
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--json") == 0) {
        ++i;
        return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
