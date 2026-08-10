#include "cli_item_materials_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_item_materials.hpp"
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

std::string stripWmatExt(std::string base) {
    stripExt(base, ".wmat");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeItemMaterial& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeItemMaterialLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wmat\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeItemMaterial& c,
                     const std::string& base) {
    std::printf("Wrote %s.wmat\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  materials : %zu\n", c.entries.size());
}

int handleGenArmor(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ArmorMaterials";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmatExt(base);
    auto c = wowee::pipeline::WoweeItemMaterialLoader::makeArmor(name);
    if (!saveOrError(c, base, "gen-mat")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWeapon(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WeaponMaterials";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmatExt(base);
    auto c = wowee::pipeline::WoweeItemMaterialLoader::makeWeapon(name);
    if (!saveOrError(c, base, "gen-mat-weapon")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMagical(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MagicalMaterials";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmatExt(base);
    auto c = wowee::pipeline::WoweeItemMaterialLoader::makeMagical(name);
    if (!saveOrError(c, base, "gen-mat-magical")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendMaterialFlagNames(uint32_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeItemMaterial;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::IsBreakable)    add("IsBreakable");
    if (flags & F::IsMagical)      add("IsMagical");
    if (flags & F::IsFlammable)    add("IsFlammable");
    if (flags & F::IsConductive)   add("IsConductive");
    if (flags & F::IsHolyCharged)  add("IsHolyCharged");
    if (flags & F::IsCursed)       add("IsCursed");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWmatExt(base);
    if (!wowee::pipeline::WoweeItemMaterialLoader::exists(base)) {
        std::fprintf(stderr, "WMAT not found: %s.wmat\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemMaterialLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmat"] = base + ".wmat";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string flagNames;
            appendMaterialFlagNames(e.materialFlags, flagNames);
            arr.push_back({
                {"materialId", e.materialId},
                {"name", e.name},
                {"description", e.description},
                {"materialKind", e.materialKind},
                {"materialKindName", wowee::pipeline::WoweeItemMaterial::materialKindName(e.materialKind)},
                {"weightCategory", e.weightCategory},
                {"weightCategoryName", wowee::pipeline::WoweeItemMaterial::weightCategoryName(e.weightCategory)},
                {"foleySoundId", e.foleySoundId},
                {"impactSoundId", e.impactSoundId},
                {"materialFlags", e.materialFlags},
                {"materialFlagsLabels", flagNames},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMAT: %s.wmat\n", base.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  materials : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind        weight   foley  impact   flags                            name\n");
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendMaterialFlagNames(e.materialFlags, flagNames);
        std::printf("  %4u    %-9s   %-6s   %5u   %5u   %-30s   %s\n",
                    e.materialId,
                    wowee::pipeline::WoweeItemMaterial::materialKindName(e.materialKind),
                    wowee::pipeline::WoweeItemMaterial::weightCategoryName(e.weightCategory),
                    e.foleySoundId, e.impactSoundId,
                    flagNames.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWmatExt(base);
    if (!wowee::pipeline::WoweeItemMaterialLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmat-json: WMAT not found: %s.wmat\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemMaterialLoader::load(base);
    if (outPath.empty()) outPath = base + ".wmat.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendMaterialFlagNames(e.materialFlags, flagNames);
        nlohmann::json je;
        je["materialId"] = e.materialId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["materialKind"] = e.materialKind;
        je["materialKindName"] =
            wowee::pipeline::WoweeItemMaterial::materialKindName(e.materialKind);
        je["weightCategory"] = e.weightCategory;
        je["weightCategoryName"] =
            wowee::pipeline::WoweeItemMaterial::weightCategoryName(e.weightCategory);
        je["foleySoundId"] = e.foleySoundId;
        je["impactSoundId"] = e.impactSoundId;
        je["materialFlags"] = e.materialFlags;
        je["materialFlagsLabels"] = flagNames;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wmat-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  materials : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseMaterialKindToken(const nlohmann::json& jv,
                               uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeItemMaterial::Hide)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "cloth")    return wowee::pipeline::WoweeItemMaterial::Cloth;
        if (s == "leather")  return wowee::pipeline::WoweeItemMaterial::Leather;
        if (s == "mail")     return wowee::pipeline::WoweeItemMaterial::Mail;
        if (s == "plate")    return wowee::pipeline::WoweeItemMaterial::Plate;
        if (s == "wood")     return wowee::pipeline::WoweeItemMaterial::Wood;
        if (s == "stone")    return wowee::pipeline::WoweeItemMaterial::Stone;
        if (s == "metal")    return wowee::pipeline::WoweeItemMaterial::Metal;
        if (s == "liquid")   return wowee::pipeline::WoweeItemMaterial::Liquid;
        if (s == "organic")  return wowee::pipeline::WoweeItemMaterial::Organic;
        if (s == "crystal")  return wowee::pipeline::WoweeItemMaterial::Crystal;
        if (s == "ethereal") return wowee::pipeline::WoweeItemMaterial::Ethereal;
        if (s == "hide")     return wowee::pipeline::WoweeItemMaterial::Hide;
    }
    return fallback;
}

uint8_t parseWeightCategoryToken(const nlohmann::json& jv,
                                 uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeItemMaterial::Heavy)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "light")  return wowee::pipeline::WoweeItemMaterial::Light;
        if (s == "medium") return wowee::pipeline::WoweeItemMaterial::Medium;
        if (s == "heavy")  return wowee::pipeline::WoweeItemMaterial::Heavy;
    }
    return fallback;
}

uint32_t parseMaterialFlagsField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeItemMaterial;
    if (jv.is_number_integer() || jv.is_number_unsigned())
        return jv.get<uint32_t>();
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        uint32_t out = 0;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find('|', pos);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(pos, end - pos);
            for (auto& ch : tok) ch = static_cast<char>(std::tolower(ch));
            if (tok == "isbreakable")        out |= F::IsBreakable;
            else if (tok == "ismagical")     out |= F::IsMagical;
            else if (tok == "isflammable")   out |= F::IsFlammable;
            else if (tok == "isconductive")  out |= F::IsConductive;
            else if (tok == "isholycharged") out |= F::IsHolyCharged;
            else if (tok == "iscursed")      out |= F::IsCursed;
            pos = end + 1;
        }
        return out;
    }
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wmat-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmat-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeItemMaterial c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItemMaterial::Entry e;
            if (je.contains("materialId"))   e.materialId = je["materialId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeItemMaterial::Cloth;
            if (je.contains("materialKind"))
                kind = parseMaterialKindToken(je["materialKind"], kind);
            else if (je.contains("materialKindName"))
                kind = parseMaterialKindToken(je["materialKindName"], kind);
            e.materialKind = kind;
            uint8_t weight = wowee::pipeline::WoweeItemMaterial::Light;
            if (je.contains("weightCategory"))
                weight = parseWeightCategoryToken(je["weightCategory"], weight);
            else if (je.contains("weightCategoryName"))
                weight = parseWeightCategoryToken(je["weightCategoryName"], weight);
            e.weightCategory = weight;
            if (je.contains("foleySoundId"))   e.foleySoundId = je["foleySoundId"].get<uint32_t>();
            if (je.contains("impactSoundId"))  e.impactSoundId = je["impactSoundId"].get<uint32_t>();
            if (je.contains("materialFlags"))
                e.materialFlags = parseMaterialFlagsField(je["materialFlags"]);
            else if (je.contains("materialFlagsLabels"))
                e.materialFlags = parseMaterialFlagsField(je["materialFlagsLabels"]);
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) {
        outBase = jsonPath;
        const std::string suffix1 = ".wmat.json";
        const std::string suffix2 = ".json";
        if (outBase.size() >= suffix1.size() &&
            outBase.compare(outBase.size() - suffix1.size(),
                            suffix1.size(), suffix1) == 0) {
            outBase.resize(outBase.size() - suffix1.size());
        } else if (outBase.size() >= suffix2.size() &&
                   outBase.compare(outBase.size() - suffix2.size(),
                                   suffix2.size(), suffix2) == 0) {
            outBase.resize(outBase.size() - suffix2.size());
        }
    }
    outBase = stripWmatExt(outBase);
    if (!wowee::pipeline::WoweeItemMaterialLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmat-json: failed to save %s.wmat\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmat\n", outBase.c_str());
    std::printf("  catalog   : %s\n", c.name.c_str());
    std::printf("  materials : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWmatExt(base);
    if (!wowee::pipeline::WoweeItemMaterialLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmat: WMAT not found: %s.wmat\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemMaterialLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    constexpr uint32_t kKnownFlagMask =
        wowee::pipeline::WoweeItemMaterial::IsBreakable |
        wowee::pipeline::WoweeItemMaterial::IsMagical |
        wowee::pipeline::WoweeItemMaterial::IsFlammable |
        wowee::pipeline::WoweeItemMaterial::IsConductive |
        wowee::pipeline::WoweeItemMaterial::IsHolyCharged |
        wowee::pipeline::WoweeItemMaterial::IsCursed;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.materialId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.materialId == 0)
            errors.push_back(ctx + ": materialId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.materialKind > wowee::pipeline::WoweeItemMaterial::Hide) {
            errors.push_back(ctx + ": materialKind " +
                std::to_string(e.materialKind) + " not in 0..11");
        }
        if (e.weightCategory > wowee::pipeline::WoweeItemMaterial::Heavy) {
            errors.push_back(ctx + ": weightCategory " +
                std::to_string(e.weightCategory) + " not in 0..2");
        }
        if (e.materialFlags & ~kKnownFlagMask) {
            warnings.push_back(ctx +
                ": materialFlags has bits outside known mask " +
                "(0x" + std::to_string(e.materialFlags & ~kKnownFlagMask) +
                ") — engine will ignore unknown flags");
        }
        // Contradictory flag combos.
        if ((e.materialFlags & wowee::pipeline::WoweeItemMaterial::IsHolyCharged) &&
            (e.materialFlags & wowee::pipeline::WoweeItemMaterial::IsCursed)) {
            warnings.push_back(ctx +
                ": both IsHolyCharged and IsCursed flags set — "
                "engine will pick one (typically IsCursed wins)");
        }
        // Plate kind is canonically heavy. Cloth is light.
        if (e.materialKind == wowee::pipeline::WoweeItemMaterial::Plate &&
            e.weightCategory != wowee::pipeline::WoweeItemMaterial::Heavy) {
            warnings.push_back(ctx +
                ": Plate kind with weightCategory=" +
                wowee::pipeline::WoweeItemMaterial::weightCategoryName(e.weightCategory) +
                " — plate armor is canonically heavy");
        }
        if (e.materialKind == wowee::pipeline::WoweeItemMaterial::Cloth &&
            e.weightCategory != wowee::pipeline::WoweeItemMaterial::Light) {
            warnings.push_back(ctx +
                ": Cloth kind with weightCategory=" +
                wowee::pipeline::WoweeItemMaterial::weightCategoryName(e.weightCategory) +
                " — cloth is canonically light");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.materialId) {
                errors.push_back(ctx + ": duplicate materialId");
                break;
            }
        }
        idsSeen.push_back(e.materialId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmat", base, errors, warnings);
    std::printf("validate-wmat: %s.wmat\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu materials, all materialIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleItemMaterialsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-mat") == 0 && i + 1 < argc) {
        outRc = handleGenArmor(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mat-weapon") == 0 && i + 1 < argc) {
        outRc = handleGenWeapon(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mat-magical") == 0 && i + 1 < argc) {
        outRc = handleGenMagical(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmat") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmat") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmat-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmat-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
