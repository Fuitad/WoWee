#include "cli_export.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace wowee_sha256 {
struct State {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint64_t totalBits = 0;
    uint8_t buf[64] = {};
    size_t bufLen = 0;
};
static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static void compress(State& s, const uint8_t* block) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
               (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3];
    uint32_t e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d;
    s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
}
static void update(State& s, const uint8_t* data, size_t len) {
    s.totalBits += len * 8;
    while (len > 0) {
        size_t take = std::min(len, sizeof(s.buf) - s.bufLen);
        std::memcpy(s.buf + s.bufLen, data, take);
        s.bufLen += take; data += take; len -= take;
        if (s.bufLen == 64) { compress(s, s.buf); s.bufLen = 0; }
    }
}
static std::string hexFinal(State& s) {
    s.buf[s.bufLen++] = 0x80;
    if (s.bufLen > 56) {
        std::memset(s.buf + s.bufLen, 0, 64 - s.bufLen);
        compress(s, s.buf); s.bufLen = 0;
    }
    std::memset(s.buf + s.bufLen, 0, 56 - s.bufLen);
    for (int i = 7; i >= 0; --i) s.buf[56 + (7 - i)] = (s.totalBits >> (i * 8)) & 0xFF;
    compress(s, s.buf);
    char out[65] = {};
    for (int i = 0; i < 8; ++i) {
        std::snprintf(out + i * 8, 9, "%08x", s.h[i]);
    }
    return std::string(out);
}
static std::string fileHex(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    State s;
    char chunk[16384];
    while (in.read(chunk, sizeof(chunk)) || in.gcount() > 0) {
        update(s, reinterpret_cast<const uint8_t*>(chunk),
               static_cast<size_t>(in.gcount()));
    }
    return hexFinal(s);
}
static std::string hex(const uint8_t* data, size_t len) {
    State s;
    update(s, data, len);
    return hexFinal(s);
}
}  // namespace wowee_sha256

