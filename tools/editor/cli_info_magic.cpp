#include "cli_info_magic.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Read the 4-byte magic + 4-byte version + length-prefixed
// catalog name + 4-byte entry count from the standard
// header that every Wowee catalog format shares. Asset and
// world formats use different headers, so we report only
// magic+version for those.
struct StandardHeader {
    uint32_t version = 0;
    std::string catalogName;
    uint32_t entryCount = 0;
    bool hasCount = false;
};

bool readStandardHeader(std::ifstream& is, StandardHeader& out) {
    if (!is.read(reinterpret_cast<char*>(&out.version), 4)) return false;
    uint32_t nameLen = 0;
    if (!is.read(reinterpret_cast<char*>(&nameLen), 4)) return false;
    if (nameLen > (1u << 20)) return false;
    out.catalogName.resize(nameLen);
    if (nameLen > 0) {
        if (!is.read(out.catalogName.data(), nameLen)) return false;
    }
    if (!is.read(reinterpret_cast<char*>(&out.entryCount), 4)) {
        // No count - version+name only (older variants)
        return true;
    }
    out.hasCount = true;
    return true;
}

int handleMagic(int& i, int argc, char** argv) {
    std::string path = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        std::fprintf(stderr,
            "info-magic: cannot read %s\n", path.c_str());
        return 1;
    }
    char magic[4];
    if (!is.read(magic, 4) || is.gcount() != 4) {
        std::fprintf(stderr,
            "info-magic: file too short to read 4-byte magic: %s\n",
            path.c_str());
        return 1;
    }
    const FormatMagicEntry* entry = findFormatByMagic(magic);
    StandardHeader hdr;
    bool standardOk = false;
    // World/asset formats have non-standard headers - skip
    // the version+name+count probe for those (entry->infoFlag
    // is null for them).
    if (entry && entry->infoFlag != nullptr) {
        standardOk = readStandardHeader(is, hdr);
    }
    if (jsonOut) {
        nlohmann::json j;
        j["path"] = path;
        char magicStr[5] = {magic[0], magic[1], magic[2], magic[3], 0};
        j["magic"] = magicStr;
        if (entry) {
            j["recognized"] = true;
            j["extension"] = entry->extension;
            j["category"] = entry->category;
            j["description"] = entry->description;
            if (entry->infoFlag) j["infoFlag"] = entry->infoFlag;
            if (standardOk) {
                j["version"] = hdr.version;
                j["catalogName"] = hdr.catalogName;
                if (hdr.hasCount) j["entryCount"] = hdr.entryCount;
            }
        } else {
            j["recognized"] = false;
        }
        std::printf("%s\n", j.dump(2).c_str());
        return entry ? 0 : 1;
    }
    char magicStr[5] = {magic[0], magic[1], magic[2], magic[3], 0};
    if (!entry) {
        std::printf("info-magic: %s\n", path.c_str());
        std::printf("  magic       : '%s' (unrecognized)\n", magicStr);
        std::printf("  hint        : not a Wowee open format file\n");
        return 1;
    }
    std::printf("info-magic: %s\n", path.c_str());
    std::printf("  magic       : '%s'\n", magicStr);
    std::printf("  format      : %s (%s)\n", entry->description, entry->extension);
    std::printf("  category    : %s\n", entry->category);
    if (standardOk) {
        std::printf("  version     : %u\n", hdr.version);
        std::printf("  catalogName : %s\n", hdr.catalogName.c_str());
        if (hdr.hasCount) {
            std::printf("  entryCount  : %u\n", hdr.entryCount);
        }
    }
    if (entry->infoFlag) {
        std::printf("  inspect with: %s %s\n",
                    entry->infoFlag, path.c_str());
    } else {
        std::printf("  inspect with: (no --info-* flag - load via "
                     "engine or asset extractor)\n");
    }
    return 0;
}

} // namespace

bool handleInfoMagic(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--info-magic") == 0 && i + 1 < argc) {
        outRc = handleMagic(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
