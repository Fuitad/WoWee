#include "cli_movie_credits_catalog.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_movie_credits.hpp"
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

std::string stripWmvcExt(std::string base) {
    stripExt(base, ".wmvc");
    return base;
}

const char* categoryName(uint8_t k) {
    using M = wowee::pipeline::WoweeMovieCredits;
    switch (k) {
        case M::Production:  return "production";
        case M::Music:       return "music";
        case M::Audio:       return "audio";
        case M::Engineering: return "engineering";
        case M::Art:         return "art";
        case M::Voice:       return "voice";
        case M::Special:     return "special";
        default:             return "unknown";
    }
}

bool saveOrError(const wowee::pipeline::WoweeMovieCredits& c,
                 const std::string& base, const char* cmd) {
    if (!wowee::pipeline::WoweeMovieCreditsLoader::save(c, base)) {
        std::fprintf(stderr, "%s: failed to save %s.wmvc\n",
                     cmd, base.c_str());
        return false;
    }
    return true;
}

void printGenSummary(const wowee::pipeline::WoweeMovieCredits& c,
                     const std::string& base) {
    size_t totalLines = 0;
    for (const auto& e : c.entries) totalLines += e.lines.size();
    std::printf("Wrote %s.wmvc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  blocks  : %zu (%zu lines total)\n",
                c.entries.size(), totalLines);
}

