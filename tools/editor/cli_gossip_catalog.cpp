#include "cli_gossip_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_gossip.hpp"
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

void appendOptFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeGossip::AllianceOnly) s += "alliance ";
    if (flags & wowee::pipeline::WoweeGossip::HordeOnly)    s += "horde ";
    if (flags & wowee::pipeline::WoweeGossip::Coinpouch)    s += "coin ";
    if (flags & wowee::pipeline::WoweeGossip::QuestGated)   s += "quest-gated ";
    if (flags & wowee::pipeline::WoweeGossip::Closes)       s += "closes ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}

bool saveOrError(const wowee::pipeline::WoweeGossip& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeGossipLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wgsp\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

uint32_t totalOptions(const wowee::pipeline::WoweeGossip& c) {
    uint32_t n = 0;
    for (const auto& e : c.entries) n += static_cast<uint32_t>(e.options.size());
    return n;
}

void printGenSummary(const wowee::pipeline::WoweeGossip& c,
                     const std::string& base) {
    std::printf("Wrote %s.wgsp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  menus   : %zu (%u options total)\n",
                c.entries.size(), totalOptions(c));
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterGossip";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgsp");
    auto c = wowee::pipeline::WoweeGossipLoader::makeStarter(name);
    if (!saveOrError(c, base, "gen-gossip")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenInnkeeper(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "InnkeeperGossip";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgsp");
    auto c = wowee::pipeline::WoweeGossipLoader::makeInnkeeper(name);
    if (!saveOrError(c, base, "gen-gossip-innkeeper")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuestGiver(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestGiverGossip";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wgsp");
    auto c = wowee::pipeline::WoweeGossipLoader::makeQuestGiver(name);
    if (!saveOrError(c, base, "gen-gossip-questgiver")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgsp");
    if (!wowee::pipeline::WoweeGossipLoader::exists(base)) {
        std::fprintf(stderr, "WGSP not found: %s.wgsp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGossipLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wgsp"] = base + ".wgsp";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        j["totalOptions"] = totalOptions(c);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["menuId"] = e.menuId;
            je["titleText"] = e.titleText;
            nlohmann::json opts = nlohmann::json::array();
            for (const auto& o : e.options) {
                std::string fs;
                appendOptFlagsStr(fs, o.requiredFlags);
                opts.push_back({
                    {"optionId", o.optionId},
                    {"text", o.text},
                    {"kind", o.kind},
                    {"kindName", wowee::pipeline::WoweeGossip::optionKindName(o.kind)},
                    {"actionTarget", o.actionTarget},
                    {"requiredFlags", o.requiredFlags},
                    {"requiredFlagsStr", fs},
                    {"moneyCostCopper", o.moneyCostCopper},
                });
            }
            je["options"] = opts;
            arr.push_back(je);
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WGSP: %s.wgsp\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  menus   : %zu (%u options total)\n",
                c.entries.size(), totalOptions(c));
    if (c.entries.empty()) return 0;
    for (const auto& e : c.entries) {
        std::printf("\n  menuId=%u\n", e.menuId);
        std::printf("    title : \"%s\"\n", e.titleText.c_str());
        if (e.options.empty()) {
            std::printf("    *no options*\n");
            continue;
        }
        for (const auto& o : e.options) {
            std::string fs;
            appendOptFlagsStr(fs, o.requiredFlags);
            std::printf("    [%-9s] target=%-5u  cost=%-6uc  flags=%s\n",
                        wowee::pipeline::WoweeGossip::optionKindName(o.kind),
                        o.actionTarget, o.moneyCostCopper, fs.c_str());
            std::printf("                 \"%s\"\n", o.text.c_str());
        }
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each menu emits scalar fields plus the
    // options array; option.kind and requiredFlags emit dual
    // int + name forms.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wgsp");
    if (outPath.empty()) outPath = base + ".wgsp.json";
    if (!wowee::pipeline::WoweeGossipLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wgsp-json: WGSP not found: %s.wgsp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGossipLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["menuId"] = e.menuId;
        je["titleText"] = e.titleText;
        nlohmann::json opts = nlohmann::json::array();
        for (const auto& o : e.options) {
            nlohmann::json jo;
            jo["optionId"] = o.optionId;
            jo["text"] = o.text;
            jo["kind"] = o.kind;
            jo["kindName"] = wowee::pipeline::WoweeGossip::optionKindName(o.kind);
            jo["actionTarget"] = o.actionTarget;
            jo["requiredFlags"] = o.requiredFlags;
            nlohmann::json fa = nlohmann::json::array();
            if (o.requiredFlags & wowee::pipeline::WoweeGossip::AllianceOnly) fa.push_back("alliance");
            if (o.requiredFlags & wowee::pipeline::WoweeGossip::HordeOnly)    fa.push_back("horde");
            if (o.requiredFlags & wowee::pipeline::WoweeGossip::Coinpouch)    fa.push_back("coin");
            if (o.requiredFlags & wowee::pipeline::WoweeGossip::QuestGated)   fa.push_back("quest-gated");
            if (o.requiredFlags & wowee::pipeline::WoweeGossip::Closes)       fa.push_back("closes");
            jo["requiredFlagsList"] = fa;
            jo["moneyCostCopper"] = o.moneyCostCopper;
            opts.push_back(jo);
        }
        je["options"] = opts;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wgsp-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wgsp\n", base.c_str());
    std::printf("  menus  : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wgsp");
    outBase = cli::withoutExt(outBase, ".wgsp");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wgsp-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wgsp-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "close")        return wowee::pipeline::WoweeGossip::Close;
        if (s == "submenu")      return wowee::pipeline::WoweeGossip::Submenu;
        if (s == "vendor")       return wowee::pipeline::WoweeGossip::Vendor;
        if (s == "trainer")      return wowee::pipeline::WoweeGossip::Trainer;
        if (s == "quest")        return wowee::pipeline::WoweeGossip::Quest;
        if (s == "tabard")       return wowee::pipeline::WoweeGossip::Tabard;
        if (s == "banker")       return wowee::pipeline::WoweeGossip::Banker;
        if (s == "innkeeper")    return wowee::pipeline::WoweeGossip::Innkeeper;
        if (s == "flight")       return wowee::pipeline::WoweeGossip::FlightMaster;
        if (s == "text")         return wowee::pipeline::WoweeGossip::TextOnly;
        if (s == "script")       return wowee::pipeline::WoweeGossip::Script;
        if (s == "battlemaster") return wowee::pipeline::WoweeGossip::Battlemaster;
        if (s == "auctioneer")   return wowee::pipeline::WoweeGossip::Auctioneer;
        return wowee::pipeline::WoweeGossip::TextOnly;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "alliance")     return wowee::pipeline::WoweeGossip::AllianceOnly;
        if (s == "horde")        return wowee::pipeline::WoweeGossip::HordeOnly;
        if (s == "coin")         return wowee::pipeline::WoweeGossip::Coinpouch;
        if (s == "quest-gated")  return wowee::pipeline::WoweeGossip::QuestGated;
        if (s == "closes")       return wowee::pipeline::WoweeGossip::Closes;
        return 0;
    };
    wowee::pipeline::WoweeGossip c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeGossip::Entry e;
            e.menuId = je.value("menuId", 0u);
            e.titleText = je.value("titleText", std::string{});
            if (je.contains("options") && je["options"].is_array()) {
                for (const auto& jo : je["options"]) {
                    wowee::pipeline::WoweeGossip::Option o;
                    o.optionId = jo.value("optionId", 0u);
                    o.text = jo.value("text", std::string{});
                    if (jo.contains("kind") && jo["kind"].is_number_integer()) {
                        o.kind = static_cast<uint8_t>(jo["kind"].get<int>());
                    } else if (jo.contains("kindName") && jo["kindName"].is_string()) {
                        o.kind = kindFromName(jo["kindName"].get<std::string>());
                    }
                    o.actionTarget = jo.value("actionTarget", 0u);
                    if (jo.contains("requiredFlags") &&
                        jo["requiredFlags"].is_number_integer()) {
                        o.requiredFlags = jo["requiredFlags"].get<uint32_t>();
                    } else if (jo.contains("requiredFlagsList") &&
                               jo["requiredFlagsList"].is_array()) {
                        for (const auto& f : jo["requiredFlagsList"]) {
                            if (f.is_string())
                                o.requiredFlags |= flagFromName(f.get<std::string>());
                        }
                    }
                    o.moneyCostCopper = jo.value("moneyCostCopper", 0u);
                    e.options.push_back(o);
                }
            }
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeGossipLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wgsp-json: failed to save %s.wgsp\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wgsp\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  menus  : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wgsp");
    if (!wowee::pipeline::WoweeGossipLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wgsp: WGSP not found: %s.wgsp\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeGossipLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    idsSeen.reserve(c.entries.size());
    // Build the set of menuIds present so we can verify
    // intra-format Submenu cross-references resolve.
    std::vector<uint32_t> menuIds;
    menuIds.reserve(c.entries.size());
    for (const auto& e : c.entries) menuIds.push_back(e.menuId);
    auto hasMenu = [&](uint32_t id) {
        for (uint32_t m : menuIds) if (m == id) return true;
        return false;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (menuId=" + std::to_string(e.menuId) + ")";
        if (e.menuId == 0) {
            errors.push_back(ctx + ": menuId is 0");
        }
        if (e.titleText.empty()) {
            warnings.push_back(ctx + ": titleText is empty");
        }
        if (e.options.empty()) {
            warnings.push_back(ctx +
                ": no options (player has no way to dismiss the menu)");
        }
        // Alliance + Horde restriction is mutually exclusive.
        for (size_t oi = 0; oi < e.options.size(); ++oi) {
            const auto& o = e.options[oi];
            std::string octx = ctx + " option " + std::to_string(oi);
            if (o.kind > wowee::pipeline::WoweeGossip::Auctioneer) {
                errors.push_back(octx + ": kind " +
                    std::to_string(o.kind) + " not in known range 0..12");
            }
            if (o.text.empty()) {
                errors.push_back(octx + ": text is empty");
            }
            // Submenu must point at a menuId that exists in the catalog.
            if (o.kind == wowee::pipeline::WoweeGossip::Submenu) {
                if (o.actionTarget == 0) {
                    errors.push_back(octx + ": Submenu option has actionTarget=0");
                } else if (!hasMenu(o.actionTarget)) {
                    errors.push_back(octx + ": Submenu actionTarget " +
                        std::to_string(o.actionTarget) +
                        " does not exist in this catalog");
                }
            }
            // Coinpouch flag without moneyCost is misleading — the
            // coin icon would show with no actual fee.
            if ((o.requiredFlags & wowee::pipeline::WoweeGossip::Coinpouch) &&
                o.moneyCostCopper == 0) {
                warnings.push_back(octx +
                    ": Coinpouch flag set but moneyCostCopper=0");
            }
            if ((o.requiredFlags & wowee::pipeline::WoweeGossip::AllianceOnly) &&
                (o.requiredFlags & wowee::pipeline::WoweeGossip::HordeOnly)) {
                errors.push_back(octx +
                    ": AllianceOnly and HordeOnly both set (incoherent)");
            }
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.menuId) {
                errors.push_back(ctx + ": duplicate menuId");
                break;
            }
        }
        idsSeen.push_back(e.menuId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wgsp", base, errors, warnings);
    std::printf("validate-wgsp: %s.wgsp\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu menus (%u options), all menuIds unique\n",
                    c.entries.size(), totalOptions(c));
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleGossipCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-gossip") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gossip-innkeeper") == 0 && i + 1 < argc) {
        outRc = handleGenInnkeeper(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-gossip-questgiver") == 0 && i + 1 < argc) {
        outRc = handleGenQuestGiver(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wgsp") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wgsp") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wgsp-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wgsp-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
