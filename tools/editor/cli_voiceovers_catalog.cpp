#include "cli_voiceovers_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_voiceovers.hpp"
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

const char* eventKindName(uint8_t k) {
    using V = wowee::pipeline::WoweeVoiceovers;
    switch (k) {
        case V::Greeting:      return "greeting";
        case V::Aggro:         return "aggro";
        case V::Death:         return "death";
        case V::QuestStart:    return "queststart";
        case V::QuestProgress: return "questprogress";
        case V::QuestComplete: return "questcomplete";
        case V::Goodbye:       return "goodbye";
        case V::Special:       return "special";
        case V::Phase:         return "phase";
        default:               return "unknown";
    }
}

const char* genderHintName(uint8_t g) {
    using V = wowee::pipeline::WoweeVoiceovers;
    switch (g) {
        case V::Male:   return "male";
        case V::Female: return "female";
        case V::Both:   return "both";
        default:        return "unknown";
    }
}


void printGenSummary(const wowee::pipeline::WoweeVoiceovers& c,
                     const std::string& base) {
    std::printf("Wrote %s.wvox\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  voices  : %zu\n", c.entries.size());
}

int handleGenQuest(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestgiverVoices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvox");
    auto c = wowee::pipeline::WoweeVoiceoversLoader::makeQuestgiver(name);
    if (!saveOrError<wowee::pipeline::WoweeVoiceoversLoader>(c, base, "gen-vox", ".wvox")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBoss(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "LichKingVoices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvox");
    auto c = wowee::pipeline::WoweeVoiceoversLoader::makeBoss(name);
    if (!saveOrError<wowee::pipeline::WoweeVoiceoversLoader>(c, base, "gen-vox-boss", ".wvox")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenVendor(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "VendorVoices";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wvox");
    auto c = wowee::pipeline::WoweeVoiceoversLoader::makeVendor(name);
    if (!saveOrError<wowee::pipeline::WoweeVoiceoversLoader>(c, base, "gen-vox-vendor", ".wvox")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wvox");
    if (!wowee::pipeline::WoweeVoiceoversLoader::exists(base)) {
        std::fprintf(stderr, "WVOX not found: %s.wvox\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeVoiceoversLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wvox"] = base + ".wvox";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"voiceId", e.voiceId},
                {"name", e.name},
                {"description", e.description},
                {"npcId", e.npcId},
                {"eventKind", e.eventKind},
                {"eventKindName", eventKindName(e.eventKind)},
                {"genderHint", e.genderHint},
                {"genderHintName", genderHintName(e.genderHint)},
                {"variantIndex", e.variantIndex},
                {"audioPath", e.audioPath},
                {"transcript", e.transcript},
                {"durationMs", e.durationMs},
                {"volumeDb", e.volumeDb},
                {"iconColorRGBA", e.iconColorRGBA},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WVOX: %s.wvox\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  voices  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   npc      event           gender  var  dur(ms)  dB   name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u   %5u   %-13s    %-6s   %2u    %5u   %+3d   %s\n",
                    e.voiceId, e.npcId,
                    eventKindName(e.eventKind),
                    genderHintName(e.genderHint),
                    e.variantIndex, e.durationMs,
                    e.volumeDb, e.name.c_str());
        if (!e.transcript.empty()) {
            std::printf("           > \"%s\"\n",
                        e.transcript.c_str());
        }
    }
    return 0;
}

int parseEventKindToken(const std::string& s) {
    using V = wowee::pipeline::WoweeVoiceovers;
    if (s == "greeting")      return V::Greeting;
    if (s == "aggro")         return V::Aggro;
    if (s == "death")         return V::Death;
    if (s == "queststart")    return V::QuestStart;
    if (s == "questprogress") return V::QuestProgress;
    if (s == "questcomplete") return V::QuestComplete;
    if (s == "goodbye")       return V::Goodbye;
    if (s == "special")       return V::Special;
    if (s == "phase")         return V::Phase;
    return -1;
}

int parseGenderHintToken(const std::string& s) {
    using V = wowee::pipeline::WoweeVoiceovers;
    if (s == "male")   return V::Male;
    if (s == "female") return V::Female;
    if (s == "both")   return V::Both;
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
                    "import-wvox-json: unknown %s token "
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
    base = cli::withoutExt(base, ".wvox");
    if (out.empty()) out = base + ".wvox.json";
    if (!wowee::pipeline::WoweeVoiceoversLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wvox-json: WVOX not found: %s.wvox\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeVoiceoversLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WVOX";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"voiceId", e.voiceId},
            {"name", e.name},
            {"description", e.description},
            {"npcId", e.npcId},
            {"eventKind", e.eventKind},
            {"eventKindName", eventKindName(e.eventKind)},
            {"genderHint", e.genderHint},
            {"genderHintName", genderHintName(e.genderHint)},
            {"variantIndex", e.variantIndex},
            {"audioPath", e.audioPath},
            {"transcript", e.transcript},
            {"durationMs", e.durationMs},
            {"volumeDb", e.volumeDb},
            {"iconColorRGBA", e.iconColorRGBA},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wvox-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu voice clips)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(in, ".wvox");
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wvox-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wvox-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeVoiceovers c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wvox-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeVoiceovers::Entry e;
        e.voiceId = je.value("voiceId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.npcId = je.value("npcId", 0u);
        if (!readEnumField(je, "eventKind", "eventKindName",
                            parseEventKindToken, "eventKind",
                            e.voiceId, e.eventKind)) return 1;
        if (!readEnumField(je, "genderHint", "genderHintName",
                            parseGenderHintToken, "genderHint",
                            e.voiceId, e.genderHint)) return 1;
        e.variantIndex = static_cast<uint8_t>(
            je.value("variantIndex", 0u));
        e.audioPath = je.value("audioPath", std::string{});
        e.transcript = je.value("transcript", std::string{});
        e.durationMs = je.value("durationMs", 0u);
        e.volumeDb = static_cast<int8_t>(
            je.value("volumeDb", 0));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeVoiceoversLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wvox-json: failed to save %s.wvox\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wvox (%zu voice clips)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wvox");
    if (!wowee::pipeline::WoweeVoiceoversLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wvox: WVOX not found: %s.wvox\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeVoiceoversLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-(npcId, eventKind, variantIndex) triple
    // uniqueness — two voice clips with all three
    // matching would be ambiguous (which one plays?).
    std::set<uint64_t> tripleSeen;
    auto tripleKey = [](uint32_t npc, uint8_t event,
                        uint8_t variant) {
        return (static_cast<uint64_t>(npc) << 32) |
               (static_cast<uint64_t>(event) << 8) |
               variant;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.voiceId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.voiceId == 0)
            errors.push_back(ctx + ": voiceId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.npcId == 0) {
            errors.push_back(ctx +
                ": npcId is 0 — voice clip is unbound to "
                "any creature");
        }
        if (e.eventKind > 8) {
            errors.push_back(ctx + ": eventKind " +
                std::to_string(e.eventKind) +
                " out of range (must be 0..8)");
        }
        if (e.genderHint > 2) {
            errors.push_back(ctx + ": genderHint " +
                std::to_string(e.genderHint) +
                " out of range (must be 0..2)");
        }
        if (e.audioPath.empty()) {
            errors.push_back(ctx +
                ": audioPath is empty — voice clip would "
                "play no audio");
        }
        if (e.durationMs == 0 && !e.audioPath.empty()) {
            warnings.push_back(ctx +
                ": durationMs=0 but audioPath set — "
                "trigger handler can't subtitle-sync "
                "without duration; consider populating "
                "from the audio file's actual length");
        }
        if (e.volumeDb < -20 || e.volumeDb > 6) {
            warnings.push_back(ctx + ": volumeDb " +
                std::to_string(e.volumeDb) +
                " outside [-20, +6] typical range — "
                "extreme values may clip or be inaudible");
        }
        if (e.transcript.empty()) {
            warnings.push_back(ctx +
                ": transcript is empty — accessibility "
                "TTS engines + chat-bubble subtitles "
                "have no text to display");
        }
        // Triple uniqueness: same NPC + event + variant
        // would pick non-deterministically.
        if (e.npcId != 0) {
            uint64_t key = tripleKey(e.npcId, e.eventKind,
                                       e.variantIndex);
            if (!tripleSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": (npcId=" + std::to_string(e.npcId) +
                    ", eventKind=" +
                    std::string(eventKindName(e.eventKind)) +
                    ", variantIndex=" +
                    std::to_string(e.variantIndex) +
                    ") triple already bound by another "
                    "voice clip — random pick at trigger "
                    "time would be ambiguous");
            }
        }
        if (!idsSeen.insert(e.voiceId).second) {
            errors.push_back(ctx + ": duplicate voiceId");
        }
    }
    return cli::reportValidation("wvox", base, jsonOut, errors, warnings,
                                 formatted("%zu voice clips, all voiceIds + "
                    "(npc,event,variant) triples unique", c.entries.size()));
}

} // namespace

bool handleVoiceoversCatalog(int& i, int argc, char** argv,
                              int& outRc) {
    if (std::strcmp(argv[i], "--gen-vox") == 0 && i + 1 < argc) {
        outRc = handleGenQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-vox-boss") == 0 && i + 1 < argc) {
        outRc = handleGenBoss(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-vox-vendor") == 0 &&
        i + 1 < argc) {
        outRc = handleGenVendor(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wvox") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wvox") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wvox-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wvox-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
