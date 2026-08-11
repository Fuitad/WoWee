#include "cli_chat_links_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_chat_links.hpp"
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

const char* linkKindName(uint8_t k) {
    using L = wowee::pipeline::WoweeChatLinks;
    switch (k) {
        case L::Item:        return "item";
        case L::Quest:       return "quest";
        case L::Spell:       return "spell";
        case L::Achievement: return "achievement";
        case L::Talent:      return "talent";
        case L::Trade:       return "trade";
        default:             return "?";
    }
}


void printGenSummary(const wowee::pipeline::WoweeChatLinks& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlnk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  links   : %zu\n", c.entries.size());
}

int handleGenStandard(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StandardChatLinks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlnk");
    auto c = wowee::pipeline::WoweeChatLinksLoader::
        makeStandardLinks(name);
    if (!saveOrError<wowee::pipeline::WoweeChatLinksLoader>(c, base, "gen-lnk-std", ".wlnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTalentTrade(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TalentTradeChatLinks";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlnk");
    auto c = wowee::pipeline::WoweeChatLinksLoader::
        makeTalentTrade(name);
    if (!saveOrError<wowee::pipeline::WoweeChatLinksLoader>(c, base, "gen-lnk-talent", ".wlnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenColorVariants(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "ItemQualityColorVariants";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlnk");
    auto c = wowee::pipeline::WoweeChatLinksLoader::
        makeColorVariants(name);
    if (!saveOrError<wowee::pipeline::WoweeChatLinksLoader>(c, base, "gen-lnk-quality", ".wlnk")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlnk");
    if (!wowee::pipeline::WoweeChatLinksLoader::exists(base)) {
        std::fprintf(stderr, "WLNK not found: %s.wlnk\n",
                     base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChatLinksLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlnk"] = base + ".wlnk";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"linkId", e.linkId},
                {"name", e.name},
                {"linkKind", e.linkKind},
                {"linkKindName", linkKindName(e.linkKind)},
                {"requireServerLookup",
                    e.requireServerLookup != 0},
                {"colorRGBA", e.colorRGBA},
                {"linkTemplate", e.linkTemplate},
                {"tooltipTemplate", e.tooltipTemplate},
                {"iconRule", e.iconRule},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLNK: %s.wlnk\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  links   : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  kind         srv  color       icon         name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  %-11s   %s  0x%08X  %-11s   %s\n",
                    e.linkId, linkKindName(e.linkKind),
                    e.requireServerLookup ? "Y" : "n",
                    e.colorRGBA,
                    e.iconRule.c_str(),
                    e.name.c_str());
    }
    return 0;
}

// Counts %d / %s placeholder occurrences in a sprintf
// template. Used by the validator to catch templates
// with no placeholders (would never substitute the
// link parameters).
int countPlaceholders(const std::string& tpl) {
    int count = 0;
    for (size_t i = 0; i + 1 < tpl.size(); ++i) {
        if (tpl[i] != '%') continue;
        char c = tpl[i + 1];
        if (c == 'd' || c == 's' || c == 'u' ||
            c == 'i' || c == 'x' || c == 'X') {
            ++count;
            ++i;  // skip the format char
        } else if (c == '%') {
            ++i;  // literal %% — don't count
        }
    }
    return count;
}

int parseLinkKindToken(const std::string& s) {
    using L = wowee::pipeline::WoweeChatLinks;
    if (s == "item")        return L::Item;
    if (s == "quest")       return L::Quest;
    if (s == "spell")       return L::Spell;
    if (s == "achievement") return L::Achievement;
    if (s == "talent")      return L::Talent;
    if (s == "trade")       return L::Trade;
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
                    "import-wlnk-json: unknown %s token "
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

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlnk");
    if (!wowee::pipeline::WoweeChatLinksLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wlnk: WLNK not found: %s.wlnk\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChatLinksLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.linkId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.linkId == 0)
            errors.push_back(ctx + ": linkId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.linkTemplate.empty()) {
            errors.push_back(ctx +
                ": linkTemplate is empty — link "
                "composer would have nothing to "
                "format");
        }
        if (e.linkKind > 5) {
            errors.push_back(ctx + ": linkKind " +
                std::to_string(e.linkKind) +
                " out of range (0..5)");
        }
        // CRITICAL: linkTemplate MUST contain at
        // least one %d or %s placeholder. A template
        // with no placeholders never substitutes link
        // parameters — the chat composer would emit
        // a static string regardless of which item /
        // quest / spell was clicked.
        int placeholderCount = countPlaceholders(e.linkTemplate);
        if (!e.linkTemplate.empty() &&
            placeholderCount == 0) {
            errors.push_back(ctx +
                ": linkTemplate has no %%d / %%s "
                "placeholders — composer would emit "
                "a static string regardless of input "
                "(every link would render identically)");
        }
        // Warn on excessive placeholders (> 12) —
        // the achievement template legitimately has
        // 9 (achievementId + chardate + 5 progress
        // criteria + completion state + name) but
        // anything beyond ~12 is suspicious.
        if (placeholderCount > 12) {
            warnings.push_back(ctx +
                ": linkTemplate has " +
                std::to_string(placeholderCount) +
                " placeholders — > 12 is unusual; "
                "verify all are intentional");
        }
        // colorRGBA = 0 means fully transparent —
        // link text would be invisible. Warn.
        if (e.colorRGBA == 0) {
            warnings.push_back(ctx +
                ": colorRGBA is 0 (fully transparent)"
                " — link text would be invisible; "
                "set a quality color");
        }
        // requireServerLookup=true with no template
        // %s for receiving server-side data is
        // suspicious but not definitive (server may
        // populate the tooltip rather than the link
        // text). Warn.
        if (e.requireServerLookup &&
            e.tooltipTemplate.empty()) {
            warnings.push_back(ctx +
                ": requireServerLookup=true but "
                "tooltipTemplate is empty — server "
                "data would not be displayed anywhere "
                "(verify intentional)");
        }
        if (!idsSeen.insert(e.linkId).second) {
            errors.push_back(ctx + ": duplicate linkId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wlnk", base, errors, warnings);
    std::printf("validate-wlnk: %s.wlnk\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu links, all linkIds unique, "
                    "linkKind 0..5, linkTemplate non-empty "
                    "with at least one %%d/%%s placeholder, "
                    "non-zero colorRGBA\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = cli::withoutExt(base, ".wlnk");
    if (out.empty()) out = base + ".wlnk.json";
    if (!wowee::pipeline::WoweeChatLinksLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wlnk-json: WLNK not found: %s.wlnk\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeChatLinksLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WLNK";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"linkId", e.linkId},
            {"name", e.name},
            {"linkKind", e.linkKind},
            {"linkKindName", linkKindName(e.linkKind)},
            {"requireServerLookup",
                e.requireServerLookup != 0},
            {"colorRGBA", e.colorRGBA},
            {"linkTemplate", e.linkTemplate},
            {"tooltipTemplate", e.tooltipTemplate},
            {"iconRule", e.iconRule},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wlnk-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu links)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wlnk");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wlnk-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wlnk-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeChatLinks c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wlnk-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeChatLinks::Entry e;
        e.linkId = je.value("linkId", 0u);
        e.name = je.value("name", std::string{});
        if (!readEnumField(je, "linkKind", "linkKindName",
                            parseLinkKindToken, "linkKind",
                            e.linkId, e.linkKind)) return 1;
        // requireServerLookup may be encoded as bool OR uint8.
        if (je.contains("requireServerLookup")) {
            const auto& v = je["requireServerLookup"];
            if (v.is_boolean()) {
                e.requireServerLookup =
                    v.get<bool>() ? 1 : 0;
            } else if (v.is_number_integer()) {
                e.requireServerLookup =
                    static_cast<uint8_t>(v.get<int>());
            }
        }
        e.colorRGBA = je.value("colorRGBA", 0u);
        e.linkTemplate = je.value("linkTemplate", std::string{});
        e.tooltipTemplate =
            je.value("tooltipTemplate", std::string{});
        e.iconRule = je.value("iconRule", std::string{});
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeChatLinksLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlnk-json: failed to save %s.wlnk\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlnk (%zu links)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

} // namespace

bool handleChatLinksCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-lnk-std") == 0 &&
        i + 1 < argc) {
        outRc = handleGenStandard(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lnk-talent") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTalentTrade(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lnk-quality") == 0 &&
        i + 1 < argc) {
        outRc = handleGenColorVariants(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlnk") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlnk") == 0 &&
        i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlnk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlnk-json") == 0 &&
        i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
