#include "cli_items_mutate.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleSetItem(int& i, int argc, char** argv) {
    // Edit fields on an existing item in place. Lookup is by
    // id by default; '#N' for index lookup. Only specified
    // flags are changed; everything else is preserved
    // verbatim - including any extra fields added by hand.
    //
    // Supported flags: --name, --quality, --displayId,
    // --itemLevel, --stackable. Each takes one positional
    // argument that follows the flag.
    std::string zoneDir = argv[++i];
    std::string lookup = argv[++i];
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "set-item: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "set-item: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "set-item: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    auto& items = doc["items"];
    int foundIdx = -1;
    if (!lookup.empty() && lookup[0] == '#') {
        try {
            int idx = std::stoi(lookup.substr(1));
            if (idx >= 0 && static_cast<size_t>(idx) < items.size())
                foundIdx = idx;
        } catch (...) {}
    } else {
        uint32_t targetId = 0;
        try { targetId = static_cast<uint32_t>(std::stoul(lookup)); }
        catch (...) {
            std::fprintf(stderr,
                "set-item: lookup '%s' is not a number\n",
                lookup.c_str());
            return 1;
        }
        for (size_t k = 0; k < items.size(); ++k) {
            if (items[k].contains("id") &&
                items[k]["id"].is_number_unsigned() &&
                items[k]["id"].get<uint32_t>() == targetId) {
                foundIdx = static_cast<int>(k);
                break;
            }
        }
    }
    if (foundIdx < 0) {
        std::fprintf(stderr,
            "set-item: no match for '%s' in %s\n",
            lookup.c_str(), path.c_str());
        return 1;
    }
    auto& it = items[foundIdx];
    std::vector<std::string> changes;
    // Walk the remaining args looking for known --field value
    // pairs. Anything unrecognized is reported and aborts so
    // typos don't silently no-op.
    while (i + 2 < argc) {
        std::string flag = argv[i + 1];
        std::string val = argv[i + 2];
        if (flag.size() < 2 || flag[0] != '-' || flag[1] != '-') break;
        if (flag == "--name") {
            it["name"] = val;
            changes.push_back("name=" + val);
        } else if (flag == "--quality") {
            try {
                uint32_t q = static_cast<uint32_t>(std::stoul(val));
                if (q > 6) {
                    std::fprintf(stderr,
                        "set-item: quality %u out of range (0..6)\n", q);
                    return 1;
                }
                it["quality"] = q;
                changes.push_back("quality=" + val);
            } catch (...) {
                std::fprintf(stderr,
                    "set-item: --quality needs a number\n");
                return 1;
            }
        } else if (flag == "--displayId") {
            try {
                it["displayId"] = static_cast<uint32_t>(std::stoul(val));
                changes.push_back("displayId=" + val);
            } catch (...) {
                std::fprintf(stderr,
                    "set-item: --displayId needs a number\n");
                return 1;
            }
        } else if (flag == "--itemLevel") {
            try {
                it["itemLevel"] = static_cast<uint32_t>(std::stoul(val));
                changes.push_back("itemLevel=" + val);
            } catch (...) {
                std::fprintf(stderr,
                    "set-item: --itemLevel needs a number\n");
                return 1;
            }
        } else if (flag == "--stackable") {
            try {
                uint32_t s = static_cast<uint32_t>(std::stoul(val));
                if (s == 0 || s > 1000) {
                    std::fprintf(stderr,
                        "set-item: stackable %u out of range (1..1000)\n", s);
                    return 1;
                }
                it["stackable"] = s;
                changes.push_back("stackable=" + val);
            } catch (...) {
                std::fprintf(stderr,
                    "set-item: --stackable needs a number\n");
                return 1;
            }
        } else {
            std::fprintf(stderr,
                "set-item: unknown flag '%s' (typo?)\n", flag.c_str());
            return 1;
        }
        i += 2;
    }
    if (changes.empty()) {
        std::fprintf(stderr,
            "set-item: no field flags supplied - nothing to change\n");
        return 1;
    }
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr,
            "set-item: failed to write %s\n", path.c_str());
        return 1;
    }
    out << doc.dump(2);
    out.close();
    std::printf("Updated item %d in %s:\n", foundIdx, path.c_str());
    for (const auto& c : changes) {
        std::printf("  %s\n", c.c_str());
    }
    return 0;
}

