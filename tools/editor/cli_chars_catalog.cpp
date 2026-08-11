#include "cli_chars_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_chars.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeChars& c,
                     const std::string& base) {
    std::printf("Wrote %s.wchc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  classes : %zu  races : %zu  outfits : %zu\n",
                c.classes.size(), c.races.size(), c.outfits.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterChars";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchc");
    auto c = wowee::pipeline::WoweeCharsLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCharsLoader>(c, base, "gen-chars", ".wchc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAlliance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceChars";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchc");
    auto c = wowee::pipeline::WoweeCharsLoader::makeAlliance(name);
    if (!saveOrError<wowee::pipeline::WoweeCharsLoader>(c, base, "gen-chars-alliance", ".wchc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAllRaces(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllRacesChars";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchc");
    auto c = wowee::pipeline::WoweeCharsLoader::makeAllRaces(name);
    if (!saveOrError<wowee::pipeline::WoweeCharsLoader>(c, base, "gen-chars-allraces", ".wchc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchc");
    if (!wowee::pipeline::WoweeCharsLoader::exists(base)) {
        std::fprintf(stderr, "WCHC not found: %s.wchc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCharsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wchc"] = base + ".wchc";
        j["name"] = c.name;
        j["classCount"] = c.classes.size();
        j["raceCount"] = c.races.size();
        j["outfitCount"] = c.outfits.size();
        nlohmann::json ca = nlohmann::json::array();
        for (const auto& cls : c.classes) {
            ca.push_back({
                {"classId", cls.classId},
                {"name", cls.name},
                {"iconPath", cls.iconPath},
                {"powerType", cls.powerType},
                {"powerTypeName", wowee::pipeline::WoweeChars::powerTypeName(cls.powerType)},
                {"displayPower", cls.displayPower},
                {"baseHealth", cls.baseHealth},
                {"baseHealthPerLevel", cls.baseHealthPerLevel},
                {"basePower", cls.basePower},
                {"basePowerPerLevel", cls.basePowerPerLevel},
                {"factionAvailability", cls.factionAvailability},
            });
        }
        j["classes"] = ca;
        nlohmann::json ra = nlohmann::json::array();
        for (const auto& r : c.races) {
            ra.push_back({
                {"raceId", r.raceId},
                {"name", r.name},
                {"iconPath", r.iconPath},
                {"factionId", r.factionId},
                {"factionName", wowee::pipeline::WoweeChars::raceFactionName(r.factionId)},
                {"maleDisplayId", r.maleDisplayId},
                {"femaleDisplayId", r.femaleDisplayId},
                {"baseStrength", r.baseStrength},
                {"baseAgility", r.baseAgility},
                {"baseStamina", r.baseStamina},
                {"baseIntellect", r.baseIntellect},
                {"baseSpirit", r.baseSpirit},
                {"startingMapId", r.startingMapId},
                {"startingZoneAreaId", r.startingZoneAreaId},
                {"defaultLanguageSpellId", r.defaultLanguageSpellId},
                {"mountSpellId", r.mountSpellId},
            });
        }
        j["races"] = ra;
        nlohmann::json oa = nlohmann::json::array();
        for (const auto& o : c.outfits) {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& it : o.items) {
                items.push_back({
                    {"itemId", it.itemId},
                    {"displaySlot", it.displaySlot},
                });
            }
            oa.push_back({
                {"classId", o.classId},
                {"raceId", o.raceId},
                {"gender", o.gender},
                {"items", items},
            });
        }
        j["outfits"] = oa;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCHC: %s.wchc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  classes : %zu  races : %zu  outfits : %zu\n",
                c.classes.size(), c.races.size(), c.outfits.size());
    if (!c.classes.empty()) {
        std::printf("\n  Classes:\n");
        std::printf("    id  power        baseHP   /lvl    name\n");
        for (const auto& cls : c.classes) {
            std::printf("    %2u  %-11s  %4u     %3u     %s\n",
                        cls.classId,
                        wowee::pipeline::WoweeChars::powerTypeName(cls.powerType),
                        cls.baseHealth, cls.baseHealthPerLevel,
                        cls.name.c_str());
        }
    }
    if (!c.races.empty()) {
        std::printf("\n  Races:\n");
        std::printf("    id  faction    map  zone   name\n");
        for (const auto& r : c.races) {
            std::printf("    %2u  %-9s  %3u  %5u   %s\n",
                        r.raceId,
                        wowee::pipeline::WoweeChars::raceFactionName(r.factionId),
                        r.startingMapId, r.startingZoneAreaId,
                        r.name.c_str());
        }
    }
    if (!c.outfits.empty()) {
        std::printf("\n  Outfits:\n");
        for (const auto& o : c.outfits) {
            std::printf("    class=%-2u race=%-2u gender=%u  items: ",
                        o.classId, o.raceId, o.gender);
            for (size_t k = 0; k < o.items.size(); ++k) {
                std::printf("%s%u@slot%u",
                            k > 0 ? ", " : "",
                            o.items[k].itemId, o.items[k].displaySlot);
            }
            std::printf("\n");
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Three top-level arrays (classes / races /
    // outfits) mirroring the binary layout. Enum-typed fields
    // (powerType, factionId) emit dual int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wchc");
    if (outPath.empty()) outPath = base + ".wchc.json";
    if (!wowee::pipeline::WoweeCharsLoader::exists(base)) {
        return reportMissing("export-wchc-json", "WCHC", base, ".wchc");
    }
    auto c = wowee::pipeline::WoweeCharsLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json ca = nlohmann::json::array();
    for (const auto& cls : c.classes) {
        ca.push_back({
            {"classId", cls.classId},
            {"name", cls.name},
            {"iconPath", cls.iconPath},
            {"powerType", cls.powerType},
            {"powerTypeName", wowee::pipeline::WoweeChars::powerTypeName(cls.powerType)},
            {"displayPower", cls.displayPower},
            {"baseHealth", cls.baseHealth},
            {"baseHealthPerLevel", cls.baseHealthPerLevel},
            {"basePower", cls.basePower},
            {"basePowerPerLevel", cls.basePowerPerLevel},
            {"factionAvailability", cls.factionAvailability},
        });
    }
    j["classes"] = ca;
    nlohmann::json ra = nlohmann::json::array();
    for (const auto& r : c.races) {
        ra.push_back({
            {"raceId", r.raceId},
            {"name", r.name},
            {"iconPath", r.iconPath},
            {"factionId", r.factionId},
            {"factionName", wowee::pipeline::WoweeChars::raceFactionName(r.factionId)},
            {"maleDisplayId", r.maleDisplayId},
            {"femaleDisplayId", r.femaleDisplayId},
            {"baseStrength", r.baseStrength},
            {"baseAgility", r.baseAgility},
            {"baseStamina", r.baseStamina},
            {"baseIntellect", r.baseIntellect},
            {"baseSpirit", r.baseSpirit},
            {"startingMapId", r.startingMapId},
            {"startingZoneAreaId", r.startingZoneAreaId},
            {"defaultLanguageSpellId", r.defaultLanguageSpellId},
            {"mountSpellId", r.mountSpellId},
        });
    }
    j["races"] = ra;
    nlohmann::json oa = nlohmann::json::array();
    for (const auto& o : c.outfits) {
        nlohmann::json items = nlohmann::json::array();
        for (const auto& it : o.items) {
            items.push_back({
                {"itemId", it.itemId},
                {"displaySlot", it.displaySlot},
            });
        }
        oa.push_back({
            {"classId", o.classId},
            {"raceId", o.raceId},
            {"gender", o.gender},
            {"genderName",
             o.gender == wowee::pipeline::WoweeChars::Female ? "female" : "male"},
            {"items", items},
        });
    }
    j["outfits"] = oa;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wchc-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source  : %s.wchc\n", base.c_str());
    std::printf("  classes : %zu  races : %zu  outfits : %zu\n",
                c.classes.size(), c.races.size(), c.outfits.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wchc");
    outBase = cli::withoutExt(outBase, ".wchc");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wchc-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wchc-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto powerFromName = [](const std::string& s) -> uint8_t {
        if (s == "mana")        return wowee::pipeline::WoweeChars::Mana;
        if (s == "rage")        return wowee::pipeline::WoweeChars::Rage;
        if (s == "focus")       return wowee::pipeline::WoweeChars::Focus;
        if (s == "energy")      return wowee::pipeline::WoweeChars::Energy;
        if (s == "runic-power") return wowee::pipeline::WoweeChars::RunicPower;
        if (s == "runes")       return wowee::pipeline::WoweeChars::Runes;
        return wowee::pipeline::WoweeChars::Mana;
    };
    auto factionFromName = [](const std::string& s) -> uint8_t {
        if (s == "alliance") return wowee::pipeline::WoweeChars::Alliance;
        if (s == "horde")    return wowee::pipeline::WoweeChars::Horde;
        if (s == "neutral")  return wowee::pipeline::WoweeChars::Neutral;
        return wowee::pipeline::WoweeChars::Alliance;
    };
    auto genderFromName = [](const std::string& s) -> uint8_t {
        if (s == "female") return wowee::pipeline::WoweeChars::Female;
        return wowee::pipeline::WoweeChars::Male;
    };
    wowee::pipeline::WoweeChars c;
    c.name = j.value("name", std::string{});
    if (j.contains("classes") && j["classes"].is_array()) {
        for (const auto& jc : j["classes"]) {
            wowee::pipeline::WoweeChars::Class cls;
            cls.classId = jc.value("classId", 0u);
            cls.name = jc.value("name", std::string{});
            cls.iconPath = jc.value("iconPath", std::string{});
            if (jc.contains("powerType") && jc["powerType"].is_number_integer()) {
                cls.powerType = static_cast<uint8_t>(jc["powerType"].get<int>());
            } else if (jc.contains("powerTypeName") && jc["powerTypeName"].is_string()) {
                cls.powerType = powerFromName(jc["powerTypeName"].get<std::string>());
            }
            cls.displayPower = static_cast<uint8_t>(
                jc.value("displayPower", static_cast<int>(cls.powerType)));
            cls.baseHealth = jc.value("baseHealth", 50u);
            cls.baseHealthPerLevel = static_cast<uint16_t>(
                jc.value("baseHealthPerLevel", 12));
            cls.basePower = jc.value("basePower", 100u);
            cls.basePowerPerLevel = static_cast<uint16_t>(
                jc.value("basePowerPerLevel", 5));
            cls.factionAvailability = static_cast<uint8_t>(
                jc.value("factionAvailability",
                          wowee::pipeline::WoweeChars::AvailableAlliance |
                          wowee::pipeline::WoweeChars::AvailableHorde));
            c.classes.push_back(cls);
        }
    }
    if (j.contains("races") && j["races"].is_array()) {
        for (const auto& jr : j["races"]) {
            wowee::pipeline::WoweeChars::Race r;
            r.raceId = jr.value("raceId", 0u);
            r.name = jr.value("name", std::string{});
            r.iconPath = jr.value("iconPath", std::string{});
            if (jr.contains("factionId") && jr["factionId"].is_number_integer()) {
                r.factionId = static_cast<uint8_t>(jr["factionId"].get<int>());
            } else if (jr.contains("factionName") && jr["factionName"].is_string()) {
                r.factionId = factionFromName(jr["factionName"].get<std::string>());
            }
            r.maleDisplayId = jr.value("maleDisplayId", 0u);
            r.femaleDisplayId = jr.value("femaleDisplayId", 0u);
            r.baseStrength = static_cast<uint16_t>(jr.value("baseStrength", 20));
            r.baseAgility = static_cast<uint16_t>(jr.value("baseAgility", 20));
            r.baseStamina = static_cast<uint16_t>(jr.value("baseStamina", 20));
            r.baseIntellect = static_cast<uint16_t>(jr.value("baseIntellect", 20));
            r.baseSpirit = static_cast<uint16_t>(jr.value("baseSpirit", 20));
            r.startingMapId = jr.value("startingMapId", 0u);
            r.startingZoneAreaId = jr.value("startingZoneAreaId", 0u);
            r.defaultLanguageSpellId = jr.value("defaultLanguageSpellId", 0u);
            r.mountSpellId = jr.value("mountSpellId", 0u);
            c.races.push_back(r);
        }
    }
    if (j.contains("outfits") && j["outfits"].is_array()) {
        for (const auto& jo : j["outfits"]) {
            wowee::pipeline::WoweeChars::Outfit o;
            o.classId = jo.value("classId", 0u);
            o.raceId = jo.value("raceId", 0u);
            if (jo.contains("gender") && jo["gender"].is_number_integer()) {
                o.gender = static_cast<uint8_t>(jo["gender"].get<int>());
            } else if (jo.contains("genderName") && jo["genderName"].is_string()) {
                o.gender = genderFromName(jo["genderName"].get<std::string>());
            }
            if (jo.contains("items") && jo["items"].is_array()) {
                for (const auto& ji : jo["items"]) {
                    wowee::pipeline::WoweeChars::OutfitItem it;
                    it.itemId = ji.value("itemId", 0u);
                    it.displaySlot = static_cast<uint8_t>(
                        ji.value("displaySlot", 0));
                    o.items.push_back(it);
                }
            }
            c.outfits.push_back(std::move(o));
        }
    }
    if (!wowee::pipeline::WoweeCharsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wchc-json: failed to save %s.wchc\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wchc\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  classes : %zu  races : %zu  outfits : %zu\n",
                c.classes.size(), c.races.size(), c.outfits.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchc");
    if (!wowee::pipeline::WoweeCharsLoader::exists(base)) {
        return reportMissing("validate-wchc", "WCHC", base, ".wchc");
    }
    auto c = wowee::pipeline::WoweeCharsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.classes.empty() && c.races.empty()) {
        warnings.push_back("catalog has zero classes and zero races");
    }
    std::vector<uint32_t> classIdsSeen;
    for (size_t k = 0; k < c.classes.size(); ++k) {
        const auto& cls = c.classes[k];
        std::string ctx = "class " + std::to_string(k) +
                          " (id=" + std::to_string(cls.classId);
        if (!cls.name.empty()) ctx += " " + cls.name;
        ctx += ")";
        if (cls.classId == 0) errors.push_back(ctx + ": classId is 0");
        if (cls.name.empty()) errors.push_back(ctx + ": name is empty");
        if (cls.baseHealth == 0) {
            errors.push_back(ctx + ": baseHealth is 0 (character dies on creation)");
        }
        if (cls.factionAvailability == 0) {
            errors.push_back(ctx +
                ": factionAvailability=0 (no faction can pick this class)");
        }
        for (uint32_t prev : classIdsSeen) {
            if (prev == cls.classId) {
                errors.push_back(ctx + ": duplicate classId");
                break;
            }
        }
        classIdsSeen.push_back(cls.classId);
    }
    std::vector<uint32_t> raceIdsSeen;
    for (size_t k = 0; k < c.races.size(); ++k) {
        const auto& r = c.races[k];
        std::string ctx = "race " + std::to_string(k) +
                          " (id=" + std::to_string(r.raceId);
        if (!r.name.empty()) ctx += " " + r.name;
        ctx += ")";
        if (r.raceId == 0) errors.push_back(ctx + ": raceId is 0");
        if (r.name.empty()) errors.push_back(ctx + ": name is empty");
        if (r.factionId > wowee::pipeline::WoweeChars::Neutral) {
            errors.push_back(ctx + ": factionId " +
                std::to_string(r.factionId) + " not in 0..2");
        }
        for (uint32_t prev : raceIdsSeen) {
            if (prev == r.raceId) {
                errors.push_back(ctx + ": duplicate raceId");
                break;
            }
        }
        raceIdsSeen.push_back(r.raceId);
    }
    // Outfit cross-references must hit real classes / races.
    for (size_t k = 0; k < c.outfits.size(); ++k) {
        const auto& o = c.outfits[k];
        std::string ctx = "outfit " + std::to_string(k) +
                          " (class=" + std::to_string(o.classId) +
                          " race=" + std::to_string(o.raceId) + ")";
        if (!c.classes.empty() && !c.findClass(o.classId)) {
            errors.push_back(ctx + ": classId does not exist in this catalog");
        }
        if (!c.races.empty() && !c.findRace(o.raceId)) {
            errors.push_back(ctx + ": raceId does not exist in this catalog");
        }
        if (o.gender > wowee::pipeline::WoweeChars::Female) {
            errors.push_back(ctx + ": gender " +
                std::to_string(o.gender) + " not 0 or 1");
        }
        if (o.items.empty()) {
            warnings.push_back(ctx +
                ": no items (player starts naked / unarmed)");
        }
        for (size_t ii = 0; ii < o.items.size(); ++ii) {
            if (o.items[ii].itemId == 0) {
                errors.push_back(ctx + " item " + std::to_string(ii) +
                    ": itemId is 0");
            }
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wchc", base, errors, warnings);
    std::printf("validate-wchc: %s.wchc\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu classes, %zu races, %zu outfits, all IDs unique\n",
                    c.classes.size(), c.races.size(), c.outfits.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCharsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-chars") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-chars-alliance") == 0 && i + 1 < argc) {
        outRc = handleGenAlliance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-chars-allraces") == 0 && i + 1 < argc) {
        outRc = handleGenAllRaces(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wchc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wchc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wchc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wchc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
