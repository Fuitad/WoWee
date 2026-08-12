#include "cli_guild_bank_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_guild_bank.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {


void printGenSummary(const wowee::pipeline::WoweeGuildBank& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgbk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardGuildBank";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgbk");
    auto c = wowee::pipeline::WoweeGuildBankLoader::
        makeStandardBank(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildBankLoader>(c, base, "gen-gbk", ".wgbk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRaid(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RaidGuildBank";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgbk");
    auto c = wowee::pipeline::WoweeGuildBankLoader::
        makeRaidGuild(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildBankLoader>(c, base, "gen-gbk-raid", ".wgbk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSmall(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SmallGuildBank";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgbk");
    auto c = wowee::pipeline::WoweeGuildBankLoader::
        makeSmallGuild(name);
    if (!saveOrError<wowee::pipeline::WoweeGuildBankLoader>(c, base, "gen-gbk-small", ".wgbk")) return 1;
    printGenSummary(c, base);
    return 0;
}

std::string formatLimit(uint32_t v) {
    using G = wowee::pipeline::WoweeGuildBank;
    if (v == G::kUnlimited) return "INF";
    return std::to_string(v);
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgbk");
    if (!wowee::pipeline::WoweeGuildBankLoader::exists(base)) {
        std::fprintf(stderr, "WGBK not found: %s.wgbk\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGuildBankLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgbk"] = base + ".wgbk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json limits = nlohmann::json::array();
            for (uint32_t r = 0;
                 r < wowee::pipeline::WoweeGuildBank::
                     kRankCount; ++r) {
                limits.push_back(e.perRankWithdrawalLimit[r]);
            }
            arr.push_back({
                {"tabId", e.tabId},
                {"guildId", e.guildId},
                {"tabName", e.tabName},
                {"iconIndex", e.iconIndex},
                {"depositOnly", e.depositOnly != 0},
                {"slotCount", e.slotCount},
                {"perRankWithdrawalLimit", limits},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGBK: %s.wgbk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabs    : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  guild  slots  dep  R0   R1   R2   R3   R4   R5   R6   R7   tabName\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %5u  %5u   %s ",
                    e.tabId, e.guildId, e.slotCount,
                    e.depositOnly ? "Y" : "n");
        for (uint32_t r = 0;
             r < wowee::pipeline::WoweeGuildBank::
                 kRankCount; ++r) {
            std::printf(" %4s",
                        formatLimit(e.perRankWithdrawalLimit[r])
                            .c_str());
        }
        std::printf("   %s\n", e.tabName.c_str());
    }
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgbk");
    if (!wowee::pipeline::WoweeGuildBankLoader::exists(base)) {
        return reportMissing("validate-wgbk", "WGBK", base, ".wgbk");
    }
    auto c = wowee::pipeline::WoweeGuildBankLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    using G = wowee::pipeline::WoweeGuildBank;
    std::set<uint32_t> idsSeen;
    using Pair = std::pair<uint32_t, std::string>;
    std::set<Pair> guildTabPairs;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tabId) +
                          " guildId=" + std::to_string(e.guildId);
        if (!e.tabName.empty()) ctx += " " + e.tabName;
        ctx += ")";
        if (e.tabId == 0)
            errors.push_back(ctx + ": tabId is 0");
        if (e.guildId == 0)
            errors.push_back(ctx + ": guildId is 0");
        if (e.tabName.empty())
            errors.push_back(ctx + ": tabName is empty");
        if (e.slotCount == 0) {
            errors.push_back(ctx +
                ": slotCount is 0 - empty tab is unusable");
        }
        if (e.slotCount > G::kMaxSlots) {
            errors.push_back(ctx + ": slotCount " +
                std::to_string(e.slotCount) +
                " exceeds vanilla cap of " +
                std::to_string(G::kMaxSlots));
        }
        // GuildMaster (rank 0) must have at least
        // some withdrawal access - usually unlimited.
        // A 0 here means even the GM can't withdraw,
        // which is almost certainly a bug.
        if (e.perRankWithdrawalLimit[0] == 0) {
            errors.push_back(ctx +
                ": perRankWithdrawalLimit[0]=0 - GuildMaster"
                " cannot withdraw, almost certainly a typo");
        }
        // Per-rank monotonicity: a lower rank should
        // not have a HIGHER limit than a higher rank.
        // Walk pairs (rank R+1 vs rank R) and flag.
        // Treat kUnlimited as "infinity" for compare.
        auto rankVal = [&](uint32_t r) -> uint64_t {
            if (e.perRankWithdrawalLimit[r] == G::kUnlimited)
                return UINT64_MAX;
            return e.perRankWithdrawalLimit[r];
        };
        for (uint32_t r = 0; r + 1 < G::kRankCount; ++r) {
            uint64_t hi = rankVal(r);
            uint64_t lo = rankVal(r + 1);
            if (lo > hi) {
                errors.push_back(ctx +
                    ": perRankWithdrawalLimit[" +
                    std::to_string(r + 1) +
                    "]=" +
                    formatLimit(e.perRankWithdrawalLimit[r + 1]) +
                    " > rank[" + std::to_string(r) + "]=" +
                    formatLimit(e.perRankWithdrawalLimit[r]) +
                    " - lower rank cannot exceed higher rank's"
                    " withdrawal cap");
            }
        }
        // depositOnly + non-zero withdrawal limit on
        // rank 0 = self-contradiction. WGBK semantics:
        // depositOnly is a tab-wide flag overriding
        // the per-rank table. Warn.
        if (e.depositOnly &&
            e.perRankWithdrawalLimit[0] != 0) {
            warnings.push_back(ctx +
                ": depositOnly flag set but rank 0 has "
                "withdrawal limit " +
                formatLimit(e.perRankWithdrawalLimit[0]) +
                " - flag will override the table at "
                "runtime, but the data is contradictory");
        }
        // Duplicate (guildId, tabName) - UI dispatch
        // would tie.
        if (e.guildId != 0 && !e.tabName.empty()) {
            Pair p{e.guildId, e.tabName};
            if (!guildTabPairs.insert(p).second) {
                errors.push_back(ctx +
                    ": duplicate (guildId=" +
                    std::to_string(e.guildId) +
                    ", tabName=" + e.tabName +
                    ") - UI tab dispatch ambiguous");
            }
        }
        if (!idsSeen.insert(e.tabId).second) {
            errors.push_back(ctx + ": duplicate tabId");
        }
    }
    return cli::reportValidation("wgbk", base, jsonOut, errors, warnings,
                                 formatted("%zu tabs, all tabIds + "
                    "(guildId,tabName) unique, slotCount "
                    "1..98, GM withdrawal > 0, per-rank "
                    "monotonicity (lower rank <= higher "
                    "rank cap)", c.entries.size()));
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wgbk");
    if (out.empty()) out = base + ".wgbk.json";
    if (!wowee::pipeline::WoweeGuildBankLoader::exists(base)) {
        return reportMissing("export-wgbk-json", "WGBK", base, ".wgbk");
    }
    auto c = wowee::pipeline::WoweeGuildBankLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WGBK";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json limits = nlohmann::json::array();
        for (uint32_t r = 0;
             r < wowee::pipeline::WoweeGuildBank::
                 kRankCount; ++r) {
            limits.push_back(e.perRankWithdrawalLimit[r]);
        }
        arr.push_back({
            {"tabId", e.tabId},
            {"guildId", e.guildId},
            {"tabName", e.tabName},
            {"iconIndex", e.iconIndex},
            {"depositOnly", e.depositOnly != 0},
            {"slotCount", e.slotCount},
            {"perRankWithdrawalLimit", limits},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wgbk-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu tabs)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wgbk");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wgbk-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wgbk-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeGuildBank c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wgbk-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeGuildBank::Entry e;
        e.tabId = je.value("tabId", 0u);
        e.guildId = je.value("guildId", 0u);
        e.tabName = je.value("tabName", std::string{});
        e.iconIndex = je.value("iconIndex", 0u);
        e.depositOnly = je.value("depositOnly", false) ? 1 : 0;
        e.slotCount = static_cast<uint16_t>(
            je.value("slotCount", 0));
        if (je.contains("perRankWithdrawalLimit") &&
            je["perRankWithdrawalLimit"].is_array()) {
            uint32_t r = 0;
            for (const auto& v :
                 je["perRankWithdrawalLimit"]) {
                if (r >= wowee::pipeline::WoweeGuildBank::
                          kRankCount) break;
                if (v.is_number_unsigned() ||
                    v.is_number_integer()) {
                    e.perRankWithdrawalLimit[r] =
                        v.get<uint32_t>();
                }
                ++r;
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeGuildBankLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgbk-json: failed to save %s.wgbk\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgbk (%zu tabs)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleGuildBankCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-gbk") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gbk-raid") == 0 &&
        i + 1 < argc) {
        outRc = handleGenRaid(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gbk-small") == 0 &&
        i + 1 < argc) {
        outRc = handleGenSmall(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgbk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgbk") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgbk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgbk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
