#include "cli_zone_mgmt.hpp"

#include "zone_manifest.hpp"
#include "npc_spawner.hpp"
#include "object_placer.hpp"
#include "quest_editor.hpp"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleCopyZone(int& i, int /*argc*/, char** argv) {
    // Duplicate a zone - copy every file then rename slug-prefixed
    // ones (heightmap/terrain/collision sidecars carry the slug in
    // their filenames, e.g. "Sample_28_30.whm") so the new zone is
    // self-consistent. Useful for templating: scaffold once, then
    // copy-zone N times to create variants.
    std::string srcDir = argv[++i];
    std::string rawName = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr, "copy-zone: source dir not found: %s\n",
                     srcDir.c_str());
        return 1;
    }
    if (!fs::exists(srcDir + "/zone.json")) {
        std::fprintf(stderr, "copy-zone: %s has no zone.json - not a zone dir\n",
                     srcDir.c_str());
        return 1;
    }
    // Slugify new name (matches scaffold-zone rules so the result
    // round-trips through unpackZone / server module gen).
    std::string newSlug;
    for (char c : rawName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            newSlug += c;
        } else if (c == ' ') {
            newSlug += '_';
        }
    }
    if (newSlug.empty()) {
        std::fprintf(stderr, "copy-zone: name '%s' has no valid characters\n",
                     rawName.c_str());
        return 1;
    }
    std::string dstDir = "custom_zones/" + newSlug;
    if (fs::exists(dstDir)) {
        std::fprintf(stderr, "copy-zone: destination already exists: %s\n",
                     dstDir.c_str());
        return 1;
    }
    // Read the source slug from its zone.json so we know what
    // prefix to rewrite. Don't trust the directory name - a user
    // could have renamed the dir without touching the manifest.
    wowee::editor::ZoneManifest src;
    if (!src.load(srcDir + "/zone.json")) {
        std::fprintf(stderr, "copy-zone: failed to parse %s/zone.json\n",
                     srcDir.c_str());
        return 1;
    }
    std::string oldSlug = src.mapName;
    if (oldSlug == newSlug) {
        std::fprintf(stderr, "copy-zone: new slug matches old (%s); nothing to do\n",
                     oldSlug.c_str());
        return 1;
    }
    // Recursive copy preserves any subdirs (e.g. data/ for DBC sidecars).
    std::error_code ec;
    fs::create_directories(dstDir);
    fs::copy(srcDir, dstDir,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             ec);
    if (ec) {
        std::fprintf(stderr, "copy-zone: copy failed: %s\n", ec.message().c_str());
        return 1;
    }
    // Rename slug-prefixed files inside the destination. Match
    // "<oldSlug>_..." or "<oldSlug>." so we catch both
    // "Sample_28_30.whm" and a hypothetical "Sample.wdt".
    int renamed = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dstDir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        bool match = (fname.size() > oldSlug.size() + 1 &&
                      fname.compare(0, oldSlug.size(), oldSlug) == 0 &&
                      (fname[oldSlug.size()] == '_' ||
                       fname[oldSlug.size()] == '.'));
        if (!match) continue;
        std::string newName = newSlug + fname.substr(oldSlug.size());
        fs::rename(entry.path(), entry.path().parent_path() / newName, ec);
        if (!ec) renamed++;
    }
    // Rewrite the destination's zone.json with the new slug so its
    // files-block (rebuilt from mapName by save()) matches the
    // renamed files on disk.
    wowee::editor::ZoneManifest dst = src;
    dst.mapName = newSlug;
    dst.displayName = rawName;
    if (!dst.save(dstDir + "/zone.json")) {
        std::fprintf(stderr, "copy-zone: failed to write %s/zone.json\n",
                     dstDir.c_str());
        return 1;
    }
    std::printf("Copied %s -> %s\n", srcDir.c_str(), dstDir.c_str());
    std::printf("  mapName  : %s -> %s\n", oldSlug.c_str(), newSlug.c_str());
    std::printf("  renamed  : %d slug-prefixed file(s)\n", renamed);
    return 0;
}