int handleCopyZoneItems(int& i, int argc, char** argv) {
    // Copy items from one zone to another. Default mode
    // replaces the destination items.json wholesale; --merge
    // appends each source item to the existing destination
    // list, re-id'ing on collision so the destination's
    // existing IDs are preserved and the source's new
    // entries get fresh ones.
    std::string fromZone = argv[++i];
    std::string toZone = argv[++i];
    bool mergeMode = false;
    if (i + 1 < argc && std::strcmp(argv[i + 1], "--merge") == 0) {
        mergeMode = true; i++;
    }
    namespace fs = std::filesystem;
    std::string srcPath = fromZone + "/items.json";
    if (!fs::exists(srcPath)) {
        std::fprintf(stderr,
            "copy-zone-items: %s has no items.json\n", fromZone.c_str());
        return 1;
    }
    if (!fs::exists(toZone) || !fs::is_directory(toZone)) {
        std::fprintf(stderr,
            "copy-zone-items: dest %s is not a directory\n",
            toZone.c_str());
        return 1;
    }
    nlohmann::json src;
    try {
        std::ifstream in(srcPath);
        in >> src;
    } catch (...) {
        std::fprintf(stderr,
            "copy-zone-items: %s is not valid JSON\n", srcPath.c_str());
        return 1;
    }
    if (!src.contains("items") || !src["items"].is_array()) {
        std::fprintf(stderr,
            "copy-zone-items: %s has no 'items' array\n",
            srcPath.c_str());
        return 1;
    }
    std::string dstPath = toZone + "/items.json";
    nlohmann::json dst = nlohmann::json::object({{"items",
                          nlohmann::json::array()}});
    int copied = 0, reIded = 0;
    if (mergeMode && fs::exists(dstPath)) {
        try {
            std::ifstream in(dstPath);
            in >> dst;
        } catch (...) {}
        if (!dst.contains("items") || !dst["items"].is_array()) {
            dst["items"] = nlohmann::json::array();
        }
        std::set<uint32_t> usedIds;
        for (const auto& it : dst["items"]) {
            if (it.contains("id") && it["id"].is_number_unsigned()) {
                usedIds.insert(it["id"].get<uint32_t>());
            }
        }
        for (const auto& it : src["items"]) {
            nlohmann::json newItem = it;
            uint32_t srcId = it.value("id", 0u);
            if (srcId == 0 || usedIds.count(srcId)) {
                // Pick the next free id.
                uint32_t fresh = 1;
                while (usedIds.count(fresh)) ++fresh;
                newItem["id"] = fresh;
                usedIds.insert(fresh);
                if (srcId != 0) reIded++;
            } else {
                usedIds.insert(srcId);
            }
            dst["items"].push_back(newItem);
            copied++;
        }
    } else {
        // Replace mode: destination becomes a verbatim copy of
        // the source items array.
        dst["items"] = src["items"];
        copied = static_cast<int>(src["items"].size());
    }
    std::ofstream out(dstPath);
    if (!out) {
        std::fprintf(stderr,
            "copy-zone-items: failed to write %s\n", dstPath.c_str());
        return 1;
    }
    out << dst.dump(2);
    out.close();
    std::printf("Copied %d item(s) from %s to %s\n",
                copied, fromZone.c_str(), toZone.c_str());
    std::printf("  mode      : %s\n",
                mergeMode ? "merge (append + re-id)" : "replace");
    std::printf("  dst total : %zu\n", dst["items"].size());
    if (reIded > 0) {
        std::printf("  re-ided   : %d (id collisions)\n", reIded);
    }
    return 0;
}

int handleCloneItem(int& i, int argc, char** argv) {
    // Duplicate the item at given 0-based index. Auto-assigns
    // the smallest unused positive id; optional <newName>
    // overrides the cloned name (without it the new entry
    // gets " (copy)" appended).
    std::string zoneDir = argv[++i];
    int idx = -1;
    try { idx = std::stoi(argv[++i]); }
    catch (...) {
        std::fprintf(stderr,
            "clone-item: index must be an integer\n");
        return 1;
    }
    std::string newName;
    if (i + 1 < argc && argv[i + 1][0] != '-') newName = argv[++i];
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "clone-item: %s has no items.json\n", zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "clone-item: %s is not valid JSON\n", path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "clone-item: %s has no 'items' array\n", path.c_str());
        return 1;
    }
    auto& items = doc["items"];
    if (idx < 0 || static_cast<size_t>(idx) >= items.size()) {
        std::fprintf(stderr,
            "clone-item: index %d out of range (have %zu)\n",
            idx, items.size());
        return 1;
    }
    // Pick the next free id.
    std::set<uint32_t> used;
    for (const auto& it : items) {
        if (it.contains("id") && it["id"].is_number_unsigned()) {
            used.insert(it["id"].get<uint32_t>());
        }
    }
    uint32_t newId = 1;
    while (used.count(newId)) ++newId;
    nlohmann::json clone = items[idx];
    clone["id"] = newId;
    if (!newName.empty()) {
        clone["name"] = newName;
    } else {
        std::string oldName = clone.value("name", std::string("(unnamed)"));
        clone["name"] = oldName + " (copy)";
    }
    items.push_back(clone);
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr,
            "clone-item: failed to write %s\n", path.c_str());
        return 1;
    }
    out << doc.dump(2);
    out.close();
    std::printf("Cloned item idx %d to '%s' (id=%u) in %s (now %zu total)\n",
                idx, clone["name"].get<std::string>().c_str(),
                newId, path.c_str(), items.size());
    return 0;
}


}  // namespace

bool handleItemsMutate(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--set-item") == 0 && i + 2 < argc) {
        outRc = handleSetItem(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--copy-zone-items") == 0 && i + 2 < argc) {
        outRc = handleCopyZoneItems(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--clone-item") == 0 && i + 2 < argc) {
        outRc = handleCloneItem(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
