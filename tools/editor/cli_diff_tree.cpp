#include "cli_diff_tree.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"
#include "cli_paths.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

struct FileInfo {
    char magic[4] = {0, 0, 0, 0};
    bool magicOk = false;
    uintmax_t size = 0;
    const FormatMagicEntry* fmt = nullptr;
};

// Walk a directory and build relativePath -> FileInfo for
// every Wowee-recognized file. Files whose magic isn't in
// the format table are skipped (so unrelated junk in the
// tree doesn't pollute the diff).
std::map<std::string, FileInfo> indexTree(const fs::path& root) {
    std::map<std::string, FileInfo> out;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        FileInfo fi;
        fi.size = entry.file_size(ec);
        if (ec) { ec.clear(); continue; }
        if (peekMagic(entry.path(), fi.magic)) {
            fi.fmt = findFormatByMagic(fi.magic);
            fi.magicOk = (fi.fmt != nullptr);
        }
        if (!fi.magicOk) continue;     // skip non-Wowee
        std::string rel = fs::relative(entry.path(), root, ec).string();
        if (ec) { ec.clear(); continue; }
        out[rel] = fi;
    }
    return out;
}

enum class ChangeKind {
    OnlyInA,       // file present in A, missing from B
    OnlyInB,       // file present in B, missing from A
    MagicChanged,  // present in both but different magic
    SizeChanged,   // same magic, different size
    Identical,     // same magic, same size (good enough as a
                   // first-cut heuristic - true byte-equal
                   // takes a hash that we don't bother with)
};

struct DiffRow {
    std::string path;
    ChangeKind kind;
    const FormatMagicEntry* fmtA = nullptr;
    const FormatMagicEntry* fmtB = nullptr;
    uintmax_t sizeA = 0;
    uintmax_t sizeB = 0;
};

const char* changeKindLabel(ChangeKind k) {
    switch (k) {
        case ChangeKind::OnlyInA:      return "only-in-A";
        case ChangeKind::OnlyInB:      return "only-in-B";
        case ChangeKind::MagicChanged: return "magic-changed";
        case ChangeKind::SizeChanged:  return "size-changed";
        case ChangeKind::Identical:    return "identical";
    }
    return "?";
}

