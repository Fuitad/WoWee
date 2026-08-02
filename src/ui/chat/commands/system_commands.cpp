// System commands: /run, /dump, /reload, /stopmacro, /clear, /logout
// Moved from ChatPanel::sendChatMessage() if/else chain (Phase 3).
#include "ui/chat/i_chat_command.hpp"
#include "ui/chat/macro_evaluator.hpp"
#include "ui/chat_panel.hpp"
#include "ui/ui_services.hpp"
#include "game/game_handler.hpp"
#include "addons/addon_manager.hpp"
#include "core/application.hpp"
#include "ui/framexml_takeover.hpp"
#include <algorithm>
#include <cctype>

namespace wowee { namespace ui {

// --- /run, /script ---
class RunCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        if (ctx.args.empty()) return {false, false};
        auto* am = ctx.services.addonManager;
        if (am) {
            am->runScript(ctx.args);
        } else {
            ctx.gameHandler.addUIError("Addon system not initialized.");
        }
        return {};
    }
    std::vector<std::string> aliases() const override { return {"run", "script"}; }
    std::string helpText() const override { return "Execute Lua code"; }
};

// --- /dump, /print ---
class DumpCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        if (ctx.args.empty()) return {false, false};
        auto* am = ctx.services.addonManager;
        if (am && am->isInitialized()) {
            // Wrap expression in print(tostring(...)) to display the value
            std::string wrapped = "local __v = " + ctx.args +
                "; if type(__v) == 'table' then "
                "  local parts = {} "
                "  for k,v in pairs(__v) do parts[#parts+1] = tostring(k)..'='..tostring(v) end "
                "  print('{' .. table.concat(parts, ', ') .. '}') "
                "else print(tostring(__v)) end";
            am->runScript(wrapped);
        } else {
            game::MessageChatData errMsg;
            errMsg.type = game::ChatType::SYSTEM;
            errMsg.language = game::ChatLanguage::UNIVERSAL;
            errMsg.message = "Addon system not initialized.";
            ctx.gameHandler.addLocalChatMessage(errMsg);
        }
        return {};
    }
    std::vector<std::string> aliases() const override { return {"dump", "print"}; }
    std::string helpText() const override { return "Evaluate & print Lua expression"; }
};

// --- /reload, /reloadui, /rl ---
class ReloadCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        auto* am = ctx.services.addonManager;
        if (am) {
            am->reload();
            am->fireEvent("VARIABLES_LOADED");
            am->fireEvent("PLAYER_LOGIN");
            am->fireEvent("PLAYER_ENTERING_WORLD");
            game::MessageChatData rlMsg;
            rlMsg.type = game::ChatType::SYSTEM;
            rlMsg.language = game::ChatLanguage::UNIVERSAL;
            rlMsg.message = "Interface reloaded.";
            ctx.gameHandler.addLocalChatMessage(rlMsg);
        } else {
            game::MessageChatData rlMsg;
            rlMsg.type = game::ChatType::SYSTEM;
            rlMsg.language = game::ChatLanguage::UNIVERSAL;
            rlMsg.message = "Addon system not available.";
            ctx.gameHandler.addLocalChatMessage(rlMsg);
        }
        return {};
    }
    std::vector<std::string> aliases() const override { return {"reload", "reloadui", "rl"}; }
    std::string helpText() const override { return "Reload all addons"; }
};

// --- /fxcheck ---
class FrameXmlCheckCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        frameXmlRequestCheck();

        // What the interface itself can see, asked through the same bindings
        // FrameXML uses. The widget report says whether a frame is shown; it
        // cannot say whether it *should* be, and those are the two readings
        // that have to be told apart. A hidden target frame is correct with no
        // target and a fault with one, and from the tree alone they are the
        // same line. print() reaches the log, so the answer lands beside the
        // report it belongs to.
        if (auto* am = ctx.services.addonManager) {
            am->runScript(
                "local function yn(v) return v and 'yes' or 'no' end\n"
                "local auras = 0\n"
                "for i = 1, 40 do if not UnitAura('player', i) then break end auras = i end\n"
                "print('[fxcheck] target=' .. yn(UnitExists('target')) ..\n"
                "      ' name=' .. tostring(UnitExists('target') and UnitName('target')) ..\n"
                "      ' | TargetFrame shown=' .. yn(TargetFrame and TargetFrame:IsShown()) ..\n"
                "      ' unit=' .. tostring(TargetFrame and TargetFrame.unit) ..\n"
                "      ' | player auras=' .. auras ..\n"
                "      ' BuffButton1=' .. tostring(BuffButton1 ~= nil) ..\n"
                "      ' | XP=' .. tostring(UnitXP('player')) .. '/' .. tostring(UnitXPMax('player')) ..\n"
                "      ' | bag0 slots=' .. tostring(GetContainerNumSlots(0)))\n");
        }

        game::MessageChatData msg;
        msg.type = game::ChatType::SYSTEM;
        msg.language = game::ChatLanguage::UNIVERSAL;
        msg.message = "FrameXML takeover check written to the log.";
        ctx.gameHandler.addLocalChatMessage(msg);
        return {};
    }
    std::vector<std::string> aliases() const override { return {"fxcheck"}; }
    std::string helpText() const override {
        return "Report the FrameXML takeover check for the interface as it is now";
    }
};

