#include "editor_app.hpp"
#include "cli_dispatch.hpp"
#include "cli_convert_single.hpp"
#include "cli_arg_required.hpp"
#include "cli_multi_arg_required.hpp"
#include "content_pack.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include "wowee_terrain.hpp"
#include "zone_manifest.hpp"
#include "terrain_editor.hpp"
#include "terrain_biomes.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/wowee_collision.hpp"
#include "pipeline/wowee_terrain_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/adt_loader.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/custom_zone_discovery.hpp"
#include "core/logger.hpp"
#include <string>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "stb_image_write.h"
#include "stb_image.h"  // implementation in stb_image_impl.cpp

// ─── Open-format consistency checks ─────────────────────────────
// Both validators are called from the per-file CLI commands AND
// from --validate-all which walks a zone dir. Returning a vector
// of error strings (empty == passed) keeps callers simple.
// Minimal SHA-256 implementation (FIPS 180-4) used by --export-zone-checksum
// to produce hashes that interoperate with `sha256sum -c`. Not exposed beyond
// this file - about 90 LoC, no external deps. See RFC 6234 for the algorithm.



int main(int argc, char* argv[]) {
    std::string dataPath;
    std::string adtMap;
    int adtX = -1, adtY = -1;

    // Detect non-GUI options that are missing their argument and bail out
    // with a helpful message instead of silently dropping into the GUI.
    // Single-arg flags live in cli_arg_required.{hpp,cpp}; multi-arg flags
    // live in cli_multi_arg_required.{hpp,cpp} along with their synopses.
    for (int i = 1; i < argc; i++) {
        for (std::size_t k = 0; k < wowee::editor::cli::kArgRequiredSize; ++k) {
            const char* opt = wowee::editor::cli::kArgRequired[k];
            if (std::strcmp(argv[i], opt) == 0 && i + 1 >= argc) {
                std::fprintf(stderr, "%s requires an argument\n", opt);
                return 1;
            }
        }
        for (std::size_t k = 0; k < wowee::editor::cli::kMultiArgRequiredSize; ++k) {
            const auto& m = wowee::editor::cli::kMultiArgRequired[k];
            if (std::strcmp(argv[i], m.flag) == 0 && i + m.needed >= argc) {
                std::fprintf(stderr, "%s\n", m.synopsis);
                return 1;
            }
        }
    }

    for (int i = 1; i < argc; i++) {
        // CLI handlers live in cli_dispatch.cpp's table-driven
        // dispatcher. handleConvertSingle is the one outlier
        // because it needs dataPath threaded in; everything else
        // goes through tryDispatchAll. Either return path captures
        // the handler's exit code via outRc.
        int outRc = 0;
        if (wowee::editor::cli::handleConvertSingle(i, argc, argv,
                                                     dataPath, outRc)) {
            return outRc;
        }
        if (wowee::editor::cli::tryDispatchAll(i, argc, argv, outRc)) {
            return outRc;
        }
        // GUI-state args don't return - they're absorbed and
        // applied to the EditorApp after argv parsing finishes.
        if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataPath = argv[++i];
        } else if (std::strcmp(argv[i], "--adt") == 0 && i + 3 < argc) {
            adtMap = argv[++i];
            adtX = std::atoi(argv[++i]);
            adtY = std::atoi(argv[++i]);
        }
    }


    if (dataPath.empty()) {
        dataPath = "Data";
        LOG_INFO("No --data path specified, using default: ", dataPath);
    }

    wowee::editor::EditorApp app;
    if (!app.initialize(dataPath)) {
        LOG_ERROR("Failed to initialize editor");
        return 1;
    }

    if (!adtMap.empty()) {
        app.loadADT(adtMap, adtX, adtY);
    }

    app.run();
    app.shutdown();

    return 0;
}
