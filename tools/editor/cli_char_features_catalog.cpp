#include "cli_char_features_catalog.hpp"
#include "cli_catalog_paths.hpp"
#include "cli_validate_report.hpp"
#include "cli_arg_parse.hpp"
#include "cli_box_emitter.hpp"

#include "pipeline/wowee_char_features.hpp"
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


void printGenSummary(const wowee::pipeline::WoweeCharFeature& c,
                     const std::string& base) {
    std::printf("Wrote %s.wchf\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  features : %zu\n", c.entries.size());
}

int handleGenStarter(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "StarterCharFeatures";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchf");
    auto c = wowee::pipeline::WoweeCharFeatureLoader::makeStarter(name);
    if (!saveOrError<wowee::pipeline::WoweeCharFeatureLoader>(c, base, "gen-chf", ".wchf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenBloodElf(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "BloodElfFemaleHair";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchf");
    auto c = wowee::pipeline::WoweeCharFeatureLoader::makeBloodElfFemale(name);
    if (!saveOrError<wowee::pipeline::WoweeCharFeatureLoader>(c, base, "gen-chf-bloodelf", ".wchf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleGenTauren(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    std::string name = "TaurenMaleFeatures";
    if (parseOptArg(i, argc, argv)) name = argv[++i];
    base = cli::withoutExt(base, ".wchf");
    auto c = wowee::pipeline::WoweeCharFeatureLoader::makeTauren(name);
    if (!saveOrError<wowee::pipeline::WoweeCharFeatureLoader>(c, base, "gen-chf-tauren", ".wchf")) return 1;
    printGenSummary(c, base);
    return 0;
}

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchf");
    if (!wowee::pipeline::WoweeCharFeatureLoader::exists(base)) {
        std::fprintf(stderr, "WCHF not found: %s.wchf\n", base.c_str());
        return 1;
    }
    auto c = wowee::pipeline::WoweeCharFeatureLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wchf"] = base + ".wchf";
        j["name"] = c.name;
        j["count"] = c.entries.size();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : c.entries) {
            arr.push_back({
                {"featureId", e.featureId},
                {"raceId", e.raceId},
                {"name", e.name},
                {"description", e.description},
                {"texturePath", e.texturePath},
                {"featureKind", e.featureKind},
                {"featureKindName", wowee::pipeline::WoweeCharFeature::featureKindName(e.featureKind)},
                {"sexId", e.sexId},
                {"sexIdName", wowee::pipeline::WoweeCharFeature::sexIdName(e.sexId)},
                {"variationIndex", e.variationIndex},
                {"requiresExpansion", e.requiresExpansion},
                {"requiresExpansionName", wowee::pipeline::WoweeCharFeature::expansionGateName(e.requiresExpansion)},
                {"geosetGroupBits", e.geosetGroupBits},
                {"hairColorOverlayId", e.hairColorOverlayId},
            });
        }
        j["entries"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WCHF: %s.wchf\n", base.c_str());
    std::printf("  catalog  : %s\n", c.name.c_str());
    std::printf("  features : %zu\n", c.entries.size());
    if (c.entries.empty()) return 0;
    std::printf("    id    race  sex     kind          var  geosets     exp        name\n");
    for (const auto& e : c.entries) {
        std::printf("  %4u    %3u  %-6s  %-12s  %3u  0x%08x  %-7s    %s\n",
                    e.featureId, e.raceId,
                    wowee::pipeline::WoweeCharFeature::sexIdName(e.sexId),
                    wowee::pipeline::WoweeCharFeature::featureKindName(e.featureKind),
                    e.variationIndex, e.geosetGroupBits,
                    wowee::pipeline::WoweeCharFeature::expansionGateName(e.requiresExpansion),
                    e.name.c_str());
    }
    return 0;
}

int handleExportJson(int& i, int argc, char** argv) {
    // Mirrors the JSON pairs added for every other novel
    // open format. Each feature emits all 9 scalar fields
    // plus dual int + name forms for featureKind / sexId /
    // requiresExpansion so hand-edits can use either.
    std::string base = argv[++i];
    std::string outPath;
    if (parseOptArg(i, argc, argv)) outPath = argv[++i];
    base = cli::withoutExt(base, ".wchf");
    if (outPath.empty()) outPath = base + ".wchf.json";
    if (!wowee::pipeline::WoweeCharFeatureLoader::exists(base)) {
        return reportMissing("export-wchf-json", "WCHF", base, ".wchf");
    }
    auto c = wowee::pipeline::WoweeCharFeatureLoader::load(base);
    nlohmann::json j;
    j["name"] = c.name;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : c.entries) {
        arr.push_back({
            {"featureId", e.featureId},
            {"raceId", e.raceId},
            {"name", e.name},
            {"description", e.description},
            {"texturePath", e.texturePath},
            {"featureKind", e.featureKind},
            {"featureKindName", wowee::pipeline::WoweeCharFeature::featureKindName(e.featureKind)},
            {"sexId", e.sexId},
            {"sexIdName", wowee::pipeline::WoweeCharFeature::sexIdName(e.sexId)},
            {"variationIndex", e.variationIndex},
            {"requiresExpansion", e.requiresExpansion},
            {"requiresExpansionName", wowee::pipeline::WoweeCharFeature::expansionGateName(e.requiresExpansion)},
            {"geosetGroupBits", e.geosetGroupBits},
            {"hairColorOverlayId", e.hairColorOverlayId},
        });
    }
    j["entries"] = arr;
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-wchf-json: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << j.dump(2) << "\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  source   : %s.wchf\n", base.c_str());
    std::printf("  features : %zu\n", c.entries.size());
    return 0;
}

