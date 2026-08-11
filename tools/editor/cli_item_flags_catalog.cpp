#include "cli_item_flags_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_item_flags.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeItemFlags& c,
                     const std::string& base) {
    std::printf("Wrote %s.wifs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  flags   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardItemFlags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wifs");
    auto c = wowee::pipeline::WoweeItemFlagsLoader::makeStandard(name);
    if (!saveOrError<wowee::pipeline::WoweeItemFlagsLoader>(c, base, "gen-ifs", ".wifs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBinding(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BindingItemFlags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wifs");
    auto c = wowee::pipeline::WoweeItemFlagsLoader::makeBinding(name);
    if (!saveOrError<wowee::pipeline::WoweeItemFlagsLoader>(c, base, "gen-ifs-binding", ".wifs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenServer(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ServerCustomItemFlags";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wifs");
    auto c = wowee::pipeline::WoweeItemFlagsLoader::makeServer(name);
    if (!saveOrError<wowee::pipeline::WoweeItemFlagsLoader>(c, base, "gen-ifs-server", ".wifs")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wifs");
    if (!wowee::pipeline::WoweeItemFlagsLoader::exists(base)) {
        std::fprintf(stderr, "WIFS not found: %s.wifs\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemFlagsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wifs"] = base + ".wifs";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"flagId", e.flagId},
                {"name", e.name},
                {"description", e.description},
                {"bitMask", e.bitMask},
                {"flagKind", e.flagKind},
                {"flagKindName", wowee::pipeline::WoweeItemFlags::flagKindName(e.flagKind)},
                {"isPositive", e.isPositive != 0},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WIFS: %s.wifs\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  flags   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    bitMask        kind        +/-   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   0x%08x     %-9s   %s    %s\n",
                    e.flagId, e.bitMask,
                    wowee::pipeline::WoweeItemFlags::flagKindName(e.flagKind),
                    e.isPositive ? "+" : "-",
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wifs");
    if (!wowee::pipeline::WoweeItemFlagsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wifs-json: WIFS not found: %s.wifs\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemFlagsLoader::load(base);
    if (outPath.empty()) outPath = base + ".wifs.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["flagId"] = e.flagId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["bitMask"] = e.bitMask;
        je["flagKind"] = e.flagKind;
        je["flagKindName"] =
            wowee::pipeline::WoweeItemFlags::flagKindName(e.flagKind);
        je["isPositive"] = e.isPositive != 0;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wifs-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  flags   : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseFlagKindToken(const nlohmann::json& jv,
                           uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeItemFlags::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "quality") return wowee::pipeline::WoweeItemFlags::Quality;
        if (s == "drop")    return wowee::pipeline::WoweeItemFlags::Drop;
        if (s == "trade")   return wowee::pipeline::WoweeItemFlags::Trade;
        if (s == "magic")   return wowee::pipeline::WoweeItemFlags::Magic;
        if (s == "account") return wowee::pipeline::WoweeItemFlags::Account;
        if (s == "server")  return wowee::pipeline::WoweeItemFlags::Server;
        if (s == "misc")    return wowee::pipeline::WoweeItemFlags::Misc;
    }
    return fallback;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wifs-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wifs-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeItemFlags c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItemFlags::Entry e;
            if (je.contains("flagId"))      e.flagId = je["flagId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            if (je.contains("bitMask"))     e.bitMask = je["bitMask"].get<uint32_t>();
            uint8_t kind = wowee::pipeline::WoweeItemFlags::Misc;
            if (je.contains("flagKind"))
                kind = parseFlagKindToken(je["flagKind"], kind);
            else if (je.contains("flagKindName"))
                kind = parseFlagKindToken(je["flagKindName"], kind);
            e.flagKind = kind;
            if (je.contains("isPositive")) {
                if (je["isPositive"].is_boolean())
                    e.isPositive = je["isPositive"].get<bool>() ? 1 : 0;
                else
                    e.isPositive = je["isPositive"].get<uint8_t>() ? 1 : 0;
            }
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wifs");
    outBase = cli::withoutExt(outBase, ".wifs");
    if (!wowee::pipeline::WoweeItemFlagsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wifs-json: failed to save %s.wifs\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wifs\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  flags   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wifs");
    if (!wowee::pipeline::WoweeItemFlagsLoader::exists(base)) {
        return reportMissing("validate-wifs", "WIFS", base, ".wifs");
    }
    auto c = wowee::pipeline::WoweeItemFlagsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    std::vector<uint32_t> bitsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.flagId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.flagId == 0)
            errors.push_back(ctx + ": flagId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.flagKind > wowee::pipeline::WoweeItemFlags::Misc) {
            errors.push_back(ctx + ": flagKind " +
                std::to_string(e.flagKind) + " not in 0..6");
        }
        if (e.bitMask == 0) {
            errors.push_back(ctx +
                ": bitMask is 0 — flag will never match anything");
        }
        // bitMask should typically be a single bit (power
        // of 2). Multi-bit masks are valid but unusual —
        // warn so author can confirm.
        if (e.bitMask != 0 && (e.bitMask & (e.bitMask - 1)) != 0) {
            warnings.push_back(ctx +
                ": bitMask 0x" + std::to_string(e.bitMask) +
                " is not a single bit (multi-bit flags are "
                "unusual; usually you want one of the "
                "individual bits)");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.flagId) {
                errors.push_back(ctx + ": duplicate flagId");
                break;
            }
        }
        idsSeen.push_back(e.flagId);
        // Two flags claiming the same bit is a serious
        // collision — engine would only match the first
        // entry's name when decoding.
        if (e.bitMask != 0) {
            for (uint32_t prevBit : bitsSeen) {
                if (prevBit == e.bitMask) {
                    errors.push_back(ctx +
                        ": duplicate bitMask 0x" +
                        std::to_string(e.bitMask) +
                        " — collides with another entry");
                    break;
                }
            }
            bitsSeen.push_back(e.bitMask);
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wifs", base, errors, warnings);
    std::printf("validate-wifs: %s.wifs\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu flags, all flagIds + bitMasks unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleItemFlagsCatalog(int& i, int argc, char** argv,
                            int& outRc) {
    if (std::strcmp(argv[i], "--gen-ifs") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ifs-binding") == 0 && i + 1 < argc) {
        outRc = handleGenBinding(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-ifs-server") == 0 && i + 1 < argc) {
        outRc = handleGenServer(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wifs") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wifs") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wifs-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wifs-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
