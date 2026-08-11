#include "cli_buff_book_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_buff_book.hpp"
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

const char* statBonusKindName(uint8_t k) {
    using B = wowee::pipeline::WoweeBuffBook;
    switch (k) {
        case B::Stamina:     return "stamina";
        case B::Intellect:   return "intellect";
        case B::Spirit:      return "spirit";
        case B::AllStats:    return "allstats";
        case B::Armor:       return "armor";
        case B::SpellPower:  return "spellpower";
        case B::AttackPower: return "attackpower";
        case B::CritRating:  return "critrating";
        case B::HasteRating: return "hasterating";
        case B::ManaRegen:   return "manaregen";
        case B::Other:       return "other";
        default:             return "unknown";
    }
}

std::string targetMaskString(uint8_t m) {
    using B = wowee::pipeline::WoweeBuffBook;
    std::string out;
    auto add = [&](const char* tag) {
        if (!out.empty()) out += "+";
        out += tag;
    };
    if (m & B::TargetSelf)     add("self");
    if (m & B::TargetParty)    add("party");
    if (m & B::TargetRaid)     add("raid");
    if (m & B::TargetFriendly) add("friendly");
    if (out.empty()) out = "none";
    return out;
}


void printGenSummary(const wowee::pipeline::WoweeBuffBook& c,
                     const std::string& base) {
    std::printf("Wrote %s.wbab\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buffs   : %zu\n", c.entries.size());
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageBuffBook";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbab");
    auto c = wowee::pipeline::WoweeBuffBookLoader::makeMage(name);
    if (!saveOrError<wowee::pipeline::WoweeBuffBookLoader>(c, base, "gen-bab", ".wbab")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenDruid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "DruidBuffBook";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbab");
    auto c = wowee::pipeline::WoweeBuffBookLoader::makeDruid(name);
    if (!saveOrError<wowee::pipeline::WoweeBuffBookLoader>(c, base, "gen-bab-druid", ".wbab")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaidMax(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidMaxBuffs";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wbab");
    auto c = wowee::pipeline::WoweeBuffBookLoader::makeRaidMax(name);
    if (!saveOrError<wowee::pipeline::WoweeBuffBookLoader>(c, base, "gen-bab-raid", ".wbab")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbab");
    if (!wowee::pipeline::WoweeBuffBookLoader::exists(base)) {
        return reportMissing("WBAB", base, ".wbab");
    }
    auto c = wowee::pipeline::WoweeBuffBookLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wbab"] = base + ".wbab";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"buffId", e.buffId},
                {"name", e.name},
                {"description", e.description},
                {"spellId", e.spellId},
                {"castClassMask", e.castClassMask},
                {"targetTypeMask", e.targetTypeMask},
                {"targetTypeNames", targetMaskString(e.targetTypeMask)},
                {"statBonusKind", e.statBonusKind},
                {"statBonusKindName",
                    statBonusKindName(e.statBonusKind)},
                {"rank", e.rank},
                {"maxStackCount", e.maxStackCount},
                {"statBonusAmount", e.statBonusAmount},
                {"duration", e.duration},
                {"previousRankId", e.previousRankId},
                {"nextRankId", e.nextRankId},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WBAB: %s.wbab\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buffs   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   spell  class  tgt              stat        rk  amt   dur(s)  prev   next  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %5u   %4u  %-15s  %-11s  %2u  %4d  %5u  %4u  %4u  %s\n",
                    e.buffId, e.spellId, e.castClassMask,
                    targetMaskString(e.targetTypeMask).c_str(),
                    statBonusKindName(e.statBonusKind),
                    e.rank, e.statBonusAmount, e.duration,
                    e.previousRankId, e.nextRankId,
                    e.name.c_str());
    }
    return 0;
}

// Token parser for statBonusKind. Returns -1 if unknown.
int parseStatBonusKindToken(const std::string& s) {
    using B = wowee::pipeline::WoweeBuffBook;
    if (s == "stamina")     return B::Stamina;
    if (s == "intellect")   return B::Intellect;
    if (s == "spirit")      return B::Spirit;
    if (s == "allstats")    return B::AllStats;
    if (s == "armor")       return B::Armor;
    if (s == "spellpower")  return B::SpellPower;
    if (s == "attackpower") return B::AttackPower;
    if (s == "critrating")  return B::CritRating;
    if (s == "hasterating") return B::HasteRating;
    if (s == "manaregen")   return B::ManaRegen;
    if (s == "other")       return B::Other;
    return -1;
}

