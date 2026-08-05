#include "addons/addon_manager.hpp"
#include "ui/framexml_takeover.hpp"
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
#include <chrono>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace wowee::addons {

AddonManager::AddonManager() = default;
AddonManager::~AddonManager() { shutdown(); }

bool AddonManager::initialize(game::GameHandler* gameHandler, const LuaServices& services) {
    gameHandler_ = gameHandler;
    luaServices_ = services;
    // Supplied here rather than by the caller: the manager is what owns the
    // list of load-on-demand addons and the Lua state that asks for them, and
    // wiring it anywhere else would mean handing one to the other.
    luaServices_.loadAddOn = [this](const std::string& name, std::string& reason) {
        return loadAddOnByName(name, reason);
    };
    luaServices_.isAddOnLoaded = [this](const std::string& name) {
        return isAddOnLoadedByName(name);
    };
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
    lodAddons_.clear();
    lodLoaded_.clear();

    // Two places are searched. The game data's own Interface\AddOns is where a
    // player's existing addons already live, and an "addons" directory beside
    // the executable is where this client's own ship without anyone having to
    // copy files into an extracted game install to try them.
    std::vector<fs::path> roots;
    {
        // Same case problem as FrameXML: this install has interface/addons in
        // lower case, and a hardcoded AddOns misses it on a case-sensitive
        // filesystem.
        //
        // Every spelling is taken, not the first that exists. This install has
        // *both* — an empty interface/AddOns beside the interface/addons that
        // holds all twenty-four Blizzard addons — and looking only until one
        // was found stopped at the empty one. Nothing load-on-demand had ever
        // loaded here: no talent frame, no macro frame, no achievements, no
        // key bindings, and Blizzard_GMChatUI reporting itself missing on
        // every login.
        std::error_code ec;
        const fs::path asked(addonsPath);
        if (fs::is_directory(asked, ec)) roots.emplace_back(asked);

        const fs::path interfaceDir = asked.parent_path();
        if (fs::is_directory(interfaceDir, ec)) {
            for (const auto& entry : fs::directory_iterator(interfaceDir, ec)) {
                if (!entry.is_directory(ec)) continue;
                std::string name = entry.path().filename().string();
                for (char& c : name) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (name != "addons") continue;
                if (fs::equivalent(entry.path(), asked, ec)) continue;
                roots.emplace_back(entry.path());
            }
        }
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
        // Said out loud, because which directory this lands on decides what
        // gets loaded and it is resolved differently on a case-insensitive
        // filesystem. A report of the interface appearing when nobody asked
        // for it is unanswerable without knowing where the scan looked.
        LOG_WARNING("AddonManager: scanning for addons in ", root.string());
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
    std::set<std::string> lodSeen;
    int duplicates = 0;

    for (const auto& dir : dirs) {
        ++scannedDirs;
        std::string dirName = dir.filename().string();
        // The original interface is not an addon and must never be loaded as
        // one. It ships with a .toc of its own, so a scan that lands on
        // Data/interface rather than Data/interface/AddOns — which is what a
        // case-insensitive filesystem can produce — would find FrameXML and
        // load the whole of it, with none of the opt-in that is supposed to
        // guard it. It has exactly one way in, and this is not it.
        {
            std::string lower = dirName;
            for (char& c : lower) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower == "framexml" || lower == "gluexml") {
                LOG_WARNING("AddonManager: refusing to load ", dirName,
                            " as an addon — the original interface loads only "
                            "through WOWEE_LOAD_FRAMEXML");
                continue;
            }
        }

        std::string tocPath = (dir / (dirName + ".toc")).string();
        auto toc = parseTocFile(tocPath);
        if (!toc) { ++noToc; continue; }

        if (toc->isLoadOnDemand()) {
            ++loadOnDemand;
            // Kept rather than dropped: LoadAddOn has to be able to find these,
            // and GetAddOnInfo lists them alongside the rest. They are simply
            // not run until something asks.
            if (lodSeen.insert(toc->addonName).second) {
                lodAddons_.push_back(*toc);
            }
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
    // On by default on this branch, which exists to bring it up: the elements
    // it has taken over are the ones being worked on, and needing a flag to
    // see them means every test run starts by remembering the flag.
    // WOWEE_LOAD_FRAMEXML=0 turns it off; master leaves it off unless asked.
    const char* wantFrameXml = std::getenv("WOWEE_LOAD_FRAMEXML");
    const bool loadIt = wantFrameXml ? (std::string(wantFrameXml) != "0") : true;
    if (loadIt && !frameXmlDir_.empty()) {
        loadFrameXml(frameXmlDir_);
        // Said once, after the interface is up: anything neither handed over
        // nor hidden is about to be on screen twice.
        ui::frameXmlReportUnaccountedElements();
    }

    // Only hand the Lua VM the addons that are actually enabled, so disabled ones
    // don't appear via GetNumAddOns/IsAddOnLoaded either.
    std::vector<TocFile> enabled;
    enabled.reserve(addons_.size() + lodAddons_.size());
    for (const auto& addon : addons_) {
        if (isAddonEnabled(addon.addonName)) enabled.push_back(addon);
    }
    // Load-on-demand addons are listed too, as WoW lists them: an addon
    // manager panel shows the talent tree and the auction house alongside
    // everything else, and GetNumAddOns counts them. They carry a flag so
    // IsAddOnLoaded does not mistake being listed for being loaded.
    for (const auto& addon : lodAddons_) {
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

    // Bindings are not in the manifest — the real client loads them itself, and
    // before the interface, so that a script asking what a command is bound to
    // during load gets an answer. Without this the file was never read at all
    // and the key bindings list had nothing to list.
    if (const auto bindings = resolveChild(dir, "Bindings.xml"); !bindings.empty()) {
        if (!loadXmlFile(bindings.string(), 0)) {
            LOG_WARNING("FrameXML: could not read the key bindings: ", lastXmlError_);
        }
    }

    int lua = 0, xml = 0, failed = 0;
    // Kept and printed together at the end. Spread through the log these are
    // unreadable: the reasons land among thousands of other lines, and one
    // broken script takes down every file that references it, so what matters
    // is seeing them side by side and spotting the cause they share.
    std::vector<std::pair<std::string, std::string>> failures;
    // Timed per file. This load runs on the main thread during world entry, so
    // whatever it costs the client is frozen for — long enough and the server
    // drops the connection for want of a heartbeat. Knowing it is slow is not
    // useful; knowing which file is.
    const auto loadStart = std::chrono::steady_clock::now();
    // Generous: all 139 files together used to run in 216ms, so a single one
    // reaching this has stopped making progress. Aborting it costs that file
    // and keeps the client answering, which beats freezing until it is killed.
    luaEngine_.setChunkTimeoutMs(5000);
    struct BudgetReset {
        LuaEngine& e;
        ~BudgetReset() { e.setChunkTimeoutMs(0); }
    } budgetReset{luaEngine_};
    auto sinceMs = [](std::chrono::steady_clock::time_point from) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - from).count();
    };
    for (const auto& filename : toc->files) {
        const auto fileStart = std::chrono::steady_clock::now();
        // Named before it is loaded, not after. Timing it afterwards says
        // nothing about the one case that matters — a file that never returns
        // prints nothing at all, and the load simply stops with the last
        // successful file as the only clue.
        // At warning level because release builds drop INFO, and this is the
        // one line that identifies a file which never returns. Noisy for 139
        // files, and worth it only while this path is still experimental.
        LOG_WARNING("FrameXML: loading ", filename);
        std::string lower = filename;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::filesystem::path resolved = resolvePath(dir, filename);
        if (resolved.empty()) {
            LOG_WARNING("FrameXML: ", filename, " is listed but not on disk");
            ++failed;
            failures.emplace_back(filename, "listed in the manifest but not on disk");
            continue;
        }
        const std::string full = resolved.string();

        // The manifest's order is the load order and matters: GlobalStrings and
        // Constants before anything reads them, Fonts before the frames that
        // inherit from them. Following it is most of what makes this possible
        // at all.
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".lua") == 0) {
            if (luaEngine_.executeFile(full)) {
                ++lua;
            } else {
                ++failed;
                failures.emplace_back(filename, luaEngine_.lastError());
            }
        } else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".xml") == 0) {
            lastXmlError_.clear();
            if (loadXmlFile(full, 0)) {
                ++xml;
            } else {
                ++failed;
                failures.emplace_back(filename, lastXmlError_.empty()
                                                    ? "(no reason recorded)"
                                                    : lastXmlError_);
            }
        }
        // Reported as it happens rather than only in the summary, because a
        // load that never reaches the summary is exactly the case worth
        // diagnosing.
        if (const auto ms = sinceMs(fileStart); ms >= 250) {
            LOG_WARNING("FrameXML: ", filename, " took ", ms, "ms");
        }
    }
    // Screen insets the panel manager reads straight off UIParent. The real
    // client supplies these; FrameXML only ever reads them, and
    // UIParentManageFramePositions adds them to a coordinate on the next line,
    // so absent they are arithmetic on nil the first time a panel opens.
    //
    // All six of them, because setting one wakes the panel manager and it then
    // reads the rest: seeding only the two offsets moved the failure from
    // LEFT_OFFSET to DEFAULT_FRAME_WIDTH one call deeper. The widths are
    // Blizzard's own defaults for a standard panel.
    luaEngine_.executeString(
        std::string("__WoweeOwnsGameMenu = ") +
        (ui::frameXmlOwns(ui::UiElement::GameMenu) ? "true" : "false") + "\n");

    // The game-menu button opens this client's settings.
    //
    // ToggleGameMenu is FrameXML's own function and it shows GameMenuFrame,
    // which is suppressed — so the button did nothing at all. Replaced rather
    // than hooked, and only while this client owns that panel: with
    // WOWEE_FRAMEXML_UI=gamemenu the original menu is drawn and should be the
    // one that answers.
    //
    // After FrameXML has loaded, because a definition written before it would
    // simply be overwritten by uiparent.lua.
    luaEngine_.executeString(
        "if not __WoweeOwnsGameMenu and __WoweeOpenClientSettings then\n"
        "  ToggleGameMenu = function() __WoweeOpenClientSettings() end\n"
        "end\n");

    // Where FrameXML's own chat output goes when this client owns the chat.
    //
    // Twenty-nine places write through DEFAULT_CHAT_FRAME:AddMessage — the
    // ready check's "you were away", the battleground countdowns, the world
    // state warnings, uiparent's four. That name is ChatFrame1: chatframe.lua
    // assigns it at file scope and ChatFrame1's own OnLoad assigns it again,
    // and suppression only stops the frame being drawn, so every one of those
    // lines was being stored on a hidden frame and never seen.
    //
    // Redirected rather than replaced. A bare table would raise the moment
    // something asked for GetID, GetWidth, GetFont, SetPoint or
    // IsUserPlaced — all of which FrameXML calls on this — so the frames stay
    // frames and only AddMessage is pointed elsewhere. A field on the table
    // wins over the metatable's method, which is what makes that work.
    //
    // All ten windows, not only the first: ChatFrame_MessageEventHandler
    // writes to whichever window a message type is registered on.
    luaEngine_.executeString(
        std::string("__WoweeOwnsChat = ") +
        (ui::frameXmlOwns(ui::UiElement::Chat) ? "true" : "false") + "\n");
    luaEngine_.executeString(
        "if not __WoweeOwnsChat and __WoweeClientChatAddMessage then\n"
        "  for i = 1, 10 do\n"
        "    local f = _G['ChatFrame' .. i]\n"
        "    if type(f) == 'table' then\n"
        "      f.AddMessage = __WoweeClientChatAddMessage\n"
        "    end\n"
        "  end\n"
        "end\n");


    luaEngine_.executeString(
        "if UIParent and UIParent.SetAttribute then\n"
        "  local defaults = {\n"
        "    TOP_OFFSET = 0, LEFT_OFFSET = 0, CENTER_OFFSET = 0,\n"
        "    RIGHT_OFFSET = 0, RIGHT_OFFSET_BUFFER = 0,\n"
        "    DEFAULT_FRAME_WIDTH = 338,\n"
        "  }\n"
        // Written straight into the attribute table rather than through
        // SetAttribute, which fires OnAttributeChanged: the panel manager runs
        // on the first one and reads the rest before the loop has set them, so
        // seeding through the setter failed on whichever name pairs() happened
        // to leave for last. These are initial values, not changes.
        "  local store = rawget(UIParent, '__attributes')\n"
        "  if not store then store = {}; rawset(UIParent, '__attributes', store) end\n"
        "  for name, value in pairs(defaults) do\n"
        "    if store[name] == nil then store[name] = value end\n"
        "  end\n"
        "end\n");

    // Let the bag windows and the character sheet be dragged around.
    //
    // A deliberate departure from 3.3.5, where neither can be moved: the bags
    // arrange themselves up the right-hand side and the character sheet is a
    // fixed panel. Asked for, and harmless — the item buttons inside them are
    // what a drag starting on a slot picks up, because a drag belongs to the
    // frame the press landed on.
    luaEngine_.executeString(
        "local function draggable(f)\n"
        "  if not f then return end\n"
        "  f:SetMovable(true)\n"
        "  f:EnableMouse(true)\n"
        "  f:RegisterForDrag('LeftButton')\n"
        "  f:SetScript('OnDragStart', function(self) self:StartMoving() end)\n"
        "  f:SetScript('OnDragStop', function(self) self:StopMovingOrSizing() end)\n"
        "end\n"
        "for i = 1, 13 do draggable(_G['ContainerFrame' .. i]) end\n"
        "draggable(CharacterFrame)\n");

    LOG_WARNING("FrameXML: ", lua, " Lua files and ", xml, " XML files loaded, ",
                failed, " failed in ", sinceMs(loadStart), "ms");
    for (const auto& [file, why] : failures) {
        LOG_WARNING("FrameXML:   ", file, " — ", why);
    }
    return failed == 0;
}

