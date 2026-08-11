#include "cli_migrate.hpp"
#include "cli_catalog_paths.hpp"

#include "pipeline/wowee_model.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleMigrateWom(int& i, int argc, char** argv) {
    // Upgrade an older WOM (v1=static, v2=animated) to WOM3 by
    // adding a default single-batch entry that covers the whole
    // mesh. WOM3 is a strict superset; tooling that consumes
    // batches (--info-batches, --export-glb per-primitive split,
    // material-aware renderers) becomes useful on previously-
    // batchless content. The save() function picks WOM3 magic
    // automatically once batches.size() > 0.
    std::string base = argv[++i];
    std::string outBase;
    if (i + 1 < argc && argv[i + 1][0] != '-') outBase = argv[++i];
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        return reportMissing("WOM", base, ".wom");
    }
    if (outBase.empty()) outBase = base;
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (!wom.isValid()) {
        std::fprintf(stderr, "migrate-wom: %s.wom has no geometry\n", base.c_str());
        return 1;
    }
    int oldVersion = wom.version;
    int batchesAdded = 0;
    if (wom.batches.empty()) {
        // Single batch covering the entire index range with the
        // first texture (or 0 if no textures exist). Opaque
        // blend mode + no flags — safe defaults that match how
        // the renderer was treating the whole mesh implicitly.
        wowee::pipeline::WoweeModel::Batch b;
        b.indexStart = 0;
        b.indexCount = static_cast<uint32_t>(wom.indices.size());
        b.textureIndex = wom.texturePaths.empty() ? 0 : 0;
        b.blendMode = 0;
        b.flags = 0;
        wom.batches.push_back(b);
        batchesAdded = 1;
    }
    // version field is recomputed inside save() based on
    // hasBatches/hasAnimation, so we don't need to set it here.
    if (!wowee::pipeline::WoweeModelLoader::save(wom, outBase)) {
        std::fprintf(stderr, "migrate-wom: failed to write %s.wom\n",
                     outBase.c_str());
        return 1;
    }
    // Re-load to verify the new version flag landed correctly.
    auto check = wowee::pipeline::WoweeModelLoader::load(outBase);
    std::printf("Migrated %s.wom -> %s.wom\n", base.c_str(), outBase.c_str());
    std::printf("  version: %d -> %u  batches: %zu -> %zu (added %d)\n",
                oldVersion, check.version,
                size_t(0), check.batches.size(), batchesAdded);
    if (batchesAdded == 0) {
        std::printf("  (already had batches; no schema change)\n");
    }
    return 0;
}

int handleMigrateZone(int& i, int argc, char** argv) {
    // Batch-runs --migrate-wom in-place on every .wom under
    // a zone directory. Idempotent (already-migrated files
    // become no-ops). Useful when wowee_editor adds a new
    // WOM3-only feature and you want to upgrade legacy zones
    // in one shot.
    std::string zoneDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir) || !fs::is_directory(zoneDir)) {
        std::fprintf(stderr,
            "migrate-zone: %s is not a directory\n", zoneDir.c_str());
        return 1;
    }
    int scanned = 0, upgraded = 0, alreadyV3 = 0, failed = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".wom") continue;
        scanned++;
        std::string base = e.path().string();
        if (base.size() >= 4) base = base.substr(0, base.size() - 4);
        auto wom = wowee::pipeline::WoweeModelLoader::load(base);
        if (!wom.isValid()) { failed++; continue; }
        if (!wom.batches.empty()) { alreadyV3++; continue; }
        wowee::pipeline::WoweeModel::Batch b;
        b.indexStart = 0;
        b.indexCount = static_cast<uint32_t>(wom.indices.size());
        b.textureIndex = 0;
        b.blendMode = 0;
        b.flags = 0;
        wom.batches.push_back(b);
        if (wowee::pipeline::WoweeModelLoader::save(wom, base)) {
            upgraded++;
            std::printf("  upgraded: %s.wom\n", base.c_str());
        } else {
            failed++;
            std::fprintf(stderr, "  FAILED: %s.wom\n", base.c_str());
        }
    }
    std::printf("\nmigrate-zone: %s\n", zoneDir.c_str());
    std::printf("  scanned   : %d WOM file(s)\n", scanned);
    std::printf("  upgraded  : %d (added single-batch entry)\n", upgraded);
    std::printf("  already v3: %d (no change needed)\n", alreadyV3);
    if (failed > 0) {
        std::printf("  FAILED    : %d (see stderr)\n", failed);
    }
    return failed == 0 ? 0 : 1;
}