int handleRenameZone(int& i, int /*argc*/, char** argv) {
    // In-place rename - like --copy-zone but no copy. Useful when
    // the user wants to fix a typo or change a name without
    // doubling disk usage. Renames the directory itself too
    // (Old/ -> New/ under the same parent), so paths shift.
    std::string srcDir = argv[++i];
    std::string rawName = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(srcDir) || !fs::is_directory(srcDir)) {
        std::fprintf(stderr, "rename-zone: source dir not found: %s\n",
                     srcDir.c_str());
        return 1;
    }
    if (!fs::exists(srcDir + "/zone.json")) {
        std::fprintf(stderr, "rename-zone: %s has no zone.json - not a zone dir\n",
                     srcDir.c_str());
        return 1;
    }
    std::string newSlug;
    for (char c : rawName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            newSlug += c;
        } else if (c == ' ') {
            newSlug += '_';
        }
    }
    if (newSlug.empty()) {
        std::fprintf(stderr, "rename-zone: name '%s' has no valid characters\n",
                     rawName.c_str());
        return 1;
    }
    wowee::editor::ZoneManifest zm;
    if (!zm.load(srcDir + "/zone.json")) {
        std::fprintf(stderr, "rename-zone: failed to parse %s/zone.json\n",
                     srcDir.c_str());
        return 1;
    }
    std::string oldSlug = zm.mapName;
    if (oldSlug == newSlug && rawName == zm.displayName) {
        std::fprintf(stderr,
            "rename-zone: nothing to do (slug=%s, displayName=%s already match)\n",
            oldSlug.c_str(), rawName.c_str());
        return 1;
    }
    // Compute target directory: same parent, new slug name. If the
    // current directory name already matches the new slug, skip
    // the dir rename (only manifest + slug-prefixed files change).
    fs::path srcPath = fs::absolute(srcDir);
    fs::path parent = srcPath.parent_path();
    fs::path dstPath = parent / newSlug;
    bool needDirRename = (srcPath.filename() != newSlug);
    if (needDirRename && fs::exists(dstPath)) {
        std::fprintf(stderr, "rename-zone: target dir already exists: %s\n",
                     dstPath.string().c_str());
        return 1;
    }
    // Rename slug-prefixed files inside the source dir BEFORE
    // moving the directory - fewer paths to fix up if anything
    // fails midway. fs::rename is atomic per-call.
    std::error_code ec;
    int renamed = 0;
    for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        bool match = (oldSlug != newSlug &&
                      fname.size() > oldSlug.size() + 1 &&
                      fname.compare(0, oldSlug.size(), oldSlug) == 0 &&
                      (fname[oldSlug.size()] == '_' ||
                       fname[oldSlug.size()] == '.'));
        if (!match) continue;
        std::string newName = newSlug + fname.substr(oldSlug.size());
        fs::rename(entry.path(), entry.path().parent_path() / newName, ec);
        if (!ec) renamed++;
    }
    // Update manifest and save BEFORE the dir rename so the file
    // exists at the path we're saving to.
    zm.mapName = newSlug;
    zm.displayName = rawName;
    if (!zm.save(srcDir + "/zone.json")) {
        std::fprintf(stderr, "rename-zone: failed to write zone.json\n");
        return 1;
    }
    // Now move the directory itself.
    std::string finalDir = srcDir;
    if (needDirRename) {
        fs::rename(srcPath, dstPath, ec);
        if (ec) {
            std::fprintf(stderr,
                "rename-zone: dir rename failed (%s); manifest already updated\n",
                ec.message().c_str());
            return 1;
        }
        finalDir = dstPath.string();
    }
    std::printf("Renamed %s -> %s\n", srcDir.c_str(), finalDir.c_str());
    std::printf("  mapName  : %s -> %s\n", oldSlug.c_str(), newSlug.c_str());
    std::printf("  renamed  : %d slug-prefixed file(s)\n", renamed);
    return 0;
}

