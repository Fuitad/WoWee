#include "cli_action_bars_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_action_bars.hpp"
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

std::string stripWactExt(std::string base) {
    stripExt(base, ".wact");
    return base;
}

bool saveOrError(const wowee::pipeline::WoweeActionBar& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeActionBarLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wact\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeActionBar& c,
                     const std::string& base) {
    std::printf("Wrote %s.wact\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
}

int handleGenWarrior(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarriorActionBar";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWactExt(base);
    auto c = wowee::pipeline::WoweeActionBarLoader::makeWarrior(name);
    if (!saveOrError(c, base, "gen-act")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageActionBar";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWactExt(base);
    auto c = wowee::pipeline::WoweeActionBarLoader::makeMage(name);
    if (!saveOrError(c, base, "gen-act-mage")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHunterPet(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HunterPetBar";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWactExt(base);
    auto c = wowee::pipeline::WoweeActionBarLoader::makeHunterPet(name);
    if (!saveOrError(c, base, "gen-act-pet")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWactExt(base);
    if (!wowee::pipeline::WoweeActionBarLoader::exists(base)) {
        std::fprintf(stderr, "WACT not found: %s.wact\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeActionBarLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wact"] = base + ".wact";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"bindingId", e.bindingId},
                {"name", e.name},
                {"description", e.description},
                {"classMask", e.classMask},
                {"spellId", e.spellId},
                {"itemId", e.itemId},
                {"buttonSlot", e.buttonSlot},
                {"barMode", e.barMode},
                {"barModeName", wowee::pipeline::WoweeActionBar::barModeName(e.barMode)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WACT: %s.wact\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    classMask    bar       slot   spellId  itemId    name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   0x%08x   %-8s   %3u   %5u    %5u     %s\n",
                    e.bindingId, e.classMask,
                    wowee::pipeline::WoweeActionBar::barModeName(e.barMode),
                    e.buttonSlot, e.spellId, e.itemId,
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = stripWactExt(base);
    if (!wowee::pipeline::WoweeActionBarLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wact-json: WACT not found: %s.wact\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeActionBarLoader::load(base);
    if (outPath.empty()) outPath = base + ".wact.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json je;
        je["bindingId"] = e.bindingId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["classMask"] = e.classMask;
        je["spellId"] = e.spellId;
        je["itemId"] = e.itemId;
        je["buttonSlot"] = e.buttonSlot;
        je["barMode"] = e.barMode;
        je["barModeName"] =
            wowee::pipeline::WoweeActionBar::barModeName(e.barMode);
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wact-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseBarModeToken(const nlohmann::json& jv, uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeActionBar::Custom)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "main")    return wowee::pipeline::WoweeActionBar::Main;
        if (s == "pet")     return wowee::pipeline::WoweeActionBar::Pet;
        if (s == "vehicle") return wowee::pipeline::WoweeActionBar::Vehicle;
        if (s == "stance1") return wowee::pipeline::WoweeActionBar::Stance1;
        if (s == "stance2") return wowee::pipeline::WoweeActionBar::Stance2;
        if (s == "stance3") return wowee::pipeline::WoweeActionBar::Stance3;
        if (s == "custom")  return wowee::pipeline::WoweeActionBar::Custom;
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
            "import-wact-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wact-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeActionBar c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeActionBar::Entry e;
            if (je.contains("bindingId"))   e.bindingId = je["bindingId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            if (je.contains("classMask"))   e.classMask = je["classMask"].get<uint32_t>();
            if (je.contains("spellId"))     e.spellId = je["spellId"].get<uint32_t>();
            if (je.contains("itemId"))      e.itemId = je["itemId"].get<uint32_t>();
            if (je.contains("buttonSlot"))  e.buttonSlot = je["buttonSlot"].get<uint8_t>();
            uint8_t mode = wowee::pipeline::WoweeActionBar::Main;
            if (je.contains("barMode"))
                mode = parseBarModeToken(je["barMode"], mode);
            else if (je.contains("barModeName"))
                mode = parseBarModeToken(je["barModeName"], mode);
            e.barMode = mode;
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) {
        outBase = jsonPath;
        const std::string suffix1 = ".wact.json";
        const std::string suffix2 = ".json";
        if (outBase.size() >= suffix1.size() &&
            outBase.compare(outBase.size() - suffix1.size(),
                            suffix1.size(), suffix1) == 0) {
            outBase.resize(outBase.size() - suffix1.size());
        } else if (outBase.size() >= suffix2.size() &&
                   outBase.compare(outBase.size() - suffix2.size(),
                                   suffix2.size(), suffix2) == 0) {
            outBase.resize(outBase.size() - suffix2.size());
        }
    }
    outBase = stripWactExt(outBase);
    if (!wowee::pipeline::WoweeActionBarLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wact-json: failed to save %s.wact\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wact\n", outBase.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  bindings : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWactExt(base);
    if (!wowee::pipeline::WoweeActionBarLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wact: WACT not found: %s.wact\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeActionBarLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.bindingId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.bindingId == 0)
            errors.push_back(ctx + ": bindingId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.classMask == 0)
            errors.push_back(ctx +
                ": classMask is 0 — no class can use this binding");
        if (e.barMode > wowee::pipeline::WoweeActionBar::Custom) {
            errors.push_back(ctx + ": barMode " +
                std::to_string(e.barMode) + " not in 0..6");
        }
        if (e.buttonSlot > 143) {
            warnings.push_back(ctx +
                ": buttonSlot " + std::to_string(e.buttonSlot) +
                " > 143 (12 bars × 12 slots = 144 max)");
        }
        // Both spellId and itemId set is contradictory.
        if (e.spellId != 0 && e.itemId != 0) {
            warnings.push_back(ctx +
                ": both spellId and itemId set — engine prefers "
                "spellId; itemId is ignored");
        }
        // Neither set means an empty button.
        if (e.spellId == 0 && e.itemId == 0) {
            warnings.push_back(ctx +
                ": both spellId=0 and itemId=0 — button will be empty");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.bindingId) {
                errors.push_back(ctx + ": duplicate bindingId");
                break;
            }
        }
        idsSeen.push_back(e.bindingId);
    }
    // Cross-entry: detect (classMask, barMode, buttonSlot)
    // collisions where overlapping classes would fight for
    // the same physical slot.
    for (size_t a = 0; a < c.entries.size(); ++a) {
        for (size_t b = a + 1; b < c.entries.size(); ++b) {
            const auto& ea = c.entries[a];
            const auto& eb = c.entries[b];
            if (ea.barMode != eb.barMode) continue;
            if (ea.buttonSlot != eb.buttonSlot) continue;
            if ((ea.classMask & eb.classMask) == 0) continue;
            warnings.push_back(
                "entries " + std::to_string(a) + " (" +
                ea.name + ") and " + std::to_string(b) + " (" +
                eb.name + ") share " +
                wowee::pipeline::WoweeActionBar::barModeName(ea.barMode) +
                " bar slot " + std::to_string(ea.buttonSlot) +
                " for overlapping classMask — slot collision");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wact", base, errors, warnings);
    std::printf("validate-wact: %s.wact\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu bindings, all bindingIds unique, no slot collisions\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleActionBarsCatalog(int& i, int argc, char** argv,
                             int& outRc) {
    if (std::strcmp(argv[i], "--gen-act") == 0 && i + 1 < argc) {
        outRc = handleGenWarrior(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-act-mage") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-act-pet") == 0 && i + 1 < argc) {
        outRc = handleGenHunterPet(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wact") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wact") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wact-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wact-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
