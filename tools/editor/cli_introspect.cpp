#include "cli_introspect.hpp"
#include "cli_help.hpp"
#include "cli_arg_required.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <io.h>
#  define WOWEE_DUP   _dup
#  define WOWEE_DUP2  _dup2
#  define WOWEE_FILENO _fileno
#  define WOWEE_CLOSE _close
#else
#  include <unistd.h>
#  define WOWEE_DUP   dup
#  define WOWEE_DUP2  dup2
#  define WOWEE_FILENO fileno
#  define WOWEE_CLOSE close
#endif

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Redirect stdout to `dst` for the lifetime of this object. Uses fd-level
// dup/dup2 instead of `stdout = tmp;` because the C standard does not
// guarantee `stdout` is an assignable lvalue (Apple/Windows reject it).
class StdoutRedirect {
public:
    explicit StdoutRedirect(FILE* dst) {
        std::fflush(stdout);
        savedFd_ = WOWEE_DUP(WOWEE_FILENO(stdout));
        if (savedFd_ >= 0 && dst) {
            WOWEE_DUP2(WOWEE_FILENO(dst), WOWEE_FILENO(stdout));
        }
    }
    ~StdoutRedirect() { restore(); }
    void restore() {
        if (savedFd_ < 0) return;
        std::fflush(stdout);
        WOWEE_DUP2(savedFd_, WOWEE_FILENO(stdout));
        WOWEE_CLOSE(savedFd_);
        savedFd_ = -1;
    }
    StdoutRedirect(const StdoutRedirect&) = delete;
    StdoutRedirect& operator=(const StdoutRedirect&) = delete;
private:
    int savedFd_ = -1;
};

int handleListCommands(int& i, int argc, char** argv) {
    // Capture printUsage's stdout and grep for '--flag' tokens at
    // the start of each line. This auto-tracks the help text as
    // commands are added - no parallel list to maintain. Result
    // is a sorted, deduped, one-per-line list of recognized flags.
    // Temp file lets us read printUsage's output back. fmemopen
    // would be cleaner but isn't available on Windows; tmpfile is
    // portable.
    FILE* tmp = std::tmpfile();
    if (!tmp) { std::fprintf(stderr, "list-commands: tmpfile failed\n"); return 1; }
    {
        StdoutRedirect redir(tmp);
        wowee::editor::cli::printUsage(argv[0]);
    }
    std::fseek(tmp, 0, SEEK_SET);
    std::set<std::string> commands;
    char line[512];
    while (std::fgets(line, sizeof(line), tmp)) {
        // Match leading whitespace then '--' then [a-z-]+
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (p[0] != '-' || p[1] != '-') continue;
        std::string flag;
        while (*p && (std::isalnum(static_cast<unsigned char>(*p)) ||
                      *p == '-' || *p == '_')) {
            flag += *p++;
        }
        if (flag.size() > 2) commands.insert(flag);
    }
    std::fclose(tmp);
    // Always include the meta-flags that printUsage describes
    // alongside others (-h/-v aliases) since the regex above only
    // captures double-dash forms.
    commands.insert("--help");
    commands.insert("--version");
    for (const auto& c : commands) std::printf("%s\n", c.c_str());
    return 0;
}