int handleGenWotLK(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "WotLKIntroCredits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmvcExt(base);
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::makeWotLKIntro(name);
    if (!saveOrError(c, base, "gen-mvc")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenQuest(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "QuestCinematicCredits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmvcExt(base);
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::makeQuestCinema(name);
    if (!saveOrError(c, base, "gen-mvc-quest")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterRollCredits";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = stripWmvcExt(base);
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::makeStarterRoll(name);
    if (!saveOrError(c, base, "gen-mvc-starter")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWmvcExt(base);
    if (!wowee::pipeline::WoweeMovieCreditsLoader::exists(base)) {
        std::fprintf(stderr, "WMVC not found: %s.wmvc\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wmvc"] = base + ".wmvc";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"rollId", e.rollId},
                {"name", e.name},
                {"description", e.description},
                {"cinematicId", e.cinematicId},
                {"category", e.category},
                {"categoryName", categoryName(e.category)},
                {"orderHint", e.orderHint},
                {"iconColorRGBA", e.iconColorRGBA},
                {"lines", e.lines},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WMVC: %s.wmvc\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  blocks  : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id   cinematic  category       order  lines  name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %5u     %-10s     %4u   %4zu   %s\n",
                    e.rollId, e.cinematicId,
                    categoryName(e.category),
                    e.orderHint, e.lines.size(),
                    e.name.c_str());
        for (const auto& L : e.lines) {
            std::printf("           | %s\n", L.c_str());
        }
    }
    return 0;
}

int parseCategoryToken(const std::string& s) {
    using M = wowee::pipeline::WoweeMovieCredits;
    if (s == "production")  return M::Production;
    if (s == "music")       return M::Music;
    if (s == "audio")       return M::Audio;
    if (s == "engineering") return M::Engineering;
    if (s == "art")         return M::Art;
    if (s == "voice")       return M::Voice;
    if (s == "special")     return M::Special;
    return -1;
}

int handleExportJson(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string out;
    if (parseOptArg(i, argc, argv)) out = argv[++i];
    base = stripWmvcExt(base);
    if (out.empty()) out = base + ".wmvc.json";
    if (!wowee::pipeline::WoweeMovieCreditsLoader::exists(base)) {
        std::fprintf(stderr,
            "export-wmvc-json: WMVC not found: %s.wmvc\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::load(base);
    nlohmann::json j;
    j["magic"] = "WMVC";
    j["version"] = 1;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"rollId", e.rollId},
            {"name", e.name},
            {"description", e.description},
            {"cinematicId", e.cinematicId},
            {"category", e.category},
            {"categoryName", categoryName(e.category)},
            {"orderHint", e.orderHint},
            {"iconColorRGBA", e.iconColorRGBA},
            {"lines", e.lines},
        });
    }
    j["entries"] = arr;
    std::ofstream os(out);
    if (!os) {
        std::fprintf(stderr,
            "export-wmvc-json: failed to open %s for write\n",
            out.c_str());
        return 1;
    }
    os << j.dump(2) << "\n";
    std::printf("Wrote %s (%zu blocks)\n",
                out.c_str(), c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string in = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) {
        outBase = in;
        if (outBase.size() >= 10 &&
            outBase.substr(outBase.size() - 10) == ".wmvc.json") {
            outBase.resize(outBase.size() - 10);
        } else {
            stripExt(outBase, ".json");
            stripExt(outBase, ".wmvc");
        }
    }
    std::ifstream is(in);
    if (!is) {
        std::fprintf(stderr,
            "import-wmvc-json: cannot open %s\n", in.c_str());
        return 1;
    }
    nlohmann::json j;
    try {
        is >> j;
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "import-wmvc-json: JSON parse error: %s\n", ex.what());
        return 1;
    }
    wowee::pipeline::WoweeMovieCredits c;
    c.name = j.value("name", std::string{});
    if (!j.contains("entries") || !j["entries"].is_array()) {
        std::fprintf(stderr,
            "import-wmvc-json: missing or non-array 'entries'\n");
        return 1;
    }
    for (const auto& je : j["entries"]) {
        wowee::pipeline::WoweeMovieCredits::Entry e;
        e.rollId = je.value("rollId", 0u);
        e.name = je.value("name", std::string{});
        e.description = je.value("description", std::string{});
        e.cinematicId = je.value("cinematicId", 0u);
        if (je.contains("category")) {
            const auto& v = je["category"];
            if (v.is_string()) {
                int parsed = parseCategoryToken(
                    v.get<std::string>());
                if (parsed < 0) {
                    std::fprintf(stderr,
                        "import-wmvc-json: unknown "
                        "category token '%s' on entry "
                        "id=%u\n",
                        v.get<std::string>().c_str(),
                        e.rollId);
                    return 1;
                }
                e.category = static_cast<uint8_t>(parsed);
            } else if (v.is_number_integer()) {
                e.category = static_cast<uint8_t>(v.get<int>());
            }
        } else if (je.contains("categoryName") &&
                   je["categoryName"].is_string()) {
            int parsed = parseCategoryToken(
                je["categoryName"].get<std::string>());
            if (parsed >= 0)
                e.category = static_cast<uint8_t>(parsed);
        }
        e.orderHint = static_cast<uint16_t>(
            je.value("orderHint", 0u));
        e.iconColorRGBA = je.value("iconColorRGBA", 0xFFFFFFFFu);
        if (je.contains("lines") && je["lines"].is_array()) {
            for (const auto& L : je["lines"]) {
                if (L.is_string())
                    e.lines.push_back(L.get<std::string>());
            }
        }
        c.entries.push_back(e);
    }
    if (!wowee::pipeline::WoweeMovieCreditsLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wmvc-json: failed to save %s.wmvc\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wmvc (%zu blocks)\n",
                outBase.c_str(), c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = stripWmvcExt(base);
    if (!wowee::pipeline::WoweeMovieCreditsLoader::exists(base)) {
        std::fprintf(stderr,
            "validate-wmvc: WMVC not found: %s.wmvc\n",
            base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeMovieCreditsLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    std::set<uint32_t> idsSeen;
    // Per-cinematic orderHint uniqueness — two blocks at
    // the same orderHint within one cinematic would
    // render in unstable order.
    std::set<uint64_t> orderSlotsSeen;
    auto orderKey = [](uint32_t cine, uint16_t order) {
        return (static_cast<uint64_t>(cine) << 32) | order;
    };
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.rollId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.rollId == 0)
            errors.push_back(ctx + ": rollId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.cinematicId == 0) {
            errors.push_back(ctx +
                ": cinematicId is 0 — credit block is "
                "unbound to any cinematic");
        }
        if (e.category > 6) {
            errors.push_back(ctx + ": category " +
                std::to_string(e.category) +
                " out of range (must be 0..6)");
        }
        if (e.lines.empty()) {
            errors.push_back(ctx +
                ": lines[] is empty — credit block has "
                "nothing to display");
        }
        // Per-line length sanity. WoW cinematic credit
        // line buffer is ~80 chars wide before wrap.
        for (size_t L = 0; L < e.lines.size(); ++L) {
            if (e.lines[L].empty()) {
                warnings.push_back(ctx +
                    ": lines[" + std::to_string(L) +
                    "] is empty — would render as a "
                    "blank line in the credit roll "
                    "(intentional spacers should still "
                    "have a placeholder character)");
            }
            if (e.lines[L].size() > 80) {
                warnings.push_back(ctx + ": lines[" +
                    std::to_string(L) +
                    "] is " + std::to_string(e.lines[L].size()) +
                    " chars (>80) — may wrap or truncate "
                    "in the credit-renderer 80-char text "
                    "buffer");
            }
        }
        if (e.cinematicId != 0) {
            uint64_t key = orderKey(e.cinematicId, e.orderHint);
            if (!orderSlotsSeen.insert(key).second) {
                errors.push_back(ctx +
                    ": (cinematicId=" +
                    std::to_string(e.cinematicId) +
                    ", orderHint=" +
                    std::to_string(e.orderHint) +
                    ") slot already occupied by another "
                    "block — credit roll order would be "
                    "non-deterministic");
            }
        }
        if (!idsSeen.insert(e.rollId).second) {
            errors.push_back(ctx + ": duplicate rollId");
        }
    }
    const bool ok = errors.empty();
    if (jsonOut) return cli::printValidationJson("wmvc", base, errors, warnings);
    std::printf("validate-wmvc: %s.wmvc\n", base.c_str());
    if (ok && warnings.empty()) {
        size_t totalLines = 0;
        for (const auto& e : c.entries) totalLines += e.lines.size();
        std::printf("  OK — %zu blocks, %zu lines, all "
                    "rollIds + per-cinematic orderHints "
                    "unique\n",
                    c.entries.size(), totalLines);
        return 0;
    }
    return cli::printValidationIssues(errors, warnings);
}

} // namespace

bool handleMovieCreditsCatalog(int& i, int argc, char** argv,
                                int& outRc) {
    if (std::strcmp(argv[i], "--gen-mvc") == 0 && i + 1 < argc) {
        outRc = handleGenWotLK(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mvc-quest") == 0 && i + 1 < argc) {
        outRc = handleGenQuest(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-mvc-starter") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wmvc") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wmvc") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wmvc-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wmvc-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
