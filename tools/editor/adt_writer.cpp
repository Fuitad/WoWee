#include "adt_writer.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <cstring>
#include <filesystem>
#include <cmath>

namespace wowee {
namespace editor {

// ADT chunk magics (little-endian as read from file)
static constexpr uint32_t MVER = 0x4D564552;
static constexpr uint32_t MHDR = 0x4D484452;
static constexpr uint32_t MCIN = 0x4D43494E;
static constexpr uint32_t MTEX = 0x4D544558;
static constexpr uint32_t MMDX = 0x4D4D4458;
static constexpr uint32_t MMID = 0x4D4D4944;
static constexpr uint32_t MWMO = 0x4D574D4F;
static constexpr uint32_t MWID = 0x4D574944;
static constexpr uint32_t MDDF = 0x4D444446;
static constexpr uint32_t MODF = 0x4D4F4446;
static constexpr uint32_t MCNK = 0x4D434E4B;
static constexpr uint32_t MCVT = 0x4D435654;
static constexpr uint32_t MCNR = 0x4D434E52;
static constexpr uint32_t MCLY = 0x4D434C59;
static constexpr uint32_t MCAL = 0x4D43414C;

void ADTWriter::writeChunkHeader(std::vector<uint8_t>& buf, uint32_t magic, uint32_t size) {
    writeU32(buf, magic);
    writeU32(buf, size);
}

void ADTWriter::writeU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 24) & 0xFF);
}

void ADTWriter::writeU16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
}

void ADTWriter::writeFloat(std::vector<uint8_t>& buf, float val) {
    // Reject NaN/inf - would silently turn into invisible terrain or
    // off-map placements after the next ADT load.
    if (!std::isfinite(val)) val = 0.0f;
    uint32_t bits;
    std::memcpy(&bits, &val, 4);
    writeU32(buf, bits);
}

void ADTWriter::writeBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + size);
}

void ADTWriter::patchSize(std::vector<uint8_t>& buf, size_t headerOffset) {
    uint32_t size = static_cast<uint32_t>(buf.size() - headerOffset - 8);
    std::memcpy(buf.data() + headerOffset + 4, &size, 4);
}

void ADTWriter::writeMVER(std::vector<uint8_t>& buf) {
    writeChunkHeader(buf, MVER, 4);
    writeU32(buf, 18); // ADT version
}

void ADTWriter::writeMHDR(std::vector<uint8_t>& buf, size_t& mhdrOffset) {
    mhdrOffset = buf.size();
    writeChunkHeader(buf, MHDR, 64);
    // 16 uint32 fields - all zeros for now (offsets filled later if needed)
    for (int i = 0; i < 16; i++) writeU32(buf, 0);
}

void ADTWriter::writeMTEX(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain) {
    size_t start = buf.size();
    writeChunkHeader(buf, MTEX, 0);
    for (const auto& tex : terrain.textures) {
        writeBytes(buf, tex.c_str(), tex.size() + 1); // null-terminated
    }
    patchSize(buf, start);
}

void ADTWriter::writeMMDX(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain) {
    size_t start = buf.size();
    writeChunkHeader(buf, MMDX, 0);
    for (const auto& name : terrain.doodadNames) {
        writeBytes(buf, name.c_str(), name.size() + 1);
    }
    patchSize(buf, start);

    // MMID: offsets into MMDX
    size_t mmidStart = buf.size();
    writeChunkHeader(buf, MMID, 0);
    uint32_t offset = 0;
    for (const auto& name : terrain.doodadNames) {
        writeU32(buf, offset);
        offset += static_cast<uint32_t>(name.size() + 1);
    }
    patchSize(buf, mmidStart);
}

void ADTWriter::writeMWMO(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain) {
    size_t start = buf.size();
    writeChunkHeader(buf, MWMO, 0);
    for (const auto& name : terrain.wmoNames) {
        writeBytes(buf, name.c_str(), name.size() + 1);
    }
    patchSize(buf, start);

    // MWID: offsets into MWMO
    size_t mwidStart = buf.size();
    writeChunkHeader(buf, MWID, 0);
    uint32_t offset = 0;
    for (const auto& name : terrain.wmoNames) {
        writeU32(buf, offset);
        offset += static_cast<uint32_t>(name.size() + 1);
    }
    patchSize(buf, mwidStart);
}

