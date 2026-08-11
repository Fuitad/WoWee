#pragma once

/**
 * gltf_glb.hpp — writing the .glb container, once.
 *
 * A .glb is a twelve-byte header and then chunks: a JSON one and a binary one,
 * each with its own length and type word. Five exporters built that by hand —
 * two in cli_bake, two in cli_world_io, one in cli_wom_io — with the four spec
 * words written as bare hex and the padding rules restated, or assumed, in each.
 *
 * It is a format where being wrong is silent on this side and total on the
 * other: a length field off by the padding produces a file that this program
 * still reports as exported and that no viewer will open. The one caller that
 * skipped the BIN padding said in a comment that its data happened to be
 * aligned already — true when it was written, and not something the next
 * change to that buffer would be told about.
 */

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wowee {
namespace editor {
namespace cli {

/// The four words the container is made of, as the glTF 2.0 spec spells them.
/// Each is the ASCII read little-endian, which is why they look like nonsense
/// as numbers: 0x46546C67 is 'glTF'.
inline constexpr uint32_t kGlbMagic = 0x46546C67;      ///< 'glTF'
inline constexpr uint32_t kGlbVersion = 2;
inline constexpr uint32_t kGlbChunkJson = 0x4E4F534A;  ///< 'JSON'
inline constexpr uint32_t kGlbChunkBin = 0x004E4942;   ///< 'BIN\0'

/// The two buffer targets a bufferView can name.
inline constexpr int kGltfArrayBuffer = 34962;
inline constexpr int kGltfElementArrayBuffer = 34963;

/// The component types these exporters use.
inline constexpr int kGltfFloat = 5126;
inline constexpr int kGltfUnsignedInt = 5125;

/// Write a .glb: the header, the JSON chunk, and the binary chunk.
///
/// Both chunks are padded to a four-byte boundary — the JSON with spaces and
/// the binary with zeros, which is what the spec asks for and not
/// interchangeable. The lengths in the headers are the padded ones, because
/// that is what a reader steps over.
///
/// False means the file could not be opened or written; `error` says which.
inline bool writeGlb(const std::string& outPath, const nlohmann::json& gltf,
                     const std::vector<uint8_t>& bin, std::string& error) {
    std::string jsonStr = gltf.dump();
    while (jsonStr.size() % 4 != 0) jsonStr += ' ';

    std::vector<uint8_t> binPadded = bin;
    while (binPadded.size() % 4 != 0) binPadded.push_back(0);

    const uint32_t jsonLen = static_cast<uint32_t>(jsonStr.size());
    const uint32_t binLen = static_cast<uint32_t>(binPadded.size());
    // Twelve for the header, eight for each chunk's length and type.
    const uint32_t totalLen = 12 + 8 + jsonLen + 8 + binLen;

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        error = "cannot open " + outPath;
        return false;
    }
    auto writeWord = [&out](uint32_t word) {
        out.write(reinterpret_cast<const char*>(&word), 4);
    };
    writeWord(kGlbMagic);
    writeWord(kGlbVersion);
    writeWord(totalLen);
    writeWord(jsonLen);
    writeWord(kGlbChunkJson);
    out.write(jsonStr.data(), jsonLen);
    writeWord(binLen);
    writeWord(kGlbChunkBin);
    if (binLen > 0) {
        out.write(reinterpret_cast<const char*>(binPadded.data()), binLen);
    }
    out.close();
    if (!out) {
        error = "failed writing " + outPath;
        return false;
    }
    return true;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
