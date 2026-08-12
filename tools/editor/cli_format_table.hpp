#pragma once

#include <cstddef>
#include <cstdint>

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

// Returns a pointer into the static table on match, nullptr
// otherwise. The 4-byte magic argument does NOT need to be
// null-terminated - only the first 4 bytes are inspected.
const FormatMagicEntry* findFormatByMagic(const char magic[4]);

// Iterate the table - used by --summary-dir to pre-allocate
// per-format counters keyed by index, and by tooling that
// wants to enumerate the full set.
const FormatMagicEntry* formatTableBegin();
const FormatMagicEntry* formatTableEnd();
size_t formatTableSize();

} // namespace cli
} // namespace editor
} // namespace wowee
