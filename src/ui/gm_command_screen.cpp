// ============================================================
// GM command screen — browse/search the GM command reference and
// send commands to the server (part of WindowManager). Opened from
// the micro-menu "GM" button.
//
// Commands are sent to the server as SAY chat messages with a "."
// prefix (AzerothCore convention); the server does the real work and
// enforces the player's actual permission level. This screen is just a
// discoverable front-end over the kGmCommands reference table.
// ============================================================
#include "ui/window_manager.hpp"
#include "ui/chat/gm_command_data.hpp"
#include "game/game_handler.hpp"
#include "game/world_packets.hpp" // game::ChatType
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

namespace wowee {
namespace ui {

namespace {

const char* securityLabel(uint8_t s) {
    switch (s) {
        case 0:  return "Player";
        case 1:  return "Moderator";
        case 2:  return "Game Master";
        case 3:  return "Administrator";
        default: return "Console";
    }
}

ImVec4 securityColor(uint8_t s) {
    switch (s) {
        case 0:  return ImVec4(0.62f, 0.62f, 0.62f, 1.0f); // gray
        case 1:  return ImVec4(0.35f, 0.80f, 0.35f, 1.0f); // green
        case 2:  return ImVec4(0.35f, 0.62f, 1.00f, 1.0f); // blue
        case 3:  return ImVec4(0.78f, 0.50f, 1.00f, 1.0f); // purple
        default: return ImVec4(1.00f, 0.55f, 0.20f, 1.0f); // orange
    }
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// First whitespace-delimited token of a command name, used to group the list
// (e.g. "gm on"/"gm off" → "gm", "go xyz"/"go creature" → "go").
std::string_view firstToken(std::string_view name) {
    size_t sp = name.find(' ');
    return sp == std::string_view::npos ? name : name.substr(0, sp);
}

} // namespace

void WindowManager::renderGmCommandScreen(game::GameHandler& gameHandler) {
    if (!showGmCommandScreen_) return;

    ImGui::SetNextWindowSize(ImVec2(660, 470), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("GM Commands", &open)) {
        ImGui::End();
        if (!open) showGmCommandScreen_ = false;
        return;
    }
    if (!open) showGmCommandScreen_ = false;

    // ---- Toolbar: search + max-level filter ----
    ImGui::SetNextItemWidth(230.0f);
    ImGui::InputTextWithHint("##gmsearch", "Search name or description...",
                             gmSearchBuf_, sizeof(gmSearchBuf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const char* secLabels[] = {"Player", "Moderator", "Game Master", "Administrator", "Console"};
    ImGui::Combo("Max level", &gmMaxSecurity_, secLabels, IM_ARRAYSIZE(secLabels));
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Commands are sent to the server as chat (\".\" prefix).\n"
                          "The server enforces your real permission level, so\n"
                          "listing a command here does not mean you can run it.");
    }

    ImGui::Separator();

    const std::string filter = toLower(gmSearchBuf_);

    auto passesFilters = [&](const GmCommandEntry& c) -> bool {
        if (static_cast<int>(c.security) > gmMaxSecurity_) return false;
        if (filter.empty()) return true;
        return toLower(c.name).find(filter) != std::string::npos ||
               toLower(c.help).find(filter) != std::string::npos;
    };

    auto drawRow = [&](int idx) {
        const auto& c = kGmCommands[idx];
        ImGui::PushID(idx);
        std::string label = "." + std::string(c.name);
        if (ImGui::Selectable(label.c_str(), gmSelectedIndex_ == idx)) {
            gmSelectedIndex_ = idx;
            // Prefill the editable command line with ".<name> " ready for args.
            std::snprintf(gmCommandBuf_, sizeof(gmCommandBuf_), ".%s ",
                          std::string(c.name).c_str());
        }
        if (ImGui::IsItemHovered() && !c.help.empty()) {
            ImGui::SetTooltip("%s\n%s", std::string(c.syntax).c_str(),
                              std::string(c.help).c_str());
        }
        ImGui::PopID();
    };

    // ---- Left: command list ----
    if (ImGui::BeginChild("##gmlist", ImVec2(300.0f, 0.0f), true)) {
        if (!filter.empty()) {
            // Flat filtered list while searching.
            int shown = 0;
            for (int i = 0; i < static_cast<int>(kGmCommands.size()); ++i) {
                if (!passesFilters(kGmCommands[i])) continue;
                drawRow(i);
                ++shown;
            }
            if (shown == 0) ImGui::TextDisabled("No matching commands.");
        } else {
            // Grouped by first token (the table is already ordered by category).
            std::string_view curGroup;
            bool haveGroup = false;
            bool headerOpen = false;
            for (int i = 0; i < static_cast<int>(kGmCommands.size()); ++i) {
                if (static_cast<int>(kGmCommands[i].security) > gmMaxSecurity_) continue;
                std::string_view g = firstToken(kGmCommands[i].name);
                if (!haveGroup || g != curGroup) {
                    curGroup = g;
                    haveGroup = true;
                    headerOpen = ImGui::CollapsingHeader(std::string(g).c_str());
                }
                if (headerOpen) drawRow(i);
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- Right: details + execute ----
    if (ImGui::BeginChild("##gmdetail", ImVec2(0.0f, 0.0f), true)) {
        if (gmSelectedIndex_ < 0 || gmSelectedIndex_ >= static_cast<int>(kGmCommands.size())) {
            ImGui::TextDisabled("Select a command from the list.");
        } else {
            const auto& c = kGmCommands[gmSelectedIndex_];
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), ".%s",
                               std::string(c.name).c_str());
            ImGui::TextColored(securityColor(c.security), "Requires: %s",
                               securityLabel(c.security));
            ImGui::Separator();

            ImGui::TextDisabled("Syntax");
            ImGui::TextWrapped("%s", std::string(c.syntax).c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Description");
            ImGui::TextWrapped("%s", std::string(c.help).c_str());
            ImGui::Separator();

            ImGui::TextDisabled("Command line (edit arguments, then Send)");
            ImGui::SetNextItemWidth(-1.0f);
            const bool entered = ImGui::InputText("##gmcmd", gmCommandBuf_, sizeof(gmCommandBuf_),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            const bool sendClicked = ImGui::Button("Send", ImVec2(90.0f, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("Sent to the server as a chat command.");

            if ((entered || sendClicked) && gmCommandBuf_[0] != '\0') {
                std::string cmd = gmCommandBuf_;
                while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
                if (!cmd.empty()) {
                    gameHandler.sendChatMessage(game::ChatType::SAY, cmd, "");
                    gameHandler.addSystemChatMessage("GM command sent: " + cmd);
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace ui
} // namespace wowee
