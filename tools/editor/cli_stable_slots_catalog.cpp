#include "cli_stable_slots_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_stable_slots.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeStableSlot& c,
                     const std::string& base) {
    std::printf("Wrote %s.wstc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardStableSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstc");
    auto c = wowee::pipeline::WoweeStableSlotLoader::makeStandard(name);
    if (!saveOrError<wowee::pipeline::WoweeStableSlotLoader>(c, base, "gen-stc", ".wstc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCata(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CataStableSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstc");
    auto c = wowee::pipeline::WoweeStableSlotLoader::makeCata(name);
    if (!saveOrError<wowee::pipeline::WoweeStableSlotLoader>(c, base, "gen-stc-cata", ".wstc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPremium(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PremiumStableSlots";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wstc");
    auto c = wowee::pipeline::WoweeStableSlotLoader::makePremium(name);
    if (!saveOrError<wowee::pipeline::WoweeStableSlotLoader>(c, base, "gen-stc-premium", ".wstc")) return 1;
    printGenSummary(c, base);
    return 0;
}

void formatGold(uint32_t copper, char* buf, size_t bufSize) {
    uint32_t g = copper / 10000;
    uint32_t s = (copper % 10000) / 100;
    uint32_t cop = copper % 100;
    if (copper == 0)         std::snprintf(buf, bufSize, "free");
    else if (g > 0)          std::snprintf(buf, bufSize, "%ug %us %uc", g, s, cop);
    else if (s > 0)          std::snprintf(buf, bufSize, "%us %uc", s, cop);
    else                     std::snprintf(buf, bufSize, "%uc", cop);
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wstc");
    if (!wowee::pipeline::WoweeStableSlotLoader::exists(base)) {
        std::fprintf(stderr, "WSTC not found: %s.wstc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeStableSlotLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wstc"] = base + ".wstc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"slotId", e.slotId},
                {"name", e.name},
                {"description", e.description},
                {"displayOrder", e.displayOrder},
                {"minLevelToUnlock", e.minLevelToUnlock},
                {"isPremium", e.isPremium != 0},
                {"copperCost", e.copperCost},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSTC: %s.wstc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   ord  unlockLvl  cost           premium  name\n");
    for (const auto& e : c.entries) {
        char goldBuf[32];
        formatGold(e.copperCost, goldBuf, sizeof(goldBuf));
        std::printf("  %4u   %u    %3u       %-13s  %s      %s\n",
                    e.slotId, e.displayOrder, e.minLevelToUnlock,
                    goldBuf, e.isPremium ? "yes" : "no ",
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wstc");
    if (!wowee::pipeline::WoweeStableSlotLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wstc-json: WSTC not found: %s.wstc\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeStableSlotLoader::load(base);
    if (outPath.empty()) outPath = base + ".wstc.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["slotId"] = e.slotId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["displayOrder"] = e.displayOrder;
        je["minLevelToUnlock"] = e.minLevelToUnlock;
        je["isPremium"] = e.isPremium != 0;
        je["copperCost"] = e.copperCost;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wstc-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wstc-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wstc-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeStableSlot c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeStableSlot::Entry e;
            if (je.contains("slotId"))           e.slotId = je["slotId"].get<uint32_t>();
            if (je.contains("name"))             e.name = je["name"].get<std::string>();
            if (je.contains("description"))      e.description = je["description"].get<std::string>();
            if (je.contains("displayOrder"))     e.displayOrder = je["displayOrder"].get<uint8_t>();
            if (je.contains("minLevelToUnlock")) e.minLevelToUnlock = je["minLevelToUnlock"].get<uint8_t>();
            if (je.contains("isPremium")) {
                if (je["isPremium"].is_boolean())
                    e.isPremium = je["isPremium"].get<bool>() ? 1 : 0;
                else
                    e.isPremium = je["isPremium"].get<uint8_t>() ? 1 : 0;
            }
            if (je.contains("copperCost"))    e.copperCost = je["copperCost"].get<uint32_t>();
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wstc");
    outBase = cli::withoutExt(outBase, ".wstc");
    if (!wowee::pipeline::WoweeStableSlotLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wstc-json: failed to save %s.wstc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wstc\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  slots   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wstc");
    if (!wowee::pipeline::WoweeStableSlotLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wstc: WSTC not found: %s.wstc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeStableSlotLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    std::vector<uint8_t> ordersSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.slotId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.slotId == 0)
            errors.push_back(ctx + ": slotId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.minLevelToUnlock > 80) {
            warnings.push_back(ctx +
                ": minLevelToUnlock " +
                std::to_string(e.minLevelToUnlock) +
                " > 80 — slot unreachable at WotLK cap");
        }
        // Premium slot with non-zero cost is contradictory —
        // donator slots should be free (status-gated, not
        // gold-gated).
        if (e.isPremium && e.copperCost > 0) {
            warnings.push_back(ctx +
                ": Premium slot with copperCost=" +
                std::to_string(e.copperCost) +
                " — donator slots are typically free; the gate "
                "is donor status, not gold");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.slotId) {
                errors.push_back(ctx + ": duplicate slotId");
                break;
            }
        }
        idsSeen.push_back(e.slotId);
        // Two slots with the same displayOrder collide in
        // the stable UI — only the first would render.
        for (uint8_t prevOrd : ordersSeen) {
            if (prevOrd == e.displayOrder) {
                warnings.push_back(ctx +
                    ": duplicate displayOrder " +
                    std::to_string(e.displayOrder) +
                    " — stable UI position collision");
                break;
            }
        }
        ordersSeen.push_back(e.displayOrder);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wstc", base, errors, warnings);
    std::printf("validate-wstc: %s.wstc\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu slots, all slotIds unique, no UI collisions\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleStableSlotsCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-stc") == 0 && i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-stc-cata") == 0 && i + 1 < argc) {
        outRc = handleGenCata(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-stc-premium") == 0 && i + 1 < argc) {
        outRc = handleGenPremium(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wstc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wstc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wstc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wstc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
