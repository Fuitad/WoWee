// Finding the files of one format under a directory.
//
// Most editor commands take a directory and work on every file of a format
// under it. Eleven walked the tree themselves for .wom alone, each repeating
// recurse, match the extension, and cut the extension off because the loaders
// take a base path and append it again.
//
// The copies had drifted in ways that only show at the edges: some guarded the
// cut with a length check and some did not, and some handed the iterator an
// error_code while others let it throw on a directory they could not read.
// None of them sorted, so a listing named its files in the filesystem's own
// order - neither creation order nor alphabetical, and not the same on another
// machine - and every tie in a later sort inherited it.
//
// The oracle is a directory built by the test rather than the old code: the
// files that should be found are the ones it wrote.
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cli_paths.hpp"

using wowee::editor::cli::basePathFor;
using wowee::editor::cli::findFilesByExtension;

namespace {

namespace fs = std::filesystem;

// A tree written fresh for each test, so nothing depends on the order the
// filesystem happens to return.
fs::path buildTree(const std::string& name,
                   const std::vector<std::string>& relativePaths) {
    const fs::path root = fs::temp_directory_path() / ("wowee_cli_paths_" + name);
    fs::remove_all(root);
    for (const std::string& rel : relativePaths) {
        const fs::path file = root / rel;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << "WOM3";
    }
    fs::create_directories(root);
    return root;
}

std::vector<std::string> relativeNames(const fs::path& root, const char* ext) {
    std::vector<std::string> out;
    for (const auto& found : findFilesByExtension(root, ext)) {
        out.push_back(found.relative);
    }
    return out;
}

}  // namespace

TEST_CASE("the extension the loader does not want is dropped", "[cli-paths]") {
    // The loaders take a base path and append the extension themselves, so a
    // command walking real files has to remove it.
    CHECK(basePathFor("/d/stock.wit", ".wit") == "/d/stock");
    CHECK(basePathFor("stock.wit", ".wit") == "stock");

    // A path that does not end in that extension is left alone, which is what
    // makes this safe to call on every file a walk turns up.
    CHECK(basePathFor("/d/stock.wcrt", ".wit") == "/d/stock.wcrt");
    CHECK(basePathFor("/d/stock", ".wit") == "/d/stock");

    // Only the suffix, never a match in the middle of the name.
    CHECK(basePathFor("/d/.wit/stock", ".wit") == "/d/.wit/stock");

    // A name that is nothing but the extension is still a name.
    CHECK(basePathFor(".wit", ".wit").empty());

    // No extension declared: asset formats have one, but a row may not.
    CHECK(basePathFor("/d/stock.wit", nullptr) == "/d/stock.wit");
    CHECK(basePathFor("/d/stock.wit", "") == "/d/stock.wit");
}

TEST_CASE("every matching file under the root is found, at any depth",
          "[cli-paths]") {
    const auto root = buildTree("depth", {
        "a.wom",
        "meshes/b.wom",
        "meshes/props/c.wom",
        "meshes/props/deep/d.wom",
    });
    const auto found = findFilesByExtension(root, ".wom");
    REQUIRE(found.size() == 4);
    CHECK(relativeNames(root, ".wom") ==
          std::vector<std::string>{"a.wom", "meshes/b.wom", "meshes/props/c.wom",
                                   "meshes/props/deep/d.wom"});
}

TEST_CASE("the extension is matched as a suffix, not as a substring",
          "[cli-paths]") {
    // .wom.json files sit beside the meshes after an export, and a directory
    // can be named for a format too. Neither is a mesh.
    const auto root = buildTree("suffix", {
        "keep.wom",
        "skip.wom.json",
        "skip.womx",
        "skip.wob",
        "wom/inside.wob",
        "notes.wom.bak",
    });
    CHECK(relativeNames(root, ".wom") == std::vector<std::string>{"keep.wom"});
}

TEST_CASE("the order is the same on every run", "[cli-paths]") {
    // Directory order is whatever the filesystem returns and differs between
    // machines, so an audit run on two of them cannot be diffed. Sorting here
    // also settles ties in the sorts callers apply afterwards: two meshes with
    // the same triangle count used to come out in the filesystem's order.
    const auto root = buildTree("order", {
        "zzz.wom", "aaa.wom", "mmm.wom", "sub/aaa.wom", "sub/zzz.wom",
    });
    const auto first = relativeNames(root, ".wom");
    CHECK(std::is_sorted(first.begin(), first.end()));
    CHECK(first == relativeNames(root, ".wom"));
}

TEST_CASE("each file carries the base path a loader wants", "[cli-paths]") {
    const auto root = buildTree("base", {"meshes/tree.wom"});
    const auto found = findFilesByExtension(root, ".wom");
    REQUIRE(found.size() == 1);
    CHECK(found[0].relative == "meshes/tree.wom");
    CHECK(found[0].base == (root / "meshes/tree").string());
    CHECK(found[0].bytes == 4);
}

TEST_CASE("a root that is not there is empty, not an error", "[cli-paths]") {
    // Commands are given a path by whoever ran them. Throwing out of the walk
    // takes down a whole audit for one bad argument.
    const auto missing = fs::temp_directory_path() / "wowee_cli_paths_absent";
    fs::remove_all(missing);
    CHECK(findFilesByExtension(missing, ".wom").empty());
}

TEST_CASE("a root with nothing of that format is empty", "[cli-paths]") {
    const auto root = buildTree("none", {"a.wob", "b.wot"});
    CHECK(findFilesByExtension(root, ".wom").empty());
}

TEST_CASE("a file given as the root is not a tree", "[cli-paths]") {
    const auto root = buildTree("filearg", {"a.wom"});
    CHECK(findFilesByExtension(root / "a.wom", ".wom").empty());
}