int handleImportJson(int& i, int argc, char** argv) {
    std::string jsonPath = argv[++i];
    std::string outBase;
    if (parseOptArg(i, argc, argv)) outBase = argv[++i];
    if (outBase.empty()) outBase = cli::baseFromJsonPath(jsonPath, ".wchf");
    outBase = cli::withoutExt(outBase, ".wchf");
    std::ifstream in(jsonPath);
    if (!in) {
        std::fprintf(stderr,
            "import-wchf-json: cannot read %s\n", jsonPath.c_str());
        return 1;
    }
    nlohmann::json j;
    try { in >> j; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "import-wchf-json: bad JSON in %s: %s\n",
            jsonPath.c_str(), e.what());
        return 1;
    }
    auto kindFromName = [](const std::string& s) -> uint8_t {
        if (s == "skin-color")   return wowee::pipeline::WoweeCharFeature::SkinColor;
        if (s == "face")         return wowee::pipeline::WoweeCharFeature::FaceVariation;
        if (s == "hair-style")   return wowee::pipeline::WoweeCharFeature::HairStyle;
        if (s == "hair-color")   return wowee::pipeline::WoweeCharFeature::HairColor;
        if (s == "facial-hair")  return wowee::pipeline::WoweeCharFeature::FacialHair;
        if (s == "facial-color") return wowee::pipeline::WoweeCharFeature::FacialColor;
        if (s == "ear-style")    return wowee::pipeline::WoweeCharFeature::EarStyle;
        if (s == "horns")        return wowee::pipeline::WoweeCharFeature::Horns;
        if (s == "markings")     return wowee::pipeline::WoweeCharFeature::Markings;
        return wowee::pipeline::WoweeCharFeature::SkinColor;
    };
    auto sexFromName = [](const std::string& s) -> uint8_t {
        if (s == "male")   return wowee::pipeline::WoweeCharFeature::Male;
        if (s == "female") return wowee::pipeline::WoweeCharFeature::Female;
        return wowee::pipeline::WoweeCharFeature::Male;
    };
    auto expansionFromName = [](const std::string& s) -> uint8_t {
        if (s == "classic") return wowee::pipeline::WoweeCharFeature::Classic;
        if (s == "tbc")     return wowee::pipeline::WoweeCharFeature::TBC;
        if (s == "wotlk")   return wowee::pipeline::WoweeCharFeature::WotLK;
        if (s == "turtle")  return wowee::pipeline::WoweeCharFeature::TurtleWoW;
        return wowee::pipeline::WoweeCharFeature::Classic;
    };
    wowee::pipeline::WoweeCharFeature c;
    c.name = j.value("name", std::string{});
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& je : j["entries"]) {
            wowee::pipeline::WoweeCharFeature::Entry e;
            e.featureId = je.value("featureId", 0u);
            e.raceId = je.value("raceId", 0u);
            e.name = je.value("name", std::string{});
            e.description = je.value("description", std::string{});
            e.texturePath = je.value("texturePath", std::string{});
            if (je.contains("featureKind") &&
                je["featureKind"].is_number_integer()) {
                e.featureKind = static_cast<uint8_t>(
                    je["featureKind"].get<int>());
            } else if (je.contains("featureKindName") &&
                       je["featureKindName"].is_string()) {
                e.featureKind = kindFromName(
                    je["featureKindName"].get<std::string>());
            }
            if (je.contains("sexId") &&
                je["sexId"].is_number_integer()) {
                e.sexId = static_cast<uint8_t>(je["sexId"].get<int>());
            } else if (je.contains("sexIdName") &&
                       je["sexIdName"].is_string()) {
                e.sexId = sexFromName(
                    je["sexIdName"].get<std::string>());
            }
            e.variationIndex = static_cast<uint8_t>(
                je.value("variationIndex", 0));
            if (je.contains("requiresExpansion") &&
                je["requiresExpansion"].is_number_integer()) {
                e.requiresExpansion = static_cast<uint8_t>(
                    je["requiresExpansion"].get<int>());
            } else if (je.contains("requiresExpansionName") &&
                       je["requiresExpansionName"].is_string()) {
                e.requiresExpansion = expansionFromName(
                    je["requiresExpansionName"].get<std::string>());
            }
            e.geosetGroupBits = je.value("geosetGroupBits", 0u);
            e.hairColorOverlayId = je.value("hairColorOverlayId", 0u);
            c.entries.push_back(e);
        }
    }
    if (!wowee::pipeline::WoweeCharFeatureLoader::save(c, outBase)) {
        std::fprintf(stderr,
            "import-wchf-json: failed to save %s.wchf\n", outBase.c_str());
        return 1;
    }
    std::printf("Wrote %s.wchf\n", outBase.c_str());
    std::printf("  source   : %s\n", jsonPath.c_str());
    std::printf("  features : %zu\n", c.entries.size());
    return 0;
}