int handleMigrateProject(int& i, int argc, char** argv) {
    // Project-level wrapper around --migrate-zone. Walks every
    // zone in <projectDir> and upgrades legacy WOMs in-place.
    // Idempotent — already-migrated files become no-ops, safe to
    // run repeatedly.
    (void)argc;
    std::string projectDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "migrate-project: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    int totalScanned = 0, totalUpgraded = 0, totalAlreadyV3 = 0, totalFailed = 0;
    // Per-zone breakdown for the summary table.
    struct ZRow { std::string name; int scanned, upgraded, alreadyV3, failed; };
    std::vector<ZRow> rows;
    for (const auto& zoneDir : zones) {
        ZRow r{fs::path(zoneDir).filename().string(), 0, 0, 0, 0};
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".wom") continue;
            r.scanned++;
            std::string base = e.path().string();
            if (base.size() >= 4) base = base.substr(0, base.size() - 4);
            auto wom = wowee::pipeline::WoweeModelLoader::load(base);
            if (!wom.isValid()) { r.failed++; continue; }
            if (!wom.batches.empty()) { r.alreadyV3++; continue; }
            wowee::pipeline::WoweeModel::Batch b;
            b.indexStart = 0;
            b.indexCount = static_cast<uint32_t>(wom.indices.size());
            b.textureIndex = 0;
            b.blendMode = 0;
            b.flags = 0;
            wom.batches.push_back(b);
            if (wowee::pipeline::WoweeModelLoader::save(wom, base)) {
                r.upgraded++;
            } else {
                r.failed++;
            }
        }
        totalScanned += r.scanned;
        totalUpgraded += r.upgraded;
        totalAlreadyV3 += r.alreadyV3;
        totalFailed += r.failed;
        rows.push_back(r);
    }
    std::printf("migrate-project: %s\n", projectDir.c_str());
    std::printf("  zones      : %zu\n", zones.size());
    std::printf("  totals     : %d scanned, %d upgraded, %d already-v3, %d failed\n",
                totalScanned, totalUpgraded, totalAlreadyV3, totalFailed);
    if (!rows.empty()) {
        std::printf("\n  zone                       scan  upgrade  v3  failed\n");
        for (const auto& r : rows) {
            std::printf("  %-26s  %4d   %5d   %3d  %5d\n",
                        r.name.substr(0, 26).c_str(),
                        r.scanned, r.upgraded, r.alreadyV3, r.failed);
        }
    }
    return totalFailed == 0 ? 0 : 1;
}

