#pragma once

/**
 * cli_paths.hpp - finding files of a format, and naming them the way the
 * loaders want.
 *
 * The editor's commands take a directory and work on every file of one format
 * under it. Eleven of them walked the tree themselves for .wom alone, each
 * repeating the same three steps: recurse, keep the regular files whose
 * extension matches, and cut the extension off because the loaders take a base
 * path and append it again.
 *
 * The walks were not identical. Some guarded the cut with a length check and
 * some did not, and some passed an error_code to the iterator while others let
 * it throw on a directory they could not read.
 *
 * None of them sorted. Directory order is the filesystem's own, which is
 * neither creation order nor alphabetical and differs between filesystems, so
 * a listing named its files in an order that no rule explains and that a
 * second machine would not reproduce. Every tie in a sort applied afterwards
 * inherited it: two meshes with the same triangle count came out in that
 * order, whatever it was.
 */

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

/// Drop a format's extension, because the loaders take a base path.
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

/// One file found under a scanned root.
struct FoundFile {
    std::filesystem::path path;  ///< the file as it was found
    std::string relative;        ///< its path below the scanned root
    std::string base;            ///< `path` without the extension, for a loader
    uint64_t bytes = 0;          ///< 0 when the size could not be read
};

/// Every regular file under `root` whose extension is `extension`, sorted by
/// the path relative to `root`.
///
/// Sorted because these lists are printed and compared: directory order is
/// the filesystem's, so the same tree listed on two machines came out in two
/// orders. An unreadable directory is skipped rather than throwing, which is
/// what a walk over content someone else wrote has to do.
inline std::vector<FoundFile> findFilesByExtension(
    const std::filesystem::path& root, const char* extension) {
    namespace fs = std::filesystem;
    std::vector<FoundFile> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;

    for (const fs::directory_entry& entry : it) {
        if (!entry.is_regular_file(ec) || ec) continue;
        if (entry.path().extension() != extension) continue;

        FoundFile found;
        found.path = entry.path();
        std::error_code relEc;
        found.relative = fs::relative(entry.path(), root, relEc).string();
        if (relEc) found.relative = entry.path().filename().string();
        found.base = basePathFor(entry.path().string(), extension);
        std::error_code sizeEc;
        const auto size = entry.file_size(sizeEc);
        found.bytes = sizeEc ? 0 : static_cast<uint64_t>(size);
        out.push_back(std::move(found));
    }
    std::sort(out.begin(), out.end(),
              [](const FoundFile& a, const FoundFile& b) {
                  return a.relative < b.relative;
              });
    return out;
}

} // namespace cli
} // namespace editor
} // namespace wowee
