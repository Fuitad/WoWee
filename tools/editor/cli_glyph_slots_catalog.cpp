#include "cli_glyph_slots_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_glyph_slots.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
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

bool saveOrError(const wowee::pipeline::WoweeGlyphSlot& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeGlyphSlotLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wgfs\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeGlyphSlot& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgfs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterGlyphSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgfs");
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-gfs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWotlk(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WotlkGlyphSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgfs");
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::makeWotlk(name);
    if (!saveOrError(c, base, "gen-gfs-wotlk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCata(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CataclysmGlyphSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgfs");
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::makeCata(name);
    if (!saveOrError(c, base, "gen-gfs-cata")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgfs");
    if (!wowee::pipeline::WoweeGlyphSlotLoader::exists(base)) {
        std::fprintf(stderr, "WGFS not found: %s.wgfs\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgfs"] = base + ".wgfs";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"slotId", e.slotId},
                {"name", e.name},
                {"description", e.description},
                {"slotKind", e.slotKind},
                {"slotKindName", wowee::pipeline::WoweeGlyphSlot::slotKindName(e.slotKind)},
                {"displayOrder", e.displayOrder},
                {"minLevelToUnlock", e.minLevelToUnlock},
                {"requiredClassMask", e.requiredClassMask},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGFS: %s.wgfs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind     ord   lvl    classMask    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %-7s   %u    %3u   0x%08x   %s\n",
                    e.slotId,
                    wowee::pipeline::WoweeGlyphSlot::slotKindName(e.slotKind),
                    e.displayOrder,
                    e.minLevelToUnlock,
                    e.requiredClassMask,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgfs");
    if (!wowee::pipeline::WoweeGlyphSlotLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wgfs-json: WGFS not found: %s.wgfs\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::load(base);
    if (outPath.empty()) outPath = base + ".wgfs.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["slotId"] = e.slotId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["slotKind"] = e.slotKind;
        je["slotKindName"] =
            wowee::pipeline::WoweeGlyphSlot::slotKindName(e.slotKind);
        je["displayOrder"] = e.displayOrder;
        je["minLevelToUnlock"] = e.minLevelToUnlock;
        je["requiredClassMask"] = e.requiredClassMask;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wgfs-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseSlotKindToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeGlyphSlot::Prime)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "major") return wowee::pipeline::WoweeGlyphSlot::Major;
        if (s == "minor") return wowee::pipeline::WoweeGlyphSlot::Minor;
        if (s == "prime") return wowee::pipeline::WoweeGlyphSlot::Prime;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wgfs-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wgfs-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeGlyphSlot c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGlyphSlot::Entry e;
            if (je.contains("slotId"))      e.slotId = je["slotId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeGlyphSlot::Major;
            if (je.contains("slotKind"))
                kind = parseSlotKindToken(je["slotKind"], kind);
            else if (je.contains("slotKindName"))
                kind = parseSlotKindToken(je["slotKindName"], kind);
            e.slotKind = kind;
            if (je.contains("displayOrder"))
                e.displayOrder = je["displayOrder"].get<uint8_t>();
            if (je.contains("minLevelToUnlock"))
                e.minLevelToUnlock = je["minLevelToUnlock"].get<uint8_t>();
            if (je.contains("requiredClassMask"))
                e.requiredClassMask = je["requiredClassMask"].get<uint32_t>();
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgfs");
    outBase = cli::withoutExt(outBase, ".wgfs");
    if (!wowee::pipeline::WoweeGlyphSlotLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgfs-json: failed to save %s.wgfs\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgfs\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgfs");
    if (!wowee::pipeline::WoweeGlyphSlotLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wgfs: WGFS not found: %s.wgfs\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGlyphSlotLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.slotId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.slotId == 0)
            errors.push_back(ctx + ": slotId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.slotKind > wowee::pipeline::WoweeGlyphSlot::Prime) {
            errors.push_back(ctx + ": slotKind " +
                std::to_string(e.slotKind) + " not in 0..2");
        }
        if (e.requiredClassMask == 0) {
            errors.push_back(ctx +
                ": requiredClassMask is 0 — no class can use "
                "this slot");
        }
        if (e.minLevelToUnlock > 80) {
            warnings.push_back(ctx +
                ": minLevelToUnlock " +
                std::to_string(e.minLevelToUnlock) +
                " > 80 — slot will never unlock at WotLK cap");
        }
        if (e.displayOrder > 4) {
            warnings.push_back(ctx +
                ": displayOrder " +
                std::to_string(e.displayOrder) +
                " > 4 — UI typically shows only 3-4 slots per kind");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.slotId) {
                errors.push_back(ctx + ": duplicate slotId");
                break;
            }
        }
        idsSeen.push_back(e.slotId);
    }
    // Cross-entry check: detect overlapping (kind,order)
    // pairs within the same class — two slots claiming the
    // same UI position would collide.
    for (size_t a = 0; a < c.entries.size(); ++a) {
        for (size_t b = a + 1; b < c.entries.size(); ++b) {
            const auto& ea = c.entries[a];
            const auto& eb = c.entries[b];
            if (ea.slotKind != eb.slotKind) continue;
            if (ea.displayOrder != eb.displayOrder) continue;
            if ((ea.requiredClassMask & eb.requiredClassMask) == 0)
                continue;
            warnings.push_back(
                "entries " + std::to_string(a) + " (" +
                ea.name + ") and " + std::to_string(b) +
                " (" + eb.name + ") share " +
                wowee::pipeline::WoweeGlyphSlot::slotKindName(ea.slotKind) +
                " kind + displayOrder " +
                std::to_string(ea.displayOrder) +
                " for overlapping classMask — UI position collision");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wgfs", base, errors, warnings);
    std::printf("validate-wgfs: %s.wgfs\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu slots, all slotIds unique, no UI overlaps\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleGlyphSlotsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-gfs") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gfs-wotlk") == 0 && i + 1 < argc) {
        outRc = handleGenWotlk(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gfs-cata") == 0 && i + 1 < argc) {
        outRc = handleGenCata(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgfs") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgfs") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgfs-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgfs-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
