#pragma once

/**
 * wowee_binary_io.hpp — the read and write primitives every .w* format uses.
 *
 * All fifty-odd formats share one layout: a four-byte magic, a version, a
 * length-prefixed name, an entry count, then records. Reading and writing that
 * comes down to four operations — a fixed-size value, a length-prefixed string,
 * and the same two backwards — plus the rule that a file's extension is added
 * if the caller left it off.
 *
 * Those four were copied into every format's .cpp, a hundred and forty-one
 * times, byte for byte. They are here now. A fix to any of them used to be a
 * hundred and forty-one edits, which in practice meant it was never a fix at
 * all: the one-megabyte cap on a string length is the only thing standing
 * between a corrupt file and a resize() that asks for four gigabytes, and if
 * that number ever needs to change it should change once.
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace wowee {
namespace pipeline {

/// Write a fixed-size value exactly as it sits in memory.
///
/// These files are not portable across endianness or padding, and never claimed
/// to be — they are written and read by this program on one machine.
template <typename T>
void writePOD(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

/// Read a fixed-size value. False means the file ended early.
template <typename T>
bool readPOD(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

/// Write a string as a 32-bit length followed by its bytes. No terminator.
inline void writeStr(std::ofstream& os, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    writePOD(os, n);
    if (n > 0) os.write(s.data(), n);
}

/// Read a length-prefixed string.
///
/// The length is the first thing a corrupt or hostile file gets to choose, so it
/// is capped at a megabyte: no name in any of these formats is remotely that
/// long, and without the cap a garbage length is a resize() of whatever four
/// bytes happened to be there.
inline bool readStr(std::ifstream& is, std::string& s) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > (1u << 20)) return false;  // 1 MiB sanity cap
    s.resize(n);
    if (n > 0) {
        is.read(s.data(), n);
        if (is.gcount() != static_cast<std::streamsize>(n)) {
            s.clear();
            return false;
        }
    }
    return true;
}

/// Write the header every .w* file starts with: magic, version, the catalog's
/// name, and how many entries follow.
inline void writeCatalogHeader(std::ofstream& os, const char magic[4], uint32_t version,
                               const std::string& name, uint32_t entryCount) {
    os.write(magic, 4);
    writePOD(os, version);
    writeStr(os, name);
    writePOD(os, entryCount);
}

/// Read that header back, and say whether this is the file it claims to be.
///
/// False means the magic is wrong — this is some other format, or not one of
/// ours at all — or the version is not the one this build reads, or the file
/// ended inside the header.
///
/// The entry count is capped for the same reason a string length is: it is the
/// next thing a corrupt file gets to choose, and it is about to become a
/// resize(). A million entries is far past anything real and far short of a
/// number that allocates the machine.
inline bool readCatalogHeader(std::ifstream& is, const char magic[4], uint32_t version,
                              std::string& name, uint32_t& entryCount) {
    char fileMagic[4];
    is.read(fileMagic, 4);
    if (is.gcount() != 4) return false;
    if (std::memcmp(fileMagic, magic, 4) != 0) return false;

    uint32_t fileVersion = 0;
    if (!readPOD(is, fileVersion) || fileVersion != version) return false;
    if (!readStr(is, name)) return false;

    entryCount = 0;
    if (!readPOD(is, entryCount)) return false;
    if (entryCount > (1u << 20)) return false;
    return true;
}

/// Add the format's extension unless the path already carries it.
///
/// Every format spelled this out with its own extension baked in, and with the
/// length of that extension written as a literal 5 — which is right for every
/// one of them and would be wrong the first time one is not four letters.
inline std::string normalizePath(std::string base, const std::string& extension) {
    if (base.size() < extension.size() ||
        base.compare(base.size() - extension.size(), extension.size(), extension) != 0) {
        base += extension;
    }
    return base;
}

}  // namespace pipeline
}  // namespace wowee
