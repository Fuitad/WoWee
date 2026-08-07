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
// Run it a second way before believing a clean report:
//
//     WOWEE_LUA_API_FALLBACK=0 framexml_run Data
//
// By default an unknown global answers with a stand-in rather than nil, which
// keeps a file alive past a name nothing implements — and hides that it was
// needed. With the fallback off, anything the interface actually depends on
// raises and names itself. As of 2026-08-07 that run is clean: no load errors,
// no addon failures, no login errors. It found the one thing that was not —
// Blizzard_BattlefieldMinimap, held up entirely by a stand-in for a frame the
// real client creates in C++.
//
// A clean default run and a failing fallback-off run is the shape to watch
// for: it means something is leaning on the stand-in and will fall over for
// anyone who turns it off.
//
// Clicking is the other thing to do with it, and it found the Send Mail tab
// raising before its frame was built. Walk the globals for tables carrying a
// widget id and a script table, keep the Buttons and CheckButtons, and Click()
// each one — but only where IsVisible() is true. Clicking a button in a panel
// nobody opened raises for reasons that are not faults: 516 of them against
// zero for the visible ones. Open a panel, click what became visible, hide it,
// move on.
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
#include "ui/link_hit.hpp"
#include "game/game_handler.hpp"
#include "game/game_services.hpp"
#include "game/character.hpp"
#include "game/spell_handler.hpp"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    // Its own log file, before anything can open one.
    //
    // The logger truncates whatever it opens, and every process using it took
    // the same path — so running this from the repository root destroyed the
    // session log of the client that had just been played, which is the one
    // file a bug report needs. A tool that quietly deletes the evidence is
    // worse than a tool that does not exist.
    // And its own config corner, for the same reason: the missing-API list and
    // the Lua error list are rewritten on exit, and both are read from when a
    // report is being diagnosed.
    if (!std::getenv("WOWEE_CONFIG_ROOT")) {
#ifdef _WIN32
        _putenv_s("WOWEE_CONFIG_ROOT", "logs/framexml_run_config");
#else
        setenv("WOWEE_CONFIG_ROOT", "logs/framexml_run_config", 0);
#endif
    }
    if (!std::getenv("WOWEE_LOG_FILE")) {
#ifdef _WIN32
        _putenv_s("WOWEE_LOG_FILE", "framexml_run.log");
#else
        setenv("WOWEE_LOG_FILE", "framexml_run.log", 0);
#endif
    }
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
    int refused = 0;
    for (const auto& addon : mgr.getLoadOnDemandAddons()) {
        // The one the client refuses on purpose, refused here too.
        //
        // That decision lives in the LoadAddOn *binding*, so asking the
        // manager directly walks straight past it — and this was loading an
        // addon the client never loads, reporting the seventy-nine globals it
        // has no server behind as though they were gaps. A harness that
        // reaches a state the client cannot is worse than one that reaches
        // less.
        if (addon.addonName == "Blizzard_Calendar") { ++refused; continue; }
        std::string why;
        if (!mgr.loadAddOnByName(addon.addonName, why)) {
            addonFailures.push_back(addon.addonName + " (" +
                                    (why.empty() ? "?" : why) + ")");
        }
    }
    std::printf("== addons: %zu of %zu load-on-demand failed (%d refused as the client does)\n",
                addonFailures.size(),
                mgr.getLoadOnDemandAddons().size() - static_cast<size_t>(refused),
                refused);
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
        // --tick:N runs N frames of the interface's own per-frame work rather
        // than evaluating an expression.
        //
        // Everything else here calls a handler directly, which tests the
        // handler and nothing about whether the client would ever reach it.
        // OnUpdate is dispatched from a list, gated on the widget's visible
        // chain, and unhooked after five consecutive failures — none of which
        // a direct call goes near. A frame whose OnUpdate raises looks
        // perfectly healthy when its function is invoked by hand, and is dead
        // for the rest of the session in the running client.
        //
        // Sixteen milliseconds a tick, which is the frame this client aims at,
        // so anything measured in seconds advances at the rate it really would.
        // --player attaches a game handler with a character in it.
        //
        // Every binding that matters starts with getGameHandler(L) and returns
        // at once when it is null, which is what the runner has always had. So
        // a check could reach a handler and never reach the thing the handler
        // asks the client — UnitFactionGroup answers nothing, PickupSpell
        // picks up nothing, and the failures that follow are the harness's,
        // not the interface's.
        //
        // Nothing here talks to a server: the services are all null pointers,
        // which GameHandler accepts, and the character is set directly.
        if (std::strcmp(argv[i], "--player") == 0) {
            static wowee::game::GameServices svc;
            static wowee::game::GameHandler gh(svc);
            constexpr uint64_t kGuid = 0x0000000000000001ull;
            gh.setPlayerGuid(kGuid);
            // Through the character list, because that is where the client
            // reads it from: getPlayerRace and getPlayerClass both go via
            // getActiveCharacter, so setting playerRace_ alone left every one
            // of them answering zero and the whole thing pointless.
            wowee::game::Character ch{};
            ch.guid = kGuid;
            ch.name = "Headless";
            ch.race = wowee::game::Race::HUMAN;
            ch.characterClass = wowee::game::Class::WARRIOR;
            ch.gender = wowee::game::Gender::MALE;
            ch.level = 80;
            gh.charactersRef().clear();
            gh.charactersRef().push_back(ch);
            gh.setActiveCharacterGuid(kGuid);
            gh.playerRaceRef() = wowee::game::Race::HUMAN;
            // A few spells, so the spellbook is not empty. getSpellBookTabs
            // rebuilds itself from the known set whenever the count changes,
            // so adding them is all that is needed — and without them
            // PickupSpell resolves slot 1 to nothing and every drag out of the
            // book is a no-op that looks like a broken drag.
            if (auto* sh = gh.getSpellHandler()) {
                for (uint32_t id : {133u, 168u, 116u}) sh->addKnownSpell(id);
            }
            if (auto* engine = mgr.getLuaEngine()) engine->setGameHandler(&gh);
            // Said every time, because the gap it leaves is the kind that
            // gets mistaken for a bug. There is a *character* here but no
            // *entity*: nothing is in the EntityManager, so resolveUnit
            // answers null and every Unit* binding that goes through it —
            // UnitClass, UnitName, UnitLevel, UnitExists — returns no values
            // at all. FrameXML then does `strupper(nil)` and similar, and a
            // sweep run over this reports raises that no player could reach.
            std::printf("   note: character only, no entity — Unit* bindings "
                        "answer empty and raises through them are artifacts\n");
            std::printf("   attached a game handler: %s, race=%u class=%u "
                        "level=%u\n",
                        ch.name.c_str(),
                        static_cast<unsigned>(gh.getPlayerRace()),
                        static_cast<unsigned>(gh.getPlayerClass()),
                        static_cast<unsigned>(ch.level));
            continue;
        }
        // --hit:X,Y says which frame the client's own hit test lands on.
        //
        // Window pixels like --mouse, so the two agree. Reimplementing the
        // test in Lua to ask this answers a different question: the real one
        // filters on the widget's own `visible`, its clip rect and its hit
        // insets, and a Lua walk over IsVisible() sees none of that. The two
        // disagreeing is itself the finding.
        if (std::strncmp(argv[i], "--hit:", 6) == 0) {
            float hx = 0.0f, hy = 0.0f;
            std::sscanf(argv[i] + 6, "%f,%f", &hx, &hy);
            relayout();
            if (auto* engine = mgr.getLuaEngine()) {
                // Through the client's own conversion, scale and all. A raw
                // flip here answered a different question and made the two
                // disagree — which read as the drop path being broken when it
                // was this line.
                float tx = hx, ty = hy;
                wowee::ui::mouseToTreeSpace(tx, ty, 1080.0f, engine->widgets().uiScale());
                const uint32_t id = engine->widgets().hitTest(tx, ty);
                const auto* w = id ? engine->widgets().get(id) : nullptr;
                std::printf("   hit at %.0f,%.0f -> %s\n", hx, hy,
                            w ? (w->name.empty() ? "(unnamed)" : w->name.c_str())
                              : "nothing");
            }
            continue;
        }
        // --mouse:X,Y,BUTTONS moves the cursor and sets the buttons held.
        //
        // Coordinates are window pixels from the top-left, the way ImGui
        // reports them and the way the client passes them, so a position read
        // off a frame's rect has to be flipped — the widget tree's y grows
        // upward. BUTTONS is any of L, R, M; an empty field is all released.
        //
        // A drag is three of these: down on the source, moved far enough to
        // pass the threshold, then up over the target. Nothing else here can
        // exercise press-move-release, and that is where the drag machinery
        // lives — which frame owns a drag, which frame is offered the drop,
        // and whether either walks up its parents.
        if (std::strncmp(argv[i], "--mouse:", 8) == 0) {
            float mx = 0.0f, my = 0.0f;
            char held[8] = {0};
            std::sscanf(argv[i] + 8, "%f,%f,%7s", &mx, &my, held);
            wowee::addons::LuaEngine::MouseButtons buttons;
            buttons.left   = std::strchr(held, 'L') != nullptr;
            buttons.right  = std::strchr(held, 'R') != nullptr;
            buttons.middle = std::strchr(held, 'M') != nullptr;
            relayout();
            if (auto* engine = mgr.getLuaEngine()) {
                engine->dispatchMouse(mx, my, 1080.0f, buttons);
            }
            std::printf("   mouse at %.0f,%.0f holding '%s'\n", mx, my,
                        held[0] ? held : "nothing");
            if (errors.size() != before) {
                ++raised;
                for (size_t k = before; k < errors.size(); ++k) {
                    std::printf("   %s\n", errors[k].c_str());
                }
            }
            continue;
        }
        // --fire:EVENT sends one event through the engine's own dispatch.
        //
        // Calling a frame's OnEvent by hand tests the handler and nothing
        // about whether the client would reach it — which table it is
        // registered in, whether the name matches, whether anything else
        // listens. This goes the way the client goes.
        if (std::strncmp(argv[i], "--fire:", 7) == 0) {
            relayout();
            mgr.fireEvent(argv[i] + 7);
            if (errors.size() == before) {
                std::printf("   no error\n");
            } else {
                ++raised;
                for (size_t k = before; k < errors.size(); ++k) {
                    std::printf("   %s\n", errors[k].c_str());
                }
            }
            continue;
        }
        if (std::strncmp(argv[i], "--tick:", 7) == 0) {
            const int ticks = std::atoi(argv[i] + 7);
            relayout();
            for (int t = 0; t < ticks; ++t) mgr.update(1.0f / 60.0f);
            std::printf("   ticked %d frame(s)\n", ticks);
            if (errors.size() != before) {
                ++raised;
                for (size_t k = before; k < errors.size(); ++k) {
                    std::printf("   %s\n", errors[k].c_str());
                }
            }
            continue;
        }
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