int handleExportZoneSummaryMd(int& i, int argc, char** argv) {
    // Render a Markdown documentation page for a zone. Useful for
    // designers tracking changes between versions, generating
    // GitHub Pages docs, or reviewing zones in PRs without
    // round-tripping through the GUI.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "export-zone-summary-md: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr,
            "export-zone-summary-md: failed to parse %s\n", manifestPath.c_str());
        return 1;
    }
    // Default output: ZONE.md sitting next to zone.json.
    if (outPath.empty()) outPath = zoneDir + "/ZONE.md";
    // Load content sub-files; missing ones contribute 0 entries.
    wowee::editor::NpcSpawner sp;
    sp.loadFromFile(zoneDir + "/creatures.json");
    wowee::editor::ObjectPlacer op;
    op.loadFromFile(zoneDir + "/objects.json");
    wowee::editor::QuestEditor qe;
    qe.loadFromFile(zoneDir + "/quests.json");
    std::ofstream md(outPath);
    if (!md) {
        std::fprintf(stderr,
            "export-zone-summary-md: cannot write %s\n", outPath.c_str());
        return 1;
    }
    md << "# " << (zm.displayName.empty() ? zm.mapName : zm.displayName) << "\n\n";
    md << "*Auto-generated by `wowee_editor --export-zone-summary-md`. "
          "Do not edit by hand.*\n\n";
    md << "## Manifest\n\n";
    md << "| Field | Value |\n";
    md << "|---|---|\n";
    md << "| Map name | `" << zm.mapName << "` |\n";
    md << "| Display name | " << zm.displayName << " |\n";
    md << "| Map ID | " << zm.mapId << " |\n";
    if (!zm.biome.empty())     md << "| Biome | " << zm.biome << " |\n";
    md << "| Base height | " << zm.baseHeight << " |\n";
    md << "| Tile count | " << zm.tiles.size() << " |\n";
    md << "| Allow flying | " << (zm.allowFlying ? "yes" : "no") << " |\n";
    md << "| PvP enabled | " << (zm.pvpEnabled ? "yes" : "no") << " |\n";
    md << "| Indoor | " << (zm.isIndoor ? "yes" : "no") << " |\n";
    md << "| Sanctuary | " << (zm.isSanctuary ? "yes" : "no") << " |\n";
    if (!zm.musicTrack.empty())   md << "| Music | `" << zm.musicTrack << "` |\n";
    if (!zm.ambienceDay.empty())  md << "| Ambient (day) | `" << zm.ambienceDay << "` |\n";
    if (!zm.ambienceNight.empty())md << "| Ambient (night) | `" << zm.ambienceNight << "` |\n";
    if (!zm.description.empty()) {
        md << "\n### Description\n\n" << zm.description << "\n";
    }
    md << "\n## Tiles\n\n";
    md << "| tx | ty |\n|---|---|\n";
    for (const auto& [tx, ty] : zm.tiles) {
        md << "| " << tx << " | " << ty << " |\n";
    }
    md << "\n## Creatures (" << sp.spawnCount() << ")\n\n";
    if (sp.spawnCount() == 0) {
        md << "*No creature spawns.*\n";
    } else {
        md << "| # | Name | Lvl | DisplayId | Pos (x, y, z) | Flags |\n";
        md << "|---|---|---|---|---|---|\n";
        for (size_t k = 0; k < sp.spawnCount(); ++k) {
            const auto& s = sp.getSpawns()[k];
            md << "| " << k << " | " << s.name << " | " << s.level << " | "
               << s.displayId << " | ("
               << s.position.x << ", " << s.position.y << ", " << s.position.z
               << ") |";
            if (s.hostile)    md << " hostile";
            if (s.questgiver) md << " quest";
            if (s.vendor)     md << " vendor";
            if (s.trainer)    md << " trainer";
            md << " |\n";
        }
    }
    md << "\n## Objects (" << op.getObjects().size() << ")\n\n";
    if (op.getObjects().empty()) {
        md << "*No object placements.*\n";
    } else {
        md << "| # | Type | Path | Pos | Scale |\n";
        md << "|---|---|---|---|---|\n";
        for (size_t k = 0; k < op.getObjects().size(); ++k) {
            const auto& o = op.getObjects()[k];
            md << "| " << k << " | "
               << (o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo")
               << " | `" << o.path << "` | ("
               << o.position.x << ", " << o.position.y << ", " << o.position.z
               << ") | " << o.scale << " |\n";
        }
    }
    md << "\n## Quests (" << qe.questCount() << ")\n\n";
    if (qe.questCount() == 0) {
        md << "*No quests.*\n";
    } else {
        using OT = wowee::editor::QuestObjectiveType;
        // The word is the format's, from beside the enum.
        auto typeName = wowee::editor::questObjectiveTypeName;
        for (size_t k = 0; k < qe.questCount(); ++k) {
            const auto& q = qe.getQuests()[k];
            md << "### " << k << ". " << q.title << "\n\n";
            md << "- Required level: " << q.requiredLevel << "\n";
            md << "- Quest giver NPC ID: " << q.questGiverNpcId << "\n";
            md << "- Turn-in NPC ID: " << q.turnInNpcId << "\n";
            md << "- XP: " << q.reward.xp << "\n";
            if (q.reward.gold || q.reward.silver || q.reward.copper) {
                md << "- Coin: " << q.reward.gold << "g "
                   << q.reward.silver << "s " << q.reward.copper << "c\n";
            }
            if (!q.objectives.empty()) {
                md << "- Objectives:\n";
                for (const auto& obj : q.objectives) {
                    md << "  - **" << typeName(obj.type) << "** "
                       << obj.targetName << " ×" << obj.targetCount;
                    if (!obj.description.empty()) {
                        md << " — *" << obj.description << "*";
                    }
                    md << "\n";
                }
            }
            if (!q.reward.itemRewards.empty()) {
                md << "- Item rewards:\n";
                for (const auto& it : q.reward.itemRewards) {
                    md << "  - `" << it << "`\n";
                }
            }
            md << "\n";
        }
    }
    md.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  zone=%s, %zu tiles, %zu creatures, %zu objects, %zu quests\n",
                zm.mapName.c_str(), zm.tiles.size(), sp.spawnCount(),
                op.getObjects().size(), qe.questCount());
    return 0;
}

