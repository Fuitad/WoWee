#include "cli_catalog_pluck.hpp"
#include "cli_arg_parse.hpp"
#include "cli_catalog_entry_key.hpp"
#include "cli_format_table.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Same shell-quoting helper as cli_bulk_validate - single
// quote and escape embedded single quotes.
std::string shellQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

bool peekMagic(const std::string& path, char magic[4]) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    if (!is.read(magic, 4) || is.gcount() != 4) return false;
    return true;
}

std::string normalizePathToBase(std::string base,
                                 const char* extension) {
    // Strip the format extension if present so subprocess
    // calls receive the bare base path the per-format
    // --info-wXXX handler expects.
    if (!extension || !*extension) return base;
    size_t extLen = std::strlen(extension);
    if (base.size() >= extLen &&
        base.compare(base.size() - extLen, extLen, extension) == 0) {
        base.resize(base.size() - extLen);
    }
    return base;
}

// Capture the full stdout of a child process invoked via
// popen. Returns the trimmed output string and the exit
// status. On platforms without WEXITSTATUS, treat any
// nonzero return as failure.
std::string runAndCapture(const std::string& cmd, int& outRc) {
    std::string buf;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        outRc = 127;
        return buf;
    }
    char chunk[4096];
    while (std::fgets(chunk, sizeof(chunk), pipe) != nullptr) {
        buf += chunk;
    }
    int rc = pclose(pipe);
#ifdef WEXITSTATUS
    if (rc != -1) {
        outRc = WEXITSTATUS(rc);
    } else {
        outRc = rc;
    }
#else
    outRc = rc;
#endif
    return buf;
}

// Walk a JSON entry object and find the value of its
// primary-key field. Convention: the primary key is the
// first field whose name ends in "Id" AND is NOT a known
// external-reference field. nlohmann::json iterates keys
// alphabetically, so we filter foreign keys before
// picking. Falls back to first numeric field if no *Id
// remains.

