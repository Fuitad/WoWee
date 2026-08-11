#include "cli_wom_info.hpp"
#include "cli_arg_parse.hpp"

#include "pipeline/wowee_model.hpp"
#include "pipeline/wowee_building.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/asset_manager.hpp"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleInfo(int& i, int argc, char** argv) {
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    // Allow either "/path/to/file.wom" or "/path/to/file"; load() expects no extension.
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr, "WOM not found: %s.wom\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wom"] = base + ".wom";
        j["version"] = wom.version;
        j["name"] = wom.name;
        j["vertices"] = wom.vertices.size();
        j["indices"] = wom.indices.size();
        j["triangles"] = wom.indices.size() / 3;
        j["textures"] = wom.texturePaths.size();
        j["bones"] = wom.bones.size();
        j["animations"] = wom.animations.size();
        j["batches"] = wom.batches.size();
        j["boundRadius"] = wom.boundRadius;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOM: %s.wom\n", base.c_str());
    std::printf("  version    : %u%s\n", wom.version,
                wom.version == 3 ? " (multi-batch)" :
                wom.version == 2 ? " (animated)" : " (static)");
    std::printf("  name       : %s\n", wom.name.c_str());
    std::printf("  vertices   : %zu\n", wom.vertices.size());
    std::printf("  indices    : %zu (%zu tris)\n", wom.indices.size(), wom.indices.size() / 3);
    std::printf("  textures   : %zu\n", wom.texturePaths.size());
    std::printf("  bones      : %zu\n", wom.bones.size());
    std::printf("  animations : %zu\n", wom.animations.size());
    std::printf("  batches    : %zu\n", wom.batches.size());
    std::printf("  boundRadius: %.2f\n", wom.boundRadius);
    return 0;
}

int handleInfoBatches(int& i, int argc, char** argv) {
    // Per-batch breakdown of a WOM3 (multi-material) model.
    // --info shows the total batch count; this drills into each
    // one's index range, texture, blend mode, and flags. Useful
    // for debugging 'why is this submesh transparent?' or
    // 'which batch has the bad UV?'.
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr, "WOM not found: %s.wom\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    // Blend modes per WoweeModel::Batch comment:
    //   0=opaque, 1=alpha-test, 2=alpha, 3=add
    auto blendName = [](uint16_t b) {
        switch (b) {
            case 0: return "opaque";
            case 1: return "alpha-test";
            case 2: return "alpha";
            case 3: return "add";
        }
        return "?";
    };
    // Flags bits:
    //   bit 0 (0x01) = unlit
    //   bit 1 (0x02) = two-sided
    //   bit 2 (0x04) = no z-write
    auto flagsStr = [](uint16_t f) {
        std::string s;
        if (f & 0x01) s += "unlit ";
        if (f & 0x02) s += "two-sided ";
        if (f & 0x04) s += "no-zwrite ";
        if (s.empty()) s = "-";
        else s.pop_back();  // drop trailing space
        return s;
    };
    if (jsonOut) {
        nlohmann::json j;
        j["wom"] = base + ".wom";
        j["version"] = wom.version;
        j["totalBatches"] = wom.batches.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < wom.batches.size(); ++k) {
            const auto& b = wom.batches[k];
            std::string tex = (b.textureIndex < wom.texturePaths.size())
                               ? wom.texturePaths[b.textureIndex]
                               : std::string("<oob>");
            arr.push_back({
                {"index", k},
                {"indexStart", b.indexStart},
                {"indexCount", b.indexCount},
                {"triangles", b.indexCount / 3},
                {"textureIndex", b.textureIndex},
                {"texturePath", tex},
                {"blendMode", b.blendMode},
                {"blendName", blendName(b.blendMode)},
                {"flags", b.flags},
                {"flagsStr", flagsStr(b.flags)},
            });
        }
        j["batches"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOM batches: %s.wom (v%u, %zu batches)\n",
                base.c_str(), wom.version, wom.batches.size());
    if (wom.batches.empty()) {
        std::printf("  *no batches (WOM1/WOM2 single-material model)*\n");
        return 0;
    }
    std::printf("  idx  iStart  iCount  tris   blend       flags          texture\n");
    for (size_t k = 0; k < wom.batches.size(); ++k) {
        const auto& b = wom.batches[k];
        std::string tex = (b.textureIndex < wom.texturePaths.size())
                           ? wom.texturePaths[b.textureIndex]
                           : std::string("<oob>");
        std::printf("  %3zu  %6u  %6u  %5u  %-10s  %-13s  %s\n",
                    k, b.indexStart, b.indexCount, b.indexCount / 3,
                    blendName(b.blendMode),
                    flagsStr(b.flags).c_str(),
                    tex.c_str());
    }
    return 0;
}

