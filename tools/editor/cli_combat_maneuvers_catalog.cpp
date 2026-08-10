#include "cli_combat_maneuvers_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_combat_maneuvers.hpp"
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

const char* categoryKindName(uint8_t k) {
    using C = wowee::pipeline::WoweeCombatManeuvers;
    switch (k) {
        case C::Stance:   return "stance";
        case C::Form:     return "form";
        case C::Aspect:   return "aspect";
        case C::Presence: return "presence";
        case C::Posture:  return "posture";
        case C::Sigil:    return "sigil";
        default:          return "unknown";
    }
}

bool saveOrError(const wowee::pipeline::WoweeCombatManeuvers& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeCombatManeuversLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wcmg\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeCombatManeuvers& c,
                     const std::string& base) {
    size_t totalSpells = 0;
    for (const auto& e : c.entries) totalSpells += e.members.size();
    std::printf("Wrote %s.wcmg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  groups  : %zu (%zu member spells total)\n",
                c.entries.size(), totalSpells);
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorStanceMutex";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmg");
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::makeWarrior(name);
    if (!saveOrError(c, base, "gen-cmg")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDruid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DruidShapeshiftMutex";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmg");
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::makeDruid(name);
    if (!saveOrError(c, base, "gen-cmg-druid")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAllMutex(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllClassMutexGroups";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wcmg");
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::makeAllMutex(name);
    if (!saveOrError(c, base, "gen-cmg-all")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmg");
    if (!wowee::pipeline::WoweeCombatManeuversLoader::exists(base)) {
        std::fprintf(stderr, "WCMG not found: %s.wcmg\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wcmg"] = base + ".wcmg";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"groupId", e.groupId},
                {"name", e.name},
                {"description", e.description},
                {"classMask", e.classMask},
                {"categoryKind", e.categoryKind},
                {"categoryKindName",
                    categoryKindName(e.categoryKind)},
                {"exclusive", e.exclusive != 0},
                {"iconColorRGBA", e.iconColorRGBA},
                {"members", e.members},
                {"memberCount", e.members.size()},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCMG: %s.wcmg\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  groups  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   classMask  category   excl   members   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %8u  %-9s  %4s  %7zu   %s\n",
                    e.groupId, e.classMask,
                    categoryKindName(e.categoryKind),
                    e.exclusive ? "yes " : "no  ",
                    e.members.size(), e.name.c_str());
        // Spell ID list as a wrapped sub-line for the
        // operator's eye.
        if (!e.members.empty()) {
            std::printf("           members:");
            size_t col = 19;
            for (size_t k = 0; k < e.members.size(); ++k) {
                char buf[16];
                int n = std::snprintf(buf, sizeof(buf),
                    " %u%s", e.members[k],
                    (k + 1 < e.members.size()) ? "," : "");
                if (col + n > 78) {
                    std::printf("\n                    ");
                    col = 20;
                }
                std::printf("%s", buf);
                col += n;
            }
            std::printf("\n");
        }
    }
    return 0;
}