int handleExportZoneCsv(int& i, int argc, char** argv) {
    // Emit creatures.csv / objects.csv / quests.csv for designers
    // who prefer spreadsheets over JSON. Round-trip back into the
    // editor isn't supported yet, but for read-only analysis (sort
    // by XP, group by faction, pivot tables in LibreOffice) CSV is
    // the lingua franca of design data.
    std::string zoneDir = argv[++i];
    std::string outDir;
    if (i + 1 < argc && argv[i + 1][0] != '-') outDir = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "export-zone-csv: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    if (outDir.empty()) outDir = zoneDir;
    // CSV-escape: wrap any field containing comma/quote/newline in
    // double quotes; double up internal quotes per RFC 4180.
    auto csvEsc = [](const std::string& s) {
        bool needs = s.find(',') != std::string::npos ||
                     s.find('"') != std::string::npos ||
                     s.find('\n') != std::string::npos;
        if (!needs) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    };
    int filesWritten = 0;
    // Creatures
    wowee::editor::NpcSpawner sp;
    if (sp.loadFromFile(zoneDir + "/creatures.json")) {
        std::string out = outDir + "/creatures.csv";
        std::ofstream f(out);
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", out.c_str());
            return 1;
        }
        f << "index,id,name,displayId,level,health,mana,faction,"
             "x,y,z,orientation,scale,hostile,questgiver,vendor,trainer\n";
        for (size_t k = 0; k < sp.spawnCount(); ++k) {
            const auto& s = sp.getSpawns()[k];
            f << k << "," << s.id << "," << csvEsc(s.name) << ","
              << s.displayId << "," << s.level << ","
              << s.health << "," << s.mana << "," << s.faction << ","
              << s.position.x << "," << s.position.y << ","
              << s.position.z << "," << s.orientation << ","
              << s.scale << ","
              << (s.hostile ? 1 : 0) << ","
              << (s.questgiver ? 1 : 0) << ","
              << (s.vendor ? 1 : 0) << ","
              << (s.trainer ? 1 : 0) << "\n";
        }
        std::printf("  wrote %s (%zu rows)\n", out.c_str(), sp.spawnCount());
        filesWritten++;
    }
    // Objects
    wowee::editor::ObjectPlacer op;
    if (op.loadFromFile(zoneDir + "/objects.json")) {
        std::string out = outDir + "/objects.csv";
        std::ofstream f(out);
        if (!f) return 1;
        f << "index,type,path,x,y,z,rotX,rotY,rotZ,scale\n";
        for (size_t k = 0; k < op.getObjects().size(); ++k) {
            const auto& o = op.getObjects()[k];
            f << k << ","
              << (o.type == wowee::editor::PlaceableType::M2 ? "m2" : "wmo") << ","
              << csvEsc(o.path) << ","
              << o.position.x << "," << o.position.y << "," << o.position.z << ","
              << o.rotation.x << "," << o.rotation.y << "," << o.rotation.z << ","
              << o.scale << "\n";
        }
        std::printf("  wrote %s (%zu rows)\n", out.c_str(),
                    op.getObjects().size());
        filesWritten++;
    }
    // Quests — flatten to one row per quest. Objectives + items
    // are joined into a single semicolon-separated cell so the
    // CSV stays one-row-per-quest (designer-friendly for sorting).
    wowee::editor::QuestEditor qe;
    if (qe.loadFromFile(zoneDir + "/quests.json")) {
        std::string out = outDir + "/quests.csv";
        std::ofstream f(out);
        if (!f) return 1;
        f << "index,id,title,requiredLevel,giverNpcId,turnInNpcId,"
             "xp,gold,silver,copper,nextQuestId,objectiveCount,"
             "objectives,itemRewards\n";
        using OT = wowee::editor::QuestObjectiveType;
        // The word is the format's, from beside the enum.
        auto typeName = wowee::editor::questObjectiveTypeName;
        for (size_t k = 0; k < qe.questCount(); ++k) {
            const auto& q = qe.getQuests()[k];
            std::string objs;
            for (size_t o = 0; o < q.objectives.size(); ++o) {
                if (o) objs += "; ";
                objs += std::string(typeName(q.objectives[o].type)) + ":" +
                        q.objectives[o].targetName + "x" +
                        std::to_string(q.objectives[o].targetCount);
            }
            std::string items;
            for (size_t r = 0; r < q.reward.itemRewards.size(); ++r) {
                if (r) items += "; ";
                items += q.reward.itemRewards[r];
            }
            f << k << "," << q.id << "," << csvEsc(q.title) << ","
              << q.requiredLevel << ","
              << q.questGiverNpcId << "," << q.turnInNpcId << ","
              << q.reward.xp << "," << q.reward.gold << ","
              << q.reward.silver << "," << q.reward.copper << ","
              << q.nextQuestId << ","
              << q.objectives.size() << ","
              << csvEsc(objs) << "," << csvEsc(items) << "\n";
        }
        std::printf("  wrote %s (%zu rows)\n", out.c_str(), qe.questCount());
        filesWritten++;
    }
    // Items — read items.json inline since the items pipeline
    // doesn't have a dedicated editor class yet.
    std::string itemsPath = zoneDir + "/items.json";
    if (fs::exists(itemsPath)) {
        nlohmann::json doc;
        try {
            std::ifstream in(itemsPath);
            in >> doc;
        } catch (...) {}
        if (doc.contains("items") && doc["items"].is_array()) {
            std::string out = outDir + "/items.csv";
            std::ofstream f(out);
            if (f) {
                f << "index,id,name,quality,itemLevel,displayId,stackable\n";
                const auto& arr = doc["items"];
                for (size_t k = 0; k < arr.size(); ++k) {
                    const auto& it = arr[k];
                    f << k << ","
                      << it.value("id", 0u) << ","
                      << csvEsc(it.value("name", std::string())) << ","
                      << it.value("quality", 1u) << ","
                      << it.value("itemLevel", 1u) << ","
                      << it.value("displayId", 0u) << ","
                      << it.value("stackable", 1u) << "\n";
                }
                std::printf("  wrote %s (%zu rows)\n", out.c_str(), arr.size());
                filesWritten++;
            }
        }
    }
    if (filesWritten == 0) {
        std::fprintf(stderr,
            "export-zone-csv: zone has no creatures/objects/quests/items to emit\n");
        return 1;
    }
    std::printf("Exported %d CSV file(s) to %s\n", filesWritten, outDir.c_str());
    return 0;
}

