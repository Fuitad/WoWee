#include "content_pack.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace wowee {
namespace editor {

// WCP file format (simple concatenated archive):
// Header: "WCP1" (4 bytes) + fileCount (4) + infoJsonSize (4)
// Info JSON block (infoJsonSize bytes)
// File table: for each file: pathLen(2) + path(pathLen) + dataSize(4)
// File data: concatenated file contents

static constexpr uint32_t WCP_MAGIC = 0x31504357; // "WCP1"

bool ContentPacker::packZone(const std::string& outputDir, const std::string& mapName,
                              const std::string& destPath, const ContentPackInfo& info) {
    namespace fs = std::filesystem;
    std::string srcDir = outputDir + "/" + mapName;

    if (!fs::exists(srcDir)) {
        LOG_ERROR("Source directory not found: ", srcDir);
        return false;
    }

    // Collect all files. Normalize path separators to '/' so packs created
    // on Windows are readable on Linux/macOS and vice versa - the unpack
    // path-traversal check rejects '\' as an absolute prefix, so a Windows
    // path leaks would silently fail to extract.
    //
    // Cap total file count at the unpack-side limit (1M) so a runaway
    // recursive_directory_iterator on a hostile symlink loop or a giant
    // accidental subdirectory doesn't produce an unpackable archive.
    constexpr size_t kMaxFiles = 1'000'000;
    std::vector<std::pair<std::string, std::string>> files; // relative path, full path
    for (auto& entry : fs::recursive_directory_iterator(srcDir)) {
        if (!entry.is_regular_file()) continue;
        if (files.size() >= kMaxFiles) {
            LOG_WARNING("WCP file count cap reached (", kMaxFiles,
                        "); remaining files in ", srcDir, " omitted");
            break;
        }
        std::string rel = fs::relative(entry.path(), srcDir).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        // fs::relative can return "../foo" when srcDir is a symlink that
        // resolves outside the pack root; reject those before they're
        // baked into a WCP that the unpacker will then refuse wholesale.
        if (rel.find("..") != std::string::npos ||
            (!rel.empty() && rel[0] == '/')) {
            LOG_WARNING("WCP skipping out-of-tree file: ", entry.path().string());
            continue;
        }
        files.push_back({rel, entry.path().string()});
    }

    if (files.empty()) {
        LOG_ERROR("No files to pack in: ", srcDir);
        return false;
    }

    // Build info JSON. Cap string lengths so a stray gigantic field can't
    // bloat the info JSON past the 16MB unpack cap (which would then make
    // the pack unreadable via readInfo / unpackZone).
    auto cap = [](std::string s, size_t n) {
        if (s.size() > n) s.resize(n);
        return s;
    };
    nlohmann::json infoObj;
    infoObj["format"] = cap(info.format, 64);
    infoObj["name"] = cap(info.name, 100);
    infoObj["author"] = cap(info.author, 100);
    infoObj["description"] = cap(info.description, 4096);
    infoObj["version"] = cap(info.version, 32);
    infoObj["mapId"] = info.mapId;
    infoObj["fileCount"] = files.size();
    nlohmann::json fileArr = nlohmann::json::array();
    for (const auto& [rel, full] : files) {
        fileArr.push_back({{"path", rel}, {"size", fs::file_size(full)}});
    }
    infoObj["files"] = fileArr;
    std::string infoJson = infoObj.dump(2);

    // Write WCP file
    std::ofstream out(destPath, std::ios::binary);
    if (!out) {
        LOG_ERROR("Failed to create pack file: ", destPath);
        return false;
    }

    // Header
    out.write(reinterpret_cast<const char*>(&WCP_MAGIC), 4);
    uint32_t fileCount = static_cast<uint32_t>(files.size());
    out.write(reinterpret_cast<const char*>(&fileCount), 4);
    uint32_t infoSize = static_cast<uint32_t>(infoJson.size());
    out.write(reinterpret_cast<const char*>(&infoSize), 4);

    // Info JSON
    out.write(infoJson.data(), infoJson.size());

    // File table + data
    for (const auto& [rel, full] : files) {
        // Truncate path length to fit u16; matches the unpack-side cap.
        // Also skip files whose disk size doesn't fit in uint32 (4GB).
        uint16_t pathLen = static_cast<uint16_t>(std::min<size_t>(rel.size(), 1024));
        out.write(reinterpret_cast<const char*>(&pathLen), 2);
        out.write(rel.data(), pathLen);

        std::ifstream fin(full, std::ios::binary | std::ios::ate);
        std::streamsize sz = fin.tellg();
        // Cap at the unpack-side per-file limit (256MB) so we never write
        // a pack the loader will reject as a whole. Files that big are
        // almost certainly an authoring mistake - log + skip the body
        // instead of producing an unpackable archive.
        constexpr uint64_t kMaxFileBytes = 256ull * 1024 * 1024;
        if (sz < 0 || static_cast<uint64_t>(sz) > kMaxFileBytes) {
            LOG_ERROR("WCP skipped file (size ", sz, " > 256MB cap): ", rel);
            uint32_t zero = 0;
            out.write(reinterpret_cast<const char*>(&zero), 4);
            continue;
        }
        uint32_t dataSize = static_cast<uint32_t>(sz);
        fin.seekg(0);
        out.write(reinterpret_cast<const char*>(&dataSize), 4);

        std::vector<char> buf(dataSize);
        fin.read(buf.data(), dataSize);
        out.write(buf.data(), dataSize);
    }

    LOG_INFO("Content pack created: ", destPath, " (", files.size(), " files, ",
             out.tellp(), " bytes)");
    return true;
}

bool ContentPacker::unpackZone(const std::string& wcpPath, const std::string& destDir) {
    std::ifstream in(wcpPath, std::ios::binary);
    if (!in) return false;

    uint32_t magic;
    in.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != WCP_MAGIC) {
        LOG_ERROR("Not a WCP file: ", wcpPath);
        return false;
    }

