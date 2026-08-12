#include "cli_items_export.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace editor {
namespace cli {

namespace {

int handleExportZoneItemsMd(int& i, int argc, char** argv) {
    // Render items.json as a Markdown table grouped by
    // quality. Useful for design docs, PR descriptions, and
    // GitHub Pages - one rendered page communicates the loot
    // landscape better than scrolling through JSON.
    std::string zoneDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    std::string path = zoneDir + "/items.json";
    if (!fs::exists(path)) {
        std::fprintf(stderr,
            "export-zone-items-md: %s has no items.json\n",
            zoneDir.c_str());
        return 1;
    }
    nlohmann::json doc;
    try {
        std::ifstream in(path);
        in >> doc;
    } catch (...) {
        std::fprintf(stderr,
            "export-zone-items-md: %s is not valid JSON\n",
            path.c_str());
        return 1;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        std::fprintf(stderr,
            "export-zone-items-md: %s has no 'items' array\n",
            path.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = zoneDir + "/ITEMS.md";
    const auto& items = doc["items"];
    static const char* qualityNames[] = {
        "Poor", "Common", "Uncommon", "Rare", "Epic",
        "Legendary", "Artifact"
    };
    // Bucket by quality so the report reads top-down from
    // best loot to filler. Reverse iteration over the buckets.
    std::map<int, std::vector<size_t>> byQuality;
    for (size_t k = 0; k < items.size(); ++k) {
        uint32_t q = items[k].value("quality", 1u);
        if (q > 6) q = 0;
        byQuality[q].push_back(k);
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-zone-items-md: cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::string zoneName = fs::path(zoneDir).filename().string();
    out << "# Items: " << zoneName << "\n\n";
    out << "Source: `" << path << "`  \n";
    out << "Total items: **" << items.size() << "**\n\n";
    // Quality histogram up top.
    out << "## Quality breakdown\n\n";
    out << "| Quality | Count |\n|---|---:|\n";
    for (int q = 6; q >= 0; --q) {
        auto it = byQuality.find(q);
        if (it == byQuality.end()) continue;
        out << "| " << qualityNames[q] << " | "
            << it->second.size() << " |\n";
    }
    out << "\n";
    // Per-quality sections, best first.
    for (int q = 6; q >= 0; --q) {
        auto qit = byQuality.find(q);
        if (qit == byQuality.end()) continue;
        out << "## " << qualityNames[q] << "\n\n";
        out << "| ID | Name | iLvl | Display | Stack |\n";
        out << "|---:|---|---:|---:|---:|\n";
        for (size_t k : qit->second) {
            const auto& it = items[k];
            std::string name = it.value("name", std::string("(unnamed)"));
            out << "| " << it.value("id", 0u) << " | "
                << name << " | "
                << it.value("itemLevel", 1u) << " | "
                << it.value("displayId", 0u) << " | "
                << it.value("stackable", 1u) << " |\n";
        }
        out << "\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  total items : %zu\n", items.size());
    std::printf("  qualities   : %zu (used)\n", byQuality.size());
    return 0;
}

int handleExportProjectItemsMd(int& i, int argc, char** argv) {
    // Project-wide items markdown. Walks every zone in
    // <projectDir> and emits one document with: project-wide
    // header + total + quality histogram, then per-zone
    // sections each containing a table (ID/name/quality/
    // ilvl/displayId/stack). Easier to scan than running
    // --export-zone-items-md N times.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "export-project-items-md: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/ITEMS.md";
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        if (!fs::exists(entry.path() / "items.json")) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    static const char* qualityNames[] = {
        "Poor", "Common", "Uncommon", "Rare", "Epic",
        "Legendary", "Artifact"
    };
    int totalItems = 0;
    std::map<int, int> globalQ;
    // Per-zone collected items so we don't have to re-read
    // each items.json twice.
    struct ZItems {
        std::string name;
        nlohmann::json items;
    };
    std::vector<ZItems> zoneItems;
    for (const auto& zoneDir : zones) {
        std::string ipath = zoneDir + "/items.json";
        nlohmann::json doc;
        try {
            std::ifstream in(ipath);
            in >> doc;
        } catch (...) { continue; }
        if (!doc.contains("items") || !doc["items"].is_array()) continue;
        ZItems z;
        z.name = fs::path(zoneDir).filename().string();
        z.items = doc["items"];
        for (const auto& it : z.items) {
            int q = static_cast<int>(it.value("quality", 1u));
            if (q < 0 || q > 6) q = 0;
            globalQ[q]++;
            totalItems++;
        }
        zoneItems.push_back(std::move(z));
    }
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-project-items-md: cannot write %s\n",
            outPath.c_str());
        return 1;
    }
    out << "# Project Items: "
        << fs::path(projectDir).filename().string() << "\n\n";
    out << "Source: `" << projectDir << "`  \n";
    out << "Zones with items: **" << zoneItems.size() << "**  \n";
    out << "Total items: **" << totalItems << "**\n\n";
    out << "## Project quality breakdown\n\n";
    out << "| Quality | Count |\n|---|---:|\n";
    for (int q = 6; q >= 0; --q) {
        auto it = globalQ.find(q);
        if (it == globalQ.end()) continue;
        out << "| " << qualityNames[q] << " | "
            << it->second << " |\n";
    }
    out << "\n";
    for (const auto& z : zoneItems) {
        out << "## Zone: " << z.name << "\n\n";
        out << "Items: **" << z.items.size() << "**\n\n";
        out << "| ID | Name | Quality | iLvl | Display | Stack |\n";
        out << "|---:|---|---|---:|---:|---:|\n";
        for (const auto& it : z.items) {
            int q = static_cast<int>(it.value("quality", 1u));
            if (q < 0 || q > 6) q = 0;
            std::string name = it.value("name", std::string("(unnamed)"));
            out << "| " << it.value("id", 0u) << " | "
                << name << " | "
                << qualityNames[q] << " | "
                << it.value("itemLevel", 1u) << " | "
                << it.value("displayId", 0u) << " | "
                << it.value("stackable", 1u) << " |\n";
        }
        out << "\n";
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  zones with items : %zu\n", zoneItems.size());
    std::printf("  total items      : %d\n", totalItems);
    return 0;
}

int handleExportProjectItemsCsv(int& i, int argc, char** argv) {
    // Single CSV with every item across every zone. The
    // zone name is the first column so a pivot table can
    // group by it; everything else mirrors --export-zone-csv
    // items columns. Saves running the per-zone CSV exporter
    // N times and concatenating manually.
    std::string projectDir = argv[++i];
    std::string outPath;
    if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
    namespace fs = std::filesystem;
    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::fprintf(stderr,
            "export-project-items-csv: %s is not a directory\n",
            projectDir.c_str());
        return 1;
    }
    if (outPath.empty()) outPath = projectDir + "/items.csv";
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(projectDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "zone.json")) continue;
        if (!fs::exists(entry.path() / "items.json")) continue;
        zones.push_back(entry.path().string());
    }
    std::sort(zones.begin(), zones.end());
    // CSV-escape the same way --export-zone-csv does.
    auto csvEsc = [](const std::string& s) {
        bool needs = s.find(',') != std::string::npos ||
                     s.find('"') != std::string::npos ||
                     s.find('\n') != std::string::npos;
        if (!needs) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    };
    std::ofstream out(outPath);
    if (!out) {
        std::fprintf(stderr,
            "export-project-items-csv: cannot write %s\n",
            outPath.c_str());
        return 1;
    }
    out << "zone,index,id,name,quality,itemLevel,displayId,stackable\n";
    int totalRows = 0;
    for (const auto& zoneDir : zones) {
        std::string zoneName = fs::path(zoneDir).filename().string();
        std::string ipath = zoneDir + "/items.json";
        nlohmann::json doc;
        try {
            std::ifstream in(ipath);
            in >> doc;
        } catch (...) { continue; }
        if (!doc.contains("items") || !doc["items"].is_array()) continue;
        const auto& items = doc["items"];
        for (size_t k = 0; k < items.size(); ++k) {
            const auto& it = items[k];
            out << csvEsc(zoneName) << "," << k << ","
                << it.value("id", 0u) << ","
                << csvEsc(it.value("name", std::string())) << ","
                << it.value("quality", 1u) << ","
                << it.value("itemLevel", 1u) << ","
                << it.value("displayId", 0u) << ","
                << it.value("stackable", 1u) << "\n";
            totalRows++;
        }
    }
    out.close();
    std::printf("Wrote %s\n", outPath.c_str());
    std::printf("  zones with items : %zu\n", zones.size());
    std::printf("  rows             : %d\n", totalRows);
    return 0;
}


}  // namespace

bool handleItemsExport(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--export-zone-items-md") == 0 && i + 1 < argc) {
        outRc = handleExportZoneItemsMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-project-items-md") == 0 && i + 1 < argc) {
        outRc = handleExportProjectItemsMd(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--export-project-items-csv") == 0 && i + 1 < argc) {
        outRc = handleExportProjectItemsCsv(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
