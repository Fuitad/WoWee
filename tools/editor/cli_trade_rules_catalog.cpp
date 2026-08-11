#include "cli_trade_rules_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_trade_rules.hpp"
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

const char* ruleKindName(uint8_t k) {
    using T = wowee::pipeline::WoweeTradeRules;
    switch (k) {
        case T::Allowed:             return "allowed";
        case T::Forbidden:           return "forbidden";
        case T::SoulboundException:  return "soulboundexception";
        case T::CrossFactionAllowed: return "crossfactionallowed";
        case T::LevelGated:          return "levelgated";
        case T::GoldEscrowMax:       return "goldescrowmax";
        case T::AuditLogged:         return "auditlogged";
        default:                     return "unknown";
    }
}

const char* targetingFilterName(uint8_t t) {
    using T = wowee::pipeline::WoweeTradeRules;
    switch (t) {
        case T::AnyPlayer:       return "anyplayer";
        case T::SameRealmOnly:   return "samerealmonly";
        case T::SameFactionOnly: return "samefactiononly";
        case T::SameAccountOnly: return "sameaccountonly";
        case T::GMOnly:          return "gmonly";
        default:                 return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeTradeRules& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtrd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rules   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardTradeRules";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrd");
    auto c = wowee::pipeline::WoweeTradeRulesLoader::makeStandard(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeRulesLoader>(c, base, "gen-trd", ".wtrd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenServerAdmin(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ServerAdminTradeRules";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrd");
    auto c = wowee::pipeline::WoweeTradeRulesLoader::makeServerAdmin(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeRulesLoader>(c, base, "gen-trd-admin", ".wtrd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRMT(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AntiRMTTradeRules";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtrd");
    auto c = wowee::pipeline::WoweeTradeRulesLoader::makeRMTPrevent(name);
    if (!saveOrError<wowee::pipeline::WoweeTradeRulesLoader>(c, base, "gen-trd-rmt", ".wtrd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtrd");
    if (!wowee::pipeline::WoweeTradeRulesLoader::exists(base)) {
        std::fprintf(stderr, "WTRD not found: %s.wtrd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTradeRulesLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtrd"] = base + ".wtrd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"ruleId", e.ruleId},
                {"name", e.name},
                {"description", e.description},
                {"ruleKind", e.ruleKind},
                {"ruleKindName", ruleKindName(e.ruleKind)},
                {"targetingFilter", e.targetingFilter},
                {"targetingFilterName",
                    targetingFilterName(e.targetingFilter)},
                {"levelRequirement", e.levelRequirement},
                {"priority", e.priority},
                {"itemCategoryFilter", e.itemCategoryFilter},
                {"goldEscrowMaxCopper", e.goldEscrowMaxCopper},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTRD: %s.wtrd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  rules   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   kind                  targeting          lvlReq  prio  catFilter   goldMax(c)    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-19s   %-15s     %3u   %3u   0x%08X   %12llu   %s\n",
                    e.ruleId,
                    ruleKindName(e.ruleKind),
                    targetingFilterName(e.targetingFilter),
                    e.levelRequirement, e.priority,
                    e.itemCategoryFilter,
                    (unsigned long long)e.goldEscrowMaxCopper,
                    e.name.c_str());
    }
    return 0;
}

int parseRuleKindToken(const std::string& s) {
    using T = wowee::pipeline::WoweeTradeRules;
    if (s == "allowed")             return T::Allowed;
    if (s == "forbidden")           return T::Forbidden;
    if (s == "soulboundexception")  return T::SoulboundException;
    if (s == "crossfactionallowed") return T::CrossFactionAllowed;
    if (s == "levelgated")          return T::LevelGated;
    if (s == "goldescrowmax")       return T::GoldEscrowMax;
    if (s == "auditlogged")         return T::AuditLogged;
    return -1;
}

int parseTargetingFilterToken(const std::string& s) {
    using T = wowee::pipeline::WoweeTradeRules;
    if (s == "anyplayer")       return T::AnyPlayer;
    if (s == "samerealmonly")   return T::SameRealmOnly;
    if (s == "samefactiononly") return T::SameFactionOnly;
    if (s == "sameaccountonly") return T::SameAccountOnly;
    if (s == "gmonly")          return T::GMOnly;
    return -1;
}

template <typename ParseFn>
bool readEnumField(const nlohmann::json& je,
                    const char* intKey,
                    const char* nameKey,
                    ParseFn parseFn,
                    const char* label,
                    uint32_t entryId,
                    uint8_t& outValue) {
    if (je.contains(intKey)) {
        const auto& v = je[intKey];
        if (v.is_string()) {
            int parsed = parseFn(v.get<std::string>());
            if (parsed < 0) {
                std::fprintf(stderr,
                    "import-wtrd-json: unknown %s token "
                    "'%s' on entry id=%u\n",
                    label, v.get<std::string>().c_str(),
                    entryId);
                return false;
            }
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
        if (v.is_number_integer()) {
            outValue = static_cast<uint8_t>(v.get<int>());
            return true;
        }
    }
    if (je.contains(nameKey) && je[nameKey].is_string()) {
        int parsed = parseFn(je[nameKey].get<std::string>());
        if (parsed >= 0) {
            outValue = static_cast<uint8_t>(parsed);
            return true;
        }
    }
    return true;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wtrd");
    if (out.empty()) out = base + ".wtrd.json";
    if (!wowee::pipeline::WoweeTradeRulesLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtrd-json: WTRD not found: %s.wtrd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTradeRulesLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WTRD";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"ruleId", e.ruleId},
            {"name", e.name},
            {"description", e.description},
            {"ruleKind", e.ruleKind},
            {"ruleKindName", ruleKindName(e.ruleKind)},
            {"targetingFilter", e.targetingFilter},
            {"targetingFilterName",
                targetingFilterName(e.targetingFilter)},
            {"levelRequirement", e.levelRequirement},
            {"priority", e.priority},
            {"itemCategoryFilter", e.itemCategoryFilter},
            {"goldEscrowMaxCopper", e.goldEscrowMaxCopper},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wtrd-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu rules)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wtrd");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wtrd-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wtrd-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeTradeRules c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wtrd-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeTradeRules::Entry e;
        e.ruleId = je.value("ruleId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "ruleKind", "ruleKindName",
                            parseRuleKindToken, "ruleKind",
                            e.ruleId, e.ruleKind)) return 1;
        if (!readEnumField(je, "targetingFilter",
                            "targetingFilterName",
                            parseTargetingFilterToken,
                            "targetingFilter",
                            e.ruleId, e.targetingFilter)) return 1;
        e.levelRequirement = static_cast<uint8_t>(
            je.value("levelRequirement", 0u));
        e.priority = static_cast<uint8_t>(je.value("priority", 1u));
        e.itemCategoryFilter = je.value("itemCategoryFilter", 0u);
        e.goldEscrowMaxCopper = je.value("goldEscrowMaxCopper",
                                            uint64_t{0});
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeTradeRulesLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtrd-json: failed to save %s.wtrd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtrd (%zu rules)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtrd");
    if (!wowee::pipeline::WoweeTradeRulesLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtrd: WTRD not found: %s.wtrd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTradeRulesLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.ruleId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.ruleId == 0)
            errors.push_back(ctx + ": ruleId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.ruleKind > 6) {
            errors.push_back(ctx + ": ruleKind " +
                std::to_string(e.ruleKind) +
                " out of range (must be 0..6)");
        }
        if (e.targetingFilter > 4) {
            errors.push_back(ctx + ": targetingFilter " +
                std::to_string(e.targetingFilter) +
                " out of range (must be 0..4)");
        }
        if (e.levelRequirement > 80) {
            warnings.push_back(ctx + ": levelRequirement " +
                std::to_string(e.levelRequirement) +
                " > 80 — exceeds current cap, the rule "
                "would never apply on a WotLK realm");
        }
        // Per-kind validity: GoldEscrowMax must specify
        // a non-zero gold cap to be meaningful (zero
        // would mean unlimited which contradicts the
        // rule's purpose).
        using T = wowee::pipeline::WoweeTradeRules;
        if (e.ruleKind == T::GoldEscrowMax &&
            e.goldEscrowMaxCopper == 0) {
            errors.push_back(ctx +
                ": GoldEscrowMax kind with goldEscrow"
                "MaxCopper=0 — rule contradicts itself "
                "(0 means unlimited but the rule's "
                "purpose is to cap)");
        }
        // GMOnly rules with low priority would be useless
        // (can't be triggered without GM intervention).
        if (e.targetingFilter == T::GMOnly &&
            e.priority < 50) {
            warnings.push_back(ctx +
                ": GMOnly targeting with priority " +
                std::to_string(e.priority) +
                " < 50 — GM-mediated trades typically "
                "need high priority to override player-"
                "initiated rules");
        }
        if (!idsSeen.insert(e.ruleId).second) {
            errors.push_back(ctx + ": duplicate ruleId");
        }
    }
    return cli::reportValidation("wtrd", base, jsonOut, errors, warnings,
                                 formatted("%zu rules, all ruleIds unique, "
                    "per-kind constraints satisfied", c.entries.size()));
}

} // namespace

bool handleTradeRulesCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-trd") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-trd-admin") == 0 && i + 1 < argc) {
        outRc = handleGenServerAdmin(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-trd-rmt") == 0 && i + 1 < argc) {
        outRc = handleGenRMT(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtrd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtrd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtrd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtrd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