int handleRemoveZone(int& i, int argc, char** argv) {
    // Delete a zone directory entirely. Requires --confirm to
    // actually delete (defense against accidental destruction
    // and against shell glob mishaps). Without --confirm,
    // just lists what would be deleted.
    std::string zoneDir = argv[++i];
    bool confirm = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--confirm") == 0) {
        confirm = true; i++;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir)) {
        std::fprintf(stderr,
            "remove-zone: %s does not exist\n", zoneDir.c_str());
        return 1;
    }
    if (!fs::exists(zoneDir + "/zone.json")) {
        // Belt-and-suspenders: refuse to wipe anything that doesn't
        // look like a zone dir, even with --confirm. Catches typos
        // like '--remove-zone .' that would nuke the whole project.
        std::fprintf(stderr,
            "remove-zone: %s has no zone.json - refusing to delete (not a zone dir)\n",
            zoneDir.c_str());
        return 1;
    }
    // Read manifest for the user-facing name.
    wowee::editor::ZoneManifest zm;
    std::string zoneName = zoneDir;
    if (zm.load(zoneDir + "/zone.json")) {
        zoneName = zm.displayName.empty() ? zm.mapName : zm.displayName;
    }
    // Walk for what would be removed (counts + total bytes).
    int fileCount = 0;
    uint64_t totalBytes = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(zoneDir, ec)) {
        if (!e.is_regular_file()) continue;
        fileCount++;
        totalBytes += e.file_size(ec);
    }
    if (!confirm) {
        std::printf("remove-zone: %s ('%s')\n",
                    zoneDir.c_str(), zoneName.c_str());
        std::printf("  would delete: %d file(s), %.1f KB\n",
                    fileCount, totalBytes / 1024.0);
        std::printf("  re-run with --confirm to actually delete\n");
        return 0;
    }
    // Confirmed - wipe it.
    uintmax_t removed = fs::remove_all(zoneDir, ec);
    if (ec) {
        std::fprintf(stderr,
            "remove-zone: failed to remove %s (%s)\n",
            zoneDir.c_str(), ec.message().c_str());
        return 1;
    }
    std::printf("Removed %s ('%s')\n", zoneDir.c_str(), zoneName.c_str());
    std::printf("  deleted: %ju filesystem entries, %.1f KB freed\n",
                static_cast<uintmax_t>(removed), totalBytes / 1024.0);
    return 0;
}

int handleClearZoneContent(int& i, int argc, char** argv) {
    // Wipe content files (creatures.json / objects.json /
    // quests.json) from a zone while keeping terrain + manifest
    // intact. Useful for templating: --copy-zone gives you a
    // duplicate; --clear-zone-content turns it into an empty
    // shell ready for fresh population.
    //
    // Pass --creatures / --objects / --quests to wipe individually,
    // or --all to wipe everything. At least one selector is required.
    std::string zoneDir = argv[++i];
    bool wipeCreatures = false, wipeObjects = false, wipeQuests = false;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        std::string opt = argv[i + 1];
        if      (opt == "--creatures") { wipeCreatures = true; ++i; }
        else if (opt == "--objects")   { wipeObjects = true;   ++i; }
        else if (opt == "--quests")    { wipeQuests = true;    ++i; }
        else if (opt == "--all") {
            wipeCreatures = wipeObjects = wipeQuests = true; ++i;
        }
        else break;  // unknown flag - stop consuming, surface the error
    }
    if (!wipeCreatures && !wipeObjects && !wipeQuests) {
        std::fprintf(stderr,
            "clear-zone-content: pass --creatures / --objects / --quests / --all\n");
        return 1;
    }
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir + "/zone.json")) {
        std::fprintf(stderr,
            "clear-zone-content: %s has no zone.json - not a zone dir\n",
            zoneDir.c_str());
        return 1;
    }
    // Delete (not blank-write) so the next --info-* doesn't see
    // an empty file and report 'total: 0' as if data existed.
    // Missing files are the canonical 'no content' state.
    int deleted = 0;
    std::error_code ec;
    auto wipe = [&](const std::string& fname) {
        std::string p = zoneDir + "/" + fname;
        if (fs::exists(p) && fs::remove(p, ec)) {
            ++deleted;
            std::printf("  removed  : %s\n", fname.c_str());
        } else if (fs::exists(p)) {
            std::fprintf(stderr,
                "  WARN: failed to remove %s (%s)\n",
                p.c_str(), ec.message().c_str());
        } else {
            std::printf("  skipped  : %s (already absent)\n", fname.c_str());
        }
    };
    std::printf("Cleared content from %s\n", zoneDir.c_str());
    if (wipeCreatures) wipe("creatures.json");
    if (wipeObjects)   wipe("objects.json");
    if (wipeQuests)    wipe("quests.json");
    // Also reset manifest.hasCreatures so server module gen
    // doesn't expect an NPC table that's no longer there.
    if (wipeCreatures) {
        wowee::editor::ZoneManifest zm;
        if (zm.load(zoneDir + "/zone.json")) {
            if (zm.hasCreatures) {
                zm.hasCreatures = false;
                zm.save(zoneDir + "/zone.json");
                std::printf("  updated  : zone.json hasCreatures = false\n");
            }
        }
    }
    std::printf("  removed  : %d file(s) total\n", deleted);
    return 0;
}


}  // namespace

bool handleZoneMgmt(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--copy-zone") == 0 && i + 2 < argc) {
        outRc = handleCopyZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--rename-zone") == 0 && i + 2 < argc) {
        outRc = handleRenameZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--remove-zone") == 0 && i + 1 < argc) {
        outRc = handleRemoveZone(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--clear-zone-content") == 0 && i + 1 < argc) {
        outRc = handleClearZoneContent(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
