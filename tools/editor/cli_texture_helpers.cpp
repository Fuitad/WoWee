#include "cli_texture_helpers.hpp"
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleGenTexture(int& i, int argc, char** argv) {
    // Synthesize a placeholder PNG texture. Lets users add a
    // working texture to their project without an external
    // image editor - useful for prototyping new meshes,
    // filling out a zone before art is final, or generating
    // test fixtures.
    //
    // <colorHex|pattern>:
    //   "RRGGBB" or "RGB" hex (case-insensitive) → solid color
    //   "checker" → 32x32 black/white checkerboard
    //   "grid"    → black background with white 1-px grid every 16
    std::string outPath = argv[++i];
    std::string spec = argv[++i];
    int W = 256, H = 256;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { W = std::stoi(argv[++i]); } catch (...) {}
    }
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        try { H = std::stoi(argv[++i]); } catch (...) {}
    }
    if (W < 1 || H < 1 || W > 8192 || H > 8192) {
        std::fprintf(stderr,
            "gen-texture: invalid size %dx%d (must be 1..8192)\n", W, H);
        return 1;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 3, 0);
    std::string lower = spec;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "checker") {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool dark = ((x / 32) + (y / 32)) & 1;
                uint8_t v = dark ? 16 : 240;
                size_t i2 = (static_cast<size_t>(y) * W + x) * 3;
                pixels[i2 + 0] = v;
                pixels[i2 + 1] = v;
                pixels[i2 + 2] = v;
            }
        }
    } else if (lower == "grid") {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool line = (x % 16 == 0) || (y % 16 == 0);
                uint8_t v = line ? 240 : 32;
                size_t i2 = (static_cast<size_t>(y) * W + x) * 3;
                pixels[i2 + 0] = v;
                pixels[i2 + 1] = v;
                pixels[i2 + 2] = v;
            }
        }
    } else {
        // Hex color. Accept "RGB" (3 chars) or "RRGGBB" (6 chars),
        // optional leading '#'.
        std::string hex = lower;
        if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
        auto fromHex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            return -1;
        };
        uint8_t r = 0, g = 0, b = 0;
        if (hex.size() == 6) {
            int hi, lo;
            if ((hi = fromHex(hex[0])) < 0) goto bad_color;
            if ((lo = fromHex(hex[1])) < 0) goto bad_color;
            r = static_cast<uint8_t>((hi << 4) | lo);
            if ((hi = fromHex(hex[2])) < 0) goto bad_color;
            if ((lo = fromHex(hex[3])) < 0) goto bad_color;
            g = static_cast<uint8_t>((hi << 4) | lo);
            if ((hi = fromHex(hex[4])) < 0) goto bad_color;
            if ((lo = fromHex(hex[5])) < 0) goto bad_color;
            b = static_cast<uint8_t>((hi << 4) | lo);
        } else if (hex.size() == 3) {
            int v0, v1, v2;
            if ((v0 = fromHex(hex[0])) < 0) goto bad_color;
            if ((v1 = fromHex(hex[1])) < 0) goto bad_color;
            if ((v2 = fromHex(hex[2])) < 0) goto bad_color;
            r = static_cast<uint8_t>((v0 << 4) | v0);
            g = static_cast<uint8_t>((v1 << 4) | v1);
            b = static_cast<uint8_t>((v2 << 4) | v2);
        } else {
            goto bad_color;
        }
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                size_t i2 = (static_cast<size_t>(y) * W + x) * 3;
                pixels[i2 + 0] = r;
                pixels[i2 + 1] = g;
                pixels[i2 + 2] = b;
            }
        }
        goto color_ok;
      bad_color:
        std::fprintf(stderr,
            "gen-texture: '%s' is not a valid hex color or 'checker'/'grid'\n",
            spec.c_str());
        return 1;
      color_ok: ;
    }
    if (!stbi_write_png(outPath.c_str(), W, H, 3,
                        pixels.data(), W * 3)) {
        std::fprintf(stderr,
            "gen-texture: stbi_write_png failed for %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  size      : %dx%d\n", W, H);
    std::printf("  spec      : %s\n", spec.c_str());
    return 0;
}

