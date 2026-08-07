// framexml_run — load FrameXML for real and run an expression against it.
//
// Every static sweep in tools/ works on the text: which names exist, which
// arguments line up, which frames are emitted. They are at their floor, and
// the bugs that are left are not visible in text — they are what happens when
// the interface actually runs. Escape opening nothing, the chat box refusing
// focus, a panel that stays empty: each is a Lua error raised inside a handler
// and swallowed, because a handler that raises looks exactly like a handler
// that decided to do nothing.
//
// This is that run, without the client. The addon manager loads the real
// FrameXML through the real emitter into a real Lua state with the real
// bindings, and the only thing missing is a game behind them — every binding
// already guards its GameHandler pointer, so a null one gives the answers of a
// player who is not logged in. That is enough for anything whose fault is in
// the interface rather than in the data, which is what the swallowed errors
// are.
//
//     framexml_run <assetPath> [expression ...]
//
//     framexml_run Data 'ToggleGameMenu()' 'ChatFrame1EditBox:Show()'
//
// Errors are collected rather than printed as they happen, so the load and
// each expression are reported separately: an error during load is a different
// question from an error the expression caused.
//
// Exit status is the number of expressions that raised, capped at 100, so a
// script can ask "did this one still work" without reading the output.

#include "addons/addon_manager.hpp"
#include "ui/widget_renderer.hpp"
#include "ui/interface_fonts.hpp"

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: framexml_run <assetPath> [expression ...]\n"
                     "  e.g. framexml_run Data 'ToggleGameMenu()'\n");
        return 2;
    }
    const std::string assetPath = argv[1];

    // FrameXML owns nothing unless it is asked to, and a harness that owns
    // nothing takes the client's side of every handover — which is not the
    // side being tested. Set before anything reads it.
    ::setenv("WOWEE_FRAMEXML_UI", "all", 0);

    wowee::addons::AddonManager mgr;
    if (!mgr.initialize(nullptr)) {
        std::fprintf(stderr, "framexml_run: Lua would not initialise\n");
        return 2;
    }

    std::vector<std::string> errors;
    if (auto* engine = mgr.getLuaEngine()) {
        engine->setLuaErrorCallback(
            [&errors](const std::string& e) { errors.push_back(e); });
    }

    mgr.setFrameXmlDir(assetPath + "/interface/FrameXML");
    mgr.scanAddons(assetPath + "/interface/AddOns");
    mgr.loadAllAddons();

    std::printf("== load: %zu error(s)\n", errors.size());
    for (const std::string& e : errors) std::printf("   %s\n", e.c_str());

    // Then every addon that waits to be asked for, because loading one is
    // where three faults were hiding and none of them was visible any other
    // way. A load-on-demand addon is loaded whole or not at all: a raise
    // during load loses the file, so the fault presents as a panel that does
    // not exist rather than one that misbehaves — no error on screen, nothing
    // in the log, just a window that never opens. Reported separately from the
    // FrameXML load above, since a broken addon is a smaller thing than a
    // broken interface.
    std::vector<std::string> addonFailures;
    for (const auto& addon : mgr.getLoadOnDemandAddons()) {
        std::string why;
        if (!mgr.loadAddOnByName(addon.addonName, why)) {
            addonFailures.push_back(addon.addonName + " (" +
                                    (why.empty() ? "?" : why) + ")");
        }
    }
    std::printf("== addons: %zu of %zu load-on-demand failed\n",
                addonFailures.size(), mgr.getLoadOnDemandAddons().size());
    for (const std::string& f : addonFailures) std::printf("   %s\n", f.c_str());

    // The events a login fires, which the interface does a great deal of its
    // setting up on. Without them the frames exist and are half-configured,
    // and that reads as breakage: a chat frame whose windows were never
    // updated has no stored alpha, so FCF_FadeInChatFrame does max(nil, ...)
    // and every click on a chat tab raises — which looks exactly like a real
    // fault in the chat, and is not one.
    //
    // Fired after the addons, so a load-on-demand panel that registered for
    // one of them hears it too.
    const size_t beforeEvents = errors.size();
    // One event, and only this one, because it is the only one that pays.
    //
    // Without it the chat frames are half-configured: no stored alpha, so
    // FCF_FadeInChatFrame does max(nil, ...) and every click on a chat tab
    // raises. That looks exactly like a real fault in the chat and is not one,
    // and it is the sort of false lead that costs an afternoon.
    //
    // VARIABLES_LOADED and PLAYER_ENTERING_WORLD were tried here and taken out
    // again. Both assume a character: paperdollframe does
    // strupper(UnitClass("player")) and pvpbattlegroundframe concatenates
    // UnitFactionGroup("player"), and with nobody logged in those are nil and
    // raise. That is an answer about the absence of a player rather than a
    // fault, and permanent errors in this report would drown the real ones —
    // its whole worth is that a nonzero count means something.
    mgr.fireEvent("UPDATE_CHAT_WINDOWS");

    std::printf("== login events: %zu error(s)\n", errors.size() - beforeEvents);
    for (size_t k = beforeEvents; k < errors.size(); ++k) {
        std::printf("   %s\n", errors[k].c_str());
    }

    // Resolve the anchors, so a question about where something ended up has an
    // answer. Nothing drives a render loop here, and without this every frame
    // reports a bottom of zero and a top equal to its own height — which is
    // not a layout, it is the absence of one, and reads as a fault in whatever
    // is being examined.
    //
    // This is the tree's own layout and not the renderer's, so the two passes
    // the renderer runs first are missing: a font string is sized from its text
    // and a tooltip from its lines, and neither has a font here. Frames sized
    // by their anchors and their own dimensions are right; anything whose size
    // comes from text it holds will read as zero.
    // A font, so that a label has a width.
    //
    // ImGui's default atlas is built entirely on the CPU — no device, no
    // window, no backend — and having one turns the renderer's own layout pass
    // from unusable into usable here. That matters more than it sounds: a
    // great deal of FrameXML is positioned against a label's extent rather
    // than a number, and without metrics every one of those labels is zero
    // wide. The auction browse anchors its rarity dropdown to the BOTTOMRIGHT
    // of the "Level Range" caption, so with no font the dropdown lands sixty
    // pixels left of where it belongs, on top of the level boxes — a fault
    // that looks exactly like the real one being investigated and is not it.
    //
    // The typeface is not the game's, so widths are close rather than exact.
    // Close is the difference between "these two frames overlap" being
    // answerable and not.
    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        io.DeltaTime = 1.0f / 60.0f;
        // The game's own faces where they are on disk, so a label measures
        // what it will really measure. Loading a TTF into an atlas is pure
        // ImGui and needs no device, which is the whole reason this is
        // possible here.
        //
        // Without them the substitute is ImGui's built-in face and widths are
        // close rather than exact — close enough to answer "is this frame
        // laid out at all", not close enough to answer "do these two overlap",
        // which is the question that comes up about anything positioned
        // against a caption's right edge.
        int faces = 0;
        for (const char* name : {"frizqt__.ttf", "morpheus.ttf", "skurri.ttf",
                                 "arialn.ttf", "friends.ttf"}) {
            const std::string file = assetPath + "/misc/fonts/" + name;
            if (ImFont* f = io.Fonts->AddFontFromFileTTF(file.c_str(), 16.0f)) {
                wowee::ui::registerInterfaceFace(name, f);
                ++faces;
            }
        }
        if (faces == 0) io.Fonts->AddFontDefault();
        std::printf("== fonts: %d of 5 of the game's own faces\n", faces);
        io.Fonts->Build();
        unsigned char* pixels = nullptr;
        int fw = 0, fh = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        ImGui::NewFrame();
    }

    // The renderer's layout rather than the tree's, so the passes that size a
    // label from its text and a tooltip from its lines run too. Drawing is the
    // other half and is not called: it needs a device.
    wowee::ui::WidgetRenderer widgets;
    widgets.initialize(nullptr, nullptr);

    auto relayout = [&mgr, &widgets] {
        if (auto* engine = mgr.getLuaEngine()) {
            widgets.layout(engine->widgets(), 1920.0f, 1080.0f);
        }
    };
    relayout();

    int raised = 0;
    for (int i = 2; i < argc; ++i) {
        const size_t before = errors.size();
        std::printf("\n== %s\n", argv[i]);
        // Before each one, not once after the load. An expression that opens a
        // panel changes where things are, and the expression that measures it
        // is the next one along — laying out only at the start meant every
        // measurement described the interface as it was before anything had
        // been asked of it. The client lays out every frame; this is the same
        // thing at the only granularity there is here.
        relayout();
        mgr.runInterfaceCommand(argv[i]);
        if (errors.size() == before) {
            std::printf("   no error\n");
        } else {
            ++raised;
            for (size_t k = before; k < errors.size(); ++k) {
                std::printf("   %s\n", errors[k].c_str());
            }
        }
    }
    return raised > 100 ? 100 : raised;
}