void ADTWriter::writeMDDF(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain) {
    size_t start = buf.size();
    writeChunkHeader(buf, MDDF, 0);
    for (const auto& p : terrain.doodadPlacements) {
        writeU32(buf, p.nameId);
        writeU32(buf, p.uniqueId);
        writeFloat(buf, p.position[0]);
        writeFloat(buf, p.position[1]);
        writeFloat(buf, p.position[2]);
        writeFloat(buf, p.rotation[0]);
        writeFloat(buf, p.rotation[1]);
        writeFloat(buf, p.rotation[2]);
        writeU16(buf, p.scale);
        writeU16(buf, p.flags);
    }
    patchSize(buf, start);
}

void ADTWriter::writeMODF(std::vector<uint8_t>& buf, const pipeline::ADTTerrain& terrain) {
    size_t start = buf.size();
    writeChunkHeader(buf, MODF, 0);
    for (const auto& p : terrain.wmoPlacements) {
        writeU32(buf, p.nameId);
        writeU32(buf, p.uniqueId);
        writeFloat(buf, p.position[0]);
        writeFloat(buf, p.position[1]);
        writeFloat(buf, p.position[2]);
        writeFloat(buf, p.rotation[0]);
        writeFloat(buf, p.rotation[1]);
        writeFloat(buf, p.rotation[2]);
        writeFloat(buf, p.extentLower[0]);
        writeFloat(buf, p.extentLower[1]);
        writeFloat(buf, p.extentLower[2]);
        writeFloat(buf, p.extentUpper[0]);
        writeFloat(buf, p.extentUpper[1]);
        writeFloat(buf, p.extentUpper[2]);
        writeU16(buf, p.flags);
        writeU16(buf, p.doodadSet);
        // MODF entry is 64 bytes total; the trailing nameSet + scale slots are
        // populated when present (WotLK+) and default to (0, 1024) otherwise.
        writeU16(buf, p.nameSet);
        writeU16(buf, p.scale != 0 ? p.scale : 1024);
    }
    patchSize(buf, start);
}

void ADTWriter::writeMCNK(std::vector<uint8_t>& buf, const pipeline::MapChunk& chunk,
                           int chunkX, int chunkY) {
    size_t mcnkStart = buf.size();
    writeChunkHeader(buf, MCNK, 0);

    // MCNK header (128 bytes)
    writeU32(buf, chunk.flags);
    writeU32(buf, chunkX);
    writeU32(buf, chunkY);
    writeU32(buf, static_cast<uint32_t>(chunk.layers.size()));
    writeU32(buf, 0); // nDoodadRefs
    // Offsets within MCNK - filled with placeholder (parser uses sub-chunk magic scanning)
    for (int i = 0; i < 5; i++) writeU32(buf, 0); // ofsHeight, ofsNormal, ofsLayer, ofsRefs, ofsAlpha
    writeU32(buf, 0); // sizeAlpha
    writeU32(buf, 0); // ofsShadow
    writeU32(buf, 0); // sizeShadow
    writeU32(buf, 0); // areaid
    writeU32(buf, 0); // nMapObjRefs
    writeU16(buf, chunk.holes);
    writeU16(buf, 0); // padding
    // 16 bytes of low-quality texture map (doodadStencil)
    for (int i = 0; i < 4; i++) writeU32(buf, 0);
    writeU32(buf, 0); // predTex
    writeU32(buf, 0); // noEffectDoodad
    writeU32(buf, 0); // ofsSndEmitters
    writeU32(buf, 0); // nSndEmitters
    writeU32(buf, 0); // ofsLiquid
    writeU32(buf, 0); // sizeLiquid
    writeFloat(buf, chunk.position[0]);
    writeFloat(buf, chunk.position[1]);
    writeFloat(buf, chunk.position[2]);
    writeU32(buf, 0); // ofsMCCV
    writeU32(buf, 0); // ofsMCLV
    writeU32(buf, 0); // unused

    // MCVT sub-chunk (145 floats = 580 bytes)
    writeChunkHeader(buf, MCVT, 145 * 4);
    for (int i = 0; i < 145; i++) {
        writeFloat(buf, chunk.heightMap.heights[i]);
    }

    // MCNR sub-chunk (145 * 3 = 435 bytes + 13 pad = 448)
    writeChunkHeader(buf, MCNR, 435 + 13);
    writeBytes(buf, chunk.normals.data(), 435);
    for (int i = 0; i < 13; i++) buf.push_back(0); // padding

    // MCLY sub-chunk
    {
        size_t mclyStart = buf.size();
        writeChunkHeader(buf, MCLY, 0);
        for (const auto& layer : chunk.layers) {
            writeU32(buf, layer.textureId);
            writeU32(buf, layer.flags);
            writeU32(buf, layer.offsetMCAL);
            writeU32(buf, layer.effectId);
        }
        patchSize(buf, mclyStart);
    }

    // MCAL sub-chunk (alpha maps)
    {
        size_t mcalStart = buf.size();
        writeChunkHeader(buf, MCAL, 0);
        if (!chunk.alphaMap.empty()) {
            writeBytes(buf, chunk.alphaMap.data(), chunk.alphaMap.size());
        }
        patchSize(buf, mcalStart);
    }

    patchSize(buf, mcnkStart);
}