int handleAddTextureToZone(int& i, int argc, char** argv) {
    // Import an existing PNG into a zone directory. Useful
    // for the "I have an artist-painted texture, get it into
    // my project" workflow - complements --gen-texture
    // (procedural placeholder) and --convert-blp-png (legacy
    // BLP migration).
    //
    // Optional <renameTo> argument lets the user store the
    // PNG under a project-specific name (e.g., a generic
    // "stone.png" downloaded from a tileset becomes
    // "courtyard_floor.png" in the zone).
    //
    // Refuses to overwrite an existing destination unless the
    // source and destination are byte-identical (idempotent
    // re-runs are safe).
    std::string zoneDir = argv[++i];
    std::string srcPng = argv[++i];
    std::string renameTo;
    if (i + 1 < argc && argv[i + 1][0] != '-') renameTo = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(zoneDir) || !fs::is_directory(zoneDir)) {
        std::fprintf(stderr,
            "add-texture-to-zone: %s is not a directory\n",
            zoneDir.c_str());
        return 1;
    }
    if (!fs::exists(srcPng) || !fs::is_regular_file(srcPng)) {
        std::fprintf(stderr,
            "add-texture-to-zone: %s is not a file\n",
            srcPng.c_str());
        return 1;
    }
    // Sanity-check: must end in .png (any case) so users
    // don't accidentally drop a .blp/.tga and get surprised
    // when nothing renders.
    std::string srcExt = fs::path(srcPng).extension().string();
    std::transform(srcExt.begin(), srcExt.end(), srcExt.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (srcExt != ".png") {
        std::fprintf(stderr,
            "add-texture-to-zone: %s is not a .png "
            "(use --convert-blp-png for .blp first)\n",
            srcPng.c_str());
        return 1;
    }
    std::string destLeaf = renameTo.empty()
                           ? fs::path(srcPng).filename().string()
                           : renameTo;
    // If the rename arg lacks an extension, append .png so
    // common typos ("stone" -> "stone.png") just work.
    if (fs::path(destLeaf).extension().string().empty()) {
        destLeaf += ".png";
    }
    std::string destPath = zoneDir + "/" + destLeaf;
    std::error_code ec;
    if (fs::exists(destPath)) {
        // Allow re-running if the bytes already match - makes
        // makefile-driven workflows idempotent.
        if (fs::file_size(srcPng, ec) == fs::file_size(destPath, ec)) {
            std::ifstream a(srcPng, std::ios::binary);
            std::ifstream b(destPath, std::ios::binary);
            std::stringstream sa, sb;
            sa << a.rdbuf(); sb << b.rdbuf();
            if (sa.str() == sb.str()) {
                std::printf("Already present: %s (no-op)\n",
                            destPath.c_str());
                return 0;
            }
        }
        std::fprintf(stderr,
            "add-texture-to-zone: %s already exists with different "
            "content (delete it first if intentional)\n",
            destPath.c_str());
        return 1;
    }
    fs::copy_file(srcPng, destPath, ec);
    if (ec) {
        std::fprintf(stderr,
            "add-texture-to-zone: copy failed (%s)\n",
            ec.message().c_str());
        return 1;
    }
    uint64_t bytes = fs::file_size(destPath, ec);
    std::printf("Imported %s -> %s\n",
                srcPng.c_str(), destPath.c_str());
    std::printf("  bytes : %llu\n",
                static_cast<unsigned long long>(bytes));
    std::printf("  next  : --add-texture-to-mesh <wom-base> %s\n",
                destPath.c_str());
    return 0;
}


}  // namespace

bool handleTextureHelpers(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--gen-texture") == 0 && i + 2 < argc) {
        outRc = handleGenTexture(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--add-texture-to-zone") == 0 && i + 2 < argc) {
        outRc = handleAddTextureToZone(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
