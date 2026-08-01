#include "addons/addon_manager.hpp"
#include "core/logger.hpp"
#include "core/config_paths.hpp"
#include <sstream>
#include "ui/xml_parser.hpp"
#include "ui/framexml_emitter.hpp"
#include <algorithm>
#include <set>
#include <optional>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace wowee::addons {

AddonManager::AddonManager() = default;
AddonManager::~AddonManager() { shutdown(); }

bool AddonManager::initialize(game::GameHandler* gameHandler, const LuaServices& services) {
    gameHandler_ = gameHandler;
    luaServices_ = services;
    if (!luaEngine_.initialize()) return false;
    luaEngine_.setGameHandler(gameHandler);
    luaEngine_.setLuaServices(luaServices_);
    return true;
}

namespace {

/// Find a child of `base` whose name matches `name` ignoring case, or empty.
///
/// Extracted game data does not agree with itself about case: this install has
/// interface/framexml in lower case beside interface/AddOns in mixed. The asset
/// manager copes because it goes through a manifest of normalised paths, but
/// anything reaching the filesystem directly has to look, and on a
/// case-sensitive filesystem a hardcoded spelling simply misses.
std::filesystem::path resolveChild(const std::filesystem::path& base,
                                   const std::string& name) {
    std::error_code ec;
    const std::filesystem::path exact = base / name;
    if (std::filesystem::exists(exact, ec)) return exact;

    auto lower = [](std::string v) {
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return v;
    };
    const std::string wanted = lower(name);
    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (lower(entry.path().filename().string()) == wanted) return entry.path();
    }
    return {};
}

/// Walk a relative path one component at a time, matching each without regard
/// to case.
std::filesystem::path resolvePath(const std::filesystem::path& base,
                                  const std::string& relative) {
    std::filesystem::path at = base;
    for (const auto& part : std::filesystem::path(relative)) {
        if (part.empty() || part == ".") continue;
        at = resolveChild(at, part.string());
        if (at.empty()) return {};
    }
    return at;
}

} // namespace

void AddonManager::scanAddons(const std::string& addonsPath) {
    addonsPath_ = addonsPath;
    addons_.clear();

    // Two places are searched. The game data's own Interface\AddOns is where a
    // player's existing addons already live, and an "addons" directory beside
    // the executable is where this client's own ship without anyone having to
    // copy files into an extracted game install to try them.
    std::vector<fs::path> roots;
    {
        // Same case problem as FrameXML: this install has interface/addons in
        // lower case, and a hardcoded AddOns misses it on a case-sensitive
        // filesystem.
        std::error_code ec;
        fs::path p(addonsPath);
        if (!fs::is_directory(p, ec)) {
            p = resolvePath(fs::path(addonsPath).parent_path().parent_path(),
                            "interface/AddOns");
        }
        if (!p.empty()) roots.emplace_back(p);
    }
    std::error_code rec;
    for (const char* local : {"addons", "../addons", "../../addons"}) {
        fs::path p = fs::absolute(local, rec);
        if (fs::is_directory(p, rec)) roots.push_back(fs::weakly_canonical(p, rec));
    }

    int scannedDirs = 0, loadOnDemand = 0, noToc = 0;
    std::vector<fs::path> dirs;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            LOG_INFO("AddonManager: no AddOns directory at ", root.string());
            continue;
        }
        LOG_INFO("AddonManager: searching ", root.string());
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (entry.is_directory()) dirs.push_back(entry.path());
        }
    }
    // Sort alphabetically for deterministic load order
    std::sort(dirs.begin(), dirs.end());

    // One addon per name, however many roots supply it. Searching more than one
    // place means the same addon can be found twice — a copy staged beside the
    // executable and the original it was staged from, say — and loading both
    // runs its Lua twice, which builds two of every frame. They sit exactly on
    // top of each other, so it reads as one frame that will not hide: the
    // toggle hides the copy it has a handle to and the other stays.
    std::set<std::string> seen;
    int duplicates = 0;

    for (const auto& dir : dirs) {
        ++scannedDirs;
        std::string dirName = dir.filename().string();
        std::string tocPath = (dir / (dirName + ".toc")).string();
        auto toc = parseTocFile(tocPath);
        if (!toc) { ++noToc; continue; }

        if (toc->isLoadOnDemand()) {
            ++loadOnDemand;
            continue;
        }

        if (!seen.insert(toc->addonName).second) {
            ++duplicates;
            LOG_INFO("AddonManager: '", toc->addonName, "' already found elsewhere; "
                     "ignoring the copy at ", dir.string());
            continue;
        }

        LOG_INFO("AddonManager: registered addon '", toc->getTitle(),
                 "' (", toc->files.size(), " files) from ", dir.string());
        addons_.push_back(std::move(*toc));
    }

    // Say what happened even when nothing loads, which is the case that used to
    // be silent: every Blizzard addon in a stock Interface directory is
    // LoadOnDemand, so a scan can look at dozens of folders, register none of
    // them, and print one line that reads like an empty directory.
    LOG_INFO("AddonManager: scanned ", scannedDirs, " directories, registered ",
             addons_.size(), " addons (", loadOnDemand, " load-on-demand, ",
             noToc, " without a .toc, ", duplicates, " duplicate)");
    // Load persisted enable/disable choices now that we know which addons exist.
    loadEnabledState();
}