    uint32_t fileCount, infoSize;
    in.read(reinterpret_cast<char*>(&fileCount), 4);
    in.read(reinterpret_cast<char*>(&infoSize), 4);
    // Sanity bounds: a zone with more than 1M files or a 16MB info block is
    // almost certainly corrupted. Reject early so we don't OOM on a malicious
    // header before reading the body.
    if (fileCount > 1'000'000 || infoSize > 16 * 1024 * 1024) {
        LOG_ERROR("WCP header rejected (fileCount=", fileCount,
                  " infoSize=", infoSize, "): ", wcpPath);
        return false;
    }

    // Read the info JSON to extract the zone name. packZone stored files
    // relative to the zone subdirectory (e.g. "MyZone_32_32.adt"), so we
    // need to recreate that subdirectory under destDir for the loader to
    // find the zone.
    std::string infoJson(infoSize, '\0');
    in.read(infoJson.data(), infoSize);
    std::string zoneName;
    try {
        auto info = nlohmann::json::parse(infoJson);
        zoneName = info.value("name", "");
    } catch (...) {}

    // The zone name becomes a directory name. A malicious WCP could carry a
    // name with traversal sequences ("../etc") or an absolute path
    // ("/etc/passwd") that would write outside destDir. Strip to a safe
    // identifier - same alphabet as the server module slug.
    std::string safeZoneName;
    for (char c : zoneName) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            safeZoneName += c;
        } else if (c == ' ') {
            safeZoneName += '_';
        }
    }
    if (safeZoneName != zoneName && !zoneName.empty()) {
        LOG_WARNING("WCP zone name sanitized: '", zoneName, "' -> '",
                    safeZoneName, "'");
    }

    namespace fs = std::filesystem;
    std::string zoneDir = safeZoneName.empty() ? destDir : destDir + "/" + safeZoneName;
    fs::create_directories(zoneDir);

    for (uint32_t i = 0; i < fileCount; i++) {
        uint16_t pathLen;
        in.read(reinterpret_cast<char*>(&pathLen), 2);
        // Cap path length - uint16 can hold up to 64KB but real zone paths
        // are well under 256 chars. Anything longer is corrupt or malicious.
        if (pathLen > 1024) {
            LOG_ERROR("WCP rejected file ", i, " path length ", pathLen, " too large");
            return false;
        }
        std::string path(pathLen, '\0');
        in.read(path.data(), pathLen);
        // Normalize separators in case this WCP was packed before the
        // pack-side normalization was added (older builds emitted '\' on
        // Windows). Backslash translation must happen BEFORE the
        // traversal check so the absolute-path rule catches Windows
        // drive letters consistently.
        std::replace(path.begin(), path.end(), '\\', '/');

        uint32_t dataSize;
        in.read(reinterpret_cast<char*>(&dataSize), 4);
        // Cap individual file size to prevent OOM from a malicious entry.
        // 256MB per packed file is well above any legitimate content.
        if (dataSize > 256 * 1024 * 1024) {
            LOG_ERROR("WCP rejected file ", path, " size ", dataSize, " too large");
            return false;
        }
        // Reject path-traversal attempts. Files like "../../etc/passwd" would
        // write outside destDir/<zoneName>/ and clobber system files.
        // (Backslashes are already normalized to '/' above.)
        if (path.find("..") != std::string::npos ||
            (!path.empty() && path[0] == '/') ||
            (path.size() >= 2 && path[1] == ':')) {  // C:\... drive prefix
            LOG_ERROR("WCP rejected suspicious path: ", path);
            return false;
        }

        std::vector<char> data(dataSize);
        in.read(data.data(), dataSize);
        // Detect short reads - indicates the WCP was truncated mid-file.
        // gcount() reflects the actual bytes read; if it's less than dataSize
        // we'd write a partial file silently and the consumer would think
        // the zone is intact.
        auto bytesRead = in.gcount();
        if (bytesRead != static_cast<std::streamsize>(dataSize)) {
            LOG_ERROR("WCP file ", path, " truncated: expected ", dataSize,
                      " got ", bytesRead);
            return false;
        }

        std::string fullPath = zoneDir + "/" + path;
        fs::create_directories(fs::path(fullPath).parent_path());
        std::ofstream fout(fullPath, std::ios::binary);
        if (!fout) {
            LOG_ERROR("WCP could not open output file: ", fullPath);
            return false;
        }
        fout.write(data.data(), dataSize);
    }

    LOG_INFO("Content pack extracted to: ", zoneDir, " (", fileCount, " files)");
    return true;
}

