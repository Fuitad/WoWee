#include "cli_pet_talents_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_pet_talents.hpp"
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

const char* treeKindName(uint8_t k) {
    using P = wowee::pipeline::WoweePetTalents;
    switch (k) {
        case P::Cunning:  return "cunning";
        case P::Ferocity: return "ferocity";
        case P::Tenacity: return "tenacity";
        default:          return "unknown";
    }
}

bool saveOrError(const wowee::pipeline::WoweePetTalents& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweePetTalentsLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wptt\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweePetTalents& c,
                     const std::string& base) {
    size_t totalSpells = 0;
    for (const auto& e : c.entries)
        totalSpells += e.spellIdsByRank.size();
    std::printf("Wrote %s.wptt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  talents : %zu (%zu rank-spells total)\n",
                c.entries.size(), totalSpells);
}

int handleGenFerocity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FerocityPetTree";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wptt");
    auto c = wowee::pipeline::WoweePetTalentsLoader::makeFerocity(name);
    if (!saveOrError(c, base, "gen-ptt")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCunning(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CunningPetTree";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wptt");
    auto c = wowee::pipeline::WoweePetTalentsLoader::makeCunning(name);
    if (!saveOrError(c, base, "gen-ptt-cunning")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTenacity(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TenacityPetTree";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wptt");
    auto c = wowee::pipeline::WoweePetTalentsLoader::makeTenacity(name);
    if (!saveOrError(c, base, "gen-ptt-tenacity")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wptt");
    if (!wowee::pipeline::WoweePetTalentsLoader::exists(base)) {
        std::fprintf(stderr, "WPTT not found: %s.wptt\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePetTalentsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wptt"] = base + ".wptt";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"talentId", e.talentId},
                {"name", e.name},
                {"description", e.description},
                {"treeKind", e.treeKind},
                {"treeKindName", treeKindName(e.treeKind)},
                {"tier", e.tier},
                {"column", e.column},
                {"maxRank", e.maxRank},
                {"prerequisiteTalentId", e.prerequisiteTalentId},
                {"requiredLoyalty", e.requiredLoyalty},
                {"iconColorRGBA", e.iconColorRGBA},
                {"spellIdsByRank", e.spellIdsByRank},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WPTT: %s.wptt\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  talents : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   tree       tier  col  ranks  prereq  loyalty   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-8s    %2u   %2u    %2u   %5u     %3u     %s\n",
                    e.talentId, treeKindName(e.treeKind),
                    e.tier, e.column, e.maxRank,
                    e.prerequisiteTalentId,
                    e.requiredLoyalty, e.name.c_str());
        if (!e.spellIdsByRank.empty()) {
            std::printf("           rank-spells:");
            for (uint32_t s : e.spellIdsByRank)
                std::printf(" %u", s);
            std::printf("\n");
        }
    }
    return 0;
}

