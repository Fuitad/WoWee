// Finding a format by its file extension.
//
// Three commands carried a byte-identical copy of this, one of them with a
// comment saying so. --fix-magic reads a file's magic, looks its extension up
// here, and proposes a rename when the two disagree, so a lookup that answers
// wrongly renames a file that was already right or leaves a mismatched one
// alone. --audit-tree and --list-orphan-jsons decide from it whether a file is
// a known format at all.
//
// The oracle is the format table itself: every row names an extension, and
// looking that extension up has to come back to the same row.
#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "cli_format_table.hpp"

using wowee::editor::cli::findFormatByExtension;
using wowee::editor::cli::findFormatByMagic;
using wowee::editor::cli::formatTableBegin;
using wowee::editor::cli::formatTableEnd;

TEST_CASE("every extension in the table finds its own row", "[format-table]") {
    // The round trip, over all 146 rows. A row whose extension finds a
    // different row is a format no command can recognise by name.
    size_t checked = 0;
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        INFO(f->description << " (" << f->extension << ")");
        REQUIRE(f->extension != nullptr);
        CHECK(findFormatByExtension(f->extension) == f);
        ++checked;
    }
    CHECK(checked > 100);
}

TEST_CASE("no two formats claim the same extension", "[format-table]") {
    // What makes the lookup well defined: it returns the first match, so a
    // second row with the same extension could never be reached by name.
    std::set<std::string> extensions;
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        INFO(f->description);
        CHECK(extensions.insert(f->extension).second);
    }
}

TEST_CASE("a renamed file in capitals is still its format", "[format-table]") {
    // The table stores lowercase. A file that has been through a system that
    // upcases names, or that someone renamed by hand, is the same file.
    CHECK(findFormatByExtension(".WOM") == findFormatByExtension(".wom"));
    CHECK(findFormatByExtension(".WsRg") == findFormatByExtension(".wsrg"));
    REQUIRE(findFormatByExtension(".WOM") != nullptr);
}

TEST_CASE("the match is the whole extension, not a prefix", "[format-table]") {
    // .wom and .womx are both in the table, so a compare that stopped at the
    // shorter of the two would answer .wom for a .womx file and --fix-magic
    // would propose renaming it.
    const auto* wom = findFormatByExtension(".wom");
    const auto* womx = findFormatByExtension(".womx");
    REQUIRE(wom != nullptr);
    REQUIRE(womx != nullptr);
    CHECK(wom != womx);

    CHECK(findFormatByExtension(".wo") == nullptr);
    CHECK(findFormatByExtension(".womxx") == nullptr);
}

TEST_CASE("an extension no format claims is not a format", "[format-table]") {
    CHECK(findFormatByExtension(".json") == nullptr);
    CHECK(findFormatByExtension(".png") == nullptr);
    CHECK(findFormatByExtension("") == nullptr);
    CHECK(findFormatByExtension(".") == nullptr);
    // A file with no extension at all: path::extension() returns empty, and
    // the walkers hand that straight in.
    CHECK(findFormatByExtension("wom") == nullptr);  // the dot is part of it
}

TEST_CASE("a format's extension and magic name the same row",
          "[format-table]") {
    // The two lookups are how --fix-magic decides a file is misnamed, so they
    // have to agree about every format or it proposes renames forever.
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        INFO(f->description);
        CHECK(findFormatByMagic(f->magic) == f);
        CHECK(findFormatByExtension(f->extension) == f);
    }
}

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kPipelineDir = std::string(WOWEE_SOURCE_DIR) + "/src/pipeline/";
#else
const std::string kPipelineDir = "src/pipeline/";
#endif

// Each format states its own magic and extension in src/pipeline, next to the
// code that reads and writes the file. That is the outside answer the table
// has to agree with.
struct DeclaredFormat {
    std::string file;
    std::string magic;
    std::string extension;
};

std::vector<DeclaredFormat> declaredFormats() {
    std::vector<DeclaredFormat> out;
    std::error_code ec;
    const std::regex magicRe(
        R"RX(constexpr char kMagic\[4\]\s*=\s*\{\s*'(.)',\s*'(.)',\s*'(.)',\s*'(.)'\s*\})RX");
    const std::regex extRe(R"RX(constexpr char kExtension\[\]\s*=\s*"([^"]+)")RX");
    for (const auto& entry :
         std::filesystem::directory_iterator(kPipelineDir, ec)) {
        if (entry.path().extension() != ".cpp") continue;
        std::ifstream in(entry.path());
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        std::smatch m, e;
        if (!std::regex_search(text, m, magicRe)) continue;
        if (!std::regex_search(text, e, extRe)) continue;
        out.push_back({entry.path().filename().string(),
                       m[1].str() + m[2].str() + m[3].str() + m[4].str(),
                       e[1].str()});
    }
    return out;
}

}  // namespace

TEST_CASE("the table names the extension each format actually writes",
          "[format-table]") {
    // Three rows did not. The loaders wrote .wol, .womx and .wow while the
    // table said .wola, .wmpx and .wowa, so every command that finds a file by
    // magic and then strips the table's extension to get a base path came away
    // with the extension still attached, and the per-format handler it invoked
    // looked for a file with the extension twice over.
    //
    // Nothing reported an error. --bulk-validate counted a real .womx as
    // "skipped (no validator)" while --validate-womx on the same file worked
    // and had a warning to report.
    const auto formats = declaredFormats();
    REQUIRE(formats.size() > 100);

    std::vector<std::string> disagree;
    for (const auto& declared : formats) {
        const auto* row = findFormatByMagic(declared.magic.c_str());
        if (!row) {
            disagree.push_back(declared.file + ": magic " + declared.magic +
                               " is in no row");
            continue;
        }
        // WoweeWeather writes ".wow", which the per-zone world manifest row
        // claims. Naming it here too would make one of the two unreachable by
        // extension, so the row still says ".wowa" and this is the one
        // disagreement the table knows about.
        if (declared.magic == "WOWA") continue;
        if (declared.extension != row->extension) {
            disagree.push_back(declared.file + ": writes " + declared.extension +
                               ", table says " + row->extension);
        }
    }
    INFO("rows disagreeing with their format: " << disagree.size());
    for (const auto& row : disagree) INFO("  " << row);
    CHECK(disagree.empty());
}
