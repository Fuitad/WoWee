#include "cli_creature_families_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_creature_families.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCreatureFamily& c,
                     const std::string& base) {
    std::printf("Wrote %s.wcef\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  families : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterFamilies";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcef");
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureFamilyLoader>(c, base, "gen-cef", ".wcef")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFerocity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FerocityPets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcef");
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::makeFerocity(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureFamilyLoader>(c, base, "gen-cef-ferocity", ".wcef")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenExotic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ExoticBeastMaster";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcef");
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::makeExotic(name);
    if (!saveOrError<wowee::pipeline::WoweeCreatureFamilyLoader>(c, base, "gen-cef-exotic", ".wcef")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendFoodNames(uint32_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeCreatureFamily;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::Meat)   add("Meat");
    if (flags & F::Fish)   add("Fish");
    if (flags & F::Bread)  add("Bread");
    if (flags & F::Cheese) add("Cheese");
    if (flags & F::Fruit)  add("Fruit");
    if (flags & F::Fungus) add("Fungus");
    if (flags & F::Raw)    add("Raw");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcef");
    if (!wowee::pipeline::WoweeCreatureFamilyLoader::exists(base)) {
        std::fprintf(stderr, "WCEF not found: %s.wcef\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcef"] = base + ".wcef";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string foodNames;
            appendFoodNames(e.petFoodTypes, foodNames);
            arr.push_back({
                {"familyId", e.familyId},
                {"name", e.name},
                {"description", e.description},
                {"familyKind", e.familyKind},
                {"familyKindName", wowee::pipeline::WoweeCreatureFamily::familyKindName(e.familyKind)},
                {"petTalentTree", e.petTalentTree},
                {"petTalentTreeName", wowee::pipeline::WoweeCreatureFamily::petTalentTreeName(e.petTalentTree)},
                {"minLevelForTame", e.minLevelForTame},
                {"skillLine", e.skillLine},
                {"petFoodTypes", e.petFoodTypes},
                {"petFoodTypesLabels", foodNames},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCEF: %s.wcef\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  families : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    kind       tree       tameLvl  skill   foods                          name\n");
    for (const auto& e : c.entries) {
        std::string foodNames;
        appendFoodNames(e.petFoodTypes, foodNames);
        std::printf("  %4u    %-9s  %-9s  %5u    %5u   %-30s %s\n",
                    e.familyId,
                    wowee::pipeline::WoweeCreatureFamily::familyKindName(e.familyKind),
                    wowee::pipeline::WoweeCreatureFamily::petTalentTreeName(e.petTalentTree),
                    e.minLevelForTame,
                    e.skillLine,
                    foodNames.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wcef");
    if (!wowee::pipeline::WoweeCreatureFamilyLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcef-json: WCEF not found: %s.wcef\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::load(base);
    if (outPath.empty()) outPath = base + ".wcef.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string foodNames;
        appendFoodNames(e.petFoodTypes, foodNames);
        nlohmann::json je;
        je["familyId"] = e.familyId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["familyKind"] = e.familyKind;
        je["familyKindName"] =
            wowee::pipeline::WoweeCreatureFamily::familyKindName(e.familyKind);
        je["petTalentTree"] = e.petTalentTree;
        je["petTalentTreeName"] =
            wowee::pipeline::WoweeCreatureFamily::petTalentTreeName(e.petTalentTree);
        je["minLevelForTame"] = e.minLevelForTame;
        je["skillLine"] = e.skillLine;
        je["petFoodTypes"] = e.petFoodTypes;
        je["petFoodTypesLabels"] = foodNames;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wcef-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  families : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseFamilyKindToken(const nlohmann::json& jv,
                             uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCreatureFamily::Exotic)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "beast")     return wowee::pipeline::WoweeCreatureFamily::Beast;
        if (s == "demon")     return wowee::pipeline::WoweeCreatureFamily::Demon;
        if (s == "undead")    return wowee::pipeline::WoweeCreatureFamily::Undead;
        if (s == "elemental") return wowee::pipeline::WoweeCreatureFamily::Elemental;
        if (s == "not-pet" ||
            s == "notpet")    return wowee::pipeline::WoweeCreatureFamily::NotPet;
        if (s == "exotic")    return wowee::pipeline::WoweeCreatureFamily::Exotic;
    }
    return fallback;
}

uint8_t parseTalentTreeToken(const nlohmann::json& jv,
                             uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeCreatureFamily::Cunning)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "none")     return wowee::pipeline::WoweeCreatureFamily::TreeNone;
        if (s == "ferocity") return wowee::pipeline::WoweeCreatureFamily::Ferocity;
        if (s == "tenacity") return wowee::pipeline::WoweeCreatureFamily::Tenacity;
        if (s == "cunning")  return wowee::pipeline::WoweeCreatureFamily::Cunning;
    }
    return fallback;
}

uint32_t parseFoodTypesField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeCreatureFamily;
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
            if (tok == "meat")        out |= F::Meat;
            else if (tok == "fish")   out |= F::Fish;
            else if (tok == "bread")  out |= F::Bread;
            else if (tok == "cheese") out |= F::Cheese;
            else if (tok == "fruit")  out |= F::Fruit;
            else if (tok == "fungus") out |= F::Fungus;
            else if (tok == "raw")    out |= F::Raw;
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
            "import-wcef-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcef-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCreatureFamily c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCreatureFamily::Entry e;
            if (je.contains("familyId"))    e.familyId = je["familyId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeCreatureFamily::Beast;
            if (je.contains("familyKind"))
                kind = parseFamilyKindToken(je["familyKind"], kind);
            else if (je.contains("familyKindName"))
                kind = parseFamilyKindToken(je["familyKindName"], kind);
            e.familyKind = kind;
            uint8_t tree = wowee::pipeline::WoweeCreatureFamily::TreeNone;
            if (je.contains("petTalentTree"))
                tree = parseTalentTreeToken(je["petTalentTree"], tree);
            else if (je.contains("petTalentTreeName"))
                tree = parseTalentTreeToken(je["petTalentTreeName"], tree);
            e.petTalentTree = tree;
            if (je.contains("minLevelForTame"))
                e.minLevelForTame = je["minLevelForTame"].get<uint8_t>();
            if (je.contains("skillLine"))
                e.skillLine = je["skillLine"].get<uint32_t>();
            if (je.contains("petFoodTypes"))
                e.petFoodTypes = parseFoodTypesField(je["petFoodTypes"]);
            else if (je.contains("petFoodTypesLabels"))
                e.petFoodTypes = parseFoodTypesField(je["petFoodTypesLabels"]);
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wcef");
    outBase = cli::withoutExt(outBase, ".wcef");
    if (!wowee::pipeline::WoweeCreatureFamilyLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcef-json: failed to save %s.wcef\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wcef\n", outBase.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  families : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcef");
    if (!wowee::pipeline::WoweeCreatureFamilyLoader::exists(base)) {
        return reportMissing("validate-wcef", "WCEF", base, ".wcef");
    }
    auto c = wowee::pipeline::WoweeCreatureFamilyLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    constexpr uint32_t kKnownFoodMask =
        wowee::pipeline::WoweeCreatureFamily::Meat |
        wowee::pipeline::WoweeCreatureFamily::Fish |
        wowee::pipeline::WoweeCreatureFamily::Bread |
        wowee::pipeline::WoweeCreatureFamily::Cheese |
        wowee::pipeline::WoweeCreatureFamily::Fruit |
        wowee::pipeline::WoweeCreatureFamily::Fungus |
        wowee::pipeline::WoweeCreatureFamily::Raw;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.familyId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.familyId == 0)
            errors.push_back(ctx + ": familyId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.familyKind > wowee::pipeline::WoweeCreatureFamily::Exotic) {
            errors.push_back(ctx + ": familyKind " +
                std::to_string(e.familyKind) + " not in 0..5");
        }
        if (e.petTalentTree > wowee::pipeline::WoweeCreatureFamily::Cunning) {
            errors.push_back(ctx + ": petTalentTree " +
                std::to_string(e.petTalentTree) + " not in 0..3");
        }
        if (e.petFoodTypes & ~kKnownFoodMask) {
            warnings.push_back(ctx +
                ": petFoodTypes has bits outside known mask " +
                "(0x" + std::to_string(e.petFoodTypes & ~kKnownFoodMask) +
                ") — engine will ignore unknown food types");
        }
        // NotPet families should not specify a talent tree —
        // confusing if they do.
        if (e.familyKind == wowee::pipeline::WoweeCreatureFamily::NotPet &&
            e.petTalentTree != wowee::pipeline::WoweeCreatureFamily::TreeNone) {
            warnings.push_back(ctx +
                ": NotPet family with petTalentTree=" +
                wowee::pipeline::WoweeCreatureFamily::petTalentTreeName(e.petTalentTree) +
                " — talent tree is irrelevant for non-pet kinds");
        }
        // Exotic families above level 80 won't be tamable
        // by anyone (level cap).
        if (e.familyKind == wowee::pipeline::WoweeCreatureFamily::Exotic &&
            e.minLevelForTame > 80) {
            warnings.push_back(ctx +
                ": Exotic family with minLevelForTame=" +
                std::to_string(e.minLevelForTame) +
                " > 80 — no hunter can reach this level");
        }
        // Pet kinds with no food types set means they can't
        // be fed — common bug, especially for hand-edited
        // sidecars.
        if ((e.familyKind == wowee::pipeline::WoweeCreatureFamily::Beast ||
             e.familyKind == wowee::pipeline::WoweeCreatureFamily::Exotic) &&
            e.petFoodTypes == 0) {
            warnings.push_back(ctx +
                ": pet-able family with no food types set — "
                "hunter pet will starve, no food will satisfy it");
        }
        if (!idsSeen.add(e.familyId)) errors.push_back(ctx + ": duplicate familyId");
    }
    return cli::reportValidation("wcef", base, jsonOut, errors, warnings,
                                 formatted("%zu families, all familyIds unique", c.entries.size()));
}

} // namespace

bool handleCreatureFamiliesCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-cef") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cef-ferocity") == 0 && i + 1 < argc) {
        outRc = handleGenFerocity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cef-exotic") == 0 && i + 1 < argc) {
        outRc = handleGenExotic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcef") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcef") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcef-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcef-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
