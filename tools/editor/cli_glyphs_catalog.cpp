#include "cli_glyphs_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_glyphs.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeGlyph& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgly\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  glyphs  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterGlyphs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgly");
    auto c = wowee::pipeline::WoweeGlyphLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeGlyphLoader>(c, base, "gen-glyphs", ".wgly")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorGlyphs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgly");
    auto c = wowee::pipeline::WoweeGlyphLoader::makeWarrior(name);
    if (!saveOrError<wowee::pipeline::WoweeGlyphLoader>(c, base, "gen-glyphs-warrior", ".wgly")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenUniversal(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UniversalGlyphs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgly");
    auto c = wowee::pipeline::WoweeGlyphLoader::makeUniversal(name);
    if (!saveOrError<wowee::pipeline::WoweeGlyphLoader>(c, base, "gen-glyphs-universal", ".wgly")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgly");
    if (!wowee::pipeline::WoweeGlyphLoader::exists(base)) {
        return reportMissing("WGLY", base, ".wgly");
    }
    auto c = wowee::pipeline::WoweeGlyphLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgly"] = base + ".wgly";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"glyphId", e.glyphId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"glyphType", e.glyphType},
                {"glyphTypeName", wowee::pipeline::WoweeGlyph::glyphTypeName(e.glyphType)},
                {"spellId", e.spellId},
                {"itemId", e.itemId},
                {"classMask", e.classMask},
                {"requiredLevel", e.requiredLevel},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGLY: %s.wgly\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  glyphs  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   type    spell   item    classMask    lvl  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-5s   %5u   %5u   0x%08x   %3u  %s\n",
                    e.glyphId,
                    wowee::pipeline::WoweeGlyph::glyphTypeName(e.glyphType),
                    e.spellId, e.itemId, e.classMask,
                    e.requiredLevel, e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each glyph emits all 8 scalar fields plus
    // a dual int + name form for glyphType so hand-edits can
    // use either representation.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgly");
    if (outPath.empty()) outPath = base + ".wgly.json";
    if (!wowee::pipeline::WoweeGlyphLoader::exists(base)) {
        return reportMissing("export-wgly-json", "WGLY", base, ".wgly");
    }
    auto c = wowee::pipeline::WoweeGlyphLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"glyphId", e.glyphId},
            {"name", e.name},
            {"description", e.description},
            {"iconPath", e.iconPath},
            {"glyphType", e.glyphType},
            {"glyphTypeName", wowee::pipeline::WoweeGlyph::glyphTypeName(e.glyphType)},
            {"spellId", e.spellId},
            {"itemId", e.itemId},
            {"classMask", e.classMask},
            {"requiredLevel", e.requiredLevel},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgly-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wgly\n", base.c_str());
    std::printf("  glyphs : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgly");
    outBase = cli::withoutExt(outBase, ".wgly");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgly-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgly-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto glyphTypeFromName = [](const std::string& s) -> uint8_t {
        if (s == "major") return wowee::pipeline::WoweeGlyph::Major;
        if (s == "minor") return wowee::pipeline::WoweeGlyph::Minor;
        if (s == "prime") return wowee::pipeline::WoweeGlyph::Prime;
        return wowee::pipeline::WoweeGlyph::Major;
    };
    wowee::pipeline::WoweeGlyph c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGlyph::Entry e;
            e.glyphId = je.value("glyphId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("glyphType") &&
                je["glyphType"].is_number_integer()) {
                e.glyphType = static_cast<uint8_t>(
                    je["glyphType"].get<int>());
            } else if (je.contains("glyphTypeName") &&
                       je["glyphTypeName"].is_string()) {
                e.glyphType = glyphTypeFromName(
                    je["glyphTypeName"].get<std::string>());
            }
            e.spellId = je.value("spellId", 0u);
            e.itemId = je.value("itemId", 0u);
            e.classMask = je.value("classMask", 0u);
            e.requiredLevel = static_cast<uint16_t>(
                je.value("requiredLevel", 25));
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeGlyphLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgly-json: failed to save %s.wgly\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgly\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  glyphs : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeGlyphLoader>(
        i, argc, argv, "wgly", "WGLY",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.glyphId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.glyphId == 0)
                errors.push_back(ctx + ": glyphId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.spellId == 0)
                errors.push_back(ctx + ": spellId is 0 "
                    "(glyph applies no aura)");
            if (e.glyphType > wowee::pipeline::WoweeGlyph::Prime) {
                errors.push_back(ctx + ": glyphType " +
                    std::to_string(e.glyphType) + " not in 0..2");
            }
            if (e.classMask == 0) {
                errors.push_back(ctx +
                    ": classMask=0 (no class can use this glyph)");
            }
            if (e.itemId == 0) {
                warnings.push_back(ctx + ": itemId=0 "
                    "(no inscribed item — glyph can't be taught)");
            }
            // WotLK glyphs unlock at character level 25 (minor),
            // 50 (major), 70 (major), 80 (prime). Anything below
            // 15 is suspicious.
            if (e.requiredLevel != 0 && e.requiredLevel < 15) {
                warnings.push_back(ctx + ": requiredLevel=" +
                    std::to_string(e.requiredLevel) +
                    " below WotLK glyph threshold (25)");
            }
            if (!idsSeen.add(e.glyphId)) errors.push_back(ctx + ": duplicate glyphId");
        }
            return formatted("%zu glyphs, all glyphIds unique", c.entries.size());
        });
}

} // namespace

bool handleGlyphsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-glyphs") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-glyphs-warrior") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-glyphs-universal") == 0 && i + 1 < argc) {
        outRc = handleGenUniversal(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgly") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgly") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgly-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgly-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
