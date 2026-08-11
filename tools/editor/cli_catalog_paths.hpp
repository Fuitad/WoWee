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
#include <string>

namespace wowee {
namespace editor {
namespace cli {

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

}  // namespace cli
}  // namespace editor
}  // namespace wowee