// Token parser for categoryKind. Returns -1 if unknown.
int parseCategoryKindToken(const std::string& s) {
    using C = wowee::pipeline::WoweeCombatManeuvers;
    if (s == "stance")   return C::Stance;
    if (s == "form")     return C::Form;
    if (s == "aspect")   return C::Aspect;
    if (s == "presence") return C::Presence;
    if (s == "posture")  return C::Posture;
    if (s == "sigil")    return C::Sigil;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wcmg");
    if (out.empty()) out = base + ".wcmg.json";
    if (!wowee::pipeline::WoweeCombatManeuversLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wcmg-json: WCMG not found: %s.wcmg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WCMG";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"groupId", e.groupId},
            {"name", e.name},
            {"description", e.description},
            {"classMask", e.classMask},
            {"categoryKind", e.categoryKind},
            {"categoryKindName",
                categoryKindName(e.categoryKind)},
            {"exclusive", e.exclusive != 0},
            {"iconColorRGBA", e.iconColorRGBA},
            {"members", e.members},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wcmg-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu groups)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wcmg");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wcmg-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wcmg-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeCombatManeuvers c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wcmg-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeCombatManeuvers::Entry e;
        e.groupId = je.value("groupId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.classMask = je.value("classMask", 0u);
        // categoryKind: int OR token string.
        if (je.contains("categoryKind")) {
            const auto& ck = je["categoryKind"];
            if (ck.is_string()) {
                int parsed = parseCategoryKindToken(
                    ck.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wcmg-json: unknown "
                        "categoryKind token '%s' on entry "
                        "id=%u\n",
                        ck.get<std::string>().c_str(), e.groupId);
                    return 1;
                }
                e.categoryKind = static_cast<uint8_t>(parsed);
            } else if (ck.is_number_integer()) {
                e.categoryKind = static_cast<uint8_t>(
                    ck.get<int>());
            }
        } else if (je.contains("categoryKindName") &&
                   je["categoryKindName"].is_string()) {
            int parsed = parseCategoryKindToken(
                je["categoryKindName"].get<std::string>());
            if (parsed >= 0)
                e.categoryKind = static_cast<uint8_t>(parsed);
        }
        // exclusive: bool OR int.
        if (je.contains("exclusive")) {
            const auto& ex = je["exclusive"];
            if (ex.is_boolean())
                e.exclusive = ex.get<bool>() ? 1 : 0;
            else if (ex.is_number_integer())
                e.exclusive = static_cast<uint8_t>(
                    ex.get<int>() != 0 ? 1 : 0);
        }
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        if (je.contains("members") && je["members"].is_array()) {
            for (const auto& m : je["members"]) {
                if (m.is_number_integer())
                    e.members.push_back(m.get<uint32_t>());
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeCombatManeuversLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wcmg-json: failed to save %s.wcmg\n",
            outBase.c_str());
        return 1;
    }
    size_t totalSpells = 0;
    for (const auto& e : c.entries) totalSpells += e.members.size();
    std::printf("Wrote %s.wcmg (%zu groups, %zu member spells)\n",
                outBase.c_str(), c.entries.size(), totalSpells);
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wcmg");
    if (!wowee::pipeline::WoweeCombatManeuversLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wcmg: WCMG not found: %s.wcmg\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCombatManeuversLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    // Track which spell IDs appear in any exclusive group
    // — a spell that appears in TWO different exclusive
    // groups creates an undecidable mutex (which group's
    // outline does the action bar use?).
    std::set<uint32_t> spellInExclusiveGroup;
    std::vector<std::pair<uint32_t, uint32_t>> doubleAssign;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.groupId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.groupId == 0)
            errors.push_back(ctx + ": groupId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.classMask == 0) {
            errors.push_back(ctx +
                ": classMask is 0 — group is not "
                "associated with any class");
        }
        if (e.categoryKind > 5) {
            errors.push_back(ctx + ": categoryKind " +
                std::to_string(e.categoryKind) +
                " out of range (must be 0..5)");
        }
        if (e.members.empty()) {
            errors.push_back(ctx +
                ": members[] is empty — mutex group has "
                "nothing to switch between");
        }
        // A single-member mutex group is technically legal
        // but suggests a content authoring error.
        if (e.members.size() == 1) {
            warnings.push_back(ctx +
                ": only 1 member spell — mutex with one "
                "element has no exclusion to enforce; "
                "verify if intentional");
        }
        // Check for duplicate spell IDs WITHIN this
        // group's members list.
        std::set<uint32_t> seen;
        for (uint32_t m : e.members) {
            if (m == 0) {
                errors.push_back(ctx +
                    ": members[] contains spellId 0");
                continue;
            }
            if (!seen.insert(m).second) {
                errors.push_back(ctx +
                    ": duplicate spellId " +
                    std::to_string(m) + " within members[]");
            }
            // Cross-group check: same spell in two
            // exclusive groups is an authoring bug.
            if (e.exclusive) {
                if (!spellInExclusiveGroup.insert(m).second) {
                    doubleAssign.push_back({m, e.groupId});
                }
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.groupId) {
                errors.push_back(ctx + ": duplicate groupId");
                break;
            }
        }
        idsSeen.push_back(e.groupId);
    }
    for (auto [spellId, groupId] : doubleAssign) {
        errors.push_back(
            "spellId " + std::to_string(spellId) +
            " appears in multiple exclusive groups "
            "(latest: groupId " + std::to_string(groupId) +
            ") — action bar mutex would be undecidable");
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wcmg", base, errors, warnings);
    std::printf("validate-wcmg: %s.wcmg\n", base.c_str());
    if (ok && warnings.empty()) {
        size_t totalSpells = 0;
        for (const auto& e : c.entries) totalSpells += e.members.size();
        std::printf("  OK — %zu groups, %zu member spells, all "
                    "groupIds unique\n",
                    c.entries.size(), totalSpells);
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleCombatManeuversCatalog(int& i, int argc, char** argv,
                                   int& outRc) {
    if (std::strcmp(argv[i], "--gen-cmg") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmg-druid") == 0 && i + 1 < argc) {
        outRc = handleGenDruid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cmg-all") == 0 && i + 1 < argc) {
        outRc = handleGenAllMutex(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wcmg") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wcmg") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wcmg-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wcmg-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