int handleExportZoneChecksum(int& i, int argc, char** argv) {
    // SHA-256 manifest of every source file in a zone, in the
    // standard sha256sum format ('<hex>  <relpath>'). Lets users
    // verify zone integrity after a download or transfer with the
    // standard system tool:
    //   wowee_editor --export-zone-checksum custom_zones/MyZone
    //   sha256sum -c custom_zones/MyZone/SHA256SUMS
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "export-zone-checksum: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/SHA256SUMS";
    // Source files only — derived outputs (.glb/.obj/.stl/.html/
    // ZONE.md/DEPS.md/quests.dot/SHA256SUMS itself) are excluded
    // since they're regeneratable and would invalidate the
    // checksum on every rebuild.
    auto isDerived = [](const fs::path& p) {
        std::string ext = p.extension().string();
        std::string name = p.filename().string();
        if (ext == ".glb" || ext == ".obj" || ext == ".stl" ||
            ext == ".html" || ext == ".dot" || ext == ".csv") return true;
        if (name == "ZONE.md" || name == "DEPS.md" ||
            name == "SHA256SUMS" || name == "Makefile") return true;
        if (ext == ".png") return true;  // BLP→PNG renders at root
        return false;
    };
    std::vector<std::pair<std::string, std::string>> entries;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        if (isDerived(e.path())) continue;
        std::string hex = wowee_sha256::fileHex(e.path().string());
        if (hex.empty()) continue;
        std::string rel = fs::relative(e.path(), zoneDir, ec).string();
        if (ec) rel = e.path().string();
        entries.push_back({hex, rel});
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-zone-checksum: cannot write %s\n", outPath.c_str());
        return 1;
    }
    for (const auto& [hash, path] : entries) {
        // sha256sum format: 64-char hex, two spaces, path.
        out << hash << "  " << path << "\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu file(s) hashed (source only, derived excluded)\n",
                entries.size());
    std::printf("  verify with: sha256sum -c %s\n", outPath.c_str());
    return 0;
}

int handleExportProjectChecksum(int& i, int argc, char** argv) {
    // Project-wide manifest in the same sha256sum format, with
    // paths kept relative to <projectDir> (so entries look like
    // "<hex>  <zoneName>/<file>"). Also emits a single SHA-256
    // fingerprint over the manifest itself — a one-line
    // identity for the whole project, handy for CI release
    // gates and reproducibility checks.
    //
    //   wowee_editor --export-project-checksum custom_zones
    //   sha256sum -c custom_zones/PROJECT_SHA256SUMS
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "export-project-checksum: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/PROJECT_SHA256SUMS";
    // Same derived-output filter as --export-zone-checksum.
    auto isDerived = [](const fs::path& p) {
        std::string ext = p.extension().string();
        std::string name = p.filename().string();
        if (ext == ".glb" || ext == ".obj" || ext == ".stl" ||
            ext == ".html" || ext == ".dot" || ext == ".csv") return true;
        if (name == "ZONE.md" || name == "DEPS.md" ||
            name == "SHA256SUMS" || name == "PROJECT_SHA256SUMS" ||
            name == "Makefile") return true;
        if (ext == ".png") return true;
        return false;
    };
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& zoneDir : zones) {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
            if (!e.is_regular_file()) continue;
            if (isDerived(e.path())) continue;
            std::string hex = wowee_sha256::fileHex(e.path().string());
            if (hex.empty()) continue;
            std::string rel = fs::relative(e.path(), projectDir, ec).string();
            if (ec) rel = e.path().string();
            entries.push_back({hex, rel});
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-project-checksum: cannot write %s\n", outPath.c_str());
        return 1;
    }
    // Hash the manifest body inline so the project fingerprint
    // is byte-identical to what `sha256sum PROJECT_SHA256SUMS`
    // would yield on the written file.
    std::string body;
    body.reserve(entries.size() * 80);
    for (const auto& [hash, path] : entries) {
        body += hash;
        body += "  ";
        body += path;
        body += "\n";
    }
    out << body;
    out.close();
    std::string fingerprint = wowee_sha256::hex(
        reinterpret_cast<const uint8_t*>(body.data()), body.size());
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  zones        : %zu\n", zones.size());
    std::printf("  files hashed : %zu\n", entries.size());
    std::printf("  fingerprint  : %s\n", fingerprint.c_str());
    std::printf("  verify with  : sha256sum -c %s\n", outPath.c_str());
    return 0;
}