void AddonManager::loadAllAddons() {
    // The original interface, when asked for. Before any addon, because addons
    // are written against a world where FrameXML has already defined its frames
    // and its several thousand functions.
    //
    // Off by default and deliberately so: this is an experiment for now, it
    // wants the missing-API fallback on beside it to get anywhere, and a
    // half-loaded FrameXML on top of the client's own interface is not a state
    // anyone wants to be in by accident.
    const char* wantFrameXml = std::getenv("WOWEE_LOAD_FRAMEXML");
    if (wantFrameXml && *wantFrameXml && std::string(wantFrameXml) != "0" &&
        !frameXmlDir_.empty()) {
        loadFrameXml(frameXmlDir_);
    }

    // Only hand the Lua VM the addons that are actually enabled, so disabled ones
    // don't appear via GetNumAddOns/IsAddOnLoaded either.
    std::vector<TocFile> enabled;
    enabled.reserve(addons_.size());
    for (const auto& addon : addons_) {
        if (isAddonEnabled(addon.addonName)) enabled.push_back(addon);
    }
    luaEngine_.setAddonList(enabled);
    int loaded = 0, failed = 0, skipped = 0;
    for (const auto& addon : addons_) {
        if (!isAddonEnabled(addon.addonName)) {
            LOG_INFO("AddonManager: skipping disabled addon: ", addon.addonName);
            skipped++;
            continue;
        }
        if (loadAddon(addon)) loaded++;
        else failed++;
    }
    addonsLoaded_ = true;
    LOG_INFO("AddonManager: loaded ", loaded, " addons",
             (failed > 0 ? (", " + std::to_string(failed) + " failed") : ""),
             (skipped > 0 ? (", " + std::to_string(skipped) + " disabled") : ""));
}

// ---- Per-addon enable/disable (persisted) ----------------------------------

bool AddonManager::isAddonEnabled(const std::string& addonName) const {
    auto it = addonEnabled_.find(addonName);
    return (it == addonEnabled_.end()) ? true : it->second;  // default: enabled
}

void AddonManager::setAddonEnabled(const std::string& addonName, bool enabled) {
    addonEnabled_[addonName] = enabled;
    saveEnabledState();
}

std::string AddonManager::enabledStatePath() {
    return core::getConfigRoot() + "/addons.cfg";
}

void AddonManager::loadEnabledState() {
    std::ifstream in(enabledStatePath());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!name.empty()) addonEnabled_[name] = (val == "1");
    }
}