int handleInfoTextures(int& i, int argc, char** argv) {
    // List every texture path a WOM references, with on-disk
    // presence for both BLP (proprietary) and PNG (sidecar)
    // forms. Useful for tracking which textures are missing
    // before --pack-wcp would fail at runtime.
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr, "WOM not found: %s.wom\n", base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    namespace fs = std::filesystem;
    // Texture paths in WOMs are usually game-relative
    // ('World/Generic/Tree.blp'); resolve them against the
    // common Data/ root for the on-disk presence check. Skip
    // the check when the path doesn't exist as either an
    // absolute or relative file (avoids false 'missing'
    // reports when the user runs from outside the data root).
    auto checkBlp = [&](const std::string& p) {
        if (fs::exists(p)) return true;
        std::string lower = p;
        for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".blp") {
            lower += ".blp";
        }
        return fs::exists("Data/" + lower);
    };
    auto sidecarPng = [&](const std::string& p) {
        std::string base = p;
        if (base.size() >= 4 &&
            (base.substr(base.size() - 4) == ".blp" ||
             base.substr(base.size() - 4) == ".BLP")) {
            base = base.substr(0, base.size() - 4);
        }
        std::string png = base + ".png";
        if (fs::exists(png)) return true;
        std::string lower = png;
        for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
        return fs::exists("Data/" + lower);
    };
    if (jsonOut) {
        nlohmann::json j;
        j["wom"] = base + ".wom";
        j["textureCount"] = wom.texturePaths.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < wom.texturePaths.size(); ++k) {
            const auto& p = wom.texturePaths[k];
            arr.push_back({
                {"index", k},
                {"path", p},
                {"blpPresent", checkBlp(p)},
                {"pngPresent", sidecarPng(p)},
            });
        }
        j["textures"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOM textures: %s.wom (%zu textures)\n",
                base.c_str(), wom.texturePaths.size());
    if (wom.texturePaths.empty()) {
        std::printf("  *no texture references*\n");
        return 0;
    }
    std::printf("  idx  blp  png  path\n");
    for (size_t k = 0; k < wom.texturePaths.size(); ++k) {
        const auto& p = wom.texturePaths[k];
        std::printf("  %3zu   %s    %s   %s\n",
                    k,
                    checkBlp(p) ? "y" : "-",
                    sidecarPng(p) ? "y" : "-",
                    p.c_str());
    }
    return 0;
}

int handleInfoDoodads(int& i, int argc, char** argv) {
    // List every doodad placement in a WOB (M2 instances inside
    // a building). Companion to --info-textures: where one
    // tracks GPU resources, this tracks scene composition.
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wob")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeBuildingLoader::exists(base)) {
        std::fprintf(stderr, "WOB not found: %s.wob\n", base.c_str());
        return 1;
    }
    auto bld = wowee::pipeline::WoweeBuildingLoader::load(base);
    if (jsonOut) {
        nlohmann::json j;
        j["wob"] = base + ".wob";
        j["count"] = bld.doodads.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < bld.doodads.size(); ++k) {
            const auto& d = bld.doodads[k];
            arr.push_back({
                {"index", k},
                {"modelPath", d.modelPath},
                {"position", {d.position.x, d.position.y, d.position.z}},
                {"rotation", {d.rotation.x, d.rotation.y, d.rotation.z}},
                {"scale", d.scale},
            });
        }
        j["doodads"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("WOB doodads: %s.wob (%zu placements)\n",
                base.c_str(), bld.doodads.size());
    if (bld.doodads.empty()) {
        std::printf("  *no doodad placements*\n");
        return 0;
    }
    std::printf("  idx  scale  pos (x, y, z)             rot (x, y, z)             model\n");
    for (size_t k = 0; k < bld.doodads.size(); ++k) {
        const auto& d = bld.doodads[k];
        std::printf("  %3zu  %5.2f  (%6.1f, %6.1f, %6.1f)  (%6.1f, %6.1f, %6.1f)  %s\n",
                    k, d.scale,
                    d.position.x, d.position.y, d.position.z,
                    d.rotation.x, d.rotation.y, d.rotation.z,
                    d.modelPath.c_str());
    }
    return 0;
}