int handleValidateProjectChecksum(int& i, int argc, char** argv) {
    // In-tool verification of the manifest produced by
    // --export-project-checksum. Equivalent to 'sha256sum -c
    // PROJECT_SHA256SUMS' but cross-platform — Windows and
    // CI runners without coreutils don't need an external tool.
    // Exit 1 if any file is missing or its hash drifted.
    std::string projectDir = argv[++i];
    std::string inPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') inPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "validate-project-checksum: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (inPath.empty()) inPath = projectDir + "/PROJECT_SHA256SUMS";
    std::ifstream in(inPath);
    if (!in) {
        std::fprintf(stderr,
            "validate-project-checksum: cannot read %s\n", inPath.c_str());
        return 1;
    }
    int ok = 0, missing = 0, mismatched = 0;
    std::vector<std::string> failures;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // sha256sum format: 64-char hex, two spaces, path.
        if (line.size() < 66 || line[64] != ' ' || line[65] != ' ') {
            std::fprintf(stderr,
                "  malformed line (skipped): %s\n", line.c_str());
            continue;
        }
        std::string expected = line.substr(0, 64);
        std::string rel = line.substr(66);
        std::string full = projectDir + "/" + rel;
        if (!fs::exists(full)) {
            missing++;
            failures.push_back(rel + " (missing)");
            continue;
        }
        std::string actual = wowee_sha256::fileHex(full);
        if (actual != expected) {
            mismatched++;
            failures.push_back(rel + " (hash mismatch)");
            continue;
        }
        ok++;
    }
    std::printf("validate-project-checksum: %s\n", inPath.c_str());
    std::printf("  ok         : %d\n", ok);
    std::printf("  missing    : %d\n", missing);
    std::printf("  mismatched : %d\n", mismatched);
    if (!failures.empty()) {
        std::printf("\n  Failures:\n");
        for (const auto& f : failures) std::printf("    - %s\n", f.c_str());
    }
    return (missing == 0 && mismatched == 0) ? 0 : 1;
}

