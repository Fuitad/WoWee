#pragma once

#include "stb_image_write.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

// Parse "RRGGBB" (6 hex chars), "RGB" (3 hex chars), or with a "#"
// prefix into 8-bit channel components. Returns false on malformed
// input. Used by every --gen-texture-* handler that takes color
// arguments. Was a static helper inside cli_gen_texture.cpp before
// being hoisted here so other texture-side modules can use it.
inline bool parseHex(std::string hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    std::transform(hex.begin(), hex.end(), hex.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
    auto fromHexC = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        return -1;
    };
    int v[6];
    if (hex.size() == 6) {
        for (int k = 0; k < 6; ++k) {
            v[k] = fromHexC(hex[k]);
            if (v[k] < 0) return false;
        }
        r = static_cast<uint8_t>((v[0] << 4) | v[1]);
        g = static_cast<uint8_t>((v[2] << 4) | v[3]);
        b = static_cast<uint8_t>((v[4] << 4) | v[5]);
        return true;
    }
    if (hex.size() == 3) {
        for (int k = 0; k < 3; ++k) {
            v[k] = fromHexC(hex[k]);
            if (v[k] < 0) return false;
        }
        r = static_cast<uint8_t>((v[0] << 4) | v[0]);
        g = static_cast<uint8_t>((v[1] << 4) | v[1]);
        b = static_cast<uint8_t>((v[2] << 4) | v[2]);
        return true;
    }
    return false;
}

// parseHex + canonical "invalid hex" error message, used by 81+
// sites in cli_gen_texture.cpp. Returns true on success so callers
// do `if (!parseHexOrError(hex, r, g, b, "cmd")) return 1;`.
inline bool parseHexOrError(const std::string& hex,
                            uint8_t& r, uint8_t& g, uint8_t& b,
                            const char* cmdName) {
    if (parseHex(hex, r, g, b)) return true;
    std::fprintf(stderr, "%s: '%s' is not a valid hex color\n",
                 cmdName, hex.c_str());
    return false;
}

// Print the canonical "Wrote <path>" + "  size : WxH" pair shown
// at the start of every --gen-texture-* handler's success report.
// 64 sites used the same two lines (with minor whitespace variation
// that this helper normalizes to one consistent label width).
inline void printPngWrote(const std::string& outPath, int W, int H) {
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  size       : %dx%d\n", W, H);
}

// Inline pixel-write helper for the inner loops of procedural
// texture handlers. 30 sites in cli_gen_texture.cpp open-coded
// the same 4-line index-and-write block. Header-inline because
// the procedural handlers call this once per pixel - the
// abstraction shouldn't cost a function-call frame.
inline void setPixelRGB(std::vector<uint8_t>& pixels, int W,
                        int x, int y,
                        uint8_t r, uint8_t g, uint8_t b) {
    std::size_t idx = (static_cast<std::size_t>(y) * W + x) * 3;
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;
}

// Shared PNG-write wrapper used by every --gen-texture-* handler.
// Calls stbi_write_png with the standard RGB/24-bit/W*3-stride
// arguments and reports a stderr message on failure. Returns true
// on success so the caller can do
// `if (!savePngOrError(...)) return 1;`.
//
// 58 sites in cli_gen_texture.cpp open-coded the same 5-line
// "if write fails, fprintf stderr and return 1" block before
// extraction.
inline bool savePngOrError(const std::string& outPath, int W, int H,
                           const std::vector<uint8_t>& pixels,
                           const char* cmdName) {
    if (stbi_write_png(outPath.c_str(), W, H, 3,
                       pixels.data(), W * 3)) {
        return true;
    }
    std::fprintf(stderr, "%s: stbi_write_png failed for %s\n",
                 cmdName, outPath.c_str());
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
