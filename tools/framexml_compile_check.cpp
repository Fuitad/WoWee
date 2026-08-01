// Does the Lua we generate from FrameXML actually compile?
//
// The unit tests check the shape of the output against cases someone thought
// of. This asks Lua itself about every real file, which is a different and
// harsher question — it found an empty function attribute emitting
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
int main(int argc, char** argv) {
    lua_State* L = luaL_newstate();
    int ok = 0, bad = 0, shown = 0;
    for (auto& e : std::filesystem::directory_iterator(argv[1])) {
        if (e.path().extension() != ".xml") continue;
        std::ifstream f(e.path()); std::stringstream ss; ss << f.rdbuf();
        wowee::ui::XmlNode root; std::string err;
        if (!wowee::ui::parseXml(ss.str(), root, err)) continue;
        auto r = wowee::ui::emitFrameXml(root);
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
    printf("emitted Lua compiles: %d   fails: %d\n", ok, bad);
    return 0;
}
