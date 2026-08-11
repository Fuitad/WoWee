#include "cli_emotes_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_emotes.hpp"
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

const char* emoteKindName(uint8_t k) {
    using E = wowee::pipeline::WoweeEmotes;
    switch (k) {
        case E::Social:   return "social";
        case E::Combat:   return "combat";
        case E::RolePlay: return "roleplay";
        case E::System:   return "system";
        default:          return "unknown";
    }
}

const char* sexFilterName(uint8_t s) {
    using E = wowee::pipeline::WoweeEmotes;
    switch (s) {
        case E::SexBoth:    return "both";
        case E::MaleOnly:   return "male";
        case E::FemaleOnly: return "female";
        default:            return "unknown";
    }
}

const char* ttsHintName(uint8_t h) {
    using E = wowee::pipeline::WoweeEmotes;
    switch (h) {
        case E::TtsTalk:    return "talk";
        case E::TtsWhisper: return "whisper";
        case E::TtsYell:    return "yell";
        case E::TtsSilent:  return "silent";
        default:            return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeEmotes& c,
                     const std::string& base) {
    std::printf("Wrote %s.wemo\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  emotes  : %zu\n", c.entries.size());
}

int handleGenBasic(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BasicSocialEmotes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wemo");
    auto c = wowee::pipeline::WoweeEmotesLoader::makeBasic(name);
    if (!saveOrError<wowee::pipeline::WoweeEmotesLoader>(c, base, "gen-emo", ".wemo")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenCombat(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "CombatEmotes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wemo");
    auto c = wowee::pipeline::WoweeEmotesLoader::makeCombat(name);
    if (!saveOrError<wowee::pipeline::WoweeEmotesLoader>(c, base, "gen-emo-combat", ".wemo")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenRolePlay(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "RolePlayEmotes";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wemo");
    auto c = wowee::pipeline::WoweeEmotesLoader::makeRolePlay(name);
    if (!saveOrError<wowee::pipeline::WoweeEmotesLoader>(c, base, "gen-emo-rp", ".wemo")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wemo");
    if (!wowee::pipeline::WoweeEmotesLoader::exists(base)) {
        std::fprintf(stderr, "WEMO not found: %s.wemo\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeEmotesLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wemo"] = base + ".wemo";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"emoteId", e.emoteId},
                {"name", e.name},
                {"description", e.description},
                {"slashCommand", e.slashCommand},
                {"animationId", e.animationId},
                {"soundId", e.soundId},
                {"targetMessage", e.targetMessage},
                {"noTargetMessage", e.noTargetMessage},
                {"emoteKind", e.emoteKind},
                {"emoteKindName", emoteKindName(e.emoteKind)},
                {"sex", e.sex},
                {"sexName", sexFilterName(e.sex)},
                {"requiredRace", e.requiredRace},
                {"ttsHint", e.ttsHint},
                {"ttsHintName", ttsHintName(e.ttsHint)},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WEMO: %s.wemo\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  emotes  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  /command       kind     anim   snd   sex     tts      \n");
    for (const auto& e : c.entries) {
        std::printf("  %4u  /%-12s  %-8s  %4u  %4u  %-5s   %-7s\n",
                    e.emoteId, e.slashCommand.c_str(),
                    emoteKindName(e.emoteKind),
                    e.animationId, e.soundId,
                    sexFilterName(e.sex),
                    ttsHintName(e.ttsHint));
    }
    return 0;
}

// Token parsers for the three WEMO enums.
int parseEmoteKindToken(const std::string& s) {
    using E = wowee::pipeline::WoweeEmotes;
    if (s == "social")   return E::Social;
    if (s == "combat")   return E::Combat;
    if (s == "roleplay") return E::RolePlay;
    if (s == "system")   return E::System;
    return -1;
}

int parseSexFilterToken(const std::string& s) {
    using E = wowee::pipeline::WoweeEmotes;
    if (s == "both")   return E::SexBoth;
    if (s == "male")   return E::MaleOnly;
    if (s == "female") return E::FemaleOnly;
    return -1;
}

int parseTtsHintToken(const std::string& s) {
    using E = wowee::pipeline::WoweeEmotes;
    if (s == "talk")    return E::TtsTalk;
    if (s == "whisper") return E::TtsWhisper;
    if (s == "yell")    return E::TtsYell;
    if (s == "silent")  return E::TtsSilent;
    return -1;
}

// Generic int-or-token coercion — same shape as the
// WMSP helper. Returns false on parse error (hard error)
// and reports via stderr; returns true on success OR
// when the field is absent (leave outValue at default).
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
                    "import-wemo-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wemo");
    if (out.empty()) out = base + ".wemo.json";
    if (!wowee::pipeline::WoweeEmotesLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wemo-json: WEMO not found: %s.wemo\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeEmotesLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WEMO";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"emoteId", e.emoteId},
            {"name", e.name},
            {"description", e.description},
            {"slashCommand", e.slashCommand},
            {"animationId", e.animationId},
            {"soundId", e.soundId},
            {"targetMessage", e.targetMessage},
            {"noTargetMessage", e.noTargetMessage},
            {"emoteKind", e.emoteKind},
            {"emoteKindName", emoteKindName(e.emoteKind)},
            {"sex", e.sex},
            {"sexName", sexFilterName(e.sex)},
            {"requiredRace", e.requiredRace},
            {"ttsHint", e.ttsHint},
            {"ttsHintName", ttsHintName(e.ttsHint)},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wemo-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu emotes)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wemo");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wemo-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wemo-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeEmotes c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wemo-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeEmotes::Entry e;
        e.emoteId = je.value("emoteId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.slashCommand = je.value("slashCommand", std::string{});
        e.animationId = je.value("animationId", 0u);
        e.soundId = je.value("soundId", 0u);
        e.targetMessage = je.value("targetMessage", std::string{});
        e.noTargetMessage = je.value("noTargetMessage",
                                       std::string{});
        if (!readEnumField(je, "emoteKind", "emoteKindName",
                            parseEmoteKindToken, "emoteKind",
                            e.emoteId, e.emoteKind)) return 1;
        if (!readEnumField(je, "sex", "sexName",
                            parseSexFilterToken, "sex",
                            e.emoteId, e.sex)) return 1;
        if (!readEnumField(je, "ttsHint", "ttsHintName",
                            parseTtsHintToken, "ttsHint",
                            e.emoteId, e.ttsHint)) return 1;
        e.requiredRace = static_cast<uint8_t>(
            je.value("requiredRace", 0u));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeEmotesLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wemo-json: failed to save %s.wemo\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wemo (%zu emotes)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wemo");
    if (!wowee::pipeline::WoweeEmotesLoader::exists(base)) {
        return reportMissing("validate-wemo", "WEMO", base, ".wemo");
    }
    auto c = wowee::pipeline::WoweeEmotesLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::vector<uint32_t> idsSeen;
    std::set<std::string> commandsSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.emoteId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.emoteId == 0)
            errors.push_back(ctx + ": emoteId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.slashCommand.empty()) {
            errors.push_back(ctx +
                ": slashCommand is empty — chat parser "
                "would fail to dispatch");
        }
        // Slash command should not start with '/' (the
        // chat parser strips it before the lookup).
        if (!e.slashCommand.empty() &&
            e.slashCommand[0] == '/') {
            errors.push_back(ctx + ": slashCommand starts "
                "with '/' — store it bare (chat parser "
                "strips the leading slash before lookup)");
        }
        // Lowercase only — chat commands are
        // case-folded before lookup, so an uppercase
        // letter would be unreachable.
        for (char ch : e.slashCommand) {
            if (ch >= 'A' && ch <= 'Z') {
                errors.push_back(ctx + ": slashCommand '" +
                    e.slashCommand +
                    "' contains uppercase — chat parser "
                    "lowercases input before lookup so "
                    "this entry would be unreachable");
                break;
            }
        }
        if (e.emoteKind > 3) {
            errors.push_back(ctx + ": emoteKind " +
                std::to_string(e.emoteKind) +
                " out of range (must be 0..3)");
        }
        if (e.sex > 2) {
            errors.push_back(ctx + ": sex " +
                std::to_string(e.sex) +
                " out of range (must be 0..2)");
        }
        if (e.ttsHint > 3) {
            errors.push_back(ctx + ": ttsHint " +
                std::to_string(e.ttsHint) +
                " out of range (must be 0..3)");
        }
        // If targetMessage references a target slot, it
        // should contain TWO %s tokens (actor + target);
        // noTargetMessage should contain exactly ONE.
        auto countPercentS = [](const std::string& s) {
            int n = 0;
            for (size_t k = 0; k + 1 < s.size(); ++k) {
                if (s[k] == '%' && s[k + 1] == 's') ++n;
            }
            return n;
        };
        int tgtTokens = countPercentS(e.targetMessage);
        int noTgtTokens = countPercentS(e.noTargetMessage);
        if (!e.targetMessage.empty() && tgtTokens < 2) {
            warnings.push_back(ctx +
                ": targetMessage has " +
                std::to_string(tgtTokens) +
                " %s token(s) — expected 2 (actor name + "
                "target name)");
        }
        if (!e.noTargetMessage.empty() && noTgtTokens != 1) {
            warnings.push_back(ctx +
                ": noTargetMessage has " +
                std::to_string(noTgtTokens) +
                " %s token(s) — expected exactly 1 (actor "
                "name)");
        }
        // Slash commands must be unique — chat parser
        // dispatches by exact match.
        if (!e.slashCommand.empty() &&
            !commandsSeen.insert(e.slashCommand).second) {
            errors.push_back(ctx + ": duplicate slashCommand "
                "'" + e.slashCommand + "' — chat parser "
                "would dispatch ambiguously");
        }
        for (uint32_t prev : idsSeen) {
            if (prev == e.emoteId) {
                errors.push_back(ctx + ": duplicate emoteId");
                break;
            }
        }
        idsSeen.push_back(e.emoteId);
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wemo", base, errors, warnings);
    std::printf("validate-wemo: %s.wemo\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu emotes, all emoteIds + "
                    "slash commands unique\n",
                    c.entries.size());
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleEmotesCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-emo") == 0 && i + 1 < argc) {
        outRc = handleGenBasic(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-emo-combat") == 0 && i + 1 < argc) {
        outRc = handleGenCombat(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-emo-rp") == 0 && i + 1 < argc) {
        outRc = handleGenRolePlay(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wemo") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wemo") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wemo-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wemo-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
