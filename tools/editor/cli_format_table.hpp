#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wowee {
namespace editor {
namespace cli {

// Shared table of every novel open format the editor
// recognizes - extracted so --info-magic and --summary-dir
// can both look up files by their 4-byte magic without
// drifting. Adding a new format requires appending one row
// in cli_format_table.cpp.
struct FormatMagicEntry {
    char magic[4];           // 4-char binary magic
    const char* extension;   // file suffix (with dot)
    const char* category;    // grouping label
    const char* infoFlag;    // --info-* flag, nullptr if none
    // The JSON key that holds an entry's own id, nullptr for
    // a format with no single one. The catalog searches used
    // to guess this from the field name and got it wrong for
    // 49 of 128 formats - see cli_catalog_entry_key.hpp.
    const char* primaryKey;
    const char* description;
};

// The magic suffix shared by every one of a format's flags.
//
// A format that has --info-wsrg also has --validate-wsrg, --export-wsrg-json
// and --import-wsrg-json: one suffix, four commands. Two bulk commands derived
// their own sibling flag from the table's --info flag and each spelled the
// rule out, so a row naming an --info flag that does not exist takes the bulk
// commands down with it - which is what happened to .wit, .wcrt, .wspn, .wlot
// and .wsnd, whose rows named a flag no handler answered.
//
// Empty for a format with no --info flag, and for a flag not of that shape.
std::string formatFlagSuffix(const char* infoFlag);

// Returns a pointer into the static table on match, nullptr
// otherwise. The 4-byte magic argument does NOT need to be
// null-terminated - only the first 4 bytes are inspected.
const FormatMagicEntry* findFormatByMagic(const char magic[4]);

// Look a format up by its file extension, including the dot and ignoring
// case: the table stores ".wsrg" and a file may be named "FOO.WSRG".
//
// Three commands carried a copy of this. --fix-magic compares the answer
// against the file's magic and proposes a rename when they disagree, so a
// wrong answer renames a file that was already correct.
//
// Returns nullptr for an extension no format claims, which includes the empty
// string a file without one produces.
const FormatMagicEntry* findFormatByExtension(const char* extension);

// Iterate the table - used by --summary-dir to pre-allocate
// per-format counters keyed by index, and by tooling that
// wants to enumerate the full set.
const FormatMagicEntry* formatTableBegin();
const FormatMagicEntry* formatTableEnd();
size_t formatTableSize();

} // namespace cli
} // namespace editor
} // namespace wowee