// Parse a "self+party+raid" style bitmask string into the
// targetTypeMask bits. Empty / "none" returns 0; unknown
// tokens return -1 with no partial result. The "+" form
// is what targetMaskString emits on export so the round
// trip uses the same syntax.
int parseTargetMaskString(const std::string& s) {
    using B = wowee::pipeline::WoweeBuffBook;
    if (s.empty() || s == "none") return 0;
    int mask = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t plus = s.find('+', pos);
        std::string tok = (plus == std::string::npos)
            ? s.substr(pos) : s.substr(pos, plus - pos);
        if      (tok == "self")     mask |= B::TargetSelf;
        else if (tok == "party")    mask |= B::TargetParty;
        else if (tok == "raid")     mask |= B::TargetRaid;
        else if (tok == "friendly") mask |= B::TargetFriendly;
        else                         return -1;
        if (plus == std::string::npos) break;
        pos = plus + 1;
    }
    return mask;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wbab");
    if (out.empty()) out = base + ".wbab.json";
    if (!wowee::pipeline::WoweeBuffBookLoader::exists(base)) {
        return reportMissing("export-wbab-json", "WBAB", base, ".wbab");
    }
    auto c = wowee::pipeline::WoweeBuffBookLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WBAB";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"buffId", e.buffId},
            {"name", e.name},
            {"description", e.description},
            {"spellId", e.spellId},
            {"castClassMask", e.castClassMask},
            {"targetTypeMask", e.targetTypeMask},
            {"targetTypeNames", targetMaskString(e.targetTypeMask)},
            {"statBonusKind", e.statBonusKind},
            {"statBonusKindName",
                statBonusKindName(e.statBonusKind)},
            {"rank", e.rank},
            {"maxStackCount", e.maxStackCount},
            {"statBonusAmount", e.statBonusAmount},
            {"duration", e.duration},
            {"previousRankId", e.previousRankId},
            {"nextRankId", e.nextRankId},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wbab-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu buffs)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wbab");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wbab-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wbab-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeBuffBook c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wbab-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeBuffBook::Entry e;
        e.buffId = je.value("buffId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.spellId = je.value("spellId", 0u);
        e.castClassMask = je.value("castClassMask", 0u);
        // targetTypeMask: int OR "+"-joined token string.
        if (je.contains("targetTypeMask")) {
            const auto& tm = je["targetTypeMask"];
            if (tm.is_string()) {
                int parsed = parseTargetMaskString(tm.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wbab-json: unknown targetTypeMask "
                        "token in '%s' on entry id=%u\n",
                        tm.get<std::string>().c_str(), e.buffId);
                    return 1;
                }
                e.targetTypeMask = static_cast<uint8_t>(parsed);
            } else if (tm.is_number_integer()) {
                e.targetTypeMask = static_cast<uint8_t>(
                    tm.get<int>());
            }
        } else if (je.contains("targetTypeNames") &&
                   je["targetTypeNames"].is_string()) {
            int parsed = parseTargetMaskString(
                je["targetTypeNames"].get<std::string>());
            if (parsed >= 0)
                e.targetTypeMask = static_cast<uint8_t>(parsed);
        }
        // statBonusKind: int OR token string.
        if (je.contains("statBonusKind")) {
            const auto& sk = je["statBonusKind"];
            if (sk.is_string()) {
                int parsed = parseStatBonusKindToken(
                    sk.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wbab-json: unknown statBonusKind "
                        "token '%s' on entry id=%u\n",
                        sk.get<std::string>().c_str(), e.buffId);
                    return 1;
                }
                e.statBonusKind = static_cast<uint8_t>(parsed);
            } else if (sk.is_number_integer()) {
                e.statBonusKind = static_cast<uint8_t>(
                    sk.get<int>());
            }
        } else if (je.contains("statBonusKindName") &&
                   je["statBonusKindName"].is_string()) {
            int parsed = parseStatBonusKindToken(
                je["statBonusKindName"].get<std::string>());
            if (parsed >= 0)
                e.statBonusKind = static_cast<uint8_t>(parsed);
        }
        e.rank = static_cast<uint8_t>(je.value("rank", 1u));
        e.maxStackCount = static_cast<uint8_t>(
            je.value("maxStackCount", 1u));
        e.statBonusAmount = je.value("statBonusAmount", 0);
        e.duration = je.value("duration", 0u);
        e.previousRankId = je.value("previousRankId", 0u);
        e.nextRankId = je.value("nextRankId", 0u);
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeBuffBookLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wbab-json: failed to save %s.wbab\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wbab (%zu buffs)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wbab");
    if (!wowee::pipeline::WoweeBuffBookLoader::exists(base)) {
        return reportMissing("validate-wbab", "WBAB", base, ".wbab");
    }
    auto c = wowee::pipeline::WoweeBuffBookLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.buffId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.buffId == 0)
            errors.push_back(ctx + ": buffId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.spellId == 0) {
            errors.push_back(ctx +
                ": spellId is 0 — buff has no spell to "
                "cast");
        }
        if (e.castClassMask == 0) {
            errors.push_back(ctx +
                ": castClassMask is 0 — no class can cast "
                "this buff");
        }
        if (e.targetTypeMask == 0) {
            errors.push_back(ctx +
                ": targetTypeMask is 0 — buff has no valid "
                "targets");
        }
        if (e.statBonusKind > 9 && e.statBonusKind != 255) {
            errors.push_back(ctx + ": statBonusKind " +
                std::to_string(e.statBonusKind) +
                " out of range (must be 0..9 or 255 Other)");
        }
        if (e.rank == 0) {
            warnings.push_back(ctx +
                ": rank is 0 — ranks are 1-indexed; rank 0 "
                "may sort unexpectedly in spellbook UI");
        }
        if (e.maxStackCount == 0) {
            warnings.push_back(ctx +
                ": maxStackCount=0 — buff cannot be applied "
                "(zero stack ceiling)");
        }
        // Self-reference check: an entry's own id should
        // never appear in its own next/previous fields.
        if (e.previousRankId == e.buffId) {
            errors.push_back(ctx +
                ": previousRankId equals buffId — would "
                "create a 1-element rank cycle");
        }
        if (e.nextRankId == e.buffId) {
            errors.push_back(ctx +
                ": nextRankId equals buffId — would create "
                "a 1-element rank cycle");
        }
        if (!idsSeen.insert(e.buffId).second) {
            errors.push_back(ctx + ": duplicate buffId");
        }
    }
    // Cross-entry checks: validate the rank chain back-
    // edges. If A.nextRankId = B then B.previousRankId
    // must = A.buffId, and vice versa. Also detect
    // chain cycles.
    auto findIdx = [&](uint32_t id) -> int {
        for (size_t k = 0; k < c.entries.size(); ++k) {
            if (c.entries[k].buffId == id) {
                return static_cast<int>(k);
            }
        }
        return -1;
    };
    for (const auto& e : c.entries) {
        if (e.nextRankId != 0) {
            int next = findIdx(e.nextRankId);
            if (next < 0) {
                errors.push_back("entry id=" +
                    std::to_string(e.buffId) +
                    " (" + e.name + "): nextRankId=" +
                    std::to_string(e.nextRankId) +
                    " references missing entry");
            } else if (c.entries[next].previousRankId !=
                       e.buffId) {
                errors.push_back("rank chain back-edge "
                    "broken: id=" + std::to_string(e.buffId) +
                    " (" + e.name + ").nextRankId=" +
                    std::to_string(e.nextRankId) +
                    " but id=" + std::to_string(e.nextRankId) +
                    " (" + c.entries[next].name +
                    ").previousRankId=" +
                    std::to_string(
                        c.entries[next].previousRankId) +
                    " (expected " +
                    std::to_string(e.buffId) + ")");
            }
        }
        if (e.previousRankId != 0) {
            int prev = findIdx(e.previousRankId);
            if (prev < 0) {
                errors.push_back("entry id=" +
                    std::to_string(e.buffId) +
                    " (" + e.name + "): previousRankId=" +
                    std::to_string(e.previousRankId) +
                    " references missing entry");
            }
        }
    }
    return cli::reportValidation("wbab", base, jsonOut, errors, warnings,
                                 formatted("%zu buffs, all buffIds unique, "
                    "rank chain back-edges symmetric", c.entries.size()));
}

} // namespace

bool handleBuffBookCatalog(int& i, int argc, char** argv,
                            int& outRc) {
    if (std::strcmp(argv[i], "--gen-bab") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bab-druid") == 0 && i + 1 < argc) {
        outRc = handleGenDruid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-bab-raid") == 0 && i + 1 < argc) {
        outRc = handleGenRaidMax(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wbab") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wbab") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wbab-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wbab-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