bool ContentPacker::readInfo(const std::string& wcpPath, ContentPackInfo& info) {
    std::ifstream in(wcpPath, std::ios::binary);
    if (!in) return false;

    uint32_t magic;
    in.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != WCP_MAGIC) return false;

    uint32_t fileCount, infoSize;
    in.read(reinterpret_cast<char*>(&fileCount), 4);
    in.read(reinterpret_cast<char*>(&infoSize), 4);
    // Same sanity bounds as unpack - refuse to allocate or read absurd
    // info JSON on a malicious header.
    if (fileCount > 1'000'000 || infoSize > 16 * 1024 * 1024) {
        LOG_ERROR("WCP readInfo header rejected (fileCount=", fileCount,
                  " infoSize=", infoSize, "): ", wcpPath);
        return false;
    }

    std::string jsonStr(infoSize, '\0');
    in.read(jsonStr.data(), infoSize);

    try {
        auto j = nlohmann::json::parse(jsonStr);
        info.name = j.value("name", "");
        info.author = j.value("author", "");
        info.description = j.value("description", "");
        info.version = j.value("version", "");
        info.format = j.value("format", "");
        info.mapId = j.value("mapId", 9000u);
        info.files.clear();
        if (j.contains("files") && j["files"].is_array()) {
            // Same cap as the header fileCount - info JSON could declare
            // more entries than the header, so this defends both readInfo
            // callers and the listing CLI from runaway memory use.
            constexpr size_t kMaxFiles = 1'000'000;
            for (const auto& jf : j["files"]) {
                if (info.files.size() >= kMaxFiles) break;
                ContentPackInfo::FileEntry fe;
                fe.path = jf.value("path", "");
                fe.size = jf.value("size", 0ULL);
                auto dot = fe.path.rfind('.');
                if (dot != std::string::npos) {
                    std::string ext = fe.path.substr(dot);
                    if (ext == ".wot" || ext == ".whm") fe.category = "terrain";
                    else if (ext == ".wom") fe.category = "model";
                    else if (ext == ".wob") fe.category = "building";
                    else if (ext == ".woc") fe.category = "collision";
                    else if (ext == ".png") fe.category = "texture";
                    else if (ext == ".json") fe.category = "data";
                    else if (ext == ".adt" || ext == ".wdt") fe.category = "legacy";
                    else fe.category = "other";
                }
                info.files.push_back(fe);
            }
        }
    } catch (...) {
        return false;
    }

    return true;
}

