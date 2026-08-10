#include "cli_tokens_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_tokens.hpp"
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

void appendTknFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeToken::AccountWide)       s += "account ";
    if (flags & wowee::pipeline::WoweeToken::Tradeable)         s += "trade ";
    if (flags & wowee::pipeline::WoweeToken::HiddenUntilEarned) s += "hidden ";
    if (flags & wowee::pipeline::WoweeToken::ResetsOnLogout)    s += "resets ";
    if (flags & wowee::pipeline::WoweeToken::ConvertsToGold)    s += "to-gold ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

bool saveOrError(const wowee::pipeline::WoweeToken& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeTokenLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wtkn\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeToken& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtkn\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tokens  : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterTokens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtkn");
    auto c = wowee::pipeline::WoweeTokenLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-tokens")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvp(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvpTokens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtkn");
    auto c = wowee::pipeline::WoweeTokenLoader::makePvp(name);
    if (!saveOrError(c, base, "gen-tokens-pvp")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenSeasonal(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "SeasonalTokens";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtkn");
    auto c = wowee::pipeline::WoweeTokenLoader::makeSeasonal(name);
    if (!saveOrError(c, base, "gen-tokens-seasonal")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtkn");
    if (!wowee::pipeline::WoweeTokenLoader::exists(base)) {
        std::fprintf(stderr, "WTKN not found: %s.wtkn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtkn"] = base + ".wtkn";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendTknFlagsStr(fs, e.flags);
            arr.push_back({
                {"tokenId", e.tokenId},
                {"name", e.name},
                {"description", e.description},
                {"iconPath", e.iconPath},
                {"category", e.category},
                {"categoryName", wowee::pipeline::WoweeToken::categoryName(e.category)},
                {"maxBalance", e.maxBalance},
                {"weeklyCap", e.weeklyCap},
                {"flags", e.flags},
                {"flagsStr", fs},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTKN: %s.wtkn\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tokens  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   category   maxBal      weekly   flags         name\n");
    for (const auto& e : c.entries) {
        std::string fs;
        appendTknFlagsStr(fs, e.flags);
        std::printf("  %4u   %-9s  %7u    %5u    %-12s  %s\n",
                    e.tokenId,
                    wowee::pipeline::WoweeToken::categoryName(e.category),
                    e.maxBalance, e.weeklyCap, fs.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each token emits all 8 scalar fields
    // plus dual int + name forms for category and the flags
    // bitset.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wtkn");
    if (outPath.empty()) outPath = base + ".wtkn.json";
    if (!wowee::pipeline::WoweeTokenLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtkn-json: WTKN not found: %s.wtkn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["tokenId"] = e.tokenId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["iconPath"] = e.iconPath;
        je["category"] = e.category;
        je["categoryName"] = wowee::pipeline::WoweeToken::categoryName(e.category);
        je["maxBalance"] = e.maxBalance;
        je["weeklyCap"] = e.weeklyCap;
        je["flags"] = e.flags;
        nlohmann::json fa = nlohmann::json::array();
        if (e.flags & wowee::pipeline::WoweeToken::AccountWide)       fa.push_back("account");
        if (e.flags & wowee::pipeline::WoweeToken::Tradeable)         fa.push_back("trade");
        if (e.flags & wowee::pipeline::WoweeToken::HiddenUntilEarned) fa.push_back("hidden");
        if (e.flags & wowee::pipeline::WoweeToken::ResetsOnLogout)    fa.push_back("resets");
        if (e.flags & wowee::pipeline::WoweeToken::ConvertsToGold)    fa.push_back("to-gold");
        je["flagsList"] = fa;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wtkn-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wtkn\n", base.c_str());
    std::printf("  tokens : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = jsonPath;
        std::string suffix = ".wtkn.json";
        if (outBase.size() > suffix.size() &&
            outBase.substr(outBase.size() - suffix.size()) == suffix) {
            outBase = outBase.substr(0, outBase.size() - suffix.size());
        } else if (outBase.size() > 5 &&
                   outBase.substr(outBase.size() - 5) == ".json") {
            outBase = outBase.substr(0, outBase.size() - 5);
        }
    }
    outBase = cli::withoutExt(outBase, ".wtkn");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wtkn-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wtkn-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto categoryFromName = [](const std::string& s) -> uint8_t {
        if (s == "misc")     return wowee::pipeline::WoweeToken::Misc;
        if (s == "pvp")      return wowee::pipeline::WoweeToken::Pvp;
        if (s == "rep")      return wowee::pipeline::WoweeToken::Reputation;
        if (s == "crafting") return wowee::pipeline::WoweeToken::Crafting;
        if (s == "seasonal") return wowee::pipeline::WoweeToken::Seasonal;
        if (s == "holiday")  return wowee::pipeline::WoweeToken::Holiday;
        return wowee::pipeline::WoweeToken::Misc;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "account") return wowee::pipeline::WoweeToken::AccountWide;
        if (s == "trade")   return wowee::pipeline::WoweeToken::Tradeable;
        if (s == "hidden")  return wowee::pipeline::WoweeToken::HiddenUntilEarned;
        if (s == "resets")  return wowee::pipeline::WoweeToken::ResetsOnLogout;
        if (s == "to-gold") return wowee::pipeline::WoweeToken::ConvertsToGold;
        return 0;
    };
    wowee::pipeline::WoweeToken c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeToken::Entry e;
            e.tokenId = je.value("tokenId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.iconPath = je.value("iconPath", std::string{});
            if (je.contains("category") && je["category"].is_number_integer()) {
                e.category = static_cast<uint8_t>(je["category"].get<int>());
            } else if (je.contains("categoryName") && je["categoryName"].is_string()) {
                e.category = categoryFromName(je["categoryName"].get<std::string>());
            }
            e.maxBalance = je.value("maxBalance", 0u);
            e.weeklyCap = je.value("weeklyCap", 0u);
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeTokenLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtkn-json: failed to save %s.wtkn\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtkn\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  tokens : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtkn");
    if (!wowee::pipeline::WoweeTokenLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtkn: WTKN not found: %s.wtkn\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTokenLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tokenId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tokenId == 0) errors.push_back(ctx + ": tokenId is 0");
        if (e.name.empty()) errors.push_back(ctx + ": name is empty");
        if (e.category > wowee::pipeline::WoweeToken::Holiday) {
            errors.push_back(ctx + ": category " +
                std::to_string(e.category) + " not in 0..5");
        }
        if (e.weeklyCap > 0 && e.maxBalance > 0 &&
            e.weeklyCap > e.maxBalance) {
            warnings.push_back(ctx +
                ": weeklyCap exceeds maxBalance (cap is unreachable)");
        }
        if ((e.flags & wowee::pipeline::WoweeToken::ResetsOnLogout) &&
            (e.flags & wowee::pipeline::WoweeToken::AccountWide)) {
            errors.push_back(ctx +
                ": ResetsOnLogout and AccountWide both set (incoherent)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.tokenId) {
                errors.push_back(ctx + ": duplicate tokenId");
                break;
            }
        }
        idsSeen.push_back(e.tokenId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wtkn", base, errors, warnings);
    std::printf("validate-wtkn: %s.wtkn\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu tokens, all tokenIds unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleTokensCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-tokens") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tokens-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenPvp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tokens-seasonal") == 0 && i + 1 < argc) {
        outRc = handleGenSeasonal(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtkn") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtkn") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtkn-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtkn-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
