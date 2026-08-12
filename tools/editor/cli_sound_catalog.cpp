#include "cli_sound_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_sound.hpp"
#include <nlohmann/json.hpp>

#include <cmath>
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

void appendFlagsStr(std::string& s, uint32_t flags) {
    if (flags & wowee::pipeline::WoweeSound::Loop)   s += "loop ";
    if (flags & wowee::pipeline::WoweeSound::Is3D)   s += "3d ";
    if (flags & wowee::pipeline::WoweeSound::Stream) s += "stream ";
    if (s.empty()) s = "-";
    else if (s.back() == ' ') s.pop_back();
}


void printGenSummary(const wowee::pipeline::WoweeSound& c,
                     const std::string& base) {
    std::printf("Wrote %s.wsnd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCatalog";
    if (i + 1 < argc && argv[i + 1][0] != '-') name = argv[++i];
    base = cli::withoutExt(base, ".wsnd");
    auto c = wowee::pipeline::WoweeSoundLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundLoader>(c, base, "gen-sound-catalog", ".wsnd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenAmbient(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "AmbientCatalog";
    if (i + 1 < argc && argv[i + 1][0] != '-') name = argv[++i];
    base = cli::withoutExt(base, ".wsnd");
    auto c = wowee::pipeline::WoweeSoundLoader::makeAmbient(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundLoader>(c, base, "gen-sound-catalog-ambient", ".wsnd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTavern(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TavernCatalog";
    if (i + 1 < argc && argv[i + 1][0] != '-') name = argv[++i];
    base = cli::withoutExt(base, ".wsnd");
    auto c = wowee::pipeline::WoweeSoundLoader::makeTavern(name);
    if (!saveOrError<wowee::pipeline::WoweeSoundLoader>(c, base, "gen-sound-catalog-tavern", ".wsnd")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wsnd");
    if (!wowee::pipeline::WoweeSoundLoader::exists(base)) {
        return reportMissing("WSND", base, ".wsnd");
    }
    auto c = wowee::pipeline::WoweeSoundLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wsnd"] = base + ".wsnd";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            std::string fs;
            appendFlagsStr(fs, e.flags);
            arr.push_back({
                {"soundId", e.soundId},
                {"kind", e.kind},
                {"kindName", wowee::pipeline::WoweeSound::kindName(e.kind)},
                {"flags", e.flags},
                {"flagsStr", fs},
                {"volume", e.volume},
                {"minDistance", e.minDistance},
                {"maxDistance", e.maxDistance},
                {"filePath", e.filePath},
                {"label", e.label},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WSND: %s.wsnd\n", base.c_str());
    std::printf("  catalog : %s\n", c.name.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id  kind     flags         vol    min    max  label / file\n");
    for (const auto& e : c.entries) {
        std::string fs;
        appendFlagsStr(fs, e.flags);
        std::printf("  %4u  %-7s  %-12s  %4.2f  %5.1f  %5.1f  %s\n",
                    e.soundId,
                    wowee::pipeline::WoweeSound::kindName(e.kind),
                    fs.c_str(),
                    e.volume, e.minDistance, e.maxDistance,
                    e.label.empty() ? e.filePath.c_str()
                                    : (e.label + "  (" + e.filePath + ")").c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Export a .wsnd to a human-editable JSON sidecar. The
    // intent is the same as the WOL/WOW/WOMX JSON pairs:
    // give a quick-author surface for hand-editing entries
    // without writing a binary patcher. All entry fields
    // round-trip; both kind int + kindName string are emitted
    // so a hand-editor can use either.
    return cli::exportCatalogJson<wowee::pipeline::WoweeSoundLoader>(
        i, argc, argv, "wsnd", "WSND", "entries ",
        [](const auto& c) {
        nlohmann::json j;
        j["name"] = c.name;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            nlohmann::json je;
            je["soundId"] = e.soundId;
            je["kind"] = e.kind;
            je["kindName"] = wowee::pipeline::WoweeSound::kindName(e.kind);
            je["flags"] = e.flags;
            // String form of flags for hand-edit clarity. The
            // importer accepts either form.
            nlohmann::json fa = nlohmann::json::array();
            if (e.flags & wowee::pipeline::WoweeSound::Loop)   fa.push_back("loop");
            if (e.flags & wowee::pipeline::WoweeSound::Is3D)   fa.push_back("3d");
            if (e.flags & wowee::pipeline::WoweeSound::Stream) fa.push_back("stream");
            je["flagsList"] = fa;
            je["volume"] = e.volume;
            je["minDistance"] = e.minDistance;
            je["maxDistance"] = e.maxDistance;
            je["filePath"] = e.filePath;
            je["label"] = e.label;
            arr.push_back(je);
        }
        j["entries"] = arr;
            return j;
        });
}

int handleImportJson(int& i, int argc, char** argv) {
    // Round-trip pair for --export-wsnd-json. Tolerates
    // either kind int or kindName string, and either flags
    // int or flagsList string array. Missing optional fields
    // fall back to WoweeSound::Entry defaults.
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wsnd");
    outBase = cli::withoutExt(outBase, ".wsnd");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wsnd-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wsnd-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "sfx")     return wowee::pipeline::WoweeSound::Sfx;
        if (s == "music")   return wowee::pipeline::WoweeSound::Music;
        if (s == "ambient") return wowee::pipeline::WoweeSound::Ambient;
        if (s == "ui")      return wowee::pipeline::WoweeSound::Ui;
        if (s == "voice")   return wowee::pipeline::WoweeSound::Voice;
        if (s == "spell")   return wowee::pipeline::WoweeSound::Spell;
        if (s == "combat")  return wowee::pipeline::WoweeSound::Combat;
        return wowee::pipeline::WoweeSound::Sfx;
    };
    auto flagFromName = [](const std::string& s) -> uint32_t {
        if (s == "loop")   return wowee::pipeline::WoweeSound::Loop;
        if (s == "3d")     return wowee::pipeline::WoweeSound::Is3D;
        if (s == "stream") return wowee::pipeline::WoweeSound::Stream;
        return 0;
    };
    wowee::pipeline::WoweeSound c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeSound::Entry e;
            e.soundId = je.value("soundId", 0u);
            if (je.contains("kind") && je["kind"].is_number_integer()) {
                e.kind = static_cast<uint8_t>(je["kind"].get<int>());
            } else if (je.contains("kindName") && je["kindName"].is_string()) {
                e.kind = kindFromName(je["kindName"].get<std::string>());
            }
            if (je.contains("flags") && je["flags"].is_number_integer()) {
                e.flags = je["flags"].get<uint32_t>();
            } else if (je.contains("flagsList") && je["flagsList"].is_array()) {
                for (const auto& f : je["flagsList"]) {
                    if (f.is_string()) e.flags |= flagFromName(f.get<std::string>());
                }
            }
            e.volume = je.value("volume", 1.0f);
            e.minDistance = je.value("minDistance", 0.0f);
            e.maxDistance = je.value("maxDistance", 0.0f);
            e.filePath = je.value("filePath", std::string{});
            e.label = je.value("label", std::string{});
            c.entries.push_back(std::move(e));
        }
    }
    if (!wowee::pipeline::WoweeSoundLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wsnd-json: failed to save %s.wsnd\n",
            outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wsnd\n", outBase.c_str());
    std::printf("  source  : %s\n", jsonPath.c_str());
    std::printf("  entries : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    return cli::validateCatalog<wowee::pipeline::WoweeSoundLoader>(
        i, argc, argv, "wsnd", "WSND",
        [](const auto& c, std::vector<std::string>& errors,
           std::vector<std::string>& warnings) {
        // Per-entry checks plus a duplicate-soundId scan.
        //
        // The scan used to be a walk of every id seen so far, for every entry -
        // quadratic in the catalog, which is what DuplicateIdCheck was written
        // to stop. The message is unchanged; only the bookkeeping moved.
        cli::DuplicateIdCheck idsSeen;
        idsSeen.reserve(c.entries.size());
        for (size_t k = 0; k < c.entries.size(); ++k) {
            const auto& e = c.entries[k];
            std::string ctx = "entry " + std::to_string(k) +
                              " (id=" + std::to_string(e.soundId) + ")";
            if (e.kind > wowee::pipeline::WoweeSound::Combat) {
                errors.push_back(ctx + ": kind " + std::to_string(e.kind) +
                                 " not in known range 0..6");
            }
            // Reject NaN/inf early - these crash audio engines.
            if (!std::isfinite(e.volume) ||
                !std::isfinite(e.minDistance) ||
                !std::isfinite(e.maxDistance)) {
                errors.push_back(ctx + ": volume/min/max distance not finite");
            }
            if (e.volume < 0.0f || e.volume > 4.0f) {
                warnings.push_back(ctx + ": volume " +
                    std::to_string(e.volume) +
                    " outside typical 0..4 range");
            }
            // 3D sounds need min < max; non-3D sounds usually have
            // both at 0 (positional fields ignored at runtime).
            if (e.flags & wowee::pipeline::WoweeSound::Is3D) {
                if (e.minDistance < 0 || e.maxDistance <= e.minDistance) {
                    errors.push_back(ctx +
                        ": 3d sound needs minDistance >= 0 and "
                        "maxDistance > minDistance");
                }
            }
            if (e.filePath.empty()) {
                errors.push_back(ctx + ": filePath is empty");
            }
            if (!idsSeen.add(e.soundId)) {
                errors.push_back(ctx +
                    ": soundId already used by an earlier entry");
            }
        }
            return formatted("%zu entries, all sound IDs unique", c.entries.size());
        });
}

} // namespace

bool handleSoundCatalog(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-sound-catalog") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sound-catalog-ambient") == 0 && i + 1 < argc) {
        outRc = handleGenAmbient(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-sound-catalog-tavern") == 0 && i + 1 < argc) {
        outRc = handleGenTavern(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wsnd") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wsnd") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wsnd-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wsnd-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
