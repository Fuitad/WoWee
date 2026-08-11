#include "cli_spell_cooldowns_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_spell_cooldowns.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeSpellCooldown& c,
                     const std::string& base) {
    std::printf("Wrote %s.wscd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCooldowns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscd");
    auto c = wowee::pipeline::WoweeSpellCooldownLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCooldownLoader>(c, base, "gen-cdb", ".wscd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenClass(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "MageClassCooldowns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscd");
    auto c = wowee::pipeline::WoweeSpellCooldownLoader::makeClass(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCooldownLoader>(c, base, "gen-cdb-class", ".wscd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenItems(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ItemCooldowns";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wscd");
    auto c = wowee::pipeline::WoweeSpellCooldownLoader::makeItems(name);
    if (!saveOrError<wowee::pipeline::WoweeSpellCooldownLoader>(c, base, "gen-cdb-items", ".wscd")) return 1;
    printGenSummary(c, base);
    return 0;
}

void appendFlagNames(uint32_t flags, std::string& out) {
    using F = wowee::pipeline::WoweeSpellCooldown;
    auto add = [&](const char* n) {
        if (!out.empty()) out += "|";
        out += n;
    };
    if (flags & F::AffectedByHaste)          add("AffectedByHaste");
    if (flags & F::SharedWithItems)          add("SharedWithItems");
    if (flags & F::OnGCDStart)               add("OnGCDStart");
    if (flags & F::IgnoresCooldownReduction) add("IgnoresCooldownReduction");
    if (out.empty()) out = "-";
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wscd");
    if (!wowee::pipeline::WoweeSpellCooldownLoader::exists(base)) {
        std::fprintf(stderr, "WSCD not found: %s.wscd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellCooldownLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wscd"] = base + ".wscd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string flagNames;
            appendFlagNames(e.categoryFlags, flagNames);
            arr.push_back({
                {"bucketId", e.bucketId},
                {"name", e.name},
                {"description", e.description},
                {"bucketKind", e.bucketKind},
                {"bucketKindName", wowee::pipeline::WoweeSpellCooldown::bucketKindName(e.bucketKind)},
                {"cooldownMs", e.cooldownMs},
                {"categoryFlags", e.categoryFlags},
                {"categoryFlagsLabels", flagNames},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSCD: %s.wscd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id     kind     cooldownMs  flags                              name\n");
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendFlagNames(e.categoryFlags, flagNames);
        std::printf("  %4u    %-7s  %10u  %-32s  %s\n",
                    e.bucketId,
                    wowee::pipeline::WoweeSpellCooldown::bucketKindName(e.bucketKind),
                    e.cooldownMs,
                    flagNames.c_str(),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wscd");
    if (!wowee::pipeline::WoweeSpellCooldownLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wscd-json: WSCD not found: %s.wscd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeSpellCooldownLoader::load(base);
    if (outPath.empty()) outPath = base + ".wscd.json";
    nlohmann::json j;
    j["catalog"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        std::string flagNames;
        appendFlagNames(e.categoryFlags, flagNames);
        nlohmann::json je;
        je["bucketId"] = e.bucketId;
        je["name"] = e.name;
        je["description"] = e.description;
        je["bucketKind"] = e.bucketKind;
        je["bucketKindName"] =
            wowee::pipeline::WoweeSpellCooldown::bucketKindName(e.bucketKind);
        je["cooldownMs"] = e.cooldownMs;
        je["categoryFlags"] = e.categoryFlags;
        je["categoryFlagsLabels"] = flagNames;
        je["iconColorRGBA"] = e.iconColorRGBA;
        arr.push_back(je);
    }
    j["entries"] = arr;
    std::ofstream os(outPath);
    if (!os) {
        std::fprintf(stderr,
            "export-wscd-json: failed to open %s for write\n",
            outPath.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

uint8_t parseBucketKindToken(const nlohmann::json& jv,
                             uint8_t fallback) {
    if (jv.is_number_integer() || jv.is_number_unsigned()) {
        int v = jv.get<int>();
        if (v < 0 || v > wowee::pipeline::WoweeSpellCooldown::Misc)
            return fallback;
        return static_cast<uint8_t>(v);
    }
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        for (auto& ch : s) ch = static_cast<char>(std::tolower(ch));
        if (s == "spell")  return wowee::pipeline::WoweeSpellCooldown::Spell;
        if (s == "item")   return wowee::pipeline::WoweeSpellCooldown::Item;
        if (s == "class")  return wowee::pipeline::WoweeSpellCooldown::Class;
        if (s == "global") return wowee::pipeline::WoweeSpellCooldown::Global;
        if (s == "misc")   return wowee::pipeline::WoweeSpellCooldown::Misc;
    }
    return fallback;
}

// Parse the categoryFlags field accepting either an int
// bitfield OR a "Pipe|Separated|Labels" string. The label
// form makes JSON sidecars easier to hand-edit; the int
// form preserves any unknown flag bits during round-trip.
uint32_t parseCategoryFlagsField(const nlohmann::json& jv) {
    using F = wowee::pipeline::WoweeSpellCooldown;
    if (jv.is_number_integer() || jv.is_number_unsigned())
        return jv.get<uint32_t>();
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        uint32_t out = 0;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find('|', pos);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(pos, end - pos);
            for (auto& ch : tok) ch = static_cast<char>(std::tolower(ch));
            if (tok == "affectedbyhaste")          out |= F::AffectedByHaste;
            else if (tok == "sharedwithitems")     out |= F::SharedWithItems;
            else if (tok == "ongcdstart")          out |= F::OnGCDStart;
            else if (tok == "ignorescooldownreduction") out |= F::IgnoresCooldownReduction;
            // unknown labels silently ignored — they're
            // already filtered by the validator's warning
            pos = end + 1;
        }
        return out;
    }
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    std::ifstream is(jsonPath);
    if (!is) {
        std::fprintf(stderr,
            "import-wscd-json: failed to open %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wscd-json: parse error in %s: %s\n",
            jsonPath.c_str(), ex.what());
        return 1;
    }
    wowee::pipeline::WoweeSpellCooldown c;
    if (j.contains("catalog") && j["catalog"].is_string())
        c.name = j["catalog"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSpellCooldown::Entry e;
            if (je.contains("bucketId"))    e.bucketId = je["bucketId"].get<uint32_t>();
            if (je.contains("name"))        e.name = je["name"].get<std::string>();
            if (je.contains("description")) e.description = je["description"].get<std::string>();
            uint8_t kind = wowee::pipeline::WoweeSpellCooldown::Spell;
            if (je.contains("bucketKind"))
                kind = parseBucketKindToken(je["bucketKind"], kind);
            else if (je.contains("bucketKindName"))
                kind = parseBucketKindToken(je["bucketKindName"], kind);
            e.bucketKind = kind;
            if (je.contains("cooldownMs")) e.cooldownMs = je["cooldownMs"].get<uint32_t>();
            // Prefer the int form of categoryFlags when both
            // are present — it preserves unknown bits across
            // round-trip; fall back to the label form when
            // only that's present (hand-edited sidecars).
            if (je.contains("categoryFlags"))
                e.categoryFlags = parseCategoryFlagsField(je["categoryFlags"]);
            else if (je.contains("categoryFlagsLabels"))
                e.categoryFlags = parseCategoryFlagsField(je["categoryFlagsLabels"]);
            if (je.contains("iconColorRGBA")) e.iconColorRGBA = je["iconColorRGBA"].get<uint32_t>();
            c.entries.push_back(e);
        }
    }
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wscd");
    outBase = cli::withoutExt(outBase, ".wscd");
    if (!wowee::pipeline::WoweeSpellCooldownLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wscd-json: failed to save %s.wscd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wscd\n", outBase.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  buckets : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeSpellCooldownLoader>(
        i, argc, argv, "wscd", "WSCD",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        cli::DuplicateIdCheck idsSeen;
        constexpr uint32_t kKnownFlagMask =
            wowee::pipeline::WoweeSpellCooldown::AffectedByHaste |
            wowee::pipeline::WoweeSpellCooldown::SharedWithItems |
            wowee::pipeline::WoweeSpellCooldown::OnGCDStart |
            wowee::pipeline::WoweeSpellCooldown::IgnoresCooldownReduction;
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.bucketId);
            if (!e.name.empty()) ctx += " " + e.name;
            ctx += ")";
            if (e.bucketId == 0)
                errors.push_back(ctx + ": bucketId is 0");
            if (e.name.empty())
                errors.push_back(ctx + ": name is empty");
            if (e.bucketKind > wowee::pipeline::WoweeSpellCooldown::Misc) {
                errors.push_back(ctx + ": bucketKind " +
                    std::to_string(e.bucketKind) + " not in 0..4");
            }
            if (e.categoryFlags & ~kKnownFlagMask) {
                warnings.push_back(ctx +
                    ": categoryFlags has bits outside known mask " +
                    "(0x" + std::to_string(e.categoryFlags & ~kKnownFlagMask) +
                    ") — engine will ignore unknown flags");
            }
            // Global bucket should be GCD-marked. Otherwise the
            // engine wouldn't trigger it on cast start.
            if (e.bucketKind == wowee::pipeline::WoweeSpellCooldown::Global &&
                !(e.categoryFlags & wowee::pipeline::WoweeSpellCooldown::OnGCDStart)) {
                warnings.push_back(ctx +
                    ": Global kind without OnGCDStart flag — "
                    "engine will not trigger this on cast start");
            }
            // SharedWithItems on a Spell-only bucket is
            // contradictory.
            if (e.bucketKind == wowee::pipeline::WoweeSpellCooldown::Spell &&
                (e.categoryFlags & wowee::pipeline::WoweeSpellCooldown::SharedWithItems)) {
                warnings.push_back(ctx +
                    ": Spell kind with SharedWithItems flag — "
                    "switch kind to Item or Misc, or drop the flag");
            }
            if (!idsSeen.add(e.bucketId)) errors.push_back(ctx + ": duplicate bucketId");
        }
            return formatted("%zu buckets, all bucketIds unique", c.entries.size());
        });
}

} // namespace

bool handleSpellCooldownsCatalog(int& i, int argc, char** argv,
                                 int& outRc) {
    if (std::strcmp(argv[i], "--gen-cdb") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cdb-class") == 0 && i + 1 < argc) {
        outRc = handleGenClass(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-cdb-items") == 0 && i + 1 < argc) {
        outRc = handleGenItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wscd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wscd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wscd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wscd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