int handlePluck(int& i, int argc, char** argv) {
    if (i + 2 >= argc) {
        std::fprintf(stderr,
            "catalog-pluck: usage: --catalog-pluck "
            "<wXXX-file> <id> [--json]\n");
        return 1;
    }
    std::string fileArg = argv[++i];
    std::string idArg = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);

    // Parse search id as unsigned integer.
    uint64_t searchId = 0;
    try {
        searchId = std::stoull(idArg);
    } catch (...) {
        std::fprintf(stderr,
            "catalog-pluck: <id> must be a numeric literal "
            "(got '%s')\n", idArg.c_str());
        return 1;
    }

    // Read the magic. If file lookup fails directly, try
    // again after appending the format-table extension
    // matched by the leading 4 bytes of any sibling file.
    std::string filePath = fileArg;
    char magic[4]{};
    if (!peekMagic(filePath, magic)) {
        // Try common extensions: scan the format table
        // and attempt each ".wXXX" suffix.
        for (const FormatMagicEntry* row = formatTableBegin();
             row != formatTableEnd(); ++row) {
            std::string with = fileArg + row->extension;
            if (peekMagic(with, magic)) {
                filePath = with;
                break;
            }
        }
    }
    if (magic[0] == 0) {
        std::fprintf(stderr,
            "catalog-pluck: cannot read magic from '%s' "
            "(file not found?)\n", fileArg.c_str());
        return 1;
    }
    const FormatMagicEntry* fmt = findFormatByMagic(magic);
    if (!fmt) {
        std::fprintf(stderr,
            "catalog-pluck: unknown magic '%c%c%c%c' in '%s'\n",
            magic[0], magic[1], magic[2], magic[3],
            filePath.c_str());
        return 1;
    }
    if (!fmt->infoFlag) {
        std::fprintf(stderr,
            "catalog-pluck: format '%c%c%c%c' has no "
            "--info-* flag in the format table - pluck "
            "is only supported for catalogs with an "
            "--info-* surface\n",
            magic[0], magic[1], magic[2], magic[3]);
        return 1;
    }

    // Build the subprocess invocation: the same binary
    // (argv[0]) with the per-format inspect flag and JSON
    // output. Strip the extension so the inspect handler
    // sees the bare base path it expects.
    std::string base = normalizePathToBase(filePath, fmt->extension);
    std::string cmd = shellQuote(argv[0]) + " " +
                       fmt->infoFlag + " " +
                       shellQuote(base) + " --json 2>/dev/null";
    int rc = 0;
    std::string stdoutBuf = runAndCapture(cmd, rc);
    if (rc != 0 || stdoutBuf.empty()) {
        std::fprintf(stderr,
            "catalog-pluck: inspect subprocess for '%s' "
            "failed (rc=%d)\n", filePath.c_str(), rc);
        return 1;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(stdoutBuf);
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "catalog-pluck: failed to parse inspect output "
            "as JSON: %s\n", ex.what());
        return 1;
    }
    if (!doc.contains("entries") || !doc["entries"].is_array()) {
        std::fprintf(stderr,
            "catalog-pluck: inspect output has no "
            "'entries' array\n");
        return 1;
    }
    // Locate the entry whose primary-key field matches. The
    // format declares which field that is; see
    // cli_catalog_entry_key.hpp for why guessing it from the
    // field's name cannot work.
    const nlohmann::json* match = nullptr;
    std::string keyName;
    for (const auto& entry : doc["entries"]) {
        const auto pk = entryPrimaryKey(entry, fmt->primaryKey);
        const bool ok = pk.found;
        const uint64_t key = pk.value;
        const std::string& fieldName = pk.name;
        if (std::getenv("WOWEE_PLUCK_DEBUG") != nullptr) {
            std::fprintf(stderr,
                "[pluck-debug] entry: pkField=%s pkValue=%llu "
                "(target=%llu)\n",
                fieldName.c_str(),
                static_cast<unsigned long long>(key),
                static_cast<unsigned long long>(searchId));
        }
        if (ok && key == searchId) {
            match = &entry;
            keyName = fieldName;
            break;
        }
    }
    if (!match) {
        std::fprintf(stderr,
            "catalog-pluck: no entry with id %llu in '%s' "
            "(searched %zu entries)\n",
            static_cast<unsigned long long>(searchId),
            filePath.c_str(),
            doc["entries"].size());
        return 1;
    }
    if (jsonOut) {
        nlohmann::json out;
        out["file"] = filePath;
        out["magic"] = std::string(magic, 4);
        out["primaryKey"] = keyName;
        out["entry"] = *match;
        std::printf("%s\n", out.dump(2).c_str());
        return 0;
    }
    // Pretty terminal output.
    std::printf("catalog-pluck: %s\n", filePath.c_str());
    std::printf("  magic       : '%c%c%c%c'\n",
                magic[0], magic[1], magic[2], magic[3]);
    std::printf("  primaryKey  : %s = %llu\n",
                keyName.c_str(),
                static_cast<unsigned long long>(searchId));
    std::printf("  entry:\n");
    for (auto it = match->begin(); it != match->end(); ++it) {
        const std::string& k = it.key();
        const auto& v = it.value();
        std::string vs;
        if (v.is_string()) {
            vs = v.get<std::string>();
        } else if (v.is_number_integer()) {
            vs = std::to_string(v.get<long long>());
        } else if (v.is_number_float()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g",
                          v.get<double>());
            vs = buf;
        } else if (v.is_boolean()) {
            vs = v.get<bool>() ? "true" : "false";
        } else {
            vs = v.dump();
        }
        std::printf("    %-22s : %s\n", k.c_str(), vs.c_str());
    }
    return 0;
}

} // namespace

bool handleCatalogPluck(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--catalog-pluck") == 0 &&
        i + 2 < argc) {
        outRc = handlePluck(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