int handleExportZoneHtml(int& i, int argc, char** argv) {
    // Generate a single-file HTML viewer next to the zone .glb.
    // Anyone with a modern browser can open it — no installs, no
    // CDN-mining the user's network. Uses model-viewer (Google's
    // web component) bundled from the unpkg CDN since it's
    // standards-based and doesn't require a build step.
    //
    // Usage flow:
    //   wowee_editor --bake-zone-glb custom_zones/MyZone
    //   wowee_editor --export-zone-html custom_zones/MyZone
    //   open custom_zones/MyZone/MyZone.html  # opens in browser
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string manifestPath = zoneDir + "/zone.json";
    if (!fs::exists(manifestPath)) {
        std::fprintf(stderr,
            "export-zone-html: %s has no zone.json\n", zoneDir.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(manifestPath)) {
        std::fprintf(stderr, "export-zone-html: parse failed\n");
        return 1;
    }
    std::string glbName = zm.mapName + ".glb";
    std::string glbPath = zoneDir + "/" + glbName;
    if (!fs::exists(glbPath)) {
        std::fprintf(stderr,
            "export-zone-html: %s does not exist — run --bake-zone-glb first\n",
            glbPath.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/" + zm.mapName + ".html";
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-zone-html: cannot write %s\n", outPath.c_str());
        return 1;
    }
    // Compute relative path from html file's parent dir to the
    // .glb so the viewer loads it. Default same-dir → just basename.
    std::string glbHref = glbName;
    // If outPath is in a different dir than the .glb, the user is
    // responsible for moving things; leaving glbHref as the
    // basename is a sensible default that fails loudly in the
    // browser console rather than producing a wrong-but-silent
    // page.
    std::string title = zm.displayName.empty()
        ? zm.mapName : zm.displayName;
    // Single-file template with model-viewer. The version pin
    // (^4.0.0) keeps the page from breaking when the unpkg
    // 'latest' silently bumps a major version.
    out << "<!doctype html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <title>" << title << " — Wowee Zone Viewer</title>\n"
           "  <script type=\"module\" "
               "src=\"https://unpkg.com/@google/model-viewer@^4.0.0/dist/model-viewer.min.js\">"
           "</script>\n"
           "  <style>\n"
           "    body { margin:0; font-family: sans-serif; background:#1a1a1a; color:#eee; }\n"
           "    header { padding:12px 20px; background:#2a2a2a; border-bottom:1px solid #444; }\n"
           "    h1 { margin:0; font-size:18px; font-weight:500; }\n"
           "    .meta { color:#aaa; font-size:13px; margin-top:4px; }\n"
           "    model-viewer { width:100vw; height:calc(100vh - 60px); background:#1a1a1a; }\n"
           "    .footer { position:fixed; bottom:8px; right:12px; color:#666; font-size:11px; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <header>\n"
           "    <h1>" << title << "</h1>\n"
           "    <div class=\"meta\">Map: <code>" << zm.mapName
        << "</code> · Tiles: " << zm.tiles.size()
        << " · MapId: " << zm.mapId << "</div>\n"
           "  </header>\n"
           "  <model-viewer\n"
           "    src=\"" << glbHref << "\"\n"
           "    alt=\"" << title << " terrain\"\n"
           "    camera-controls\n"
           "    auto-rotate\n"
           "    rotation-per-second=\"15deg\"\n"
           "    shadow-intensity=\"1\"\n"
           "    exposure=\"1.2\"\n"
           "    environment-image=\"neutral\">\n"
           "  </model-viewer>\n"
           "  <div class=\"footer\">Generated by wowee_editor --export-zone-html</div>\n"
           "</body>\n"
           "</html>\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  references %s (must sit next to .html)\n", glbHref.c_str());
    std::printf("  open in any modern browser — no install required\n");
    return 0;
}

int handleExportProjectHtml(int& i, int argc, char** argv) {
    // Project-level index page linking every zone's HTML viewer.
    // Pairs with --export-zone-html (single zone) and
    // --bake-zone-glb (terrain bake). Designed for github-pages
    // style 'all my zones' showcase.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "export-project-html: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/index.html";
    // Walk for zones (dirs with zone.json). For each, record:
    //   - display name
    //   - relative path to its .html viewer (or null if not generated)
    //   - tile count, content counts
    struct ZoneEntry {
        std::string name, dirRel, htmlRel, glbRel;
        bool htmlExists = false, glbExists = false;
        int tiles = 0, creatures = 0, objects = 0, quests = 0;
    };
    std::vector<ZoneEntry> entries;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        wowee::editor::ZoneManifest zm;
        if (!zm.load((entry.path() / "zone.json").string())) continue;
        ZoneEntry ze;
        ze.name = zm.displayName.empty() ? zm.mapName : zm.displayName;
        ze.dirRel = entry.path().filename().string();
        ze.htmlRel = ze.dirRel + "/" + zm.mapName + ".html";
        ze.glbRel = ze.dirRel + "/" + zm.mapName + ".glb";
        ze.htmlExists = fs::exists(entry.path() / (zm.mapName + ".html"));
        ze.glbExists = fs::exists(entry.path() / (zm.mapName + ".glb"));
        ze.tiles = static_cast<int>(zm.tiles.size());
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile((entry.path() / "creatures.json").string())) {
            ze.creatures = static_cast<int>(sp.spawnCount());
        }
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile((entry.path() / "objects.json").string())) {
            ze.objects = static_cast<int>(op.getObjects().size());
        }
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile((entry.path() / "quests.json").string())) {
            ze.quests = static_cast<int>(qe.questCount());
        }
        entries.push_back(ze);
    }
    std::sort(entries.begin(), entries.end(),
              [](const ZoneEntry& a, const ZoneEntry& b) {
                  return a.name < b.name;
              });
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-project-html: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << "<!doctype html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <title>Wowee Project — Zone Index</title>\n"
           "  <style>\n"
           "    body { margin:0; font-family: sans-serif; background:#1a1a1a; color:#eee; padding:20px; }\n"
           "    h1 { margin:0 0 8px; font-size:22px; }\n"
           "    .count { color:#aaa; font-size:14px; margin-bottom:24px; }\n"
           "    .zones { display:grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap:16px; }\n"
           "    .zone { background:#2a2a2a; border:1px solid #444; border-radius:6px; padding:14px; }\n"
           "    .zone h3 { margin:0 0 6px; font-size:16px; }\n"
           "    .zone .stats { color:#aaa; font-size:13px; }\n"
           "    .zone a { color:#7af; text-decoration:none; font-size:13px; display:inline-block; margin-top:8px; }\n"
           "    .zone a:hover { text-decoration:underline; }\n"
           "    .zone .nolink { color:#666; font-style:italic; font-size:13px; margin-top:8px; }\n"
           "    .footer { margin-top:30px; color:#666; font-size:11px; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <h1>Wowee Project — Zone Index</h1>\n"
           "  <div class=\"count\">" << entries.size() << " zone(s) found in <code>"
        << projectDir << "</code></div>\n"
           "  <div class=\"zones\">\n";
    for (const auto& z : entries) {
        out << "    <div class=\"zone\">\n"
               "      <h3>" << z.name << "</h3>\n"
               "      <div class=\"stats\">"
            << z.tiles << " tile" << (z.tiles == 1 ? "" : "s") << " · "
            << z.creatures << " creature" << (z.creatures == 1 ? "" : "s") << " · "
            << z.objects << " object" << (z.objects == 1 ? "" : "s") << " · "
            << z.quests << " quest" << (z.quests == 1 ? "" : "s") << "</div>\n";
        if (z.htmlExists) {
            out << "      <a href=\"" << z.htmlRel << "\">Open viewer →</a>\n";
        } else if (z.glbExists) {
            out << "      <div class=\"nolink\">No HTML viewer (run --export-zone-html)</div>\n";
        } else {
            out << "      <div class=\"nolink\">No .glb (run --bake-zone-glb)</div>\n";
        }
        out << "    </div>\n";
    }
    out << "  </div>\n"
           "  <div class=\"footer\">Generated by wowee_editor --export-project-html</div>\n"
           "</body>\n"
           "</html>\n";
    out.close();
    int withViewer = 0;
    for (const auto& z : entries) if (z.htmlExists) withViewer++;
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu zone(s) listed, %d with viewable HTML\n",
                entries.size(), withViewer);
    return 0;
}