int handleInfoCliStats(int& i, int argc, char** argv) {
    // Meta-stats on the CLI surface: total command count + per-
    // category breakdown by prefix verb (--info-*, --validate-*,
    // --diff-*, etc.). Useful for tracking growth over time and
    // spotting category imbalances.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    // Re-use --list-commands' parser. Capture printUsage stdout.
    FILE* tmp = std::tmpfile();
    if (!tmp) { std::fprintf(stderr, "info-cli-stats: tmpfile failed\n"); return 1; }
    {
        StdoutRedirect redir(tmp);
        wowee::editor::cli::printUsage(argv[0]);
    }
    std::fseek(tmp, 0, SEEK_SET);
    std::set<std::string> commands;
    char line[512];
    while (std::fgets(line, sizeof(line), tmp)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (p[0] != '-' || p[1] != '-') continue;
        std::string flag;
        while (*p && (std::isalnum(static_cast<unsigned char>(*p)) ||
                      *p == '-' || *p == '_')) { flag += *p++; }
        if (flag.size() > 2) commands.insert(flag);
    }
    std::fclose(tmp);
    commands.insert("--help");
    commands.insert("--version");
    // Bucket by category - verb is the second token after '--',
    // up to the next dash. So '--info-zone-tree' -> 'info'.
    std::map<std::string, int> byCategory;
    int maxLen = 0;
    for (const auto& c : commands) {
        if (static_cast<int>(c.size()) > maxLen) maxLen = static_cast<int>(c.size());
        size_t verbStart = 2;  // skip '--'
        size_t verbEnd = c.find('-', verbStart);
        std::string verb = (verbEnd == std::string::npos)
            ? c.substr(verbStart)
            : c.substr(verbStart, verbEnd - verbStart);
        byCategory[verb]++;
    }
    if (jsonOut) {
        nlohmann::json j;
        j["totalCommands"] = commands.size();
        j["maxFlagLength"] = maxLen;
        nlohmann::json cats = nlohmann::json::object();
        for (const auto& [v, c] : byCategory) cats[v] = c;
        j["byCategory"] = cats;
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    std::printf("CLI surface stats\n");
    std::printf("  total commands : %zu\n", commands.size());
    std::printf("  longest flag   : %d chars\n", maxLen);
    std::printf("\n  Categories (by verb prefix, sorted by count):\n");
    // Sort by count descending for the table.
    std::vector<std::pair<std::string, int>> sorted(
        byCategory.begin(), byCategory.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    for (const auto& [verb, count] : sorted) {
        std::printf("    --%-12s %4d\n", verb.c_str(), count);
    }
    return 0;
}

int handleInfoCliCategories(int& i, int argc, char** argv) {
    // Discovery view of every CLI flag grouped by verb prefix.
    // Where --info-cli-stats just counts per category, this
    // lists every command in each category - handy for "I
    // know I want to gen something but what shapes/textures
    // are available?"
    FILE* tmp = std::tmpfile();
    if (!tmp) {
        std::fprintf(stderr, "info-cli-categories: tmpfile failed\n");
        return 1;
    }
    {
        StdoutRedirect redir(tmp);
        wowee::editor::cli::printUsage(argv[0]);
    }
    std::fseek(tmp, 0, SEEK_SET);
    std::set<std::string> commands;
    char line[512];
    while (std::fgets(line, sizeof(line), tmp)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (p[0] != '-' || p[1] != '-') continue;
        std::string flag;
        while (*p && (std::isalnum(static_cast<unsigned char>(*p)) ||
                      *p == '-' || *p == '_')) { flag += *p++; }
        if (flag.size() > 2) commands.insert(flag);
    }
    std::fclose(tmp);
    commands.insert("--help");
    commands.insert("--version");
    std::map<std::string, std::vector<std::string>> byCategory;
    for (const auto& c : commands) {
        size_t verbStart = 2;
        size_t verbEnd = c.find('-', verbStart);
        std::string verb = (verbEnd == std::string::npos)
            ? c.substr(verbStart)
            : c.substr(verbStart, verbEnd - verbStart);
        byCategory[verb].push_back(c);
    }
    std::printf("CLI commands by category (%zu total):\n\n",
                commands.size());
    // Sort categories by count descending, commands within
    // each alphabetically.
    std::vector<std::pair<std::string, std::vector<std::string>>> sorted(
        byCategory.begin(), byCategory.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  if (a.second.size() != b.second.size())
                      return a.second.size() > b.second.size();
                  return a.first < b.first;
              });
    for (const auto& [verb, cmds] : sorted) {
        std::printf("--%s (%zu):\n", verb.c_str(), cmds.size());
        for (const auto& c : cmds) {
            std::printf("  %s\n", c.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

int handleInfoCliHelp(int& i, int argc, char** argv) {
    // Substring search through the help text. With 130+ commands,
    // 'is there a thing for X?' is a common ask - this answers it
    // without making the user scroll the full --help output:
    //
    //   wowee_editor --info-cli-help quest
    //   wowee_editor --info-cli-help validate
    //   wowee_editor --info-cli-help glb
    std::string pattern = argv[++i];
    // Lowercase the pattern for case-insensitive match.
    std::string patLower = pattern;
    for (auto& c : patLower) c = std::tolower(static_cast<unsigned char>(c));
    // Capture printUsage stdout, walk line-by-line, print every
    // line containing the pattern (case-insensitive). Continuation
    // lines (the indented description on the line after a flag)
    // are emitted along with the flag line for context.
    FILE* tmp = std::tmpfile();
    if (!tmp) {
        std::fprintf(stderr, "info-cli-help: tmpfile failed\n"); return 1;
    }
    {
        StdoutRedirect redir(tmp);
        wowee::editor::cli::printUsage(argv[0]);
    }
    std::fseek(tmp, 0, SEEK_SET);
    std::vector<std::string> lines;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), tmp)) {
        std::string s = buf;
        if (!s.empty() && s.back() == '\n') s.pop_back();
        lines.push_back(std::move(s));
    }
    std::fclose(tmp);
    int matches = 0;
    for (size_t k = 0; k < lines.size(); ++k) {
        std::string lower = lines[k];
        for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
        if (lower.find(patLower) == std::string::npos) continue;
        std::printf("%s\n", lines[k].c_str());
        // Look ahead for a continuation line (indented and not
        // starting with '--'). Print it for context.
        if (k + 1 < lines.size()) {
            const auto& next = lines[k + 1];
            if (!next.empty() && next[0] == ' ' &&
                next.find("--") == std::string::npos) {
                std::printf("%s\n", next.c_str());
            }
        }
        matches++;
    }
    if (matches == 0) {
        std::fprintf(stderr, "info-cli-help: no matches for '%s'\n",
                     pattern.c_str());
        return 1;
    }
    std::fprintf(stderr, "\n%d line(s) matched '%s'\n", matches, pattern.c_str());
    return 0;
}

int handleGenCompletion(int& i, int argc, char** argv) {
    // Emit a bash or zsh completion script. Re-execs the editor's
    // own --list-commands at completion time so newly-added flags
    // light up automatically without regenerating the script.
    std::string shell = argv[++i];
    if (shell != "bash" && shell != "zsh") {
        std::fprintf(stderr,
            "gen-completion: shell must be 'bash' or 'zsh', got '%s'\n",
            shell.c_str());
        return 1;
    }
    // Use argv[0] as the binary name in the completion so it
    // works whether the user installed it as 'wowee_editor' or
    // a custom alias. Strip directory components for the
    // completion-name registration (bash 'complete -F' expects
    // a basename).
    std::string self = argv[0];
    auto slash = self.find_last_of('/');
    std::string baseName = (slash != std::string::npos)
        ? self.substr(slash + 1)
        : self;
    if (shell == "bash") {
        std::printf(
            "# wowee_editor bash completion - source from ~/.bashrc:\n"
            "#   source <(%s --gen-completion bash)\n"
            "_wowee_editor_complete() {\n"
            "  local cur prev cmds\n"
            "  COMPREPLY=()\n"
            "  cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
            "  prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
            "  # Cache the command list per shell session.\n"
            "  if [[ -z \"$_WOWEE_EDITOR_CMDS\" ]]; then\n"
            "    _WOWEE_EDITOR_CMDS=$(%s --list-commands 2>/dev/null)\n"
            "  fi\n"
            "  if [[ \"$cur\" == --* ]]; then\n"
            "    COMPREPLY=( $(compgen -W \"$_WOWEE_EDITOR_CMDS\" -- \"$cur\") )\n"
            "    return 0\n"
            "  fi\n"
            "  # Default: complete file paths for arg slots.\n"
            "  COMPREPLY=( $(compgen -f -- \"$cur\") )\n"
            "}\n"
            "complete -F _wowee_editor_complete %s\n",
            self.c_str(), self.c_str(), baseName.c_str());
    } else {
        // zsh - simpler descriptor-based completion.
        std::printf(
            "# wowee_editor zsh completion - source from ~/.zshrc:\n"
            "#   source <(%s --gen-completion zsh)\n"
            "_wowee_editor_complete() {\n"
            "  local -a cmds\n"
            "  if [[ -z \"$_WOWEE_EDITOR_CMDS\" ]]; then\n"
            "    export _WOWEE_EDITOR_CMDS=$(%s --list-commands 2>/dev/null)\n"
            "  fi\n"
            "  cmds=( ${(f)_WOWEE_EDITOR_CMDS} )\n"
            "  _arguments \"*: :($cmds)\"\n"
            "}\n"
            "compdef _wowee_editor_complete %s\n",
            self.c_str(), self.c_str(), baseName.c_str());
    }
    return 0;
}

int handleValidateCliHelp(int& i, int argc, char** argv) {
    // Self-check: every flag we declare in kArgRequired (the list
    // of commands needing positional args) must appear in the
    // help text printUsage emits. Catches drift where someone
    // adds a handler + argument check but forgets the help line.
    bool jsonOut = (i + 1 < argc &&
                    std::strcmp(argv[i + 1], "--json") == 0);
    if (jsonOut) i++;
    // Capture printUsage's stdout.
    FILE* tmp = std::tmpfile();
    if (!tmp) { std::fprintf(stderr, "validate-cli-help: tmpfile failed\n"); return 1; }
    {
        StdoutRedirect redir(tmp);
        wowee::editor::cli::printUsage(argv[0]);
    }
    std::fseek(tmp, 0, SEEK_SET);
    std::string helpText;
    char chunk[1024];
    while (std::fgets(chunk, sizeof(chunk), tmp)) helpText += chunk;
    std::fclose(tmp);
    // Walk kArgRequired and check each appears in the help.
    std::vector<std::string> missing;
    for (std::size_t k = 0; k < kArgRequiredSize; ++k) {
        const char* opt = kArgRequired[k];
        if (helpText.find(opt) == std::string::npos) {
            missing.push_back(opt);
        }
    }
    if (jsonOut) {
        nlohmann::json j;
        j["totalArgRequired"] = kArgRequiredSize;
        j["missing"] = missing;
        j["passed"] = missing.empty();
        std::printf("%s\n", j.dump(2).c_str());
        return missing.empty() ? 0 : 1;
    }
    std::printf("CLI help self-check\n");
    std::printf("  kArgRequired entries : %zu\n", kArgRequiredSize);
    if (missing.empty()) {
        std::printf("  PASSED - every kArgRequired flag is documented\n");
        return 0;
    }
    std::printf("  FAILED - %zu flag(s) missing from help text:\n", missing.size());
    for (const auto& m : missing) std::printf("    - %s\n", m.c_str());
    (void)argc;
    return 1;
}

int handleHelp(int& /*i*/, int /*argc*/, char** argv) {
    wowee::editor::cli::printUsage(argv[0]);
    return 0;
}

int handleVersion(int& /*i*/, int /*argc*/, char** /*argv*/) {
    std::printf("Wowee World Editor v1.0.0\n");
    std::printf("Open formats: WOT/WHM/WOM/WOB/WOC/WCP + PNG/JSON (all novel)\n");
    std::printf("By Kelsi Davis\n");
    return 0;
}

int handleListPacks(int& /*i*/, int /*argc*/, char** /*argv*/) {
    // Sister to --list-primitives: lists every --gen-*-pack
    // composite flag from the shared kArgRequired registry.
    // Auto-tracks new packs as they're added - no parallel list.
    std::vector<std::string> packs;
    for (std::size_t k = 0; k < kArgRequiredSize; ++k) {
        const char* flag = kArgRequired[k];
        std::size_t len = std::strlen(flag);
        // Match --gen-*-pack (anything starting with --gen- and
        // ending in -pack, but NOT --gen-mesh-* or --gen-texture-*
        // which are individual primitives).
        if (std::strncmp(flag, "--gen-", 6) != 0) continue;
        if (std::strncmp(flag, "--gen-mesh-", 11) == 0) continue;
        if (std::strncmp(flag, "--gen-texture-", 14) == 0) continue;
        if (len > 5 && std::strcmp(flag + len - 5, "-pack") == 0) {
            packs.emplace_back(flag);
        }
    }
    std::sort(packs.begin(), packs.end());
    std::printf("Composite packs (%zu):\n", packs.size());
    for (const auto& p : packs) std::printf("  %s\n", p.c_str());
    return 0;
}

int handleListPrimitives(int& i, int argc, char** argv) {
    // Focused subset of --list-commands: just the procedural
    // primitives (--gen-mesh-* and --gen-texture-* flags). Useful
    // when authoring content packs to discover what's available
    // without scrolling through the full --help dump. Walks the
    // shared kArgRequired registry so it auto-tracks new
    // primitives as they're added - no parallel list.
    bool jsonOut = false;
    bool meshOnly = false;
    bool textureOnly = false;
    while (i + 1 < argc && argv[i + 1][0] == '-') {
        if (std::strcmp(argv[i + 1], "--json") == 0) {
            jsonOut = true; ++i;
        } else if (std::strcmp(argv[i + 1], "--mesh") == 0) {
            meshOnly = true; ++i;
        } else if (std::strcmp(argv[i + 1], "--texture") == 0) {
            textureOnly = true; ++i;
        } else {
            break;
        }
    }
    std::vector<std::string> meshes;
    std::vector<std::string> textures;
    for (std::size_t k = 0; k < kArgRequiredSize; ++k) {
        const char* flag = kArgRequired[k];
        if (std::strncmp(flag, "--gen-mesh-", 11) == 0) {
            meshes.emplace_back(flag);
        } else if (std::strncmp(flag, "--gen-texture-", 14) == 0) {
            textures.emplace_back(flag);
        }
    }
    std::sort(meshes.begin(), meshes.end());
    std::sort(textures.begin(), textures.end());
    if (jsonOut) {
        nlohmann::json j;
        if (!textureOnly) j["meshes"] = meshes;
        if (!meshOnly)    j["textures"] = textures;
        if (!textureOnly) j["meshCount"] = meshes.size();
        if (!meshOnly)    j["textureCount"] = textures.size();
        std::printf("%s\n", j.dump(2).c_str());
        return 0;
    }
    if (!textureOnly) {
        std::printf("Procedural meshes (%zu):\n", meshes.size());
        for (const auto& m : meshes) std::printf("  %s\n", m.c_str());
    }
    if (!meshOnly) {
        if (!textureOnly) std::printf("\n");
        std::printf("Procedural textures (%zu):\n", textures.size());
        for (const auto& t : textures) std::printf("  %s\n", t.c_str());
    }
    return 0;
}

}  // namespace

bool handleIntrospect(int& i, int argc, char** argv, int& outRc) {
    if (std::strcmp(argv[i], "--list-commands") == 0) {
        outRc = handleListCommands(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-primitives") == 0) {
        outRc = handleListPrimitives(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--list-packs") == 0) {
        outRc = handleListPacks(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-cli-stats") == 0) {
        outRc = handleInfoCliStats(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-cli-categories") == 0) {
        outRc = handleInfoCliCategories(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--info-cli-help") == 0 && i + 1 < argc) {
        outRc = handleInfoCliHelp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--gen-completion") == 0 && i + 1 < argc) {
        outRc = handleGenCompletion(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--validate-cli-help") == 0) {
        outRc = handleValidateCliHelp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--help") == 0 ||
        std::strcmp(argv[i], "-h") == 0) {
        outRc = handleHelp(i, argc, argv); return true;
    }
    if (std::strcmp(argv[i], "--version") == 0 ||
        std::strcmp(argv[i], "-v") == 0) {
        outRc = handleVersion(i, argc, argv); return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