int handleDiff(int& i, int argc, char** argv) {
    std::string dirA = argv[++i];
    std::string dirB = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dirA) || !fs::is_directory(dirA)) {
        std::fprintf(stderr,
            "diff-tree: not a directory: %s\n", dirA.c_str());
        return 1;
    }
    if (!fs::exists(dirB) || !fs::is_directory(dirB)) {
        std::fprintf(stderr,
            "diff-tree: not a directory: %s\n", dirB.c_str());
        return 1;
    }
    auto idxA = indexTree(dirA);
    auto idxB = indexTree(dirB);
    std::vector<DiffRow> rows;
    // Walk A's keys: each is either OnlyInA or present in
    // both (which becomes MagicChanged / SizeChanged /
    // Identical depending on the comparison).
    for (const auto& [path, fa] : idxA) {
        auto it = idxB.find(path);
        if (it == idxB.end()) {
            DiffRow r;
            r.path = path;
            r.kind = ChangeKind::OnlyInA;
            r.fmtA = fa.fmt;
            r.sizeA = fa.size;
            rows.push_back(std::move(r));
            continue;
        }
        const FileInfo& fb = it->second;
        DiffRow r;
        r.path = path;
        r.fmtA = fa.fmt;
        r.fmtB = fb.fmt;
        r.sizeA = fa.size;
        r.sizeB = fb.size;
        if (fa.fmt != fb.fmt) r.kind = ChangeKind::MagicChanged;
        else if (fa.size != fb.size) r.kind = ChangeKind::SizeChanged;
        else r.kind = ChangeKind::Identical;
        rows.push_back(std::move(r));
    }
    // Now walk B's keys looking for OnlyInB.
    for (const auto& [path, fb] : idxB) {
        if (idxA.find(path) != idxA.end()) continue;
        DiffRow r;
        r.path = path;
        r.kind = ChangeKind::OnlyInB;
        r.fmtB = fb.fmt;
        r.sizeB = fb.size;
        rows.push_back(std::move(r));
    }
    size_t onlyA = 0, onlyB = 0, magicCh = 0, sizeCh = 0, identical = 0;
    for (const auto& r : rows) {
        switch (r.kind) {
            case ChangeKind::OnlyInA:      ++onlyA; break;
            case ChangeKind::OnlyInB:      ++onlyB; break;
            case ChangeKind::MagicChanged: ++magicCh; break;
            case ChangeKind::SizeChanged:  ++sizeCh; break;
            case ChangeKind::Identical:    ++identical; break;
        }
    }
    bool anyDiff = (onlyA + onlyB + magicCh + sizeCh) > 0;
    if (jsonOut) {
        nlohmann::json j;
        j["dirA"] = dirA;
        j["dirB"] = dirB;
        j["countA"] = idxA.size();
        j["countB"] = idxB.size();
        j["onlyInA"] = onlyA;
        j["onlyInB"] = onlyB;
        j["magicChanged"] = magicCh;
        j["sizeChanged"] = sizeCh;
        j["identical"] = identical;
        j["allIdentical"] = !anyDiff;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rows) {
            if (r.kind == ChangeKind::Identical) continue;
            nlohmann::json je;
            je["path"] = r.path;
            je["kind"] = changeKindLabel(r.kind);
            if (r.fmtA) je["formatA"] = r.fmtA->extension;
            if (r.fmtB) je["formatB"] = r.fmtB->extension;
            je["sizeA"] = r.sizeA;
            je["sizeB"] = r.sizeB;
            arr.push_back(je);
        }
        j["differences"] = arr;
        std::printf("%s\n", j.dump(2).c_str());
        return anyDiff ? 1 : 0;
    }
    std::printf("diff-tree: %s vs %s\n", dirA.c_str(), dirB.c_str());
    std::printf("  files in A      : %zu\n", idxA.size());
    std::printf("  files in B      : %zu\n", idxB.size());
    std::printf("  only-in-A       : %zu\n", onlyA);
    std::printf("  only-in-B       : %zu\n", onlyB);
    std::printf("  magic-changed   : %zu\n", magicCh);
    std::printf("  size-changed    : %zu\n", sizeCh);
    std::printf("  identical       : %zu\n", identical);
    if (!anyDiff) {
        std::printf("  trees are identical at the magic+size level\n");
        return 0;
    }
    auto printGroup = [&](ChangeKind k, const char* heading,
                           bool showSizes) {
        size_t n = 0;
        for (const auto& r : rows) if (r.kind == k) ++n;
        if (n == 0) return;
        std::printf("\n  %s (%zu):\n", heading, n);
        for (const auto& r : rows) {
            if (r.kind != k) continue;
            if (showSizes) {
                std::printf("    %s   [%llu B -> %llu B]\n",
                            r.path.c_str(),
                            static_cast<unsigned long long>(r.sizeA),
                            static_cast<unsigned long long>(r.sizeB));
            } else {
                std::printf("    %s\n", r.path.c_str());
            }
        }
    };
    printGroup(ChangeKind::OnlyInA, "Removed from B (present in A only)", false);
    printGroup(ChangeKind::OnlyInB, "Added to B (not in A)", false);
    printGroup(ChangeKind::MagicChanged,
               "Magic changed between A and B (format swapped!)", true);
    printGroup(ChangeKind::SizeChanged,
               "Same magic but byte size changed (content edited)", true);
    return 1;
}

} // namespace

bool handleDiffTree(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--diff-tree") == 0 && i + 2 < argc) {
        outRc = handleDiff(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
