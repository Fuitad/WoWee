#include "cli_pet_care_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_pet_care.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

const char* actionKindName(uint8_t k) {
    using P = wowee::pipeline::WoweePetCare;
    switch (k) {
        case P::Revive:    return "revive";
        case P::Mend:      return "mend";
        case P::Feed:      return "feed";
        case P::Dismiss:   return "dismiss";
        case P::Tame:      return "tame";
        case P::BeastLore: return "beastlore";
        case P::Stable:    return "stable";
        case P::Untrain:   return "untrain";
        case P::Rename:    return "rename";
        case P::Abandon:   return "abandon";
        case P::Summon:    return "summon";
        default:           return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweePetCare& c,
                     const std::string& base) {
    std::printf("Wrote %s.wpcr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  actions : %zu\n", c.entries.size());
}

int handleGenHunter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HunterPetCare";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcr");
    auto c = wowee::pipeline::WoweePetCareLoader::makeHunterCare(name);
    if (!saveOrError<wowee::pipeline::WoweePetCareLoader>(c, base, "gen-pcr", ".wpcr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenStable(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StableMasterActions";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcr");
    auto c = wowee::pipeline::WoweePetCareLoader::makeStableActions(name);
    if (!saveOrError<wowee::pipeline::WoweePetCareLoader>(c, base, "gen-pcr-stable", ".wpcr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWarlock(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarlockMinionSummons";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wpcr");
    auto c = wowee::pipeline::WoweePetCareLoader::makeWarlockMinions(name);
    if (!saveOrError<wowee::pipeline::WoweePetCareLoader>(c, base, "gen-pcr-warlock", ".wpcr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcr");
    if (!wowee::pipeline::WoweePetCareLoader::exists(base)) {
        return reportMissing("WPCR", base, ".wpcr");
    }
    auto c = wowee::pipeline::WoweePetCareLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wpcr"] = base + ".wpcr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"actionId", e.actionId},
                {"name", e.name},
                {"description", e.description},
                {"spellId", e.spellId},
                {"classFilter", e.classFilter},
                {"actionKind", e.actionKind},
                {"actionKindName", actionKindName(e.actionKind)},
                {"happinessRestore", e.happinessRestore},
                {"requiresPet", e.requiresPet != 0},
                {"requiresStableNPC", e.requiresStableNPC != 0},
                {"costCopper", e.costCopper},
                {"reagentItemId", e.reagentItemId},
                {"castTimeMs", e.castTimeMs},
                {"cooldownSec", e.cooldownSec},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPCR: %s.wpcr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  actions : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   spell   class   kind        happy  pet  stb  cost(c)  reagent  cast(ms)  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u   %5u   %-9s    %+3d   %s   %s    %6u    %5u    %5u   %s\n",
                    e.actionId, e.spellId, e.classFilter,
                    actionKindName(e.actionKind),
                    e.happinessRestore,
                    e.requiresPet ? "Y" : "n",
                    e.requiresStableNPC ? "Y" : "n",
                    e.costCopper, e.reagentItemId,
                    e.castTimeMs, e.name.c_str());
    }
    return 0;
}

int parseActionKindToken(const std::string& s) {
    using P = wowee::pipeline::WoweePetCare;
    if (s == "revive")    return P::Revive;
    if (s == "mend")      return P::Mend;
    if (s == "feed")      return P::Feed;
    if (s == "dismiss")   return P::Dismiss;
    if (s == "tame")      return P::Tame;
    if (s == "beastlore") return P::BeastLore;
    if (s == "stable")    return P::Stable;
    if (s == "untrain")   return P::Untrain;
    if (s == "rename")    return P::Rename;
    if (s == "abandon")   return P::Abandon;
    if (s == "summon")    return P::Summon;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wpcr");
    if (out.empty()) out = base + ".wpcr.json";
    if (!wowee::pipeline::WoweePetCareLoader::exists(base)) {
        return reportMissing("export-wpcr-json", "WPCR", base, ".wpcr");
    }
    auto c = wowee::pipeline::WoweePetCareLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPCR";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"actionId", e.actionId},
            {"name", e.name},
            {"description", e.description},
            {"spellId", e.spellId},
            {"classFilter", e.classFilter},
            {"actionKind", e.actionKind},
            {"actionKindName", actionKindName(e.actionKind)},
            {"happinessRestore", e.happinessRestore},
            {"requiresPet", e.requiresPet != 0},
            {"requiresStableNPC", e.requiresStableNPC != 0},
            {"costCopper", e.costCopper},
            {"reagentItemId", e.reagentItemId},
            {"castTimeMs", e.castTimeMs},
            {"cooldownSec", e.cooldownSec},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wpcr-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu actions)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wpcr");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wpcr-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wpcr-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweePetCare c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wpcr-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweePetCare::Entry e;
        e.actionId = je.value("actionId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.spellId = je.value("spellId", 0u);
        e.classFilter = je.value("classFilter", 0u);
        if (je.contains("actionKind")) {
            const auto& v = je["actionKind"];
            if (v.is_string()) {
                int parsed = parseActionKindToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wpcr-json: unknown "
                        "actionKind token '%s' on entry "
                        "id=%u\n",
                        v.get<std::string>().c_str(),
                        e.actionId);
                    return 1;
                }
                e.actionKind = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.actionKind = static_cast<uint8_t>(v.get<int>());
            }
        } else if (je.contains("actionKindName") &&
                   je["actionKindName"].is_string()) {
            int parsed = parseActionKindToken(
                je["actionKindName"].get<std::string>());
            if (parsed >= 0)
                e.actionKind = static_cast<uint8_t>(parsed);
        }
        e.happinessRestore = static_cast<int8_t>(
            je.value("happinessRestore", 0));
        if (je.contains("requiresPet")) {
            const auto& v = je["requiresPet"];
            if (v.is_boolean())
                e.requiresPet = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.requiresPet = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        if (je.contains("requiresStableNPC")) {
            const auto& v = je["requiresStableNPC"];
            if (v.is_boolean())
                e.requiresStableNPC = v.get<bool>() ? 1 : 0;
            else if (v.is_number_integer())
                e.requiresStableNPC = static_cast<uint8_t>(
                    v.get<int>() != 0 ? 1 : 0);
        }
        e.costCopper = je.value("costCopper", 0u);
        e.reagentItemId = je.value("reagentItemId", 0u);
        e.castTimeMs = je.value("castTimeMs", 0u);
        e.cooldownSec = je.value("cooldownSec", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweePetCareLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wpcr-json: failed to save %s.wpcr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wpcr (%zu actions)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wpcr");
    if (!wowee::pipeline::WoweePetCareLoader::exists(base)) {
        return reportMissing("validate-wpcr", "WPCR", base, ".wpcr");
    }
    auto c = wowee::pipeline::WoweePetCareLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.actionId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.actionId == 0)
            errors.push_back(ctx + ": actionId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.classFilter == 0) {
            errors.push_back(ctx +
                ": classFilter is 0 - no class can use "
                "this action");
        }
        if (e.actionKind > 10) {
            errors.push_back(ctx + ": actionKind " +
                std::to_string(e.actionKind) +
                " out of range (must be 0..10)");
        }
        if (e.happinessRestore < -25 || e.happinessRestore > 25) {
            warnings.push_back(ctx +
                ": happinessRestore " +
                std::to_string(e.happinessRestore) +
                " outside +/-25 - pet happiness range "
                "is normally [-100, +100], single-action "
                "swing >25 is unusual");
        }
        // Per-kind validity rules:
        using P = wowee::pipeline::WoweePetCare;
        if (e.actionKind == P::Tame && e.requiresPet != 0) {
            errors.push_back(ctx +
                ": Tame action requires NO pet active "
                "(requiresPet must be 0) - you can't tame "
                "while another pet is out");
        }
        if (e.actionKind == P::Summon && e.requiresPet != 0) {
            errors.push_back(ctx +
                ": Summon action requires NO pet active "
                "(requiresPet must be 0) - Warlock can't "
                "summon while another minion is out");
        }
        if (e.actionKind == P::Revive && e.requiresPet != 0) {
            warnings.push_back(ctx +
                ": Revive action with requiresPet=1 - "
                "revive should target a DEAD pet, not "
                "require an active one. Verify intent.");
        }
        if (e.actionKind == P::Stable &&
            e.requiresStableNPC == 0) {
            warnings.push_back(ctx +
                ": Stable action with requiresStableNPC=0 "
                "- stable slot purchases are normally "
                "gated to stable-master conversation");
        }
        // Tame with cooldown - Tame Beast has a fixed
        // 15-second internal cooldown in 3.3.5; warn if
        // unset.
        if (e.actionKind == P::Tame && e.cooldownSec == 0) {
            warnings.push_back(ctx +
                ": Tame action with cooldownSec=0 - Tame "
                "Beast canonically has a 15-sec internal "
                "cooldown to prevent macro-spam");
        }
        if (!idsSeen.insert(e.actionId).second) {
            errors.push_back(ctx + ": duplicate actionId");
        }
    }
    return cli::reportValidation("wpcr", base, jsonOut, errors, warnings,
                                 formatted("%zu actions, all actionIds "
                    "unique, per-kind constraints "
                    "satisfied", c.entries.size()));
}

} // namespace

bool handlePetCareCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-pcr") == 0 && i + 1 < argc) {
        outRc = handleGenHunter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pcr-stable") == 0 && i + 1 < argc) {
        outRc = handleGenStable(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-pcr-warlock") == 0 && i + 1 < argc) {
        outRc = handleGenWarlock(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wpcr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wpcr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wpcr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wpcr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
