#include "cli_tabards_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_tabards.hpp"
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

const char* backgroundPatternName(uint8_t p) {
    using T = wowee::pipeline::WoweeTabards;
    switch (p) {
        case T::Solid:     return "solid";
        case T::Gradient:  return "gradient";
        case T::Chevron:   return "chevron";
        case T::Quartered: return "quartered";
        case T::Starburst: return "starburst";
        default:           return "unknown";
    }
}

const char* borderPatternName(uint8_t p) {
    using T = wowee::pipeline::WoweeTabards;
    switch (p) {
        case T::BorderNone:       return "none";
        case T::BorderThin:       return "thin";
        case T::BorderThick:      return "thick";
        case T::BorderDecorative: return "decorative";
        default:                  return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeTabards& c,
                     const std::string& base) {
    std::printf("Wrote %s.wtbd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabards : %zu\n", c.entries.size());
}

int handleGenAlliance(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AllianceClassicTabards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtbd");
    auto c = wowee::pipeline::WoweeTabardsLoader::makeAllianceClassic(name);
    if (!saveOrError<wowee::pipeline::WoweeTabardsLoader>(c, base, "gen-tbd", ".wtbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenHorde(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "HordeClassicTabards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtbd");
    auto c = wowee::pipeline::WoweeTabardsLoader::makeHordeClassic(name);
    if (!saveOrError<wowee::pipeline::WoweeTabardsLoader>(c, base, "gen-tbd-horde", ".wtbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenFaction(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "FactionVendorTabards";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wtbd");
    auto c = wowee::pipeline::WoweeTabardsLoader::makeFactionVendor(name);
    if (!saveOrError<wowee::pipeline::WoweeTabardsLoader>(c, base, "gen-tbd-faction", ".wtbd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtbd");
    if (!wowee::pipeline::WoweeTabardsLoader::exists(base)) {
        std::fprintf(stderr, "WTBD not found: %s.wtbd\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTabardsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wtbd"] = base + ".wtbd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"tabardId", e.tabardId},
                {"name", e.name},
                {"description", e.description},
                {"backgroundPattern", e.backgroundPattern},
                {"backgroundPatternName",
                    backgroundPatternName(e.backgroundPattern)},
                {"backgroundColor", e.backgroundColor},
                {"borderPattern", e.borderPattern},
                {"borderPatternName",
                    borderPatternName(e.borderPattern)},
                {"borderColor", e.borderColor},
                {"emblemId", e.emblemId},
                {"emblemColor", e.emblemColor},
                {"guildId", e.guildId},
                {"creatorPlayerId", e.creatorPlayerId},
                {"isApproved", e.isApproved != 0},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WTBD: %s.wtbd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  tabards : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   bg-pattern   border       emblem  guild  approved   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-10s   %-10s   %4u    %4u  %s        %s\n",
                    e.tabardId,
                    backgroundPatternName(e.backgroundPattern),
                    borderPatternName(e.borderPattern),
                    e.emblemId, e.guildId,
                    e.isApproved ? "yes" : "no ",
                    e.name.c_str());
    }
    return 0;
}

int parseBackgroundPatternToken(const std::string& s) {
    using T = wowee::pipeline::WoweeTabards;
    if (s == "solid")     return T::Solid;
    if (s == "gradient")  return T::Gradient;
    if (s == "chevron")   return T::Chevron;
    if (s == "quartered") return T::Quartered;
    if (s == "starburst") return T::Starburst;
    return -1;
}

int parseBorderPatternToken(const std::string& s) {
    using T = wowee::pipeline::WoweeTabards;
    if (s == "none")       return T::BorderNone;
    if (s == "thin")       return T::BorderThin;
    if (s == "thick")      return T::BorderThick;
    if (s == "decorative") return T::BorderDecorative;
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
                    "import-wtbd-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wtbd");
    if (out.empty()) out = base + ".wtbd.json";
    if (!wowee::pipeline::WoweeTabardsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wtbd-json: WTBD not found: %s.wtbd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTabardsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WTBD";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"tabardId", e.tabardId},
            {"name", e.name},
            {"description", e.description},
            {"backgroundPattern", e.backgroundPattern},
            {"backgroundPatternName",
                backgroundPatternName(e.backgroundPattern)},
            {"backgroundColor", e.backgroundColor},
            {"borderPattern", e.borderPattern},
            {"borderPatternName",
                borderPatternName(e.borderPattern)},
            {"borderColor", e.borderColor},
            {"emblemId", e.emblemId},
            {"emblemColor", e.emblemColor},
            {"guildId", e.guildId},
            {"creatorPlayerId", e.creatorPlayerId},
            {"isApproved", e.isApproved != 0},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wtbd-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu tabards)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wtbd");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wtbd-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wtbd-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeTabards c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wtbd-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeTabards::Entry e;
        e.tabardId = je.value("tabardId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "backgroundPattern",
                            "backgroundPatternName",
                            parseBackgroundPatternToken,
                            "backgroundPattern",
                            e.tabardId,
                            e.backgroundPattern)) return 1;
        if (!readEnumField(je, "borderPattern",
                            "borderPatternName",
                            parseBorderPatternToken,
                            "borderPattern",
                            e.tabardId,
                            e.borderPattern)) return 1;
        e.emblemId = static_cast<uint16_t>(
            je.value("emblemId", 0u));
        e.backgroundColor = je.value("backgroundColor",
                                       0xFF000000u);
        e.borderColor = je.value("borderColor", 0xFFFFFFFFu);
        e.emblemColor = je.value("emblemColor", 0xFFFFFFFFu);
        e.guildId = je.value("guildId", 0u);
        e.creatorPlayerId = je.value("creatorPlayerId", 0u);
        if (je.contains("isApproved")) {
            const auto& a = je["isApproved"];
            if (a.is_boolean())
                e.isApproved = a.get<bool>() ? 1 : 0;
            else if (a.is_number_integer())
                e.isApproved = static_cast<uint8_t>(
                    a.get<int>() != 0 ? 1 : 0);
        }
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeTabardsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wtbd-json: failed to save %s.wtbd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wtbd (%zu tabards)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wtbd");
    if (!wowee::pipeline::WoweeTabardsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wtbd: WTBD not found: %s.wtbd\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeTabardsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.tabardId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.tabardId == 0)
            errors.push_back(ctx + ": tabardId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.backgroundPattern > 4) {
            errors.push_back(ctx + ": backgroundPattern " +
                std::to_string(e.backgroundPattern) +
                " out of range (must be 0..4)");
        }
        if (e.borderPattern > 3) {
            errors.push_back(ctx + ": borderPattern " +
                std::to_string(e.borderPattern) +
                " out of range (must be 0..3)");
        }
        if (e.emblemId > 1023) {
            warnings.push_back(ctx + ": emblemId " +
                std::to_string(e.emblemId) +
                " > 1023 — beyond the canonical glyph "
                "range; verify the renderer supports it");
        }
        // All three colors should have non-zero alpha
        // (alpha=0 would render an invisible layer of
        // the tabard composition).
        auto checkAlpha = [&](uint32_t color, const char* what) {
            uint8_t a = (color >> 24) & 0xFF;
            if (a == 0) {
                warnings.push_back(ctx + ": " + what +
                    " has alpha=0 — this layer would "
                    "render fully transparent");
            }
        };
        checkAlpha(e.backgroundColor, "backgroundColor");
        checkAlpha(e.borderColor, "borderColor");
        checkAlpha(e.emblemColor, "emblemColor");
        // Color-similarity heuristic: if background and
        // emblem colors are too close, the emblem won't
        // be visible against the background. Compare the
        // RGB channels with a small tolerance.
        auto colorDist = [](uint32_t a, uint32_t b) -> int {
            int dr = ((a) & 0xFF) - ((b) & 0xFF);
            int dg = ((a >> 8) & 0xFF) - ((b >> 8) & 0xFF);
            int db = ((a >> 16) & 0xFF) - ((b >> 16) & 0xFF);
            return dr * dr + dg * dg + db * db;
        };
        if (colorDist(e.backgroundColor, e.emblemColor) < 1500) {
            warnings.push_back(ctx +
                ": emblemColor is visually similar to "
                "backgroundColor (squared RGB distance < "
                "1500) — emblem may not be readable; "
                "consider a contrasting color");
        }
        if (!idsSeen.insert(e.tabardId).second) {
            errors.push_back(ctx + ": duplicate tabardId");
        }
    }
    return cli::reportValidation("wtbd", base, jsonOut, errors, warnings,
                                 formatted("%zu tabards, all tabardIds "
                    "unique, contrasting colors", c.entries.size()));
}

} // namespace

bool handleTabardsCatalog(int& i, int argc, char** argv,
                           int& outRc) {
    if (std::strcmp(argv[i], "--gen-tbd") == 0 && i + 1 < argc) {
        outRc = handleGenAlliance(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tbd-horde") == 0 && i + 1 < argc) {
        outRc = handleGenHorde(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-tbd-faction") == 0 && i + 1 < argc) {
        outRc = handleGenFaction(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wtbd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wtbd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wtbd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wtbd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