int handleMigrateJsondbc(int& i, int argc, char** argv) {
    // Auto-fix common schema problems in JSON DBC sidecars so they
    // pass --validate-jsondbc cleanly. Designed for upgrading
    // sidecars produced by older asset_extract versions or from
    // third-party tools that omit fields the runtime now expects:
    //   - missing 'format' tag → add 'wowee-dbc-json-1.0'
    //   - missing 'source' field → derive from filename
    //   - missing 'fieldCount' → infer from first row
    //   - recordCount mismatch → recompute from actual records[]
    // Wrong-width rows are not silently fixed (data loss risk);
    // they're surfaced as warnings so the user can decide.
    std::string path = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    if (outPath.empty()) outPath = path;  // in-place
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr,
            "migrate-jsondbc: cannot open %s\n", path.c_str());
        return 1;
    }
    nlohmann::json doc;
    try { in >> doc; }
    catch (const std::exception& e) {
        std::fprintf(stderr,
            "migrate-jsondbc: bad JSON in %s (%s)\n",
            path.c_str(), e.what());
        return 1;
    }
    in.close();
    if (!doc.is_object()) {
        std::fprintf(stderr,
            "migrate-jsondbc: top-level value is not an object\n");
        return 1;
    }
    int fixes = 0;
    if (!doc.contains("format") || !doc["format"].is_string()) {
        doc["format"] = "wowee-dbc-json-1.0";
        fixes++;
        std::printf("  added: format = 'wowee-dbc-json-1.0'\n");
    } else if (doc["format"] != "wowee-dbc-json-1.0") {
        std::printf("  retained existing format: '%s' (not changed)\n",
                    doc["format"].get<std::string>().c_str());
    }
    if (!doc.contains("source") || !doc["source"].is_string() ||
        doc["source"].get<std::string>().empty()) {
        // Derive from input path's stem + .dbc — best-effort
        // matching the convention asset_extract uses.
        std::string stem = std::filesystem::path(path).stem().string();
        doc["source"] = stem + ".dbc";
        fixes++;
        std::printf("  added: source = '%s'\n",
                    doc["source"].get<std::string>().c_str());
    }
    // recordCount + fieldCount are non-negotiable for re-import.
    if (!doc.contains("records") || !doc["records"].is_array()) {
        std::fprintf(stderr,
            "migrate-jsondbc: 'records' missing or not an array — cannot fix\n");
        return 1;
    }
    const auto& records = doc["records"];
    uint32_t actualCount = static_cast<uint32_t>(records.size());
    uint32_t headerCount = doc.value("recordCount", 0u);
    if (headerCount != actualCount) {
        doc["recordCount"] = actualCount;
        fixes++;
        std::printf("  fixed: recordCount %u -> %u (matches actual)\n",
                    headerCount, actualCount);
    }
    // Infer fieldCount from first row if missing.
    if (!doc.contains("fieldCount") ||
        !doc["fieldCount"].is_number_integer()) {
        if (!records.empty() && records[0].is_array()) {
            uint32_t inferred = static_cast<uint32_t>(records[0].size());
            doc["fieldCount"] = inferred;
            fixes++;
            std::printf("  inferred: fieldCount = %u (from first row)\n",
                        inferred);
        }
    }
    // Surface wrong-width rows as warnings (no auto-fix).
    uint32_t fc = doc.value("fieldCount", 0u);
    int badRows = 0;
    for (size_t r = 0; r < records.size(); ++r) {
        if (records[r].is_array() && records[r].size() != fc) {
            if (++badRows <= 3) {
                std::printf("  WARN: row %zu has %zu cells, expected %u\n",
                            r, records[r].size(), fc);
            }
        }
    }
    if (badRows > 3) {
        std::printf("  WARN: ... and %d more wrong-width rows\n",
                    badRows - 3);
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "migrate-jsondbc: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << doc.dump(2) << "\n";
    out.close();
    std::printf("Migrated %s -> %s\n", path.c_str(), outPath.c_str());
    std::printf("  fixes applied: %d\n", fixes);
    if (badRows > 0) {
        std::printf("  warnings     : %d wrong-width rows (NOT auto-fixed)\n",
                    badRows);
    }
    return 0;
}

}  // namespace

bool handleMigrate(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--migrate-wom") == 0 && i + 1 < argc) {
        outRc = handleMigrateWom(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--migrate-zone") == 0 && i + 1 < argc) {
        outRc = handleMigrateZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--migrate-project") == 0 && i + 1 < argc) {
        outRc = handleMigrateProject(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--migrate-jsondbc") == 0 && i + 1 < argc) {
        outRc = handleMigrateJsondbc(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
