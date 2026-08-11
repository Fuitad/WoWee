#include "cli_item_sets_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_item_sets.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeItemSet& c,
                     const std::string& base) {
    std::printf("Wrote %s.wset\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterItemSets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wset");
    auto c = wowee::pipeline::WoweeItemSetLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeItemSetLoader>(c, base, "gen-itset", ".wset")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTier(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TierItemSets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wset");
    auto c = wowee::pipeline::WoweeItemSetLoader::makeTier(name);
    if (!saveOrError<wowee::pipeline::WoweeItemSetLoader>(c, base, "gen-itset-tier", ".wset")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenPvP(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "PvPItemSets";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wset");
    auto c = wowee::pipeline::WoweeItemSetLoader::makePvP(name);
    if (!saveOrError<wowee::pipeline::WoweeItemSetLoader>(c, base, "gen-itset-pvp", ".wset")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendEntryJson(nlohmann::json& arr,
                     const wowee::pipeline::WoweeItemSet::Entry& e) {
    nlohmann::json items = nlohmann::json::array();
    for (size_t k = 0; k < e.pieceCount &&
         k < wowee::pipeline::WoweeItemSet::kMaxPieces; ++k) {
        items.push_back(e.itemIds[k]);
    }
    nlohmann::json bonuses = nlohmann::json::array();
    for (size_t k = 0; k < e.bonusCount &&
         k < wowee::pipeline::WoweeItemSet::kMaxBonuses; ++k) {
        bonuses.push_back({
            {"threshold", e.bonusThresholds[k]},
            {"spellId", e.bonusSpellIds[k]},
        });
    }
    arr.push_back({
        {"setId", e.setId},
        {"name", e.name},
        {"description", e.description},
        {"pieceCount", e.pieceCount},
        {"bonusCount", e.bonusCount},
        {"requiredClassMask", e.requiredClassMask},
        {"requiredSkillId", e.requiredSkillId},
        {"requiredSkillRank", e.requiredSkillRank},
        {"itemIds", items},
        {"bonuses", bonuses},
    });
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wset");
    if (!wowee::pipeline::WoweeItemSetLoader::exists(base)) {
        std::fprintf(stderr, "WSET not found: %s.wset\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeItemSetLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wset"] = base + ".wset";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) appendEntryJson(arr, e);
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSET: %s.wset\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  sets    : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    pieces  bonuses  classMask  skill  rank   first-itemId  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %3u     %3u      0x%02x      %5u  %5u   %12u  %s\n",
                    e.setId, e.pieceCount, e.bonusCount,
                    e.requiredClassMask, e.requiredSkillId,
                    e.requiredSkillRank, e.itemIds[0],
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each set emits all 8 scalar fields plus
    // two nested arrays (itemIds[] and bonuses[{threshold,
    // spellId}]) — only the populated slots are emitted to
    // keep hand-edits compact. classMask is dumped as a raw
    // integer; users can hand-edit using the bit positions
    // documented in the WCHC catalog.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wset");
    if (outPath.empty()) outPath = base + ".wset.json";
    if (!wowee::pipeline::WoweeItemSetLoader::exists(base)) {
        return reportMissing("export-wset-json", "WSET", base, ".wset");
    }
    auto c = wowee::pipeline::WoweeItemSetLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) appendEntryJson(arr, e);
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wset-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source : %s.wset\n", base.c_str());
    std::printf("  sets   : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wset");
    outBase = cli::withoutExt(outBase, ".wset");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wset-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wset-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    wowee::pipeline::WoweeItemSet c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeItemSet::Entry e;
            e.setId = je.value("setId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.requiredClassMask = je.value("requiredClassMask", 0u);
            e.requiredSkillId = static_cast<uint16_t>(
                je.value("requiredSkillId", 0));
            e.requiredSkillRank = static_cast<uint16_t>(
                je.value("requiredSkillRank", 0));
            // Items + bonuses are derived from array sizes —
            // the explicit pieceCount / bonusCount fields in
            // the binary header are redundant on import (the
            // exporter emits them so info dumps stay
            // self-consistent, but on import we recompute).
            for (size_t k = 0;
                 k < wowee::pipeline::WoweeItemSet::kMaxPieces; ++k) {
                e.itemIds[k] = 0;
            }
            for (size_t k = 0;
                 k < wowee::pipeline::WoweeItemSet::kMaxBonuses; ++k) {
                e.bonusThresholds[k] = 0;
                e.bonusSpellIds[k] = 0;
            }
            if (je.contains("itemIds") && je["itemIds"].is_array()) {
                size_t slot = 0;
                for (const auto& ji : je["itemIds"]) {
                    if (slot >= wowee::pipeline::WoweeItemSet::kMaxPieces)
                        break;
                    e.itemIds[slot++] = ji.get<uint32_t>();
                }
                e.pieceCount = static_cast<uint8_t>(slot);
            }
            if (je.contains("bonuses") && je["bonuses"].is_array()) {
                size_t slot = 0;
                for (const auto& jb : je["bonuses"]) {
                    if (slot >= wowee::pipeline::WoweeItemSet::kMaxBonuses)
                        break;
                    e.bonusThresholds[slot] = static_cast<uint8_t>(
                        jb.value("threshold", 0));
                    e.bonusSpellIds[slot] = jb.value("spellId", 0u);
                    ++slot;
                }
                e.bonusCount = static_cast<uint8_t>(slot);
            }
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeItemSetLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wset-json: failed to save %s.wset\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wset\n", outBase.c_str());
    std::printf("  source : %s\n", jsonPath.c_str());
    std::printf("  sets   : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wset");
    if (!wowee::pipeline::WoweeItemSetLoader::exists(base)) {
        return reportMissing("validate-wset", "WSET", base, ".wset");
    }
    auto c = wowee::pipeline::WoweeItemSetLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.setId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.setId == 0)
            errors.push_back(ctx + ": setId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.pieceCount == 0)
            errors.push_back(ctx +
                ": pieceCount=0 (set has no items)");
        if (e.pieceCount > wowee::pipeline::WoweeItemSet::kMaxPieces) {
            errors.push_back(ctx + ": pieceCount " +
                std::to_string(e.pieceCount) + " exceeds kMaxPieces (8)");
        }
        if (e.bonusCount > wowee::pipeline::WoweeItemSet::kMaxBonuses) {
            errors.push_back(ctx + ": bonusCount " +
                std::to_string(e.bonusCount) + " exceeds kMaxBonuses (4)");
        }
        // Verify the populated piece slots are non-zero and
        // the unpopulated tail is zero — drift between
        // pieceCount and the actual slot data confuses the
        // runtime resolver.
        for (size_t p = 0; p < wowee::pipeline::WoweeItemSet::kMaxPieces;
             ++p) {
            if (p < e.pieceCount) {
                if (e.itemIds[p] == 0) {
                    errors.push_back(ctx + ": piece slot " +
                        std::to_string(p) + " is 0 but pieceCount=" +
                        std::to_string(e.pieceCount));
                }
            } else {
                if (e.itemIds[p] != 0) {
                    warnings.push_back(ctx + ": piece slot " +
                        std::to_string(p) + " has itemId=" +
                        std::to_string(e.itemIds[p]) +
                        " but is past pieceCount " +
                        std::to_string(e.pieceCount) +
                        " (silently ignored at runtime)");
                }
            }
        }
        // Bonus thresholds must be ascending and within
        // pieceCount — a bonus that requires more pieces
        // than the set has can never trigger.
        uint8_t prevThreshold = 0;
        for (size_t b = 0; b < e.bonusCount; ++b) {
            uint8_t t = e.bonusThresholds[b];
            uint32_t s = e.bonusSpellIds[b];
            if (t == 0) {
                errors.push_back(ctx + ": bonus " +
                    std::to_string(b) + " has threshold=0");
            }
            if (t > e.pieceCount) {
                errors.push_back(ctx + ": bonus " +
                    std::to_string(b) + " threshold " +
                    std::to_string(t) + " > pieceCount " +
                    std::to_string(e.pieceCount) +
                    " (bonus can never trigger)");
            }
            if (s == 0) {
                errors.push_back(ctx + ": bonus " +
                    std::to_string(b) +
                    " threshold set but spellId=0 "
                    "(bonus has no aura)");
            }
            if (b > 0 && t <= prevThreshold) {
                errors.push_back(ctx + ": bonus " +
                    std::to_string(b) + " threshold " +
                    std::to_string(t) +
                    " not strictly greater than previous " +
                    std::to_string(prevThreshold));
            }
            prevThreshold = t;
        }
        if (!idsSeen.add(e.setId)) errors.push_back(ctx + ": duplicate setId");
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wset", base, errors, warnings);
    std::printf("validate-wset: %s.wset\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu sets, all setIds unique, all bonus thresholds reachable\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleItemSetsCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-itset") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-itset-tier") == 0 && i + 1 < argc) {
        outRc = handleGenTier(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-itset-pvp") == 0 && i + 1 < argc) {
        outRc = handleGenPvP(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wset") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wset") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wset-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wset-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