int handleValidate(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    base = cli::withoutExt(base, ".wchf");
    if (!wowee::pipeline::WoweeCharFeatureLoader::exists(base)) {
        return reportMissing("validate-wchf", "WCHF", base, ".wchf");
    }
    auto c = wowee::pipeline::WoweeCharFeatureLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    if (c.entries.empty()) {
        warnings.push_back("catalog has zero entries");
    }
    cli::DuplicateIdCheck idsSeen;
    // (race, sex, kind, variation) tuples must be unique —
    // duplicates would shadow each other in the carousel.
    std::set<std::string> tupleSeen;
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const auto& e = c.entries[k];
        std::string ctx = "entry " + std::to_string(k) +
                          " (id=" + std::to_string(e.featureId);
        if (!e.name.empty()) ctx += " " + e.name;
        ctx += ")";
        if (e.featureId == 0)
            errors.push_back(ctx + ": featureId is 0");
        if (e.name.empty())
            errors.push_back(ctx + ": name is empty");
        if (e.raceId == 0)
            errors.push_back(ctx +
                ": raceId is 0 (feature not bound to a WCHC race)");
        if (e.featureKind > wowee::pipeline::WoweeCharFeature::Markings) {
            errors.push_back(ctx + ": featureKind " +
                std::to_string(e.featureKind) + " not in 0..8");
        }
        if (e.sexId > wowee::pipeline::WoweeCharFeature::Female) {
            errors.push_back(ctx + ": sexId " +
                std::to_string(e.sexId) + " not in 0..1");
        }
        if (e.requiresExpansion > wowee::pipeline::WoweeCharFeature::TurtleWoW) {
            errors.push_back(ctx + ": requiresExpansion " +
                std::to_string(e.requiresExpansion) + " not in 0..3");
        }
        if (e.texturePath.empty()) {
            errors.push_back(ctx +
                ": texturePath is empty (feature has no texture)");
        }
        // Check tuple uniqueness.
        std::string tuple =
            std::to_string(e.raceId) + "/" +
            std::to_string(e.sexId) + "/" +
            std::to_string(e.featureKind) + "/" +
            std::to_string(e.variationIndex);
        if (tupleSeen.count(tuple)) {
            errors.push_back(ctx +
                ": duplicate (race=" + std::to_string(e.raceId) +
                ", sex=" + std::to_string(e.sexId) +
                ", kind=" + std::to_string(e.featureKind) +
                ", variation=" + std::to_string(e.variationIndex) +
                ") — would shadow earlier entry in carousel");
        }
        tupleSeen.insert(tuple);
        if (!idsSeen.add(e.featureId)) errors.push_back(ctx + ": duplicate featureId");
    }
    return cli::reportValidation("wchf", base, jsonOut, errors, warnings,
                                 formatted("%zu features, all featureIds unique, "
                    "all (race,sex,kind,variation) tuples unique", c.entries.size()));
}

} // namespace

bool handleCharFeaturesCatalog(int& i, int argc, char** argv,
                               int& outRc) {
    if (std::strcmp(argv[i], "--gen-chf") == 0 && i + 1 < argc) {
        outRc = handleGenStarter(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-chf-bloodelf") == 0 && i + 1 < argc) {
        outRc = handleGenBloodElf(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-chf-tauren") == 0 && i + 1 < argc) {
        outRc = handleGenTauren(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-wchf") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wchf") == 0 && i + 1 < argc) {
        outRc = handleValidate(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-wchf-json") == 0 && i + 1 < argc) {
        outRc = handleExportJson(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--import-wchf-json") == 0 && i + 1 < argc) {
        outRc = handleImportJson(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