static bool checkMagic(const std::string& path, uint32_t expectedMagic) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    // Require minimum body bytes after the 4-byte magic. A 4-byte file
    // with only the right magic is not a valid asset; the format-specific
    // load routines would reject it but the magic-only check would pass.
    f.seekg(0, std::ios::end);
    auto fileSize = f.tellg();
    if (fileSize < 8) return false;
    f.seekg(0, std::ios::beg);
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    return magic == expectedMagic;
}

// Returns true if `magic` matches any of the WOM family magics (WOM1/WOM2/WOM3).
static bool checkAnyMagic(const std::string& path,
                           std::initializer_list<uint32_t> expected) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    if (f.tellg() < 8) return false;
    f.seekg(0, std::ios::beg);
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    for (uint32_t e : expected) if (magic == e) return true;
    return false;
}

ContentPacker::ValidationResult ContentPacker::validateZone(const std::string& zoneDir) {
    namespace fs = std::filesystem;
    ValidationResult r;
    if (!fs::exists(zoneDir)) return r;

    static constexpr uint32_t WHM_MAGIC = 0x314D4857; // "WHM1"
    static constexpr uint32_t WOM_MAGIC = 0x314D4F57; // "WOM1"
    static constexpr uint32_t WOM2_MAGIC = 0x324D4F57; // "WOM2"
    static constexpr uint32_t WOM3_MAGIC = 0x334D4F57; // "WOM3"
    static constexpr uint32_t WOB_MAGIC = 0x31424F57; // "WOB1"
    static constexpr uint32_t WOC_MAGIC = 0x31434F57; // "WOC1"

    for (auto& entry : fs::recursive_directory_iterator(zoneDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::string fname = entry.path().filename().string();
        if (ext == ".wot") { r.hasWot = true; r.wotCount++; }
        if (ext == ".whm") {
            r.hasWhm = true; r.whmCount++;
            if (checkMagic(entry.path().string(), WHM_MAGIC)) r.whmValid = true;
        }
        if (ext == ".wom") {
            r.hasWom = true; r.womCount++;
            if (checkAnyMagic(entry.path().string(), {WOM_MAGIC, WOM2_MAGIC, WOM3_MAGIC}))
                r.womValid = true;
            else r.womInvalidCount++;
        }
        if (ext == ".wob") {
            r.hasWob = true; r.wobCount++;
            if (checkMagic(entry.path().string(), WOB_MAGIC)) r.wobValid = true;
            else r.wobInvalidCount++;
        }
        if (ext == ".woc") {
            r.hasWoc = true; r.wocCount++;
            if (checkMagic(entry.path().string(), WOC_MAGIC)) r.wocValid = true;
            else r.wocInvalidCount++;
        }
        if (ext == ".png") { r.hasPng = true; r.pngCount++; }
        if (fname == "zone.json") r.hasZoneJson = true;
        if (fname == "creatures.json") r.hasCreatures = true;
        if (fname == "quests.json") r.hasQuests = true;
        if (fname == "objects.json") r.hasObjects = true;
    }
    return r;
}

int ContentPacker::ValidationResult::openFormatScore() const {
    int score = 0;
    if (hasWot) score++;
    if (hasWhm && whmValid) score++;
    if (hasZoneJson) score++;
    if (hasPng) score++;
    if (hasWom && womValid) score++;
    if (hasWob && wobValid) score++;
    if (hasWoc && wocValid) score++;
    return score; // max 7 for fully open
}

std::string ContentPacker::ValidationResult::summary() const {
    std::string s;
    auto add = [&](bool has, bool valid, const char* name) {
        if (!has) return;
        s += name;
        if (!valid) s += "(!)";
        s += " ";
    };
    add(hasWot, true, "WOT");
    add(hasWhm, whmValid, "WHM");
    add(hasWom, womValid, "WOM");
    add(hasWob, wobValid, "WOB");
    add(hasWoc, wocValid, "WOC");
    if (hasZoneJson) s += "zone.json ";
    if (hasPng) s += "PNG ";
    if (hasCreatures) s += "creatures ";
    if (hasQuests) s += "quests ";
    if (hasObjects) s += "objects ";
    return s.empty() ? "(empty)" : s;
}

} // namespace editor
} // namespace wowee
