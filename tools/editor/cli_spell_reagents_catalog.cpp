#include "cli_spell_reagents_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_reagents.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSpellReagent& c,
                     const std::string& base) {
    std::printf("Wrote %s.wspr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
}

int handleGenMage(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageReagents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspr");
    auto c = wowee::pipeline::WoweeSpellReagentLoader::makeMage(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellReagentLoader>(c, base, "gen-spr", ".wspr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenWarlock(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WarlockReagents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspr");
    auto c = wowee::pipeline::WoweeSpellReagentLoader::makeWarlock(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellReagentLoader>(c, base, "gen-spr-warlock", ".wspr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRez(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ResurrectionReagents";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wspr");
    auto c = wowee::pipeline::WoweeSpellReagentLoader::makeRez(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellReagentLoader>(c, base, "gen-spr-rez", ".wspr")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspr");
    if (!wowee::pipeline::WoweeSpellReagentLoader::exists(base)) {
        std::fprintf(stderr, "WSPR not found: %s.wspr\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellReagentLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wspr"] = base + ".wspr";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json items = nlohmann::json::array();
            nlohmann::json counts = nlohmann::json::array();
            for (int s = 0; s < wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots; ++s) {
                items.push_back(e.reagentItemId[s]);
                counts.push_back(e.reagentCount[s]);
            }
            arr.push_back({
                {"reagentSetId", e.reagentSetId},
                {"name", e.name},
                {"description", e.description},
                {"spellId", e.spellId},
                {"reagentItemId", items},
                {"reagentCount", counts},
                {"reagentKind", e.reagentKind},
                {"reagentKindName", wowee::pipeline::WoweeSpellReagent::reagentKindName(e.reagentKind)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSPR: %s.wspr\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   spellId   kind          slots  reagents (item x count)             name\n");
    for (const auto& e : c.entries) {
        std::string slots;
        int used = 0;
        for (int s = 0; s < wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots; ++s) {
            if (e.reagentItemId[s] == 0) continue;
            if (!slots.empty()) slots += ", ";
            slots += std::to_string(e.reagentItemId[s]) + " x " +
                     std::to_string(e.reagentCount[s]);
            ++used;
        }
        if (slots.empty()) slots = "(none)";
        std::printf("  %4u   %5u    %-12s    %d    %-35s  %s\n",
                    e.reagentSetId, e.spellId,
                    wowee::pipeline::WoweeSpellReagent::reagentKindName(e.reagentKind),
                    used, slots.c_str(), e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wspr");
    if (!wowee::pipeline::WoweeSpellReagentLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wspr-json: WSPR not found: %s.wspr\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellReagentLoader::load(base);
    if (outPath.empty()) outPath = base + ".wspr.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        nlohmann::json items = nlohmann::json::array();
        nlohmann::json counts = nlohmann::json::array();
        for (int s = 0; s < wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots; ++s) {
            items.push_back(e.reagentItemId[s]);
            counts.push_back(e.reagentCount[s]);
        }
        nlohmann::json je;
        je["reagentSetId"] = e.reagentSetId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["spellId"] = e.spellId;
        je["reagentItemId"] = items;
        je["reagentCount"] = counts;
        je["reagentKind"] = e.reagentKind;
        je["reagentKindName"] =
            wowee::pipeline::WoweeSpellReagent::reagentKindName(e.reagentKind);
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wspr-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseReagentKindToken(const nlohmann::json& jv,
                              uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellReagent::Tradeable)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "standard")     return wowee::pipeline::WoweeSpellReagent::Standard;
        if (s == "soul-shard" ||
            s == "soulshard")    return wowee::pipeline::WoweeSpellReagent::SoulShard;
        if (s == "focused-item" ||
            s == "focuseditem")  return wowee::pipeline::WoweeSpellReagent::FocusedItem;
        if (s == "catalyst")     return wowee::pipeline::WoweeSpellReagent::Catalyst;
        if (s == "tradeable")    return wowee::pipeline::WoweeSpellReagent::Tradeable;
    }
    return fallback;
}

// Read the reagentItemId / reagentCount fixed-length array
// fields from the JSON sidecar. Pads with zeros if the
// JSON array is shorter than kMaxReagentSlots, truncates
// silently if longer (extra entries dropped).
void readReagentSlotArray(const nlohmann::json& arr,
                           uint32_t out[wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots]) {
    for (int s = 0; s < wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots; ++s)
        out[s] = 0;
    if (!arr.is_array()) return;
    int n = std::min<int>(arr.size(),
        wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots);
    for (int s = 0; s < n; ++s)
        out[s] = arr[s].get<uint32_t>();
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wspr-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wspr-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellReagent c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellReagent::Entry e;
            if (je.contains("reagentSetId")) e.reagentSetId = je["reagentSetId"].get<uint32_t>();
            if (je.contains("name"))         e.name = je["name"].get<std::string>();
            if (je.contains("description"))  e.description = je["description"].get<std::string>();
            if (je.contains("spellId"))      e.spellId = je["spellId"].get<uint32_t>();
            if (je.contains("reagentItemId"))
                readReagentSlotArray(je["reagentItemId"], e.reagentItemId);
            if (je.contains("reagentCount"))
                readReagentSlotArray(je["reagentCount"], e.reagentCount);
            uint8_t kind = wowee::pipeline::WoweeSpellReagent::Standard;
            if (je.contains("reagentKind"))
                kind = parseReagentKindToken(je["reagentKind"], kind);
            else if (je.contains("reagentKindName"))
                kind = parseReagentKindToken(je["reagentKindName"], kind);
            e.reagentKind = kind;
            if (je.contains("iconColorRGBA"))
                e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wspr");
    outBase = cli::withoutExt(outBase, ".wspr");
    if (!wowee::pipeline::WoweeSpellReagentLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wspr-json: failed to save %s.wspr\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wspr\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wspr");
    if (!wowee::pipeline::WoweeSpellReagentLoader::exists(base)) {
        return reportMissing("validate-wspr", "WSPR", base, ".wspr");
    }
    auto c = wowee::pipeline::WoweeSpellReagentLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    std::vector<uint32_t> spellsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.reagentSetId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.reagentSetId == 0)
            errors.push_back(ctx + ": reagentSetId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.spellId == 0)
            errors.push_back(ctx +
                ": spellId is 0 — missing WSPL cross-ref");
        if (e.reagentKind > wowee::pipeline::WoweeSpellReagent::Tradeable) {
            errors.push_back(ctx + ": reagentKind " +
                std::to_string(e.reagentKind) + " not in 0..4");
        }
        // Per-slot checks: itemId+count must be both set or both clear
        int usedSlots = 0;
        for (int s = 0; s < wowee::pipeline::WoweeSpellReagent::kMaxReagentSlots; ++s) {
            uint32_t it = e.reagentItemId[s];
            uint32_t cnt = e.reagentCount[s];
            if (it != 0 && cnt == 0) {
                warnings.push_back(ctx +
                    ": slot " + std::to_string(s) +
                    " has itemId=" + std::to_string(it) +
                    " but count=0 — reagent will not be consumed");
            } else if (it == 0 && cnt != 0) {
                warnings.push_back(ctx +
                    ": slot " + std::to_string(s) +
                    " has count=" + std::to_string(cnt) +
                    " but itemId=0 — count is unreachable");
            }
            if (it != 0) ++usedSlots;
        }
        // SoulShard kind should reference item 6265 in slot 0.
        // Other shard ids are server-custom but the canonical
        // case is worth flagging.
        if (e.reagentKind == wowee::pipeline::WoweeSpellReagent::SoulShard &&
            e.reagentItemId[0] != 6265 && e.reagentItemId[0] != 0) {
            warnings.push_back(ctx +
                ": SoulShard kind with non-canonical reagent " +
                "id " + std::to_string(e.reagentItemId[0]) +
                " in slot 0 (canonical Soul Shard is item 6265)");
        }
        // FocusedItem kind: reagent is required to cast but
        // not consumed. Should still have an itemId set.
        if (e.reagentKind == wowee::pipeline::WoweeSpellReagent::FocusedItem &&
            usedSlots == 0) {
            warnings.push_back(ctx +
                ": FocusedItem kind with no reagent slots set " +
                "— focused-item gating has nothing to gate");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.reagentSetId) {
                errors.push_back(ctx + ": duplicate reagentSetId");
                break;
            }
        }
        idsSeen.push_back(e.reagentSetId);
        // Two reagent sets for the same spell collide —
        // engine would honor only the first.
        if (e.spellId != 0) {
            for (uint32_t prevSpell : spellsSeen) {
                if (prevSpell == e.spellId) {
                    warnings.push_back(ctx +
                        ": duplicate spellId " +
                        std::to_string(e.spellId) +
                        " — only first reagent set will be used");
                    break;
                }
            }
            spellsSeen.push_back(e.spellId);
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wspr", base, errors, warnings);
    std::printf("validate-wspr: %s.wspr\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu reagent sets, all reagentSetIds unique, all spell ids set\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleSpellReagentsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-spr") == 0 && i + 1 < argc) {
        outRc = handleGenMage(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spr-warlock") == 0 && i + 1 < argc) {
        outRc = handleGenWarlock(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-spr-rez") == 0 && i + 1 < argc) {
        outRc = handleGenRez(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wspr") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wspr") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wspr-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wspr-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
