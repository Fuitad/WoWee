#include "cli_bulk_validate.hpp"
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

struct PerFile {
    fs::path path;
    const FormatMagicEntry* fmt = nullptr;
    int exitCode = 0;        // 0 = OK, anything else = validator complained
    bool skipped = false;    // file is a known asset format with no validator
};

int handleBulk(int& i, int argc, char** argv) {
    std::string dir = argv[++i];
    bool jsonOut = consumeJsonFlag(i, argc, argv);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::fprintf(stderr,
            "bulk-validate: not a directory: %s\n", dir.c_str());
        return 1;
    }
    // argv[0] is this binary's invocation path - needed
    // so each file can be validated via a fresh subprocess
    // call, isolating one file's failures from another's.
    std::string self = argv[0];
    std::vector<PerFile> rows;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        char magic[4];
        if (!peekMagic(entry.path(), magic)) continue;
        const FormatMagicEntry* fmt = findFormatByMagic(magic);
        if (!fmt) continue;     // non-Wowee file
        PerFile pf;
        pf.path = entry.path();
        pf.fmt = fmt;
        const std::string suffix = formatFlagSuffix(fmt->infoFlag);
        const std::string validateFlag =
            suffix.empty() ? std::string() : "--validate-" + suffix;
        if (validateFlag.empty()) {
            // Asset-style format with no validator hooked
            // up - count it but don't try to invoke.
            pf.skipped = true;
            rows.push_back(std::move(pf));
            continue;
        }
        std::string cmd = shellQuote(self) + " " +
                           validateFlag + " " +
                           shellQuote(entry.path().string()) +
                           " >/dev/null 2>&1";
        int rc = std::system(cmd.c_str());
        // std::system returns the wait-status; on POSIX
        // WEXITSTATUS extracts the actual program exit code.
#ifdef _WIN32
        pf.exitCode = rc;
#else
        if (rc == -1) pf.exitCode = -1;
        else if (WIFEXITED(rc)) pf.exitCode = WEXITSTATUS(rc);
        else pf.exitCode = 1;
#endif
        rows.push_back(std::move(pf));
    }
    size_t total = rows.size();
    size_t okCount = 0;
    size_t failCount = 0;
    size_t skipCount = 0;
    for (const auto& r : rows) {
        if (r.skipped) ++skipCount;
        else if (r.exitCode == 0) ++okCount;
        else ++failCount;
    }
    bool ok = (failCount == 0);
    if (jsonOut) {
        nlohmann::json j;
        j["dir"] = dir;
        j["total"] = total;
        j["ok"] = okCount;
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
    std::printf("bulk-validate: %s\n", dir.c_str());
    std::printf("  total recognized : %zu\n", total);
    std::printf("  passed           : %zu\n", okCount);
    std::printf("  failed           : %zu\n", failCount);
    std::printf("  skipped (no val) : %zu\n", skipCount);
    if (ok) {
        std::printf("  OK - every catalog with a validator passed\n");
        return 0;
    }
    std::printf("\n  failures:\n");
    for (const auto& r : rows) {
        if (r.skipped || r.exitCode == 0) continue;
        std::printf("    %s   [%s, exit %d]\n",
                    fs::relative(r.path, dir).string().c_str(),
                    r.fmt->extension, r.exitCode);
    }
    std::printf("\n  re-run the per-format validator on a failure for "
                "details, e.g.:\n");
    // Show one example so the user knows the followup
    // command. Pick the first failure.
    for (const auto& r : rows) {
        if (r.skipped || r.exitCode == 0) continue;
        const std::string suffix = formatFlagSuffix(r.fmt->infoFlag);
        const std::string flag =
            suffix.empty() ? std::string() : "--validate-" + suffix;
        std::printf("    %s %s %s\n",
                    self.c_str(), flag.c_str(),
                    r.path.string().c_str());
        break;
    }
    return 1;
}

} // namespace

bool handleBulkValidate(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--bulk-validate") == 0 && i + 1 < argc) {
        outRc = handleBulk(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