// --- /stopmacro [conditions] ---
class StopMacroCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        bool shouldStop = true;
        if (!ctx.args.empty()) {
            std::string condArg = ctx.args;
            while (!condArg.empty() && condArg.front() == ' ') condArg.erase(condArg.begin());
            if (!condArg.empty() && condArg.front() == '[') {
                // Append a sentinel action so evaluateMacroConditionals can signal a match.
                uint64_t tgtOver = static_cast<uint64_t>(-1);
                std::string hit = evaluateMacroConditionals(condArg + " __stop__", ctx.gameHandler, tgtOver);
                shouldStop = !hit.empty();
            }
        }
        if (shouldStop) ctx.panel.macroStopped() = true;
        return {};
    }
    std::vector<std::string> aliases() const override { return {"stopmacro"}; }
    std::string helpText() const override { return "Stop macro execution"; }
};

// --- /clear ---
class ClearCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        ctx.gameHandler.clearChatHistory();
        return {};
    }
    std::vector<std::string> aliases() const override { return {"clear"}; }
    std::string helpText() const override { return "Clear chat history"; }
};

// --- /difficulty ---
class DifficultyCommand : public IChatCommand {
public:
    ChatCommandResult execute(ChatCommandContext& ctx) override {
        std::string arg = ctx.args;
        // Trim whitespace
        size_t first = arg.find_first_not_of(" \t");
        size_t last  = arg.find_last_not_of(" \t");
        if (first != std::string::npos)
            arg = arg.substr(first, last - first + 1);
        else
            arg.clear();
        for (auto& ch : arg) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        uint32_t diff = 0;
        bool valid = true;
        if (arg == "normal" || arg == "0")         diff = 0;
        else if (arg == "heroic" || arg == "1")    diff = 1;
        else if (arg == "25" || arg == "25normal" || arg == "25man" || arg == "2")
            diff = 2;
        else if (arg == "25heroic" || arg == "25manheroic" || arg == "3")
            diff = 3;
        else valid = false;

        if (!valid || arg.empty()) {
            game::MessageChatData msg;
            msg.type = game::ChatType::SYSTEM;
            msg.language = game::ChatLanguage::UNIVERSAL;
            msg.message = "Usage: /difficulty normal|heroic|25|25heroic  (0-3)";
            ctx.gameHandler.addLocalChatMessage(msg);
        } else {
            static constexpr const char* kDiffNames[] = {
                "Normal (5-man)", "Heroic (5-man)", "Normal (25-man)", "Heroic (25-man)"
            };
            game::MessageChatData msg;
            msg.type = game::ChatType::SYSTEM;
            msg.language = game::ChatLanguage::UNIVERSAL;
            msg.message = std::string("Setting difficulty to: ") + kDiffNames[diff];
            ctx.gameHandler.addLocalChatMessage(msg);
            ctx.gameHandler.sendSetDifficulty(diff);
        }
        return {};
    }
    std::vector<std::string> aliases() const override { return {"difficulty"}; }
    std::string helpText() const override { return "Set dungeon difficulty"; }
};

// --- Registration ---
void registerSystemCommands(ChatCommandRegistry& reg) {
    reg.registerCommand(std::make_unique<RunCommand>());
    reg.registerCommand(std::make_unique<DumpCommand>());
    reg.registerCommand(std::make_unique<ReloadCommand>());
    reg.registerCommand(std::make_unique<FrameXmlCheckCommand>());
    reg.registerCommand(std::make_unique<StopMacroCommand>());
    reg.registerCommand(std::make_unique<ClearCommand>());
    reg.registerCommand(std::make_unique<DifficultyCommand>());
}

} // namespace ui
} // namespace wowee
