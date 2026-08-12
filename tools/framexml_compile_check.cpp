// Does the Lua we generate from FrameXML actually compile?
//
// The unit tests check the shape of the output against cases someone thought
// of. This asks Lua itself about every real file, which is a different and
// harsher question - it found an empty function attribute emitting
// SetScript("X", ), and temporaries running into Lua's limit of 200 locals per
// function. Neither degrades: the whole chunk refuses to compile.
//
// Build (needs the project's lua51 built):
//   g++ -std=c++20 -Iinclude -Iextern/lua-5.1.5/src \
//       tools/framexml_compile_check.cpp src/ui/xml_parser.cpp \
//       src/ui/framexml_emitter.cpp build/lib/liblua51.a -o /tmp/fxcheck
//   /tmp/fxcheck Data/interface/framexml
// Emits every FrameXML file and asks Lua whether the result compiles.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include "ui/xml_parser.hpp"
#include "ui/framexml_emitter.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
int main(int argc, char** argv) {
    // Run from the build with no argument and it looks where the game data
    // normally sits, so this can be a test rather than something remembered.
    const char* dir = (argc > 1) ? argv[1] : "Data/interface/framexml";
    if (!std::filesystem::is_directory(dir)) {
        printf("no FrameXML at %s; nothing to check\n", dir);
        return 77;  // ctest reads this as skipped, not passed.
    }
    // Somewhere to read the result, because "it compiles" and "it says what it
    // should" are different questions and only the first was ever asked.
    const char* dumpDir = std::getenv("WOWEE_FRAMEXML_EMIT_DIR");
    if (dumpDir && *dumpDir) {
        std::error_code ec;
        std::filesystem::create_directories(dumpDir, ec);
    }
    lua_State* L = luaL_newstate();
    int ok = 0, bad = 0, shown = 0, unparsed = 0, unbuilt = 0;
    // Recursive so this works on the addons directory too, where each addon
    // is a folder of its own. FrameXML is flat and unaffected.
    for (auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().extension() != ".xml") continue;
        std::ifstream f(e.path()); std::stringstream ss; ss << f.rdbuf();
        wowee::ui::XmlNode root; std::string err;
        // Counted, not skipped. A file the reader cannot get through never
        // reaches the compiler at all, so passing over it quietly reports a
        // clean run on files that in fact never loaded.
        if (!wowee::ui::parseXml(ss.str(), root, err)) {
            ++unparsed;
            printf("  UNPARSED %-30s %s\n", e.path().filename().string().c_str(),
                   err.c_str());
            continue;
        }
        auto r = wowee::ui::emitFrameXml(root);
        if (dumpDir && *dumpDir) {
            std::ofstream out(std::filesystem::path(dumpDir) /
                              (e.path().filename().string() + ".lua"));
            if (out) out << r.lua;
        }
        for (const auto& w : r.warnings) {
            if (w.find("not a known frame type") != std::string::npos) {
                printf("  UNBUILT  %-30s %s\n",
                       e.path().filename().string().c_str(), w.c_str());
                ++unbuilt;
            }
        }
        if (r.lua.empty()) { ++ok; continue; }
        std::string chunk = "local __WoweeTemplates={} "
                            "local function __WoweeMissingTemplate() end\n" + r.lua;
        if (luaL_loadbuffer(L, chunk.c_str(), chunk.size(), "=chunk") == 0) { ++ok; }
        else {
            ++bad;
            if (shown++ < 6)
                printf("  FAIL %-34s %s\n", e.path().filename().string().c_str(),
                       lua_tostring(L, -1));
        }
        lua_settop(L, 0);
    }
    printf("emitted Lua compiles: %d   fails: %d   unparsed XML: %d   "
           "unbuilt elements: %d\n", ok, bad, unparsed, unbuilt);
    // A file that will not compile is lost whole, so this is a failure rather
    // than a number to read past.
    return (bad > 0 || unparsed > 0) ? 1 : 0;
}
