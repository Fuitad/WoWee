#include "cli_localization_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_localization.hpp"
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

const char* languageCodeName(uint8_t l) {
    using L = wowee::pipeline::WoweeLocalization;
    switch (l) {
        case L::enUS:    return "enUS";
        case L::enGB:    return "enGB";
        case L::deDE:    return "deDE";
        case L::esES:    return "esES";
        case L::frFR:    return "frFR";
        case L::itIT:    return "itIT";
        case L::koKR:    return "koKR";
        case L::ptBR:    return "ptBR";
        case L::ruRU:    return "ruRU";
        case L::zhCN:    return "zhCN";
        case L::zhTW:    return "zhTW";
        case L::Unknown: return "Unknown";
        default:         return "?";
    }
}

const char* namespaceName(uint8_t n) {
    using L = wowee::pipeline::WoweeLocalization;
    switch (n) {
        case L::UI:       return "ui";
        case L::Quest:    return "quest";
        case L::Item:     return "item";
        case L::Spell:    return "spell";
        case L::Creature: return "creature";
        case L::Tooltip:  return "tooltip";
        case L::Gossip:   return "gossip";
        case L::System:   return "system";
        default:          return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeLocalization& c,
                     const std::string& base) {
    std::printf("Wrote %s.wlan\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  strings : %zu\n", c.entries.size());
}

int handleGenUI(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "UIBasicsLocalization";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlan");
    auto c = wowee::pipeline::WoweeLocalizationLoader::makeUIBasics(name);
    if (!saveOrError<wowee::pipeline::WoweeLocalizationLoader>(c, base, "gen-lan", ".wlan")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuest(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestSampleLocalization";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlan");
    auto c = wowee::pipeline::WoweeLocalizationLoader::makeQuestSample(name);
    if (!saveOrError<wowee::pipeline::WoweeLocalizationLoader>(c, base, "gen-lan-quest", ".wlan")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTooltip(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TooltipSetLocalization";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wlan");
    auto c = wowee::pipeline::WoweeLocalizationLoader::makeTooltipSet(name);
    if (!saveOrError<wowee::pipeline::WoweeLocalizationLoader>(c, base, "gen-lan-tooltip", ".wlan")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlan");
    if (!wowee::pipeline::WoweeLocalizationLoader::exists(base)) {
        std::fprintf(stderr, "WLAN not found: %s.wlan\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLocalizationLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wlan"] = base + ".wlan";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"stringId", e.stringId},
                {"name", e.name},
                {"description", e.description},
                {"languageCode", e.languageCode},
                {"languageCodeName",
                    languageCodeName(e.languageCode)},
                {"namespace", e.namespace_},
                {"namespaceName", namespaceName(e.namespace_)},
                {"originalKey", e.originalKey},
                {"localizedText", e.localizedText},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WLAN: %s.wlan\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  strings : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   lang  ns         key                       text\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %-4s  %-9s  %-25s  %s\n",
                    e.stringId,
                    languageCodeName(e.languageCode),
                    namespaceName(e.namespace_),
                    e.originalKey.c_str(),
                    e.localizedText.c_str());
    }
    return 0;
}

int parseLanguageCodeToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLocalization;
    if (s == "enUS") return L::enUS;
    if (s == "enGB") return L::enGB;
    if (s == "deDE") return L::deDE;
    if (s == "esES") return L::esES;
    if (s == "frFR") return L::frFR;
    if (s == "itIT") return L::itIT;
    if (s == "koKR") return L::koKR;
    if (s == "ptBR") return L::ptBR;
    if (s == "ruRU") return L::ruRU;
    if (s == "zhCN") return L::zhCN;
    if (s == "zhTW") return L::zhTW;
    return -1;
}

int parseNamespaceToken(const std::string& s) {
    using L = wowee::pipeline::WoweeLocalization;
    if (s == "ui")       return L::UI;
    if (s == "quest")    return L::Quest;
    if (s == "item")     return L::Item;
    if (s == "spell")    return L::Spell;
    if (s == "creature") return L::Creature;
    if (s == "tooltip")  return L::Tooltip;
    if (s == "gossip")   return L::Gossip;
    if (s == "system")   return L::System;
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
                    "import-wlan-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wlan");
    if (out.empty()) out = base + ".wlan.json";
    if (!wowee::pipeline::WoweeLocalizationLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wlan-json: WLAN not found: %s.wlan\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLocalizationLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WLAN";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"stringId", e.stringId},
            {"name", e.name},
            {"description", e.description},
            {"languageCode", e.languageCode},
            {"languageCodeName",
                languageCodeName(e.languageCode)},
            {"namespace", e.namespace_},
            {"namespaceName", namespaceName(e.namespace_)},
            {"originalKey", e.originalKey},
            {"localizedText", e.localizedText},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wlan-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu strings)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wlan");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wlan-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wlan-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeLocalization c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wlan-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeLocalization::Entry e;
        e.stringId = je.value("stringId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        if (!readEnumField(je, "languageCode",
                            "languageCodeName",
                            parseLanguageCodeToken,
                            "languageCode",
                            e.stringId,
                            e.languageCode)) return 1;
        if (!readEnumField(je, "namespace", "namespaceName",
                            parseNamespaceToken, "namespace",
                            e.stringId, e.namespace_)) return 1;
        e.originalKey = je.value("originalKey", std::string{});
        e.localizedText = je.value("localizedText", std::string{});
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeLocalizationLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wlan-json: failed to save %s.wlan\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wlan (%zu strings)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wlan");
    if (!wowee::pipeline::WoweeLocalizationLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wlan: WLAN not found: %s.wlan\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeLocalizationLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(originalKey, languageCode, namespace_) triple
    // uniqueness — two entries with all three matching
    // would tie at runtime when the locale-aware text
    // layer looks up an override.
    std::set<std::string> tripleSeen;
    auto tripleKey = [](const std::string& key, uint8_t lang,
                        uint8_t ns) {
        return std::to_string(lang) + "|" +
               std::to_string(ns) + "|" + key;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.stringId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.stringId == 0)
            errors.push_back(ctx + ": stringId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.languageCode > 10 && e.languageCode != 255) {
            errors.push_back(ctx + ": languageCode " +
                std::to_string(e.languageCode) +
                " out of range (must be 0..10 or 255 "
                "Unknown)");
        }
        if (e.namespace_ > 7) {
            errors.push_back(ctx + ": namespace " +
                std::to_string(e.namespace_) +
                " out of range (must be 0..7)");
        }
        if (e.originalKey.empty()) {
            errors.push_back(ctx +
                ": originalKey is empty — locale-aware "
                "text layer has nothing to look up");
        }
        if (e.localizedText.empty()) {
            warnings.push_back(ctx +
                ": localizedText is empty — override "
                "would render blank, possibly worse than "
                "falling through to default");
        }
        // Triple uniqueness check.
        if (!e.originalKey.empty()) {
            std::string key = tripleKey(e.originalKey,
                                          e.languageCode,
                                          e.namespace_);
            if (!tripleSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": (originalKey='" + e.originalKey +
                    "', languageCode=" +
                    std::string(languageCodeName(e.languageCode)) +
                    ", namespace=" +
                    std::string(namespaceName(e.namespace_)) +
                    ") triple already bound by another "
                    "entry — locale lookup would tie "
                    "non-deterministically");
            }
        }
        if (!idsSeen.insert(e.stringId).second) {
            errors.push_back(ctx + ": duplicate stringId");
        }
    }
    return cli::reportValidation("wlan", base, jsonOut, errors, warnings,
                                 formatted("%zu strings, all stringIds + "
                    "(key,lang,ns) triples unique", c.entries.size()));
}

} // namespace

bool handleLocalizationCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-lan") == 0 && i + 1 < argc) {
        outRc = handleGenUI(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lan-quest") == 0 &&
        i + 1 < argc) {
        outRc = handleGenQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-lan-tooltip") == 0 &&
        i + 1 < argc) {
        outRc = handleGenTooltip(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wlan") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wlan") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wlan-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wlan-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