int handleExportProjectMd(int& i, int argc, char** argv) {
    // Markdown counterpart to --export-project-html. Generates a
    // README.md indexing every zone with counts + bake/viewer
    // status. GitHub renders it natively at the project root.
    // Pairs with --export-zone-summary-md (per-zone) — the project
    // README links to each zone's per-zone .md.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "export-project-md: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/README.md";
    // Per-zone collection: name + counts + which artifacts exist.
    struct Row {
        std::string name, dirRel, mapName;
        int tiles = 0, creatures = 0, objects = 0, quests = 0;
        bool hasGlb = false, hasHtml = false, hasZoneMd = false;
    };
    std::vector<Row> rows;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        wowee::editor::ZoneManifest zm;
        if (!zm.load((entry.path() / "zone.json").string())) continue;
        Row r;
        r.name = zm.displayName.empty() ? zm.mapName : zm.displayName;
        r.dirRel = entry.path().filename().string();
        r.mapName = zm.mapName;
        r.tiles = static_cast<int>(zm.tiles.size());
        wowee::editor::NpcSpawner sp;
        if (sp.loadFromFile((entry.path() / "creatures.json").string())) {
            r.creatures = static_cast<int>(sp.spawnCount());
        }
        wowee::editor::ObjectPlacer op;
        if (op.loadFromFile((entry.path() / "objects.json").string())) {
            r.objects = static_cast<int>(op.getObjects().size());
        }
        wowee::editor::QuestEditor qe;
        if (qe.loadFromFile((entry.path() / "quests.json").string())) {
            r.quests = static_cast<int>(qe.questCount());
        }
        r.hasGlb = fs::exists(entry.path() / (zm.mapName + ".glb"));
        r.hasHtml = fs::exists(entry.path() / (zm.mapName + ".html"));
        r.hasZoneMd = fs::exists(entry.path() / "ZONE.md");
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.name < b.name; });
    int totalT = 0, totalC = 0, totalO = 0, totalQ = 0;
    for (const auto& r : rows) {
        totalT += r.tiles; totalC += r.creatures;
        totalO += r.objects; totalQ += r.quests;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-project-md: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << "# Wowee Project — Zone Index\n\n";
    out << "*Auto-generated. " << rows.size()
        << " zone(s) discovered in `" << projectDir << "`.*\n\n";
    out << "## Summary\n\n";
    out << "| Metric | Total |\n|---|---:|\n";
    out << "| Zones      | " << rows.size() << " |\n";
    out << "| Tiles      | " << totalT << " |\n";
    out << "| Creatures  | " << totalC << " |\n";
    out << "| Objects    | " << totalO << " |\n";
    out << "| Quests     | " << totalQ << " |\n\n";
    out << "## Zones\n\n";
    out << "| Zone | Tiles | Creatures | Objects | Quests | Bake | Viewer | Docs |\n";
    out << "|---|---:|---:|---:|---:|:---:|:---:|:---:|\n";
    for (const auto& r : rows) {
        out << "| ";
        if (r.hasZoneMd) {
            out << "[" << r.name << "](" << r.dirRel << "/ZONE.md)";
        } else {
            out << r.name;
        }
        out << " | " << r.tiles << " | " << r.creatures << " | "
            << r.objects << " | " << r.quests << " | "
            << (r.hasGlb ? "✓" : "—") << " | "
            << (r.hasHtml ? "[view](" + r.dirRel + "/" + r.mapName + ".html)" : "—") << " | "
            << (r.hasZoneMd ? "[md](" + r.dirRel + "/ZONE.md)" : "—") << " |\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu zone(s) indexed (%d tiles, %d creatures, %d objects, %d quests)\n",
                rows.size(), totalT, totalC, totalO, totalQ);
    return 0;
}

