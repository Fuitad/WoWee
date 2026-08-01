#include "addons/addon_manager.hpp"
#include "core/logger.hpp"
#include "core/config_paths.hpp"
#include <algorithm>
#include <set>
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

void AddonManager::scanAddons(const std::string& addonsPath) {
    addonsPath_ = addonsPath;
    addons_.clear();

    // Two places are searched. The game data's own Interface\AddOns is where a
    // player's existing addons already live, and an "addons" directory beside
    // the executable is where this client's own ship without anyone having to
    // copy files into an extracted game install to try them.
    std::vector<fs::path> roots;
    roots.emplace_back(addonsPath);
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
            // Says so at INFO rather than DEBUG: an addon whose frames are all
            // in XML will load, report success and put nothing on the screen,
            // and there is no way to tell that from a broken addon otherwise.
            LOG_WARNING("AddonManager: '", addon.addonName, "' has XML (", filename,
                        ") which is not loaded yet — anything defined there will be missing");
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