bool AddonManager::loadXmlFile(const std::string& path, int depth) {
    // Includes nest, and a file that includes itself would otherwise recurse
    // until the stack gives out.
    constexpr int kMaxDepth = 16;
    if (depth > kMaxDepth) {
        lastXmlError_ = "include nesting too deep";
        LOG_ERROR("AddonManager: include nesting too deep at ", path);
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        lastXmlError_ = "not on disk";
        LOG_WARNING("AddonManager: XML not found: ", path);
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    ui::XmlNode root;
    std::string error;
    if (!ui::parseXml(buffer.str(), root, error)) {
        lastXmlError_ = "XML parse: " + error;
        LOG_ERROR("AddonManager: ", path, ": ", error);
        return false;
    }

    ui::EmitResult emitted = ui::emitFrameXml(root);
    for (const auto& w : emitted.warnings) {
        LOG_WARNING("AddonManager: ", path, ": ", w);
    }

    // The Lua the XML became, on disk, when asked for.
    //
    // Everything downstream of here reads as a Lua problem — a global that is
    // nil, a frame with no size — and the answer is nearly always in what the
    // emitter wrote rather than in what the script did with it. Reading it is
    // the difference between finding a mis-substituted $parent in one grep and
    // inferring it from a frame that ended up in the wrong place.
    if (const char* dumpDir = std::getenv("WOWEE_FRAMEXML_EMIT_DIR")) {
        if (*dumpDir) {
            std::error_code ec;
            fs::create_directories(dumpDir, ec);
            const fs::path out =
                fs::path(dumpDir) / (fs::path(path).filename().string() + ".lua");
            std::ofstream f(out);
            if (f) f << emitted.lua;
        }
    }

    const fs::path dir = fs::path(path).parent_path();
    bool ok = true;

    // Resolved without regard to case, the same as the manifest's own files. A
    // Script element says MovieFrame.lua and the file on disk is
    // movieframe.lua, so joining the two naively fails — which took out most of
    // FrameXML on the first attempt, one referenced script at a time.
    auto sibling = [&](const std::string& rawName) {
        // Windows separators, because the interface is written with them. On
        // anything else a backslash is an ordinary character in a filename, so
        // "..\\..\\FrameXML\\UIPanelTemplates.xml" resolved to nothing and the
        // include silently failed — which failed the file that asked for it,
        // and the guild bank's own XML is one of the two that do.
        std::string name = rawName;
        std::replace(name.begin(), name.end(), '\\', '/');

        // Relative to the file that named it first.
        if (fs::path p = resolvePath(dir, name); !p.empty()) return p;

        // Then FrameXML itself. An addon includes a shared template by bare
        // name — inspectpvpframe.xml asks for PVPFrameTemplates.xml — and by a
        // path back out of its own folder, and both mean the same place.
        const fs::path base = fs::path(frameXmlDir_);
        if (!base.empty()) {
            if (fs::path p = resolvePath(base, fs::path(name).filename().string());
                !p.empty()) {
                return p;
            }
        }
        return dir / name;
    };

    // Order matters and is not the order the emitter reports things in. Includes
    // carry the templates a file inherits from, and scripts define the functions
    // its handlers name, so both have to be in place before any frame is built.
    // A file is only as loadable as what it pulls in, so the reason kept here is
    // the first real one — the include or script that actually broke — rather
    // than the name of whichever file happened to reference it.
    for (const auto& inc : emitted.includeFiles) {
        if (!loadXmlFile(sibling(inc).string(), depth + 1)) {
            if (ok) lastXmlError_ = "include " + inc + ": " + lastXmlError_;
            ok = false;
        }
    }
    for (const auto& script : emitted.scriptFiles) {
        if (!luaEngine_.executeFile(sibling(script).string())) {
            if (ok) lastXmlError_ = "script " + script + ": " + luaEngine_.lastError();
            LOG_ERROR("AddonManager: ", path, " referenced ", script, " which failed");
            ok = false;
        }
    }
    if (!emitted.lua.empty()) {
        if (!luaEngine_.executeString(emitted.lua)) {
            if (ok) lastXmlError_ = "frames: " + luaEngine_.lastError();
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

        // Through resolvePath, because a .toc names its files the way Blizzard
        // wrote them — Blizzard_TalentUI.xml — and this install has them in
        // lower case. Concatenating the two finds nothing on a case-sensitive
        // filesystem, which is every one of the Blizzard load-on-demand addons.
        const fs::path resolved = resolvePath(fs::path(addon.basePath), filename);
        const std::string fullPath =
            resolved.empty() ? (addon.basePath + "/" + filename) : resolved.string();

        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".lua") {
            if (!luaEngine_.executeFile(fullPath)) {
                LOG_ERROR("AddonManager: '", addon.addonName, "' failed on ", filename);
                success = false;
            } else {
                LOG_INFO("AddonManager: ran ", addon.addonName, "/", filename);
            }
        } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xml") {
            if (!loadXmlFile(fullPath, 0)) success = false;
        }
    }

    // Fire ADDON_LOADED event after all addon files are executed
    // This is the standard WoW pattern for addon initialization
    if (success) {
        luaEngine_.fireEvent("ADDON_LOADED", {addon.addonName});
    }
    return success;
}

