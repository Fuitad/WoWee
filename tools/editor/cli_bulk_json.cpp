#include "cli_bulk_json.hpp"
#include "cli_arg_parse.hpp"
#include "cli_format_table.hpp"
#include "cli_paths.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace wowee {
namespace editor {
namespace cli {

namespace {

namespace fs = std::filesystem;

int runSubprocessExitCode(const std::string& cmd) {
    int rc = std::system(cmd.c_str());
#ifdef _WIN32
    return rc;
#else
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return 1;
#endif
}

struct JobResult {
    fs::path path;
    const FormatMagicEntry* fmt = nullptr;
    int exitCode = 0;
    bool skipped = false;
};

int handleExport(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "bulk-export-json: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::string self = argv[0];
    std::vector<JobResult> rows;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        // Skip files that are themselves .json sidecars -
        // export only operates on binary .w* sources.
        if (entry.path().extension() == ".json") continue;
        char magic[4];
        if (!peekMagic(entry.path(), magic)) continue;
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        if (!fmt) continue;
        JobResult r;
        r.path = entry.path();
        r.fmt = fmt;
        const std::string suffix = formatFlagSuffix(fmt->infoFlag);
        const std::string flag =
            suffix.empty() ? std::string() : "--export-" + suffix + "-json";
        if (flag.empty()) {
            r.skipped = true;
            rows.push_back(std::move(r));
            continue;
        }
        // strip the .wXXX extension so the per-format
        // exporter sees the base path it expects.
        std::string base = entry.path().string();
        std::string ext = entry.path().extension().string();
        if (!ext.empty() && base.size() > ext.size()) {
            base = base.substr(0, base.size() - ext.size());
        }
        std::string cmd = shellQuote(self) + " " + flag + " " +
                           shellQuote(base) + " >/dev/null 2>&1";
        r.exitCode = runSubprocessExitCode(cmd);
        rows.push_back(std::move(r));
    }
    size_t total = rows.size();
    size_t okCount = 0, failCount = 0, skipCount = 0;
    for (const auto& r : rows) {
        if (r.skipped) ++skipCount;
        else if (r.exitCode == 0) ++okCount;
        else ++failCount;
    }
    bool ok = (failCount == 0);
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["mode"] = "export";
        j["total"] = total;
        j["exported"] = okCount;
        j["failed"] = failCount;
        j["skipped"] = skipCount;
        j["allOk"] = ok;
        nlohmann::json failArr = nlohmann::json::array();
        for (const auto& r : rows) {
            if (r.skipped || r.exitCode == 0) continue;
            failArr.push_back({
                {"path", fs::relative(r.path, dir).string()},
                {"format", std::string(r.fmt->extension)},
                {"exitCode", r.exitCode},
            });
        }
        j["failures"] = failArr;
        std::printf("%s\n", j.dump(2).c_str());
        return ok ? 0 : 1;
    }
    std::printf("bulk-export-json: %s\n", dir.c_str());
    std::printf("  total recognized : %zu\n", total);
    std::printf("  exported         : %zu\n", okCount);
    std::printf("  failed           : %zu\n", failCount);
    std::printf("  skipped (no exp) : %zu\n", skipCount);
    if (ok) {
        std::printf("  OK - every catalog with an exporter wrote a .json sidecar\n");
        return 0;
    }
    std::printf("\n  failures:\n");
    for (const auto& r : rows) {
        if (r.skipped || r.exitCode == 0) continue;
        std::printf("    %s   [%s, exit %d]\n",
                    fs::relative(r.path, dir).string().c_str(),
                    r.fmt->extension, r.exitCode);
    }
    return 1;
}

// For import: walk *.json files, look at the inner shape
// to figure out the format, then call the per-format
// importer. Easier approach: derive the format from the
// sidecar's *.wXXX.json filename pattern (which the
// exporters all produce).
int handleImport(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "bulk-import-json: not a directory: %s\n", dir.c_str());
        return 1;
    }
    std::string self = argv[0];
    std::vector<JobResult> rows;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& p = entry.path();
        // We only care about .wXXX.json sidecars where the
        // .wXXX matches a known format. Peek the filename
        // suffix.
        std::string fname = p.filename().string();
        if (fname.size() < 6) continue;
        // Tail must be ".json"
        if (fname.compare(fname.size() - 5, 5, ".json") != 0) continue;
        // Strip ".json" then look at the resulting extension
        std::string stem = fname.substr(0, fname.size() - 5);
        size_t dot = stem.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = stem.substr(dot);
        // Match the .wXXX extension against the format
        // table. Iterate (no per-extension index lookup
        // helper exposed yet).
        const FormatMagicEntry* fmt = nullptr;
        for (const FormatMagicEntry* f = formatTableBegin();
             f != formatTableEnd(); ++f) {
            if (ext == f->extension) { fmt = f; break; }
        }
        if (!fmt) continue;
        JobResult r;
        r.path = p;
        r.fmt = fmt;
        const std::string suffix = formatFlagSuffix(fmt->infoFlag);
        const std::string flag =
            suffix.empty() ? std::string() : "--import-" + suffix + "-json";
        if (flag.empty()) {
            r.skipped = true;
            rows.push_back(std::move(r));
            continue;
        }
        std::string cmd = shellQuote(self) + " " + flag + " " +
                           shellQuote(p.string()) + " >/dev/null 2>&1";
        r.exitCode = runSubprocessExitCode(cmd);
        rows.push_back(std::move(r));
    }
    size_t total = rows.size();
    size_t okCount = 0, failCount = 0, skipCount = 0;
    for (const auto& r : rows) {
        if (r.skipped) ++skipCount;
        else if (r.exitCode == 0) ++okCount;
        else ++failCount;
    }
    bool ok = (failCount == 0);
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["mode"] = "import";
        j["total"] = total;
        j["imported"] = okCount;
        j["failed"] = failCount;
        j["skipped"] = skipCount;
        j["allOk"] = ok;
        nlohmann::json failArr = nlohmann::json::array();
        for (const auto& r : rows) {
            if (r.skipped || r.exitCode == 0) continue;
            failArr.push_back({
                {"path", fs::relative(r.path, dir).string()},
                {"format", std::string(r.fmt->extension)},
                {"exitCode", r.exitCode},
            });
        }
        j["failures"] = failArr;
        std::printf("%s\n", j.dump(2).c_str());
        return ok ? 0 : 1;
    }
    std::printf("bulk-import-json: %s\n", dir.c_str());
    std::printf("  total sidecars   : %zu\n", total);
    std::printf("  imported         : %zu\n", okCount);
    std::printf("  failed           : %zu\n", failCount);
    std::printf("  skipped (no imp) : %zu\n", skipCount);
    if (ok) {
        std::printf("  OK - every .json sidecar was imported back to binary\n");
        return 0;
    }
    std::printf("\n  failures:\n");
    for (const auto& r : rows) {
        if (r.skipped || r.exitCode == 0) continue;
        std::printf("    %s   [%s, exit %d]\n",
                    fs::relative(r.path, dir).string().c_str(),
                    r.fmt->extension, r.exitCode);
    }
    return 1;
}

} // namespace

bool handleBulkJson(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--bulk-export-json") == 0 && i + 1 < argc) {
        outRc = handleExport(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--bulk-import-json") == 0 && i + 1 < argc) {
        outRc = handleImport(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