std::vector<uint8_t> ADTWriter::serialize(const pipeline::ADTTerrain& terrain) {
    std::vector<uint8_t> buf;
    buf.reserve(2 * 1024 * 1024);

    writeMVER(buf);

    size_t mhdrOffset = 0;
    writeMHDR(buf, mhdrOffset);

    // MCIN placeholder (256 entries × 16 bytes = 4096 bytes)
    size_t mcinStart = buf.size();
    writeChunkHeader(buf, MCIN, 4096);
    for (int i = 0; i < 256 * 4; i++) writeU32(buf, 0);

    writeMTEX(buf, terrain);
    writeMMDX(buf, terrain);
    writeMWMO(buf, terrain);
    writeMDDF(buf, terrain);
    writeMODF(buf, terrain);

    // Write 256 MCNK chunks and record offsets
    std::vector<size_t> mcnkOffsets(256);
    std::vector<uint32_t> mcnkSizes(256);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int idx = y * 16 + x;
            mcnkOffsets[idx] = buf.size();
            writeMCNK(buf, terrain.chunks[idx], x, y);
            mcnkSizes[idx] = static_cast<uint32_t>(buf.size() - mcnkOffsets[idx]);
        }
    }

    // Patch MCIN with offsets and sizes
    for (int i = 0; i < 256; i++) {
        size_t entryOffset = mcinStart + 8 + i * 16;
        uint32_t offset = static_cast<uint32_t>(mcnkOffsets[i]);
        uint32_t size = mcnkSizes[i];
        std::memcpy(buf.data() + entryOffset, &offset, 4);
        std::memcpy(buf.data() + entryOffset + 4, &size, 4);
        // flags and asyncId stay 0
    }

    return buf;
}

bool ADTWriter::write(const pipeline::ADTTerrain& terrain, const std::string& path) {
    auto data = serialize(terrain);

    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file for writing: ", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    LOG_INFO("ADT written: ", path, " (", data.size(), " bytes)");
    return true;
}

bool ADTWriter::writeWDT(const std::string& mapName, int tileX, int tileY,
                          const std::string& path) {
    std::vector<uint8_t> buf;
    buf.reserve(32768);

    // MVER
    writeChunkHeader(buf, MVER, 4);
    writeU32(buf, 18);

    // MPHD (map header - 32 bytes, all zeros = no special flags)
    writeChunkHeader(buf, 0x4D504844, 32);
    for (int i = 0; i < 8; i++) writeU32(buf, 0);

    // MAIN (64×64 grid of 8-byte entries: flags + asyncId)
    writeChunkHeader(buf, 0x4D41494E, 64 * 64 * 8);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            if (x == tileX && y == tileY) {
                writeU32(buf, 1); // FLAG_EXISTS
            } else {
                writeU32(buf, 0);
            }
            writeU32(buf, 0); // asyncId
        }
    }

    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to write WDT: ", path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    LOG_INFO("WDT written: ", path, " (", buf.size(), " bytes, map=", mapName, ")");
    return true;
}

} // namespace editor
} // namespace wowee
