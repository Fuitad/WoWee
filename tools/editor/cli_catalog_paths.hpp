#pragma once

/**
 * cli_catalog_paths.hpp — turning a path the user typed into the base name a
 * catalog writes.
 *
 * The formats are stored as "<base><extension>" and their JSON sidecars as
 * "<base><extension>.json", and a user may type either, or neither, or the base
 * on its own. Working back to the base was written out in every import handler,
 * in two different spellings that had to agree — and one of them measured the
 * suffix with a hardcoded 10, which is the length of ".wxxx.json" and would be
 * wrong for any extension that is not four letters.
 */

#include <cstdio>
#include <filesystem>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

/// Make sure the directory an output file is about to be written into exists.
///
/// std::filesystem::create_directories("") throws — "Invalid argument" — and a
/// bare filename has an empty parent path. So
///
///     wowee_editor --gen-mesh-altar altar
///
/// terminated on an uncaught exception and dumped core, while the same command
/// with "./altar" worked. Twenty-five of the mesh generators had exactly that
/// line, and none of them was inside a try.
///
/// The error_code overload is used deliberately: a directory that cannot be
/// made is a thing to report when the write fails, which it is about to, and
/// not a reason to abort the process before the message can be printed.
inline void ensureParentDirectory(const std::filesystem::path& file) {
    const std::filesystem::path parent = file.parent_path();
    if (parent.empty()) return;  // writing into the working directory
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
}

/// `base` without a trailing `extension`, if it has one.
inline std::string withoutExt(std::string base, const std::string& extension) {
    if (base.size() >= extension.size() &&
        base.compare(base.size() - extension.size(), extension.size(), extension) == 0) {
        base.resize(base.size() - extension.size());
    }
    return base;
}

/// The base name a JSON sidecar belongs to.
///
/// "zones.wgfs.json" and "zones.wgfs" and "zones" all answer "zones". The
/// combined suffix is tried first: stripping ".json" and ".wgfs" separately
/// gets there too, but only if both are present in that order, and a file named
/// "zones.json" would otherwise keep a ".wgfs" that was never there.
inline std::string baseFromJsonPath(std::string path, const std::string& extension) {
    const std::string combined = extension + ".json";
    if (path.size() >= combined.size() &&
        path.compare(path.size() - combined.size(), combined.size(), combined) == 0) {
        path.resize(path.size() - combined.size());
        return path;
    }
    return withoutExt(withoutExt(std::move(path), ".json"), extension);
}

/// Save a catalog, or say on stderr which command failed and to what path.
///
/// Every one of the 139 format handlers defined this as a file-local function
/// with its own types and its own extension baked into the message. The
/// extension is the only thing that varied, and it is derivable — a catalog is
/// always written to "<base><extension>".
template <typename Loader, typename Catalog>
bool saveOrError(const Catalog& cat, const std::string& base, const char* cmd,
                 const char* extension) {
    if (Loader::save(cat, base)) return true;
    std::fprintf(stderr, "%s: failed to save %s%s\n", cmd, base.c_str(), extension);
    return false;
}

/// The same refusal with no command in front of it, which is how the --info-*
/// handlers word theirs. Kept as its own overload rather than folded into the
/// one below so the text each handler already printed is unchanged.
inline int reportMissing(const char* tag, const std::string& base, const char* extension) {
    std::fprintf(stderr, "%s not found: %s%s\n", tag, base.c_str(), extension);
    return 1;
}

/// Report a catalog that is not there, and answer the process exit code.
///
/// Written out at each of the 153 places a handler checks whether the file it
/// was asked about exists. `tag` is the format's own name for itself in a
/// message — "WSMC" — and the path is always "<base><extension>".
inline int reportMissing(const char* cmd, const char* tag, const std::string& base,
                         const char* extension) {
    std::fprintf(stderr, "%s: %s not found: %s%s\n", cmd, tag, base.c_str(), extension);
    return 1;
}

}  // namespace cli
}  // namespace editor
}  // namespace wowee
