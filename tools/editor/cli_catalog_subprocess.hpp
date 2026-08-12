#pragma once

/**
 * cli_catalog_subprocess.hpp - asking a format's own handler for its entries.
 *
 * The catalog searches do not parse .w* files. They re-invoke this same
 * executable with the format's --info flag and --json, and read the result:
 * one exporter per format, written once, rather than 128 parsers written
 * again in the search commands.
 *
 * Four commands did that, and each carried the same four steps - quote a path
 * for the shell, strip the extension the handler does not want, run the child
 * and capture its stdout, parse and check for an "entries" array. The pieces
 * were copied byte for byte in three of the four and reformatted in the
 * fourth.
 *
 * Every failure here is a file quietly skipped. A handler that exits nonzero,
 * output that is not JSON, and a document with no entries are all "there is
 * nothing to search in this file", which is correct for an asset format and
 * silent when it is caused by a wrong flag: seven table rows named a flag no
 * handler answers, and the item catalog was invisible to all four commands
 * without a word of complaint. The status below is so a caller that wants to
 * say which of those happened still can.
 */

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_format_table.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace wowee {
namespace editor {
namespace cli {

/// Wrap a string so a POSIX shell passes it through unchanged.
///
/// Single quotes, with the one character that cannot appear inside them
/// spliced in as '"'"'. Paths under Data are user-named and a directory such
/// as "Bob's zone" would otherwise end the quoting and run the rest as
/// commands.
inline std::string shellQuote(const std::string& s) {
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

/// The first four bytes of a file, which is its format magic.
///
/// False when the file cannot be opened or is shorter than four bytes; the
/// buffer is untouched in that case.
inline bool peekMagic(const std::filesystem::path& path, char magic[4]) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;
    char head[4] = {};
    const bool ok = std::fread(head, 1, 4, f) == 4;
    std::fclose(f);
    // Only on success: a two-byte file otherwise leaves two bytes of a magic
    // and two of whatever the caller's buffer held, which still compares
    // against the format table.
    if (ok) std::memcpy(magic, head, 4);
    return ok;
}

/// Drop the format's extension, because the --info handlers take a base path.
///
/// A path that does not end in that extension is returned unchanged, so this
/// is safe to call on anything.
inline std::string basePathFor(std::string path, const char* extension) {
    if (!extension || !*extension) return path;
    const size_t extLen = std::strlen(extension);
    if (path.size() >= extLen &&
        path.compare(path.size() - extLen, extLen, extension) == 0) {
        path.resize(path.size() - extLen);
    }
    return path;
}

/// Run a command and collect everything it writes to stdout.
///
/// `outRc` is the child's exit status, or 127 if it could not be started.
inline std::string runAndCapture(const std::string& cmd, int& outRc) {
    std::string buf;
    std::FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        outRc = 127;
        return buf;
    }
    char chunk[4096];
    while (std::fgets(chunk, sizeof(chunk), pipe) != nullptr) buf += chunk;
    const int rc = pclose(pipe);
#ifdef WEXITSTATUS
    outRc = (rc != -1) ? WEXITSTATUS(rc) : rc;
#else
    outRc = rc;
#endif
    return buf;
}

/// Why a file yielded no entries, for the one command that reports it.
enum class CatalogReadStatus {
    Ok,
    NoHandler,      ///< the format has no --info flag: an asset, not a catalog
    HandlerFailed,  ///< the child exited nonzero or printed nothing
    NotJson,        ///< output that would not parse
    NoEntries,      ///< parsed, but carries no "entries" array
};

struct CatalogRead {
    CatalogReadStatus status = CatalogReadStatus::NoHandler;
    nlohmann::json doc;  ///< the whole document, only when status is Ok
    int exitCode = 0;    ///< the handler's status, for HandlerFailed
    std::string error;   ///< what the JSON parser said, for NotJson

    bool ok() const { return status == CatalogReadStatus::Ok; }
    const nlohmann::json& entries() const { return doc["entries"]; }
};

/// Ask `fmt`'s own --info handler what is in `path`.
///
/// `self` is argv[0]: the searches re-invoke this executable rather than
/// linking the handlers, so a format's entries are read by the one piece of
/// code that already knows how.
inline CatalogRead readCatalogEntries(const std::string& self,
                                      const FormatMagicEntry& fmt,
                                      const std::filesystem::path& path) {
    CatalogRead out;
    if (!fmt.infoFlag) return out;

    const std::string cmd = shellQuote(self) + " " + fmt.infoFlag + " " +
                            shellQuote(basePathFor(path.string(),
                                                   fmt.extension)) +
                            " --json 2>/dev/null";
    int rc = 0;
    const std::string text = runAndCapture(cmd, rc);
    out.exitCode = rc;
    if (rc != 0 || text.empty()) {
        out.status = CatalogReadStatus::HandlerFailed;
        return out;
    }
    try {
        out.doc = nlohmann::json::parse(text);
    } catch (const std::exception& ex) {
        out.status = CatalogReadStatus::NotJson;
        out.error = ex.what();
        return out;
    }
    if (!out.doc.contains("entries") || !out.doc["entries"].is_array()) {
        out.status = CatalogReadStatus::NoEntries;
        return out;
    }
    out.status = CatalogReadStatus::Ok;
    return out;
}

} // namespace cli
} // namespace editor
} // namespace wowee