namespace {
std::string lowered(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}
}  // namespace

bool AddonManager::isAddOnLoadedByName(const std::string& name) const {
    return lodLoaded_.count(lowered(name)) != 0;
}

bool AddonManager::loadAddOnByName(const std::string& name, std::string& reason) {
    const std::string key = lowered(name);
    // Already loaded is success, not an error: FrameXML calls LoadAddOn every
    // time a panel is opened and only checks the first return.
    if (lodLoaded_.count(key)) { reason.clear(); return true; }

    const TocFile* found = nullptr;
    for (const TocFile& a : lodAddons_) {
        if (lowered(a.addonName) == key) { found = &a; break; }
    }
    if (!found) {
        // Not on the on-demand list is not the same as not present. An addon
        // whose .toc omits LoadOnDemand is loaded at startup instead, and
        // FrameXML still asks for it by name: uiparent.lua calls
        // UIParentLoadAddOn("Blizzard_TokenUI"), whose .toc has no such line,
        // and that reported MISSING for an addon already running — raising the
        // "Couldn't load" popup over a UI that was working.
        for (const TocFile& a : addons_) {
            if (lowered(a.addonName) != key) continue;
            if (!isAddonEnabled(a.addonName)) { reason = "DISABLED"; return false; }
            // Only once the startup pass has actually run it. Before that it is
            // listed but not loaded, and saying otherwise would have the caller
            // use frames that do not exist yet.
            if (addonsLoaded_) { reason.clear(); return true; }
            break;
        }
        reason = "MISSING";
        return false;
    }
    if (!isAddonEnabled(found->addonName)) { reason = "DISABLED"; return false; }

    // Recorded before loading, not after: the addon's own files run during
    // this call and one of them may call LoadAddOn on the same name, which
    // would otherwise recurse until the stack gave out.
    lodLoaded_.insert(key);
    LOG_INFO("AddonManager: loading on demand: ", found->addonName);
    if (!loadAddon(*found)) {
        reason = "LOAD_ON_DEMAND_ERROR";
        LOG_WARNING("AddonManager: '", found->addonName, "' failed to load on demand");
        // Left in the loaded set deliberately. A half-run addon has already
        // built frames and set globals, and running its files a second time
        // would build them again — the duplicate-frame problem the scan goes
        // out of its way to avoid.
        return false;
    }
    return true;
}

bool AddonManager::runScript(const std::string& code) {
    return luaEngine_.executeString(code);
}

void AddonManager::runInterfaceCommand(const std::string& lua) {
    if (lua.empty()) return;
    if (!luaEngine_.executeString(lua)) {
        LOG_WARNING("interface command failed: ", lua);
    }
}

bool AddonManager::interfaceCommandBoolean(const std::string& expression) {
    if (expression.empty()) return false;
    return luaEngine_.evaluateBoolean(expression);
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