int handleExportQuestGraph(int& i, int argc, char** argv) {
    // Render quest chains as a Graphviz DOT graph. Visualizing
    // quest dependencies in plain text rapidly becomes unreadable
    // past ~10 quests; piping this through 'dot -Tpng -o q.png'
    // makes complex chains immediately legible.
    //
    //   wowee_editor --export-quest-graph custom_zones/MyZone
    //   dot -Tpng custom_zones/MyZone/quests.dot -o quests.png
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        outPath = argv[++i];
    }
    std::string path = zoneDir + "/quests.json";
    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr,
            "export-quest-graph: %s not found\n", path.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/quests.dot";
    wowee::editor::QuestEditor qe;
    if (!qe.loadFromFile(path)) {
        std::fprintf(stderr,
            "export-quest-graph: failed to parse %s\n", path.c_str());
        return 1;
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-quest-graph: cannot write %s\n", outPath.c_str());
        return 1;
    }
    // DOT-escape strings (just quotes and backslashes) — quest
    // titles can include arbitrary punctuation that breaks DOT
    // parsing if not escaped.
    auto dotEsc = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    const auto& quests = qe.getQuests();
    // Build an index of valid quest IDs so dangling chain
    // pointers can be styled differently (red, dashed).
    std::unordered_set<uint32_t> validIds;
    for (const auto& q : quests) validIds.insert(q.id);
    out << "digraph QuestChains {\n";
    out << "  // Generated by wowee_editor --export-quest-graph\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box, style=filled, fontname=\"sans-serif\"];\n";
    // Nodes: one per quest, colored by completion-readiness:
    //   green = has objectives + reward + valid NPCs
    //   yellow = missing some non-fatal field (description, etc.)
    //   gray = no objectives (won't actually complete in-game)
    for (const auto& q : quests) {
        bool hasObjs = !q.objectives.empty();
        bool hasReward = (q.reward.xp > 0 || !q.reward.itemRewards.empty());
        std::string color = hasObjs ? (hasReward ? "lightgreen" : "lightyellow")
                                     : "lightgray";
        std::string label = "[" + std::to_string(q.id) + "] " + dotEsc(q.title);
        if (q.requiredLevel > 1) {
            label += "\\nlvl " + std::to_string(q.requiredLevel);
        }
        if (q.reward.xp > 0) {
            label += "  " + std::to_string(q.reward.xp) + " XP";
        }
        out << "  q" << q.id << " [label=\"" << label
            << "\", fillcolor=" << color << "];\n";
    }
    // Edges: quest -> nextQuestId. Style chain-pointers to
    // missing quests differently so they stand out visually.
    int chainEdges = 0, brokenEdges = 0;
    for (const auto& q : quests) {
        if (q.nextQuestId == 0) continue;
        if (validIds.count(q.nextQuestId) == 0) {
            out << "  q" << q.id << " -> q" << q.nextQuestId
                << " [color=red, style=dashed, label=\"missing\"];\n";
            out << "  q" << q.nextQuestId
                << " [label=\"<missing> [" << q.nextQuestId
                << "]\", fillcolor=mistyrose, style=\"filled,dashed\"];\n";
            brokenEdges++;
        } else {
            out << "  q" << q.id << " -> q" << q.nextQuestId << ";\n";
            chainEdges++;
        }
    }
    out << "}\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu quests, %d chain edges, %d broken (red/dashed)\n",
                quests.size(), chainEdges, brokenEdges);
    std::printf("  next: dot -Tpng %s -o quests.png\n", outPath.c_str());
    return 0;
}


}  // namespace

bool handleExport(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--export-zone-summary-md") == 0 && i + 1 < argc) {
        outRc = handleExportZoneSummaryMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-zone-csv") == 0 && i + 1 < argc) {
        outRc = handleExportZoneCsv(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-zone-checksum") == 0 && i + 1 < argc) {
        outRc = handleExportZoneChecksum(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-project-checksum") == 0 && i + 1 < argc) {
        outRc = handleExportProjectChecksum(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-project-checksum") == 0 && i + 1 < argc) {
        outRc = handleValidateProjectChecksum(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-zone-html") == 0 && i + 1 < argc) {
        outRc = handleExportZoneHtml(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-project-html") == 0 && i + 1 < argc) {
        outRc = handleExportProjectHtml(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-project-md") == 0 && i + 1 < argc) {
        outRc = handleExportProjectMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-quest-graph") == 0 && i + 1 < argc) {
        outRc = handleExportQuestGraph(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