int handleInfoAttachParticleSequence(int& i, int argc, char** argv) {
    // Three M2 inspectors share an entry point — they all need
    // the same M2Loader::load + skin merge dance, then differ
    // only in which sub-array they iterate.
    enum Kind { kAttach, kParticle, kSequence };
    Kind kind;
    const char* cmdName;
    if (std::strcmp(argv[i], "--info-attachments") == 0) {
        kind = kAttach; cmdName = "info-attachments";
    } else if (std::strcmp(argv[i], "--info-particles") == 0) {
        kind = kParticle; cmdName = "info-particles";
    } else {
        kind = kSequence; cmdName = "info-sequences";
    }
    std::string path = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "%s: cannot open %s\n", cmdName, path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    // Auto-merge skin for vertex/index counts to match render.
    std::vector<uint8_t> skinBytes;
    {
        std::string skinPath = pipeline::skinPathForM2(path);
        std::ifstream sf(skinPath, std::ios::binary);
        if (sf) {
            skinBytes.assign((std::istreambuf_iterator<char>(sf)),
                              std::istreambuf_iterator<char>());
        }
    }
    auto m2 = wowee::pipeline::M2Loader::load(bytes);
    if (!skinBytes.empty()) {
        wowee::pipeline::M2Loader::loadSkin(skinBytes, m2);
    }
    if (kind == kAttach) {
        if (jsonOut) {
            nlohmann::json j;
            j["m2"] = path;
            j["count"] = m2.attachments.size();
            nlohmann::json arr = nlohmann::json::array();
            for (size_t k = 0; k < m2.attachments.size(); ++k) {
                const auto& a = m2.attachments[k];
                arr.push_back({
                    {"index", k}, {"id", a.id}, {"bone", a.bone},
                    {"position", {a.position.x, a.position.y, a.position.z}}
                });
            }
            j["attachments"] = arr;
            std::printf("%s\n", j.dump(2).c_str());
            return 0;
        }
        std::printf("M2 attachments: %s (%zu)\n", path.c_str(),
                    m2.attachments.size());
        if (m2.attachments.empty()) {
            std::printf("  *no attachments*\n");
            return 0;
        }
        std::printf("  idx   id  bone  pos (x, y, z)\n");
        for (size_t k = 0; k < m2.attachments.size(); ++k) {
            const auto& a = m2.attachments[k];
            std::printf("  %3zu  %3u  %4u  (%6.2f, %6.2f, %6.2f)\n",
                        k, a.id, a.bone,
                        a.position.x, a.position.y, a.position.z);
        }
        return 0;
    }
    if (kind == kParticle) {
        auto blendName = [](uint8_t b) {
            switch (b) {
                case 0: return "opaque";
                case 1: return "alphakey";
                case 2: return "alpha";
                case 4: return "add";
            }
            return "?";
        };
        if (jsonOut) {
            nlohmann::json j;
            j["m2"] = path;
            j["particleEmitters"] = m2.particleEmitters.size();
            j["ribbonEmitters"] = m2.ribbonEmitters.size();
            nlohmann::json parts = nlohmann::json::array();
            for (size_t k = 0; k < m2.particleEmitters.size(); ++k) {
                const auto& p = m2.particleEmitters[k];
                parts.push_back({
                    {"index", k}, {"particleId", p.particleId},
                    {"bone", p.bone}, {"texture", p.texture},
                    {"blendingType", p.blendingType},
                    {"blendName", blendName(p.blendingType)},
                    {"emitterType", p.emitterType},
                    {"position", {p.position.x, p.position.y, p.position.z}}
                });
            }
            j["particles"] = parts;
            nlohmann::json ribbons = nlohmann::json::array();
            for (size_t k = 0; k < m2.ribbonEmitters.size(); ++k) {
                const auto& r = m2.ribbonEmitters[k];
                ribbons.push_back({
                    {"index", k}, {"ribbonId", r.ribbonId},
                    {"bone", r.bone},
                    {"textureIndex", r.textureIndex},
                    {"materialIndex", r.materialIndex},
                    {"position", {r.position.x, r.position.y, r.position.z}}
                });
            }
            j["ribbons"] = ribbons;
            std::printf("%s\n", j.dump(2).c_str());
            return 0;
        }
        std::printf("M2 emitters: %s\n", path.c_str());
        std::printf("  particles: %zu, ribbons: %zu\n",
                    m2.particleEmitters.size(), m2.ribbonEmitters.size());
        if (!m2.particleEmitters.empty()) {
            std::printf("\n  Particles:\n");
            std::printf("    idx   id  bone  tex  blend     type  pos (x, y, z)\n");
            for (size_t k = 0; k < m2.particleEmitters.size(); ++k) {
                const auto& p = m2.particleEmitters[k];
                std::printf("    %3zu  %3d  %4u  %3u  %-8s  %4u  (%5.1f, %5.1f, %5.1f)\n",
                            k, p.particleId, p.bone, p.texture,
                            blendName(p.blendingType), p.emitterType,
                            p.position.x, p.position.y, p.position.z);
            }
        }
        if (!m2.ribbonEmitters.empty()) {
            std::printf("\n  Ribbons:\n");
            std::printf("    idx   id  bone  tex  mat  pos (x, y, z)\n");
            for (size_t k = 0; k < m2.ribbonEmitters.size(); ++k) {
                const auto& r = m2.ribbonEmitters[k];
                std::printf("    %3zu  %3d  %4u  %3u  %3u  (%5.1f, %5.1f, %5.1f)\n",
                            k, r.ribbonId, r.bone, r.textureIndex, r.materialIndex,
                            r.position.x, r.position.y, r.position.z);
            }
        }
        return 0;
    }
    // kind == kSequence
    if (jsonOut) {
        nlohmann::json j;
        j["m2"] = path;
        j["count"] = m2.sequences.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < m2.sequences.size(); ++k) {
            const auto& s = m2.sequences[k];
            arr.push_back({
                {"index", k}, {"id", s.id},
                {"variation", s.variationIndex},
                {"durationMs", s.duration}, {"flags", s.flags},
                {"movingSpeed", s.movingSpeed},
                {"frequency", s.frequency},
                {"blendTimeMs", s.blendTime}
            });
        }
        j["sequences"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("M2 sequences: %s (%zu)\n", path.c_str(),
                m2.sequences.size());
    if (m2.sequences.empty()) {
        std::printf("  *no sequences*\n");
        return 0;
    }
    std::printf("  idx   id  var  duration  flags    speed  blend\n");
    for (size_t k = 0; k < m2.sequences.size(); ++k) {
        const auto& s = m2.sequences[k];
        std::printf("  %3zu  %3u  %3u  %8u  %5u   %5.2f  %5u\n",
                    k, s.id, s.variationIndex,
                    s.duration, s.flags,
                    s.movingSpeed, s.blendTime);
    }
    return 0;
}

int handleInfoBones(int& i, int argc, char** argv) {
    // Inspect M2 bone tree. Shows parent index, key-bone ID
    // (-1 if not a named bone), pivot offset, and a depth
    // indicator computed by walking up parents — useful for
    // debugging skeleton structure when something looks wrong
    // in the renderer ('why is this bone not following its parent?').
    std::string path = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "info-bones: cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    auto m2 = wowee::pipeline::M2Loader::load(bytes);
    // Compute depth per bone — guard against cycles by capping
    // walk length at boneCount (a real DAG can't exceed that).
    std::vector<int> depths(m2.bones.size(), -1);
    for (size_t k = 0; k < m2.bones.size(); ++k) {
        int d = 0;
        int idx = static_cast<int>(k);
        while (idx >= 0 && d <= static_cast<int>(m2.bones.size())) {
            int parent = m2.bones[idx].parentBone;
            if (parent < 0) break;
            idx = parent;
            d++;
        }
        depths[k] = d;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["m2"] = path;
        j["count"] = m2.bones.size();
        nlohmann::json arr = nlohmann::json::array();
        for (size_t k = 0; k < m2.bones.size(); ++k) {
            const auto& b = m2.bones[k];
            arr.push_back({
                {"index", k}, {"keyBoneId", b.keyBoneId},
                {"parent", b.parentBone}, {"flags", b.flags},
                {"depth", depths[k]},
                {"pivot", {b.pivot.x, b.pivot.y, b.pivot.z}}
            });
        }
        j["bones"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("M2 bones: %s (%zu)\n", path.c_str(), m2.bones.size());
    if (m2.bones.empty()) {
        std::printf("  *no bones (static model)*\n");
        return 0;
    }
    std::printf("  idx  parent  depth  keyBone  flags    pivot (x, y, z)\n");
    for (size_t k = 0; k < m2.bones.size(); ++k) {
        const auto& b = m2.bones[k];
        // Indent the keyBone column by depth so the tree shape
        // is visible at a glance.
        std::printf("  %3zu  %6d  %5d  %7d  %5u    (%6.2f, %6.2f, %6.2f)\n",
                    k, b.parentBone, depths[k], b.keyBoneId, b.flags,
                    b.pivot.x, b.pivot.y, b.pivot.z);
    }
    return 0;
}

int handleValidateWom(int& i, int argc, char** argv) {
    // Static sanity checks on a .wom: catches malformed
    // hand-built or import-corrupted models before they reach
    // the renderer (where errors usually crash or render blank).
    // Mirrors --validate-wol / --validate-wow. Reports each
    // failed check with details and exits non-zero on any
    // failure; clean models print a single OK line.
    std::string base = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        std::fprintf(stderr, "validate-wom: WOM not found: %s.wom\n",
                     base.c_str());
        return 1;
    }
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    // 1) version field
    if (wom.version < 1 || wom.version > 3) {
        errors.push_back("version " + std::to_string(wom.version) +
                         " not in supported range 1-3");
    }
    // 2) non-empty geometry
    if (wom.vertices.empty()) errors.push_back("vertex list is empty");
    if (wom.indices.empty())  errors.push_back("index list is empty");
    if (wom.indices.size() % 3 != 0) {
        errors.push_back("index count " +
                         std::to_string(wom.indices.size()) +
                         " is not a multiple of 3 (not a triangle list)");
    }
    // 3) all indices < vertex count
    uint32_t vCount = static_cast<uint32_t>(wom.vertices.size());
    size_t oobIdx = 0;
    for (uint32_t idx : wom.indices) {
        if (idx >= vCount) { oobIdx++; }
    }
    if (oobIdx > 0) {
        errors.push_back(std::to_string(oobIdx) +
                         " triangle indices reference out-of-range vertices");
    }
    // 4) bone refs (only meaningful when bones exist)
    if (!wom.bones.empty()) {
        size_t oobBoneIdx = 0;
        size_t badWeightSum = 0;
        for (const auto& v : wom.vertices) {
            int sum = 0;
            for (int k = 0; k < 4; ++k) {
                if (v.boneWeights[k] > 0 &&
                    v.boneIndices[k] >= wom.bones.size()) {
                    oobBoneIdx++;
                }
                sum += v.boneWeights[k];
            }
            // Allow either 0 (no skinning) or 255 (full skinning,
            // possibly split across slots). Anything else is a
            // weight-table mistake.
            if (sum != 0 && sum != 255) {
                badWeightSum++;
            }
        }
        if (oobBoneIdx > 0) {
            errors.push_back(std::to_string(oobBoneIdx) +
                             " vertex bone-index slots reference out-of-range bones");
        }
        if (badWeightSum > 0) {
            warnings.push_back(std::to_string(badWeightSum) +
                               " vertices have boneWeights summing to neither 0 nor 255");
        }
        // parentBone < bones.size() (or -1 for root)
        size_t oobParent = 0;
        for (const auto& b : wom.bones) {
            if (b.parentBone >= 0 &&
                b.parentBone >= static_cast<int16_t>(wom.bones.size())) {
                oobParent++;
            }
        }
        if (oobParent > 0) {
            errors.push_back(std::to_string(oobParent) +
                             " bones reference out-of-range parent bones");
        }
    }
    // 5) WOM3 batch coverage: union of all batch ranges should
    //    equal [0, indices.size()) without gaps or overlaps, and
    //    each batch.textureIndex must be a valid index.
    if (wom.hasBatches()) {
        size_t oobTex = 0, oobRange = 0;
        for (const auto& b : wom.batches) {
            if (!wom.texturePaths.empty() &&
                b.textureIndex >= wom.texturePaths.size()) {
                oobTex++;
            }
            if (static_cast<size_t>(b.indexStart) + b.indexCount >
                wom.indices.size()) {
                oobRange++;
            }
        }
        if (oobTex > 0) {
            errors.push_back(std::to_string(oobTex) +
                             " batch.textureIndex values out of range");
        }
        if (oobRange > 0) {
            errors.push_back(std::to_string(oobRange) +
                             " batches index past end of index buffer");
        }
        // Coverage check via bytemap of triangles.
        size_t triCount = wom.indices.size() / 3;
        std::vector<uint8_t> covered(triCount, 0);
        for (const auto& b : wom.batches) {
            uint32_t tStart = b.indexStart / 3;
            uint32_t tEnd = (b.indexStart + b.indexCount) / 3;
            for (uint32_t t = tStart; t < tEnd && t < triCount; ++t)
                covered[t]++;
        }
        size_t uncovered = 0, overlapped = 0;
        for (auto c : covered) {
            if (c == 0) uncovered++;
            else if (c > 1) overlapped++;
        }
        if (uncovered > 0) {
            warnings.push_back(std::to_string(uncovered) +
                               " triangles not covered by any batch");
        }
        if (overlapped > 0) {
            warnings.push_back(std::to_string(overlapped) +
                               " triangles covered by multiple batches");
        }
    }
    // 6) bounds sanity
    if (wom.boundRadius <= 0) {
        warnings.push_back("boundRadius <= 0 (model will fail frustum culling)");
    }
    if (wom.boundMin.x > wom.boundMax.x ||
        wom.boundMin.y > wom.boundMax.y ||
        wom.boundMin.z > wom.boundMax.z) {
        errors.push_back("boundMin > boundMax on at least one axis (inverted AABB)");
    }
    // 7) animation sanity (WOM2/3): per-bone keyframe arrays
    //    must have one entry per bone in the model.
    for (size_t a = 0; a < wom.animations.size(); ++a) {
        const auto& anim = wom.animations[a];
        if (!wom.bones.empty() &&
            anim.boneKeyframes.size() != wom.bones.size()) {
            errors.push_back("animation " + std::to_string(a) +
                             " (id=" + std::to_string(anim.id) +
                             ") has " + std::to_string(anim.boneKeyframes.size()) +
                             " bone tracks but model has " +
                             std::to_string(wom.bones.size()) + " bones");
        }
    }

    bool ok = errors.empty();
    if (jsonOut) {
        nlohmann::json j;
        j["wom"] = base + ".wom";
        j["ok"] = ok;
        j["errors"] = errors;
        j["warnings"] = warnings;
        std::printf("%s\n", j.dump(2).c_str());
        return ok ? 0 : 1;
    }
    std::printf("validate-wom: %s.wom\n", base.c_str());
    if (ok && warnings.empty()) {
        std::printf("  OK — %zu vertices, %zu triangles, %zu batches, %zu bones, %zu animations\n",
                    wom.vertices.size(), wom.indices.size() / 3,
                    wom.batches.size(), wom.bones.size(),
                    wom.animations.size());
        return 0;
    }
    if (!warnings.empty()) {
        std::printf("  warnings (%zu):\n", warnings.size());
        for (const auto& w : warnings)
            std::printf("    - %s\n", w.c_str());
    }
    if (!errors.empty()) {
        std::printf("  ERRORS (%zu):\n", errors.size());
        for (const auto& e : errors)
            std::printf("    - %s\n", e.c_str());
    }
    return ok ? 0 : 1;
}

int handleExportBonesDot(int& i, int argc, char** argv) {
    // Render WOM bone hierarchy as Graphviz DOT. Mirrors
    // --export-quest-graph for skeleton trees: trying to read
    // a 50-bone tree from --info-bones output is painful;
    // pipe this through `dot -Tpng` for the picture.
    std::string base = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".wom")
        base = base.substr(0, base.size() - 4);
    if (!wowee::pipeline::WoweeModelLoader::exists(base)) {
        return reportMissing("export-bones-dot", "WOM", base, ".wom");
    }
    if (outPath.empty()) outPath = base + ".bones.dot";
    auto wom = wowee::pipeline::WoweeModelLoader::load(base);
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-bones-dot: cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << "digraph BoneTree {\n";
    out << "  // Generated by wowee_editor --export-bones-dot\n";
    out << "  rankdir=TB;\n";
    out << "  node [shape=box, style=filled, fontname=\"sans-serif\", fontsize=10];\n";
    // Color: green for keybones (named anchor points), gray for
    // internal/blend bones. Root bones (parent=-1) get yellow border.
    for (size_t k = 0; k < wom.bones.size(); ++k) {
        const auto& b = wom.bones[k];
        bool isKey = (b.keyBoneId >= 0);
        std::string fill = isKey ? "lightgreen" : "lightgrey";
        std::string label = "[" + std::to_string(k) + "]";
        if (isKey) label += "\\nkey=" + std::to_string(b.keyBoneId);
        out << "  b" << k << " [label=\"" << label
            << "\", fillcolor=" << fill;
        if (b.parentBone == -1) out << ", penwidth=2, color=goldenrod";
        out << "];\n";
    }
    // Edges: child -> parent (parent is up).
    int rootCount = 0;
    for (size_t k = 0; k < wom.bones.size(); ++k) {
        int16_t p = wom.bones[k].parentBone;
        if (p < 0 || p >= static_cast<int16_t>(wom.bones.size())) {
            rootCount++;
            continue;
        }
        out << "  b" << p << " -> b" << k << ";\n";
    }
    out << "}\n";
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  %zu bones, %d root(s)\n",
                wom.bones.size(), rootCount);
    std::printf("  next: dot -Tpng %s -o bones.png\n", outPath.c_str());
    return 0;
}


}  // namespace

bool handleWomInfo(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info") == 0 && i + 1 < argc) {
        outRc = handleInfo(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-batches") == 0 && i + 1 < argc) {
        outRc = handleInfoBatches(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-textures") == 0 && i + 1 < argc) {
        outRc = handleInfoTextures(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-doodads") == 0 && i + 1 < argc) {
        outRc = handleInfoDoodads(i, argc, argv); return true;
    }
    if ((std::strcmp(argv[i], "--info-attachments") == 0 ||
         std::strcmp(argv[i], "--info-particles") == 0 ||
         std::strcmp(argv[i], "--info-sequences") == 0) &&
        i + 1 < argc) {
        outRc = handleInfoAttachParticleSequence(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-bones") == 0 && i + 1 < argc) {
        outRc = handleInfoBones(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-bones-dot") == 0 && i + 1 < argc) {
        outRc = handleExportBonesDot(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-wom") == 0 && i + 1 < argc) {
        outRc = handleValidateWom(i, argc, argv); return true;
    }
    return false;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
