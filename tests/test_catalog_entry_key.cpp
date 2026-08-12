// Which field of a catalog entry is that entry's id.
//
// Four commands search catalogs by id and each guessed the answer from the
// field's name: the first key ending in "Id" that was not in a hand-kept list
// of names meaning "a reference to another entry". The four lists had drifted
// to 51, 28, 18 and 22 names with none a superset of another, so the same
// entry answered to a different id depending on which command asked.
//
// No name-based list can be right. `familyId` is a foreign key on a creature
// and the primary key of a creature family; `guildId` is the key of a guild
// and a reference from a tabard. One of the two has to lose. Measured against
// the info handlers that emit these entries, the commands named a field other
// than the entry's own id for 49 of the 128 catalog formats - an item search
// matched displayId, a creature search matched familyId - and none of that
// reports an error, it just matches the wrong entries or none.
//
// The oracle is outside this code and outside the search commands: each
// format's own --info handler emits its entries, and the field it writes first
// is that format's statement of what identifies an entry. These tests read
// those handlers and hold the format table to what they say.
#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli_catalog_entry_key.hpp"
#include "cli_format_table.hpp"

using wowee::editor::cli::entryPrimaryKey;
using wowee::editor::cli::formatTableBegin;
using wowee::editor::cli::formatTableEnd;

namespace {

#ifdef WOWEE_SOURCE_DIR
const std::string kEditorDir = std::string(WOWEE_SOURCE_DIR) + "/tools/editor/";
#else
const std::string kEditorDir = "tools/editor/";
#endif

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Every cli_*.cpp in the editor, named once. A directory walk rather than a
// list, so a handler added tomorrow is checked without anyone remembering to
// add it here.
const std::vector<std::string>& editorSources() {
    static const std::vector<std::string> sources = [] {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(kEditorDir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("cli_", 0) == 0 &&
                entry.path().extension() == ".cpp") {
                out.push_back(name);
            }
        }
        return out;
    }();
    return sources;
}

// flag -> the text of every handler that answers it.
const std::map<std::string, std::vector<std::string>>& dispatchers() {
    static const std::map<std::string, std::vector<std::string>> map = [] {
        std::map<std::string, std::vector<std::string>> out;
        const std::regex pattern(
            R"RX(strcmp\(argv\[i\],\s*"(--info-[a-z0-9-]+)"\))RX");
        for (const std::string& name : editorSources()) {
            const std::string text = slurp(kEditorDir + name);
            if (text.empty()) continue;
            for (std::sregex_iterator it(text.begin(), text.end(), pattern), end;
                 it != end; ++it) {
                out[(*it)[1].str()].push_back(text);
            }
        }
        return out;
    }();
    return map;
}

// The first field a handler writes for one entry, which is that format's own
// statement of what identifies an entry.
//
// Handlers emit an entry one of two ways, and both start from the same loop:
//
//     for (const auto& e : c.entries) {
//         arr.push_back({{"emoteId", e.emoteId}, ...
//     for (const auto& e : c.entries) {
//         je["criteriaId"] = e.criteriaId; ...
//
// Returns empty for a handler whose entries are not emitted field by field.
std::string firstEmittedEntryField(const std::string& handler) {
    const std::regex loop(R"RX(for\s*\(\s*const auto&\s*(\w+)\s*:\s*\w+\.entries\s*\))RX");
    std::string best;
    size_t bestFields = 0;
    for (std::sregex_iterator it(handler.begin(), handler.end(), loop), end;
         it != end; ++it) {
        const std::string var = (*it)[1].str();
        std::string body = handler.substr((*it).position() + (*it).length(),
                                          4000);
        // The emit ends where the array is handed to the document.
        const size_t cut = body.find("[\"entries\"]");
        if (cut != std::string::npos) body = body.substr(0, cut);

        // Count the fields so a loop that only walks entries loses to the one
        // that writes them.
        const std::regex braced(R"RX(\{"([A-Za-z]+)",\s*)RX" + var + R"RX(\.)RX");
        const std::regex assigned(R"RX(\w+\["([A-Za-z]+)"\]\s*=\s*)RX" + var +
                                  R"RX(\.)RX");
        for (const std::regex& form : {braced, assigned}) {
            const std::sregex_iterator first(body.begin(), body.end(), form);
            const size_t count = std::distance(first, std::sregex_iterator());
            if (count > bestFields) {
                bestFields = count;
                best = (*first)[1].str();
            }
        }
    }
    return best;
}

}  // namespace

TEST_CASE("the test can read the editor sources it checks", "[catalog]") {
    // Without this the sweeps below pass by finding nothing, which is the one
    // way a source-reading test lies.
    REQUIRE(editorSources().size() > 100);
    REQUIRE(dispatchers().size() > 100);
}

TEST_CASE("every --info flag in the format table is answered by a handler",
          "[catalog]") {
    // Five rows named a flag no handler dispatches - --info-witm, and the
    // spawn, loot and sound equivalents - and two more named a flag belonging
    // to a different handler. Every command that shells out through this table
    // silently skipped those formats: the item catalog, the one anybody would
    // search first, was invisible to all of them. Nothing failed, because a
    // subprocess that exits nonzero is how the search skips an unreadable file.
    std::vector<std::string> unanswered;
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        if (!f->infoFlag) continue;
        if (!dispatchers().count(f->infoFlag)) unanswered.push_back(f->infoFlag);
    }
    INFO("flags no handler dispatches: " << unanswered.size());
    for (const auto& flag : unanswered) INFO("  " << flag);
    CHECK(unanswered.empty());
}