int parseTreeKindToken(const std::string& s) {
    using P = wowee::pipeline::WoweePetTalents;
    if (s == "cunning")  return P::Cunning;
    if (s == "ferocity") return P::Ferocity;
    if (s == "tenacity") return P::Tenacity;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wptt");
    if (out.empty()) out = base + ".wptt.json";
    if (!wowee::pipeline::WoweePetTalentsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wptt-json: WPTT not found: %s.wptt\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePetTalentsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WPTT";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"talentId", e.talentId},
            {"name", e.name},
            {"description", e.description},
            {"treeKind", e.treeKind},
            {"treeKindName", treeKindName(e.treeKind)},
            {"tier", e.tier},
            {"column", e.column},
            {"maxRank", e.maxRank},
            {"prerequisiteTalentId", e.prerequisiteTalentId},
            {"requiredLoyalty", e.requiredLoyalty},
            {"iconColorRGBA", e.iconColorRGBA},
            {"spellIdsByRank", e.spellIdsByRank},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wptt-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu talents)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wptt");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wptt-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wptt-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweePetTalents c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wptt-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweePetTalents::Entry e;
        e.talentId = je.value("talentId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (je.contains("treeKind")) {
            const auto& v = je["treeKind"];
            if (v.is_string()) {
                int parsed = parseTreeKindToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wptt-json: unknown "
                        "treeKind token '%s' on entry "
                        "id=%u\n",
                        v.get<std::string>().c_str(),
                        e.talentId);
                    return 1;
                }
                e.treeKind = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.treeKind = static_cast<uint8_t>(v.get<int>());
            }
        } else if (je.contains("treeKindName") &&
                   je["treeKindName"].is_string()) {
            int parsed = parseTreeKindToken(
                je["treeKindName"].get<std::string>());
            if (parsed >= 0)
                e.treeKind = static_cast<uint8_t>(parsed);
        }
        e.tier = static_cast<uint8_t>(je.value("tier", 0u));
        e.column = static_cast<uint8_t>(je.value("column", 0u));
        e.maxRank = static_cast<uint8_t>(je.value("maxRank", 1u));
        e.prerequisiteTalentId = je.value(
            "prerequisiteTalentId", 0u);
        e.requiredLoyalty = static_cast<uint8_t>(
            je.value("requiredLoyalty", 0u));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        if (je.contains("spellIdsByRank") &&
            je["spellIdsByRank"].is_array()) {
            for (const auto& s : je["spellIdsByRank"]) {
                if (s.is_number_integer())
                    e.spellIdsByRank.push_back(s.get<uint32_t>());
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweePetTalentsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wptt-json: failed to save %s.wptt\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wptt (%zu talents)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wptt");
    if (!wowee::pipeline::WoweePetTalentsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wptt: WPTT not found: %s.wptt\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweePetTalentsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Track (tree, tier, column) cell occupancy — two
    // talents in the same cell would render on top of
    // each other.
    std::set<uint32_t> cellsSeen;
    auto cellKey = [](uint8_t tree, uint8_t tier, uint8_t col) {
        return static_cast<uint32_t>(tree) << 16 |
               static_cast<uint32_t>(tier) << 8  |
               static_cast<uint32_t>(col);
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.talentId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.talentId == 0)
            errors.push_back(ctx + ": talentId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.treeKind > 2) {
            errors.push_back(ctx + ": treeKind " +
                std::to_string(e.treeKind) +
                " out of range (must be 0..2)");
        }
        if (e.tier > 6) {
            errors.push_back(ctx + ": tier " +
                std::to_string(e.tier) +
                " > 6 — pet trees have 7 tiers (0-6)");
        }
        if (e.column > 2) {
            errors.push_back(ctx + ": column " +
                std::to_string(e.column) +
                " > 2 — pet trees have 3 columns (0-2)");
        }
        if (e.maxRank == 0 || e.maxRank > 5) {
            errors.push_back(ctx + ": maxRank " +
                std::to_string(e.maxRank) +
                " out of range (must be 1..5)");
        }
        // spellIdsByRank size must equal maxRank.
        if (e.spellIdsByRank.size() !=
            static_cast<size_t>(e.maxRank)) {
            errors.push_back(ctx +
                ": spellIdsByRank.size() = " +
                std::to_string(e.spellIdsByRank.size()) +
                " does not match maxRank " +
                std::to_string(e.maxRank));
        }
        // No spell ID may be 0 within the array.
        for (size_t s = 0; s < e.spellIdsByRank.size(); ++s) {
            if (e.spellIdsByRank[s] == 0) {
                errors.push_back(ctx +
                    ": spellIdsByRank[" + std::to_string(s) +
                    "] = 0 (rank " + std::to_string(s + 1) +
                    " has no spell)");
            }
        }
        // Self-reference check for prereq.
        if (e.prerequisiteTalentId == e.talentId) {
            errors.push_back(ctx +
                ": prerequisiteTalentId equals talentId — "
                "would create a 1-element prereq cycle");
        }
        // Cell occupancy uniqueness.
        if (e.tier <= 6 && e.column <= 2 && e.treeKind <= 2) {
            uint32_t key = cellKey(e.treeKind, e.tier, e.column);
            if (!cellsSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": cell (tree=" +
                    std::string(treeKindName(e.treeKind)) +
                    ", tier=" + std::to_string(e.tier) +
                    ", column=" + std::to_string(e.column) +
                    ") already occupied by another talent");
            }
        }
        if (!idsSeen.insert(e.talentId).second) {
            errors.push_back(ctx + ": duplicate talentId");
        }
    }
    // Cross-entry: prereq must resolve to an existing
    // talent in the same tree (you can't prereq a Ferocity
    // talent from a Cunning slot).
    auto findEntry = [&](uint32_t id) -> const
        wowee::pipeline::WoweePetTalents::Entry* {
        for (const auto& e : c.entries) {
            if (e.talentId == id) return &e;
        }
        return nullptr;
    };
    for (const auto& e : c.entries) {
        if (e.prerequisiteTalentId == 0) continue;
        auto* pre = findEntry(e.prerequisiteTalentId);
        if (!pre) {
            errors.push_back("entry id=" +
                std::to_string(e.talentId) +
                " (" + e.name + "): prerequisiteTalentId=" +
                std::to_string(e.prerequisiteTalentId) +
                " references missing entry");
            continue;
        }
        if (pre->treeKind != e.treeKind) {
            errors.push_back("entry id=" +
                std::to_string(e.talentId) +
                " (" + e.name + "): prereq " +
                std::to_string(e.prerequisiteTalentId) +
                " (" + pre->name + ") is in tree '" +
                std::string(treeKindName(pre->treeKind)) +
                "' but this talent is in tree '" +
                std::string(treeKindName(e.treeKind)) +
                "' — prereq must be in same tree");
        }
        if (pre->tier >= e.tier) {
            errors.push_back("entry id=" +
                std::to_string(e.talentId) +
                " (" + e.name + "): prereq tier " +
                std::to_string(pre->tier) +
                " >= this talent's tier " +
                std::to_string(e.tier) +
                " — prereqs must be in earlier tiers");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wptt", base, errors, warnings);
    std::printf("validate-wptt: %s.wptt\n", base.c_str());
    if (ok && warnings.empty()) {
        size_t totalSpells = 0;
        for (const auto& e : c.entries)
            totalSpells += e.spellIdsByRank.size();
        std::printf("  OK — %zu talents, %zu rank-spells, "
                    "all talentIds + cells unique, prereqs "
                    "valid + earlier-tier\n",
                    c.entries.size(), totalSpells);
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handlePetTalentsCatalog(int& i, int argc, char** argv,
                               int& outRc) {
    if (std::strcmp(argv[i], "--gen-ptt") == 0 && i + 1 < argc) {
        outRc = handleGenFerocity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ptt-cunning") == 0 &&
        i + 1 < argc) {
        outRc = handleGenCunning(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ptt-tenacity") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTenacity(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wptt") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wptt") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wptt-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wptt-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