void AddonManager::saveEnabledState() const {
    std::ofstream out(enabledStatePath(), std::ios::trunc);
    if (!out) {
        LOG_WARNING("AddonManager: could not write ", enabledStatePath());
        return;
    }
    // Persist an explicit line only for addons we actually know about, so stale
    // entries for removed addons don't accumulate.
    for (const auto& addon : addons_) {
        out << addon.addonName << "=" << (isAddonEnabled(addon.addonName) ? "1" : "0") << "\n";
    }
}

std::string AddonManager::getSavedVariablesPath(const TocFile& addon) const {
    return addon.basePath + "/" + addon.addonName + ".lua.saved";
}

std::string AddonManager::getSavedVariablesPerCharacterPath(const TocFile& addon) const {
    if (characterName_.empty()) return "";
    return addon.basePath + "/" + addon.addonName + "." + characterName_ + ".lua.saved";
}

bool AddonManager::loadFrameXml(const std::string& frameXmlDir) {
    std::error_code ec;
    std::filesystem::path dir(frameXmlDir);
    if (!std::filesystem::is_directory(dir, ec)) {
        // The directory itself may be spelled differently on disk.
        dir = resolvePath(std::filesystem::path(frameXmlDir).parent_path(),
                          std::filesystem::path(frameXmlDir).filename().string());
    }
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        LOG_WARNING("FrameXML: no directory at ", frameXmlDir);
        return false;
    }
    const std::filesystem::path tocPath = resolveChild(dir, "FrameXML.toc");
    auto toc = tocPath.empty() ? std::nullopt : parseTocFile(tocPath.string());
    if (!toc) {
        LOG_WARNING("FrameXML: no manifest in ", dir.string());
        return false;
    }
    const std::string resolvedDir = dir.string();

    LOG_WARNING("FrameXML: attempting to load the original interface — ",
                toc->files.size(), " files from ", resolvedDir);

    int lua = 0, xml = 0, failed = 0;
    for (const auto& filename : toc->files) {
        std::string lower = filename;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::filesystem::path resolved = resolvePath(dir, filename);
        if (resolved.empty()) {
            LOG_WARNING("FrameXML: ", filename, " is listed but not on disk");
            ++failed;
            continue;
        }
        const std::string full = resolved.string();

        // The manifest's order is the load order and matters: GlobalStrings and
        // Constants before anything reads them, Fonts before the frames that
        // inherit from them. Following it is most of what makes this possible
        // at all.
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".lua") == 0) {
            if (luaEngine_.executeFile(full)) ++lua; else { ++failed;
                LOG_ERROR("FrameXML: ", filename, " failed"); }
        } else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".xml") == 0) {
            if (loadXmlFile(full, 0)) ++xml; else ++failed;
        }
    }
    LOG_WARNING("FrameXML: ", lua, " Lua files and ", xml, " XML files loaded, ",
                failed, " failed");
    return failed == 0;
}