TEST_CASE("a declared primary key is the field its own handler writes first",
          "[catalog]") {
    // The column is only worth having if it cannot drift from the handler it
    // describes, so the check is against the emit order rather than merely
    // against the set of fields: creature entries carry a familyId, and a row
    // claiming that as the creature's own id has to fail.
    //
    // It also catches a row pointing at a different format's handler, which is
    // how --info-objects and --info-quests were wrong: those flags are
    // dispatched, just not by the handler for that file.
    //
    // Reordering an emitter is therefore a change to the table too. That is
    // the intended coupling: the first field written is the declaration, and
    // there is one of it.
    std::vector<std::string> mismatched;
    size_t declared = 0;
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        if (!f->infoFlag || !f->primaryKey) continue;
        ++declared;
        const auto it = dispatchers().find(f->infoFlag);
        if (it == dispatchers().end()) continue;  // reported by the test above
        bool agrees = false;
        for (const std::string& handler : it->second) {
            if (firstEmittedEntryField(handler) == f->primaryKey) agrees = true;
        }
        if (!agrees) {
            mismatched.push_back(std::string(f->infoFlag) + " declares " +
                                 f->primaryKey);
        }
    }
    CHECK(declared > 100);
    INFO("rows disagreeing with their handler: " << mismatched.size());
    for (const auto& row : mismatched) INFO("  " << row);
    CHECK(mismatched.empty());
}

TEST_CASE("no two formats share a magic or an --info flag", "[catalog]") {
    // findFormatByMagic returns the first row that matches, so a duplicate
    // magic is a row that can never be reached, and a duplicate flag would
    // make two formats shell out to the same handler.
    std::set<std::string> magics;
    std::set<std::string> flags;
    for (const auto* f = formatTableBegin(); f != formatTableEnd(); ++f) {
        INFO(f->description);
        CHECK(magics.insert(std::string(f->magic, 4)).second);
        if (f->infoFlag) CHECK(flags.insert(f->infoFlag).second);
    }
}

TEST_CASE("the declared key wins over a reference that sorts before it",
          "[catalog]") {
    // The shape of the whole bug. nlohmann::json iterates keys alphabetically,
    // so achievementId is reached before criteriaId and the guess returns the
    // achievement an entry belongs to as though it were the entry's own id.
    const nlohmann::json criteria{
        {"achievementId", 4321}, {"criteriaId", 99}, {"targetId", 7},
        {"name", "Kill ten rats"},
    };

    const auto declared = entryPrimaryKey(criteria, "criteriaId");
    CHECK(declared.found);
    CHECK(declared.value == 99);
    CHECK(declared.name == "criteriaId");

    // What the guess does with the same entry, and why the column exists: it
    // skips both names on its reference list and lands on targetId.
    const auto guessed = entryPrimaryKey(criteria, nullptr);
    CHECK(guessed.name == "targetId");
}

TEST_CASE("a declared key is used even when it names a reference elsewhere",
          "[catalog]") {
    // guildId is on the reference list because a tabard points at a guild with
    // it. The guild catalog uses it as its own key, and both have to work.
    const nlohmann::json guild{{"guildId", 12}, {"factionId", 67}};
    CHECK(entryPrimaryKey(guild, "guildId").value == 12);

    const nlohmann::json tabard{
        {"tabardId", 5}, {"emblemId", 3}, {"guildId", 12},
    };
    const auto pick = entryPrimaryKey(tabard, "tabardId");
    CHECK(pick.value == 5);
    CHECK(pick.name == "tabardId");
}

TEST_CASE("an entry missing its declared key falls back rather than failing",
          "[catalog]") {
    // A catalog written by an older version of the exporter, or an entry the
    // handler emits without the field. The guess is still worse than the
    // declaration, but it is better than reporting the entry as unidentifiable.
    const nlohmann::json entry{{"emoteId", 42}, {"animationId", 3}};
    const auto pick = entryPrimaryKey(entry, "missingId");
    CHECK(pick.found);
    CHECK(pick.name == "emoteId");
    CHECK(pick.value == 42);
}

TEST_CASE("the fallback prefers an id-shaped field to any other number",
          "[catalog]") {
    const nlohmann::json entry{{"level", 60}, {"rankId", 8}, {"name", "x"}};
    CHECK(entryPrimaryKey(entry, nullptr).name == "rankId");
}

TEST_CASE("only --catalog-find reaches for a field with no id in its name",
          "[catalog]") {
    // The last resort belongs to one command. --catalog-find reports something
    // for an entry with no id-shaped field at all; the other three skip it,
    // and that difference is deliberate rather than drift.
    const nlohmann::json entry{{"count", 3}, {"name", "x"}};
    CHECK_FALSE(entryPrimaryKey(entry, nullptr).found);

    const auto lastResort = entryPrimaryKey(entry, nullptr, true);
    CHECK(lastResort.found);
    CHECK(lastResort.name == "count");
    CHECK(lastResort.value == 3);
}

TEST_CASE("a non-integer field never answers for the id", "[catalog]") {
    // An id emitted as a string, or a float, is not something to compare a
    // numeric search against.
    const nlohmann::json entry{
        {"questId", "1234"}, {"weightId", 1.5}, {"zoneId", 12},
    };
    const auto pick = entryPrimaryKey(entry, "questId");
    CHECK(pick.found);
    CHECK(pick.name == "zoneId");  // the declared key was not an integer
    CHECK_FALSE(entryPrimaryKey(nlohmann::json::array(), "questId").found);
    CHECK_FALSE(entryPrimaryKey(nlohmann::json(7), "questId").found);
}
