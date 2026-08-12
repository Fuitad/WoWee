// How the catalog searches reach a file's contents.
//
// They do not parse .w* files. They re-invoke this executable with the
// format's --info flag and read the JSON back, so the one exporter that
// already knows each format is the only thing that reads it. Four commands
// carried the same four steps to do that, copied byte for byte in three of
// them and reformatted in the fourth.
//
// Every failure on that path is a file silently skipped, which is correct for
// an asset format and invisible when it is caused by something else: seven
// rows of the format table named an --info flag no handler answers, and the
// item catalog was unsearchable by all four commands without a word of
// complaint.
//
// The oracle for the quoting is POSIX sh itself, which the test runs.
#include <catch_amalgamated.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "cli_catalog_subprocess.hpp"

using wowee::editor::cli::peekMagic;
using wowee::editor::cli::runAndCapture;
using wowee::editor::cli::shellQuote;

namespace {

// What a POSIX shell actually passes to a child for this argument. The point
// of shellQuote is that this is the identity function.
std::string throughTheShell(const std::string& argument) {
    int rc = -1;
    std::string out =
        runAndCapture("printf %s " + shellQuote(argument), rc);
    CHECK(rc == 0);
    return out;
}

std::filesystem::path scratchDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "wowee_catalog_subprocess_test";
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("a quoted argument reaches the child unchanged", "[catalog]") {
    // Data paths are named by whoever made the zone.
    CHECK(throughTheShell("plain") == "plain");
    CHECK(throughTheShell("with space") == "with space");
    CHECK(throughTheShell("Bob's zone") == "Bob's zone");
    CHECK(throughTheShell("quote\"and'both") == "quote\"and'both");
    CHECK(throughTheShell("./Data/terrain/azeroth_32_48.whm") ==
          "./Data/terrain/azeroth_32_48.whm");
}

TEST_CASE("shell metacharacters are data, not syntax", "[catalog]") {
    // The command is handed to popen, so an unquoted path containing any of
    // these would run as a command. Nothing here is exotic in a filename.
    CHECK(throughTheShell("a;b") == "a;b");
    CHECK(throughTheShell("$(echo no)") == "$(echo no)");
    CHECK(throughTheShell("`echo no`") == "`echo no`");
    CHECK(throughTheShell("a|b") == "a|b");
    CHECK(throughTheShell("a&b") == "a&b");
    CHECK(throughTheShell("*") == "*");
    CHECK(throughTheShell("~") == "~");
    CHECK(throughTheShell("a\nb") == "a\nb");
}

TEST_CASE("the magic is the first four bytes", "[catalog]") {
    const auto path = scratchDir() / "sample.wit";
    {
        std::ofstream out(path, std::ios::binary);
        out << "WITM\x01\x00\x00\x00";
    }
    char magic[4] = {};
    REQUIRE(peekMagic(path, magic));
    CHECK(std::string(magic, 4) == "WITM");
}

TEST_CASE("a file too short to have a magic is not one", "[catalog]") {
    // An empty or truncated file must answer false rather than leave the
    // caller comparing whatever was on the stack against the format table.
    const auto dir = scratchDir();
    const auto empty = dir / "empty.wit";
    { std::ofstream out(empty, std::ios::binary); }
    const auto stub = dir / "stub.wit";
    {
        std::ofstream out(stub, std::ios::binary);
        out << "WI";
    }

    char magic[4] = {'z', 'z', 'z', 'z'};
    CHECK_FALSE(peekMagic(empty, magic));
    CHECK_FALSE(peekMagic(stub, magic));
    CHECK_FALSE(peekMagic(dir / "no_such_file.wit", magic));
    CHECK(std::string(magic, 4) == "zzzz");
}

TEST_CASE("a child that fails is reported as failing", "[catalog]") {
    // Every caller treats a nonzero status as "skip this file", so a command
    // that cannot run must not look like one that produced nothing to search.
    int rc = -1;
    CHECK(runAndCapture("exit 3", rc).empty());
    CHECK(rc == 3);

    rc = -1;
    const std::string text = runAndCapture("printf 'a\\nb\\n'", rc);
    CHECK(rc == 0);
    CHECK(text == "a\nb\n");
}

TEST_CASE("output longer than one read is captured whole", "[catalog]") {
    // A catalog's JSON runs to hundreds of kilobytes and the capture buffer is
    // four. Truncated output parses as invalid JSON and the file is skipped,
    // which reads as an empty catalog rather than an error.
    int rc = -1;
    const std::string text =
        runAndCapture("printf 'x%.0s' $(seq 1 20000)", rc);
    CHECK(rc == 0);
    CHECK(text.size() == 20000);
}