bool AddonManager::loadXmlFile(const std::string& path, int depth) {
    // Includes nest, and a file that includes itself would otherwise recurse
    // until the stack gives out.
    constexpr int kMaxDepth = 16;
    if (depth > kMaxDepth) {
        LOG_ERROR("AddonManager: include nesting too deep at ", path);
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LOG_WARNING("AddonManager: XML not found: ", path);
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    ui::XmlNode root;
    std::string error;
    if (!ui::parseXml(buffer.str(), root, error)) {
        LOG_ERROR("AddonManager: ", path, ": ", error);
        return false;
    }

    ui::EmitResult emitted = ui::emitFrameXml(root);
    for (const auto& w : emitted.warnings) {
        LOG_WARNING("AddonManager: ", path, ": ", w);
    }

    const std::string dir = fs::path(path).parent_path().string();
    bool ok = true;

    // Order matters and is not the order the emitter reports things in. Includes
    // carry the templates a file inherits from, and scripts define the functions
    // its handlers name, so both have to be in place before any frame is built.
    for (const auto& inc : emitted.includeFiles) {
        if (!loadXmlFile(dir + "/" + inc, depth + 1)) ok = false;
    }
    for (const auto& script : emitted.scriptFiles) {
        if (!luaEngine_.executeFile(dir + "/" + script)) {
            LOG_ERROR("AddonManager: ", path, " referenced ", script, " which failed");
            ok = false;
        }
    }
    if (!emitted.lua.empty()) {
        if (!luaEngine_.executeString(emitted.lua)) {
            LOG_ERROR("AddonManager: frames from ", path, " failed to build");
            ok = false;
        } else {
            LOG_INFO("AddonManager: built frames from ", path);
        }
    }
    return ok;
}

bool AddonManager::loadAddon(const TocFile& addon) {
    // Load SavedVariables before addon code (so globals are available at load time)
    auto savedVars = addon.getSavedVariables();
    if (!savedVars.empty()) {
        std::string svPath = getSavedVariablesPath(addon);
        luaEngine_.loadSavedVariables(svPath);
        LOG_DEBUG("AddonManager: loaded saved variables for '", addon.addonName, "'");
    }
    // Load per-character SavedVariables
    auto savedVarsPC = addon.getSavedVariablesPerCharacter();
    if (!savedVarsPC.empty()) {
        std::string svpcPath = getSavedVariablesPerCharacterPath(addon);
        if (!svpcPath.empty()) {
            luaEngine_.loadSavedVariables(svpcPath);
            LOG_DEBUG("AddonManager: loaded per-character saved variables for '", addon.addonName, "'");
        }
    }

    bool success = true;
    for (const auto& filename : addon.files) {
        std::string lower = filename;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".lua") {
            std::string fullPath = addon.basePath + "/" + filename;
            if (!luaEngine_.executeFile(fullPath)) {
                LOG_ERROR("AddonManager: '", addon.addonName, "' failed on ", filename);
                success = false;
            } else {
                LOG_INFO("AddonManager: ran ", addon.addonName, "/", filename);
            }
        } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xml") {
            if (!loadXmlFile(addon.basePath + "/" + filename, 0)) success = false;
        }
    }

    // Fire ADDON_LOADED event after all addon files are executed
    // This is the standard WoW pattern for addon initialization
    if (success) {
        luaEngine_.fireEvent("ADDON_LOADED", {addon.addonName});
    }
    return success;
}

bool AddonManager::runScript(const std::string& code) {
    return luaEngine_.executeString(code);
}

void AddonManager::fireEvent(const std::string& event, const std::vector<std::string>& args) {
    luaEngine_.fireEvent(event, args);
}

void AddonManager::update(float deltaTime) {
    luaEngine_.dispatchOnUpdate(deltaTime);
}

void AddonManager::saveAllSavedVariables() {
    for (const auto& addon : addons_) {
        auto savedVars = addon.getSavedVariables();
        if (!savedVars.empty()) {
            std::string svPath = getSavedVariablesPath(addon);
            luaEngine_.saveSavedVariables(svPath, savedVars);
        }
        auto savedVarsPC = addon.getSavedVariablesPerCharacter();
        if (!savedVarsPC.empty()) {
            std::string svpcPath = getSavedVariablesPerCharacterPath(addon);
            if (!svpcPath.empty()) {
                luaEngine_.saveSavedVariables(svpcPath, savedVarsPC);
            }
        }
    }
}

bool AddonManager::reload() {
    LOG_INFO("AddonManager: reloading all addons...");
    saveAllSavedVariables();
    addons_.clear();
    luaEngine_.shutdown();

    if (!luaEngine_.initialize()) {
        LOG_ERROR("AddonManager: failed to reinitialize Lua VM during reload");
        return false;
    }
    luaEngine_.setGameHandler(gameHandler_);
    luaEngine_.setLuaServices(luaServices_);

    if (!addonsPath_.empty()) {
        scanAddons(addonsPath_);
        loadAllAddons();
    }
    LOG_INFO("AddonManager: reload complete");
    return true;
}

void AddonManager::shutdown() {
    saveAllSavedVariables();
    addons_.clear();
    luaEngine_.shutdown();
}

} // namespace wowee::addons
