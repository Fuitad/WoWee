# Unit test infrastructure using Catch2 v3 (amalgamated)

# Catch2 amalgamated as a library target
add_library(catch2_main STATIC
    ${CMAKE_SOURCE_DIR}/extern/catch2/catch_amalgamated.cpp
)
target_include_directories(catch2_main PUBLIC
    ${CMAKE_SOURCE_DIR}/extern/catch2
)
# Catch2 v3 needs C++17 minimum
target_compile_features(catch2_main PUBLIC cxx_std_17)

# ── ASAN / UBSan propagation ────────────────────────────────
# Collect all test target names so we can apply sanitizer flags at the end.
set(ALL_TEST_TARGETS "")

# Helper: register a test target for ASAN/UBSan if enabled.
# Also links Winsock on Windows: several tests pull in networking sources
# (auth, socket code) whose symbols live in ws2_32 there rather than libc.
# Linking it for every test keeps a new test from breaking the Windows build.
macro(register_test_target _target)
    list(APPEND ALL_TEST_TARGETS ${_target})
    if(WIN32)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endif()
endmacro()

# Shared source files used across multiple tests
set(TEST_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)

# Include directories matching the main target
# GLM, resolved the three ways find_package can present it.
#
# It is not vendored: it comes from find_package(glm QUIET), and its include
# path arrives with the imported target rather than through any directory this
# file lists. Three shapes exist in the wild - GLM 1.0 exposes the headers as
# glm::glm-header-only and may build glm::glm as a real library beside it, and
# some distributions provide neither target, only GLM_INCLUDE_DIRS. A target
# that asks for glm::glm alone gets nothing in the third case and fails on
# glm/glm.hpp.
#
# Linux has a system copy under /usr/include and compiles either way, so this
# only ever shows up somewhere else - which is what makes it easy to leave
# broken. The main target and the editor have walked all three branches since
# they were written; this is the same ladder, once, for the tests.
function(wowee_test_link_glm target)
    if(TARGET glm::glm-header-only)
        target_link_libraries(${target} PRIVATE glm::glm-header-only)
    elseif(TARGET glm::glm)
        target_link_libraries(${target} PRIVATE glm::glm)
    elseif(glm_FOUND)
        target_include_directories(${target} PRIVATE ${GLM_INCLUDE_DIRS})
    endif()
endfunction()

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
set(TEST_SYSTEM_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/extern
)

# Stock-style hill climbing regression test. Run directly with:
#   cmake --build build --target test_hill_climbing && ctest --test-dir build -R hill_climbing
add_executable(test_hill_climbing
    test_movement_limits.cpp
)
target_include_directories(test_hill_climbing PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_hill_climbing SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_hill_climbing PRIVATE catch2_main)
# glm is not vendored - it comes from find_package(glm), and its include path
# arrives with the imported target rather than through any directory this file
# lists. Adding TEST_SYSTEM_INCLUDE_DIRS above does not supply it, which is why
# the first attempt at this failed CI unchanged: Linux has a system copy under
# /usr/include and compiles either way, so only macOS ever reports it.
# Same two-branch check as the top level, where GLM 1.0 may build glm::glm as a
# real library and expose the headers as glm::glm-header-only.
wowee_test_link_glm(test_hill_climbing)
add_test(NAME hill_climbing COMMAND test_hill_climbing)
register_test_target(test_hill_climbing)

# ── test_cli_catalog_paths ──────────────────────────────────
# Working a catalog's base name back out of a path the user typed. Pure header
# in tools/editor, so the test reaches into that directory for it.
add_executable(test_cli_catalog_paths test_cli_catalog_paths.cpp ${TEST_COMMON_SOURCES})
target_include_directories(test_cli_catalog_paths PRIVATE
    ${TEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/tools/editor)
target_include_directories(test_cli_catalog_paths SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_cli_catalog_paths PRIVATE catch2_main)
add_test(NAME cli_catalog_paths COMMAND test_cli_catalog_paths)
register_test_target(test_cli_catalog_paths)

# ── test_shared_rules ───────────────────────────────────────
# The rules this codebase collapsed out of many copies into one. A rule with a
# single home is a rule one mistake breaks everywhere, so the cases that were
# actually wrong in one of the copies are pinned here.
# Pure rules only - the item texture paths and the settings vocabulary are
# header-only, and skinPathForM2 comes from m2_loader, which knows nothing about
# where bytes come from.
add_executable(test_shared_rules test_shared_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/m2_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/settings_schema.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_shared_rules PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_shared_rules SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_shared_rules PRIVATE catch2_main)
wowee_test_link_glm(test_shared_rules)
add_test(NAME shared_rules COMMAND test_shared_rules)
register_test_target(test_shared_rules)

# ── test_geoset_rules ────────────────────────────────────────
# Choosing a geoset when the model lacks the one asked for. Pure header, so the
# test needs nothing but Catch.
add_executable(test_geoset_rules test_geoset_rules.cpp ${TEST_COMMON_SOURCES})
target_include_directories(test_geoset_rules PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_geoset_rules SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_geoset_rules PRIVATE catch2_main)
add_test(NAME geoset_rules COMMAND test_geoset_rules)
register_test_target(test_geoset_rules)

# ── test_framexml ───────────────────────────────────────────
add_executable(test_framexml
    test_framexml.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_emitter.cpp
)
target_include_directories(test_framexml PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_framexml SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_framexml PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_framexml PRIVATE catch2_main)
add_test(NAME framexml COMMAND test_framexml)
register_test_target(test_framexml)

# ── test_framexml_takeover ──────────────────────────────────
# Which interface draws what in the configuration a run actually gets. The
# transition's state is "FrameXML draws all of it", and that rested on a list
# of forty-nine names and a grouping rule agreeing about fifty-two elements,
# in a file where either can be edited alone. An element falling out is not an
# error - this client just draws its own version again.
add_executable(test_framexml_takeover
    test_framexml_takeover.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_takeover.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_framexml_takeover PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_framexml_takeover SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_framexml_takeover PRIVATE catch2_main)
add_test(NAME framexml_takeover COMMAND test_framexml_takeover)
register_test_target(test_framexml_takeover)

# ── test_lua_arg_coercion ───────────────────────────────────
# The lenient numeric argument, which is a pure function of the Lua stack and
# so needs no game, window or interface - while the fault it prevents is
# invisible at every other level: a raise inside a click handler is swallowed,
# so an auction search that never sent looked exactly like one that found
# nothing.
add_executable(test_lua_arg_coercion
    test_lua_arg_coercion.cpp
)
target_include_directories(test_lua_arg_coercion PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_lua_arg_coercion SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_lua_arg_coercion PRIVATE catch2_main lua51)
wowee_test_link_glm(test_lua_arg_coercion)
add_test(NAME lua_arg_coercion COMMAND test_lua_arg_coercion)
register_test_target(test_lua_arg_coercion)

# ── framexml_emit ────────────────────────────────────────────
# Not a test: a one-file dump of what the emitter produces, for answering
# questions about emitted Lua without starting the client.
add_executable(framexml_emit
    ${CMAKE_SOURCE_DIR}/tools/framexml_emit.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_emitter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/xml_parser.cpp
)
target_include_directories(framexml_emit PRIVATE ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_include_directories(framexml_emit SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(framexml_emit PRIVATE lua51)

# ── test_settings_panel_layout ───────────────────────────────
# The options panels are generated from the schema by Lua, and nothing else
# checks the arithmetic between them. A control laid out past the bottom of a
# panel registers, refreshes and answers correctly - it is just not on screen.
add_executable(test_settings_panel_layout
    test_settings_panel_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/settings_schema.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_settings_panel_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_settings_panel_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_settings_panel_layout PRIVATE catch2_main)
add_test(NAME settings_panel_layout COMMAND test_settings_panel_layout)
register_test_target(test_settings_panel_layout)

# ── test_addon_lua_snippets ──────────────────────────────────
# The Lua this client injects into the interface lives as C++ string literals,
# which nothing compiles until the client runs - and a syntax error there is a
# false from executeString and a line in a warning-only log. This asks Lua
# whether they parse.
add_executable(test_addon_lua_snippets
    test_addon_lua_snippets.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_addon_lua_snippets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_addon_lua_snippets SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_addon_lua_snippets PRIVATE catch2_main lua51)
add_test(NAME addon_lua_snippets COMMAND test_addon_lua_snippets)
register_test_target(test_addon_lua_snippets)

# ── framexml_compiles ───────────────────────────────────────
# Not a unit test: it emits every real FrameXML file and asks Lua itself
# whether the result compiles. The unit tests above check cases someone
# thought of; this checks all 126, and a file that will not compile is lost
# whole rather than degraded. Skipped when the game data is not present.
add_executable(framexml_compile_check
    ${CMAKE_SOURCE_DIR}/tools/framexml_compile_check.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_emitter.cpp
)
target_include_directories(framexml_compile_check PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(framexml_compile_check SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(framexml_compile_check PRIVATE lua51)
add_test(NAME framexml_compiles COMMAND framexml_compile_check
         ${CMAKE_SOURCE_DIR}/Data/interface/framexml)
set_tests_properties(framexml_compiles PROPERTIES SKIP_RETURN_CODE 77)

# The Blizzard addons go through the same emitter and were never checked by it.
# Their XML is where the load-on-demand panels come from - a file that will not
# compile takes its whole panel with it, and only when someone opens it.
add_test(NAME addon_xml_compiles COMMAND framexml_compile_check
         ${CMAKE_SOURCE_DIR}/Data/interface/addons)
set_tests_properties(addon_xml_compiles PROPERTIES SKIP_RETURN_CODE 77)

# ── test_taxi_map_projection ────────────────────────────────
# The order of the coordinate conversions behind a flight map marker. Silent
# when wrong - every marker still lands somewhere, just not where it belongs.
add_executable(test_taxi_map_projection
    test_taxi_map_projection.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/coordinate_projection.cpp
)
target_include_directories(test_taxi_map_projection PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_taxi_map_projection SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_taxi_map_projection PRIVATE catch2_main)
wowee_test_link_glm(test_taxi_map_projection)
add_test(NAME taxi_map_projection COMMAND test_taxi_map_projection)
register_test_target(test_taxi_map_projection)

# ── test_inventory_slots ─────────────────────────────────────────
# Where the bank, its bags and the keyring sit. A wrong slot here moves an
# item somewhere real that nobody asked for, so the numbers are pinned.
add_executable(test_inventory_slots test_inventory_slots.cpp)
target_include_directories(test_inventory_slots PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_inventory_slots SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_inventory_slots PRIVATE catch2_main)
add_test(NAME inventory_slots COMMAND test_inventory_slots)
register_test_target(test_inventory_slots)

# ── sweep_guard ──────────────────────────────────────────────────
# The fast interface sweeps, run as a test so they stop depending on someone
# remembering. Each fault they catch raises nothing, logs nothing and fails no
# other test: a panel drawn twice, a binding answering short, a manifest entry
# resolving to no file, bootstrap Lua replacing the binding beneath it. Under
# three seconds for all of them; the slower reports stay manual.
if(Python3_Interpreter_FOUND)
    add_test(NAME sweep_guard
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/sweep_guard.py)
endif()

# ── test_chat_types ─────────────────────────────────────────────
# A chat type is a byte off the wire and the event name is built from it, so a
# value missing from the enum is a line of chat that never appears - no error,
# no log. The whole run between LOOT and the battleground block was absent once.
add_executable(test_chat_types test_chat_types.cpp)
target_include_directories(test_chat_types PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_chat_types SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_chat_types PRIVATE catch2_main)
wowee_test_link_glm(test_chat_types)
add_test(NAME chat_types COMMAND test_chat_types)
register_test_target(test_chat_types)

# ── test_text_tokens ────────────────────────────────────────
# The server writes the player as a blank - "$N, you have done well" - and the
# client fills it in. Nothing did on the chat path, so every scripted NPC in the
# game addressed the player as "$N". Pure: the substitution takes the four facts
# it needs about the reader rather than a client.
add_executable(test_text_tokens test_text_tokens.cpp ${CMAKE_SOURCE_DIR}/src/game/text_tokens.cpp)
target_include_directories(test_text_tokens PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_text_tokens SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_text_tokens PRIVATE catch2_main)
add_test(NAME text_tokens COMMAND test_text_tokens)
register_test_target(test_text_tokens)

# ── test_reputation_standing ────────────────────────────────
# Header-only and constexpr, so this needs nothing linked: the point is that the
# thresholds have one home now and stay put.
add_executable(test_reputation_standing test_reputation_standing.cpp)
target_include_directories(test_reputation_standing PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_reputation_standing SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_reputation_standing PRIVATE catch2_main)
add_test(NAME reputation_standing COMMAND test_reputation_standing)

# ── test_widget_tree ────────────────────────────────────────
add_executable(test_widget_tree
    test_widget_tree.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widget_tree.cpp
)
target_include_directories(test_widget_tree PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_widget_tree SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_widget_tree PRIVATE catch2_main)
add_test(NAME widget_tree COMMAND test_widget_tree)
register_test_target(test_widget_tree)

# ── test_escape_action ──────────────────────────────────────
add_executable(test_escape_action
    test_escape_action.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/escape_action.cpp
)
target_include_directories(test_escape_action PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_escape_action SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_escape_action PRIVATE catch2_main)
add_test(NAME escape_action COMMAND test_escape_action)
register_test_target(test_escape_action)

# ── test_placement_rotation ─────────────────────────────────
add_executable(test_placement_rotation
    test_placement_rotation.cpp
)
target_include_directories(test_placement_rotation PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_placement_rotation SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_placement_rotation PRIVATE catch2_main)
wowee_test_link_glm(test_placement_rotation)
add_test(NAME placement_rotation COMMAND test_placement_rotation)
register_test_target(test_placement_rotation)

# ── test_water_mask ─────────────────────────────────────────
add_executable(test_water_mask
    test_water_mask.cpp
)
target_include_directories(test_water_mask PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_water_mask SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_water_mask PRIVATE catch2_main)
add_test(NAME water_mask COMMAND test_water_mask)
register_test_target(test_water_mask)

# ── test_addon_globals ──────────────────────────────────────
add_executable(test_addon_globals
    test_addon_globals.cpp
)
target_include_directories(test_addon_globals PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_addon_globals SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_addon_globals PRIVATE catch2_main)
add_test(NAME addon_globals COMMAND test_addon_globals)
register_test_target(test_addon_globals)

# ── test_monster_move_facing ─────────────────────────────────
# SMSG_MONSTER_MOVE's move-type byte and the facing after it, which three
# expansion parsers used to read for themselves. A wrong length here misreads
# every field that follows.
add_executable(test_monster_move_facing
    test_monster_move_facing.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
)
target_include_directories(test_monster_move_facing PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_monster_move_facing SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_monster_move_facing PRIVATE catch2_main)
wowee_test_link_glm(test_monster_move_facing)
add_test(NAME monster_move_facing COMMAND test_monster_move_facing)
register_test_target(test_monster_move_facing)

# ── test_packet ──────────────────────────────────────────────
add_executable(test_packet
    test_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_packet PRIVATE catch2_main)
add_test(NAME packet COMMAND test_packet)
register_test_target(test_packet)

# ── test_srp ─────────────────────────────────────────────────
add_executable(test_srp
    test_srp.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/auth/srp.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/big_num.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
)
target_include_directories(test_srp PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_srp SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_srp PRIVATE catch2_main OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME srp COMMAND test_srp)
register_test_target(test_srp)

# ── test_m2_ribbon_tracks ────────────────────────────────────
# A ribbon emitter's alpha and visibility tracks, whose keys are fixed16 and
# bytes rather than floats. Real-asset coverage; skips when unextracted.
add_executable(test_m2_ribbon_tracks
    test_m2_ribbon_tracks.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/m2_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_m2_ribbon_tracks PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_m2_ribbon_tracks SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_m2_ribbon_tracks PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_m2_ribbon_tracks PRIVATE catch2_main)
wowee_test_link_glm(test_m2_ribbon_tracks)
add_test(NAME m2_ribbon_tracks COMMAND test_m2_ribbon_tracks)
register_test_target(test_m2_ribbon_tracks)

# ── test_shapeshift_forms ────────────────────────────────────
# The stance bar's three questions, which used to be three different lists.
add_executable(test_shapeshift_forms
    test_shapeshift_forms.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/shapeshift_forms.cpp)
target_include_directories(test_shapeshift_forms PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_shapeshift_forms SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_shapeshift_forms PRIVATE catch2_main)
wowee_test_link_glm(test_shapeshift_forms)
add_test(NAME shapeshift_forms COMMAND test_shapeshift_forms)
register_test_target(test_shapeshift_forms)

# ── test_tile_size_agreement ─────────────────────────────────
# The ADT tile size, which three headers each declared and two files spelled
# with two fewer decimals. Pins the value against its own arithmetic.
add_executable(test_tile_size_agreement
    test_tile_size_agreement.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/terrain_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/adt_loader.cpp
)
target_include_directories(test_tile_size_agreement PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_tile_size_agreement SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_tile_size_agreement PRIVATE catch2_main)
wowee_test_link_glm(test_tile_size_agreement)
add_test(NAME tile_size_agreement COMMAND test_tile_size_agreement)
register_test_target(test_tile_size_agreement)

# ── test_chunk_surface ───────────────────────────────────────
# Where a point inside a terrain chunk sits in the world. The clutter and
# doodad scatterers each had their own copy, including the crossed axes.
add_executable(test_chunk_surface
    test_chunk_surface.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/terrain_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/adt_loader.cpp
)
target_include_directories(test_chunk_surface PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_chunk_surface SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_chunk_surface PRIVATE catch2_main)
wowee_test_link_glm(test_chunk_surface)
add_test(NAME chunk_surface COMMAND test_chunk_surface)
register_test_target(test_chunk_surface)

# ── test_spline_body ─────────────────────────────────────────
# The body of SMSG_MONSTER_MOVE, whose head two parsers shared and neither
# covered. Checked against AzerothCore's own WriteCommonMonsterMovePart.
add_executable(test_spline_body
    test_spline_body.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_spline_body PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_spline_body SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_spline_body PRIVATE catch2_main)
wowee_test_link_glm(test_spline_body)
add_test(NAME spline_body COMMAND test_spline_body)
register_test_target(test_spline_body)

# ── test_spline ──────────────────────────────────────────────
add_executable(test_spline
    test_spline.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/math/spline.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_spline PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_spline SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_spline PRIVATE catch2_main)
wowee_test_link_glm(test_spline)
add_test(NAME spline COMMAND test_spline)
register_test_target(test_spline)

# ── test_transport_path_repo ─────────────────────────────────
add_executable(test_transport_path_repo
    test_transport_path_repo.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/transport_path_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/math/spline.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_loader.cpp
)
target_include_directories(test_transport_path_repo PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_transport_path_repo SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_transport_path_repo PRIVATE catch2_main)
wowee_test_link_glm(test_transport_path_repo)
add_test(NAME transport_path_repo COMMAND test_transport_path_repo)
register_test_target(test_transport_path_repo)

# ── test_opcode_table ────────────────────────────────────────
add_executable(test_opcode_table
    test_opcode_table.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
)
target_include_directories(test_opcode_table PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_opcode_table SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_opcode_table PRIVATE catch2_main)
add_test(NAME opcode_table COMMAND test_opcode_table)
register_test_target(test_opcode_table)

# ── test_equipment_set_packets ───────────────────────────────
# Every guid in the equipment-set family is packed and all twenty were read and
# written as fixed eight-byte values - plus an ignore mask that the server does
# not send at all. Saving a set could never have worked.
add_executable(test_equipment_set_packets
    test_equipment_set_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_equipment_set_packets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_equipment_set_packets SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_equipment_set_packets PRIVATE catch2_main)
add_test(NAME equipment_set_packets COMMAND test_equipment_set_packets)
register_test_target(test_equipment_set_packets)

# ── test_auction_sort_packet ─────────────────────────────────
# The sort block on CMSG_AUCTION_LIST_ITEMS was a hardcoded zero, and the
# browse tab re-queries rather than sorting locally, so its column headers did
# nothing at all. Two orderings have to agree and they run opposite ways.
add_executable(test_auction_sort_packet
    test_auction_sort_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_auction_sort_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_auction_sort_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_auction_sort_packet PRIVATE catch2_main)
# These reach glm through a header rather than by naming it, so the need is not
# visible at the target: test_shared_rules names m2_loader.cpp, and it is
# m2_loader.hpp that includes glm two lines in. Linux has a system copy under
# /usr/include and compiles either way; macOS does not, reports the first one
# and stops - which is why one name appeared in a failing log while eight were
# short. tools/test_glm_link_check.py answers the macOS question on any host.
wowee_test_link_glm(test_auction_sort_packet)
add_test(NAME auction_sort_packet COMMAND test_auction_sort_packet)
register_test_target(test_auction_sort_packet)

# ── test_achievement_criteria ────────────────────────────────
# The criteria progress record. Its counter is a packed guid and was read as a
# fixed uint64, which put every field behind it out of place - and in the login
# message, where records repeat until a sentinel, took the whole list with it.
add_executable(test_achievement_criteria
    test_achievement_criteria.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_achievement_criteria PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_achievement_criteria SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_achievement_criteria PRIVATE catch2_main)
add_test(NAME achievement_criteria COMMAND test_achievement_criteria)
register_test_target(test_achievement_criteria)

# ── test_taxi_packets ────────────────────────────────────────
# The multi-hop flight request. A field the server does not read was being
# written between the guid and the node count, so the count was read as a node
# and every route longer than one hop was refused.
add_executable(test_taxi_packets
    test_taxi_packets.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_taxi_packets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_taxi_packets SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_taxi_packets PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_taxi_packets)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_taxi_packets PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME taxi_packets COMMAND test_taxi_packets)
register_test_target(test_taxi_packets)

# ── test_use_item_packet ─────────────────────────────────────
add_executable(test_use_item_packet
    test_use_item_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_use_item_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_use_item_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_use_item_packet PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                   OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_use_item_packet)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_use_item_packet PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME use_item_packet COMMAND test_use_item_packet)
register_test_target(test_use_item_packet)

# ── test_normal_map ────────────────────────────────────────────────────
# The normal-map pass the character and WMO renderers used to duplicate. Both
# copies lived inside a renderer class and needed a Vulkan context to reach, so
# this is its first test.
add_executable(test_normal_map
    test_normal_map.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rendering/normal_map.cpp)
target_include_directories(test_normal_map PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_normal_map SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_normal_map PRIVATE catch2_main)
wowee_test_link_glm(test_normal_map)
add_test(NAME normal_map COMMAND test_normal_map)
register_test_target(test_normal_map)

# ── test_spatial_grid ────────────────────────────────────────
# The uniform grid the M2 and WMO renderers narrow collision queries with. Both
# had their own copy, private to a renderer and unreachable without a Vulkan
# context, so this is its first test.
add_executable(test_spatial_grid
    test_spatial_grid.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_spatial_grid PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_spatial_grid SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_spatial_grid PRIVATE catch2_main)
wowee_test_link_glm(test_spatial_grid)
add_test(NAME spatial_grid COMMAND test_spatial_grid)
register_test_target(test_spatial_grid)

# ── test_asset_manifest_load ───────────────────────────────────────────
# The 32 MB asset manifest, pinned so a change to how it is read cannot change
# what it read. Skips when Data/manifest.json is not in the checkout.
add_executable(test_asset_manifest_load
    test_asset_manifest_load.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/asset_manifest.cpp)
target_include_directories(test_asset_manifest_load PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_asset_manifest_load SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_asset_manifest_load PRIVATE catch2_main)
wowee_test_link_glm(test_asset_manifest_load)
add_test(NAME asset_manifest_load COMMAND test_asset_manifest_load
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
register_test_target(test_asset_manifest_load)

# ── test_macro_text ──────────────────────────────────────────
# Reading a macro body. The action bar and the chat panel each had their own
# copy of the line walk, neither with a test.
add_executable(test_macro_text
    test_macro_text.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ui/macro_text.cpp)
target_include_directories(test_macro_text PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_macro_text SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_macro_text PRIVATE catch2_main)
wowee_test_link_glm(test_macro_text)
add_test(NAME macro_text COMMAND test_macro_text)
register_test_target(test_macro_text)

# ── test_water_surface_grid ──────────────────────────────────
# Projecting a world position onto a water surface's grid, which
# water_renderer.cpp did in four places and never tested.
add_executable(test_water_surface_grid
    test_water_surface_grid.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_water_surface_grid PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_water_surface_grid SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_water_surface_grid PRIVATE catch2_main)
wowee_test_link_glm(test_water_surface_grid)
add_test(NAME water_surface_grid COMMAND test_water_surface_grid)
register_test_target(test_water_surface_grid)

# ── test_dxt_block ───────────────────────────────────────────
# The colour half of a DXT block, decoded three times in blp_loader.cpp and
# never tested; the BLP tests stop at header validation.
add_executable(test_dxt_block
    test_dxt_block.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_dxt_block PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_dxt_block SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_dxt_block PRIVATE catch2_main)
wowee_test_link_glm(test_dxt_block)
add_test(NAME dxt_block COMMAND test_dxt_block)
register_test_target(test_dxt_block)

# ── test_vertex_layouts ──────────────────────────────────────
# The WMO and terrain vertex layouts against the shaders that read them. Each
# layout was stated three or four times, once with the stride and every offset
# as a literal; the shader source is the outside oracle.
add_executable(test_vertex_layouts
    test_vertex_layouts.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_vertex_layouts PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_vertex_layouts SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_vertex_layouts PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_vertex_layouts PRIVATE catch2_main)
wowee_test_link_glm(test_vertex_layouts)
add_test(NAME vertex_layouts COMMAND test_vertex_layouts)
register_test_target(test_vertex_layouts)

# ── test_movement_flags ──────────────────────────────────────
# What the movement flag word means. Two handlers spelled these out against
# their own copy of the flags; they now both ask MovementInfo. Header-only, so
# no sources beyond the common set.
add_executable(test_movement_flags
    test_movement_flags.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_movement_flags PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_movement_flags SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_movement_flags PRIVATE catch2_main)
wowee_test_link_glm(test_movement_flags)
add_test(NAME movement_flags COMMAND test_movement_flags)
register_test_target(test_movement_flags)

# ── test_attacker_state_layout ─────────────────────────────────────────
# SMSG_ATTACKERSTATEUPDATE for classic and TBC, pinned before the shared head
# was factored out. Same link set as the other two layout tests.
add_executable(test_attacker_state_layout
    test_attacker_state_layout.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_attacker_state_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_attacker_state_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_attacker_state_layout PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                         OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_attacker_state_layout)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_attacker_state_layout PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME attacker_state_layout COMMAND test_attacker_state_layout)
register_test_target(test_attacker_state_layout)

# ── test_item_query_layout ─────────────────────────────────────────────
# SMSG_ITEM_QUERY_SINGLE_RESPONSE for classic and TBC, pinned before the two
# readers were merged. Same link set as test_char_enum_layout.
add_executable(test_item_query_layout
    test_item_query_layout.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_item_query_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_item_query_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_item_query_layout PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                     OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_item_query_layout)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_item_query_layout PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME item_query_layout COMMAND test_item_query_layout)
register_test_target(test_item_query_layout)

# ── test_char_enum_layout ──────────────────────────────────────────────
# SMSG_CHAR_ENUM, parsed by three separate functions with no coverage at all.
# Links the same set test_taxi_packets does: the classic and TBC parsers pull
# in the rest of the packet layer through their shared base.
add_executable(test_char_enum_layout
    test_char_enum_layout.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_char_enum_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_char_enum_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_char_enum_layout PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                    OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_char_enum_layout)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_char_enum_layout PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME char_enum_layout COMMAND test_char_enum_layout)
register_test_target(test_char_enum_layout)

# ── test_battlefield_status_packet ─────────────────────────────────────
# SMSG_BATTLEFIELD_STATUS, whose header was read two bytes short.
add_executable(test_battlefield_status_packet
    test_battlefield_status_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_battlefield_status_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_battlefield_status_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_battlefield_status_packet PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                   OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_battlefield_status_packet)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_battlefield_status_packet PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME battlefield_status_packet COMMAND test_battlefield_status_packet)
register_test_target(test_battlefield_status_packet)

# ── test_lfg_join_packet ─────────────────────────────────────
# Every field of CMSG_LFG_JOIN, which was wrong in every field and silent.
add_executable(test_lfg_join_packet
    test_lfg_join_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_lfg_join_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_lfg_join_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_lfg_join_packet PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                   OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_lfg_join_packet)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_lfg_join_packet PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME lfg_join_packet COMMAND test_lfg_join_packet)
register_test_target(test_lfg_join_packet)

# ── test_learn_talent_packet ─────────────────────────────────
# The rank field counts from zero on the wire and from one everywhere else.
add_executable(test_learn_talent_packet
    test_learn_talent_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_learn_talent_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_learn_talent_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_learn_talent_packet PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                       OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_learn_talent_packet)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_learn_talent_packet PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME learn_talent_packet COMMAND test_learn_talent_packet)
register_test_target(test_learn_talent_packet)

# ── test_item_query_sockets ─────────────────────────────────
# The socket block of the item query, which the layout sweep cannot reach.
add_executable(test_item_query_sockets
    test_item_query_sockets.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_item_query_sockets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_item_query_sockets SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_item_query_sockets PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                   OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_item_query_sockets)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_item_query_sockets PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME item_query_sockets COMMAND test_item_query_sockets)
register_test_target(test_item_query_sockets)

# ── test_mail_packets ───────────────────────────────────────
add_executable(test_mail_packets
    test_mail_packets.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_mail_packets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_mail_packets SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_mail_packets PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                 OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_mail_packets)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_mail_packets PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME mail_packets COMMAND test_mail_packets)
register_test_target(test_mail_packets)

# ── test_raid_target_packets ────────────────────────────────
add_executable(test_raid_target_packets
    test_raid_target_packets.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_raid_target_packets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_raid_target_packets SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_raid_target_packets PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                 OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_raid_target_packets)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_raid_target_packets PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME raid_target_packets COMMAND test_raid_target_packets)
register_test_target(test_raid_target_packets)

# ── test_entity ──────────────────────────────────────────────
add_executable(test_entity
    test_entity.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/entity.cpp
    ${CMAKE_SOURCE_DIR}/src/math/spline.cpp
)
target_include_directories(test_entity PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_entity SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_entity PRIVATE catch2_main)
wowee_test_link_glm(test_entity)
add_test(NAME entity COMMAND test_entity)
register_test_target(test_entity)

# ── test_dbc_loader ──────────────────────────────────────────
add_executable(test_dbc_loader
    test_dbc_loader.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_layout.cpp
)
target_include_directories(test_dbc_loader PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_dbc_loader SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_dbc_loader PRIVATE catch2_main)
add_test(NAME dbc_loader COMMAND test_dbc_loader)
register_test_target(test_dbc_loader)

# ── test_m2_structs ──────────────────────────────────────────
add_executable(test_m2_structs
    test_m2_structs.cpp
    ../src/rendering/m2_model_classifier.cpp
)
target_include_directories(test_m2_structs PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_m2_structs SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_m2_structs PRIVATE catch2_main)
wowee_test_link_glm(test_m2_structs)
add_test(NAME m2_structs COMMAND test_m2_structs)
register_test_target(test_m2_structs)

# ── test_m2_color_alpha ──────────────────────────────────────
# Loads the peasant lumberjack carry models (WotLK v264 + vanilla v256) and
# pins the M2 color-alpha track parsing that hides the duplicate wood bundle.
add_executable(test_m2_color_alpha
    test_m2_color_alpha.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/m2_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_m2_color_alpha PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_m2_color_alpha SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_m2_color_alpha PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_m2_color_alpha PRIVATE catch2_main)
wowee_test_link_glm(test_m2_color_alpha)
add_test(NAME m2_color_alpha COMMAND test_m2_color_alpha)
register_test_target(test_m2_color_alpha)

# ── test_m2_footstep_events ──────────────────────────────────
# Loads real WotLK v264 + vanilla v256 models and pins the $FSD footfall
# event parsing that syncs footstep audio to foot-strike keyframes.
add_executable(test_m2_footstep_events
    test_m2_footstep_events.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/m2_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_m2_footstep_events PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_m2_footstep_events SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_m2_footstep_events PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_m2_footstep_events PRIVATE catch2_main)
wowee_test_link_glm(test_m2_footstep_events)
add_test(NAME m2_footstep_events COMMAND test_m2_footstep_events)
register_test_target(test_m2_footstep_events)

# ── test_blp_loader ──────────────────────────────────────────
add_executable(test_blp_loader
    test_blp_loader.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/blp_loader.cpp
)
target_include_directories(test_blp_loader PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_blp_loader SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_blp_loader PRIVATE catch2_main)
add_test(NAME blp_loader COMMAND test_blp_loader)
register_test_target(test_blp_loader)

# ── test_frustum ─────────────────────────────────────────────
add_executable(test_frustum
    test_frustum.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/frustum.cpp
)
target_include_directories(test_frustum PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_frustum SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_frustum PRIVATE catch2_main)
wowee_test_link_glm(test_frustum)
add_test(NAME frustum COMMAND test_frustum)
register_test_target(test_frustum)

# ── test_packed_time ─────────────────────────────────────────
# Header-only domain logic: the one uint32 that carries a whole date, which
# four separate readers in this tree got wrong in four different ways.
add_executable(test_packed_time
    test_packed_time.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_packed_time PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_packed_time SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_packed_time PRIVATE catch2_main)
add_test(NAME packed_time COMMAND test_packed_time)
register_test_target(test_packed_time)

# ── test_animation_ids ───────────────────────────────────────
add_executable(test_animation_ids
    test_animation_ids.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rendering/animation/animation_ids.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_loader.cpp
)
target_include_directories(test_animation_ids PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_animation_ids SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_animation_ids PRIVATE catch2_main)
add_test(NAME animation_ids COMMAND test_animation_ids)
register_test_target(test_animation_ids)

# ── test_locomotion_fsm ──────────────────────────────────────
add_executable(test_locomotion_fsm
    test_locomotion_fsm.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rendering/animation/locomotion_fsm.cpp
)
target_include_directories(test_locomotion_fsm PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_locomotion_fsm SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_locomotion_fsm PRIVATE catch2_main)
add_test(NAME locomotion_fsm COMMAND test_locomotion_fsm)
register_test_target(test_locomotion_fsm)

# ── test_combat_fsm ──────────────────────────────────────────
add_executable(test_combat_fsm
    test_combat_fsm.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rendering/animation/combat_fsm.cpp
)
target_include_directories(test_combat_fsm PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_combat_fsm SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_combat_fsm PRIVATE catch2_main)
add_test(NAME combat_fsm COMMAND test_combat_fsm)
register_test_target(test_combat_fsm)

# ── test_activity_fsm ────────────────────────────────────────
add_executable(test_activity_fsm
    test_activity_fsm.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/rendering/animation/activity_fsm.cpp
)
target_include_directories(test_activity_fsm PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_activity_fsm SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_activity_fsm PRIVATE catch2_main)
add_test(NAME activity_fsm COMMAND test_activity_fsm)
register_test_target(test_activity_fsm)

# NPC animator tests removed - NpcAnimator replaced by generic CharacterAnimator

# ── test_anim_capability ─────────────────────────────────────
# Header-only struct tests - no source files needed
add_executable(test_anim_capability
    test_anim_capability.cpp
)
target_include_directories(test_anim_capability PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_anim_capability SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_anim_capability PRIVATE catch2_main)
add_test(NAME anim_capability COMMAND test_anim_capability)
register_test_target(test_anim_capability)

# ── test_indoor_shadows ──────────────────────────────────────
add_executable(test_indoor_shadows
    test_indoor_shadows.cpp
)
target_include_directories(test_indoor_shadows PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_indoor_shadows SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_indoor_shadows PRIVATE catch2_main)
wowee_test_link_glm(test_indoor_shadows)
add_test(NAME indoor_shadows COMMAND test_indoor_shadows)
register_test_target(test_indoor_shadows)

# ── test_zone_ambience ─────────────────────────────────────────
add_executable(test_zone_ambience
    test_zone_ambience.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/zone_ambience.cpp
)
target_include_directories(test_zone_ambience PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_zone_ambience SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_zone_ambience PRIVATE catch2_main)
wowee_test_link_glm(test_zone_ambience)
add_test(NAME zone_ambience COMMAND test_zone_ambience)
register_test_target(test_zone_ambience)

# ── test_transport_components ────────────────────────────────
add_executable(test_transport_components
    test_transport_components.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/transport_clock_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/game/transport_animator.cpp
    ${CMAKE_SOURCE_DIR}/src/math/spline.cpp
)
target_include_directories(test_transport_components PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_transport_components SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_transport_components PRIVATE catch2_main)
wowee_test_link_glm(test_transport_components)
add_test(NAME transport_components COMMAND test_transport_components)
register_test_target(test_transport_components)

# ── test_world_map ────────────────────────────────────────────
add_executable(test_world_map
    test_world_map.cpp
)
target_include_directories(test_world_map PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map PRIVATE catch2_main)
wowee_test_link_glm(test_world_map)
add_test(NAME world_map COMMAND test_world_map)
register_test_target(test_world_map)

# ── test_world_map_coordinate_projection ──────────────────────
add_executable(test_world_map_coordinate_projection
    test_world_map_coordinate_projection.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/coordinate_projection.cpp
)
target_include_directories(test_world_map_coordinate_projection PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map_coordinate_projection SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map_coordinate_projection PRIVATE catch2_main)
wowee_test_link_glm(test_world_map_coordinate_projection)
add_test(NAME world_map_coordinate_projection COMMAND test_world_map_coordinate_projection)
register_test_target(test_world_map_coordinate_projection)

# ── test_world_map_map_resolver ───────────────────────────────
add_executable(test_world_map_map_resolver
    test_world_map_map_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/map_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/coordinate_projection.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_world_map_map_resolver PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map_map_resolver SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map_map_resolver PRIVATE catch2_main)
wowee_test_link_glm(test_world_map_map_resolver)
add_test(NAME world_map_map_resolver COMMAND test_world_map_map_resolver)
register_test_target(test_world_map_map_resolver)

# ── test_world_map_view_state_machine ─────────────────────────
add_executable(test_world_map_view_state_machine
    test_world_map_view_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/view_state_machine.cpp
)
target_include_directories(test_world_map_view_state_machine PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map_view_state_machine SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map_view_state_machine PRIVATE catch2_main)
wowee_test_link_glm(test_world_map_view_state_machine)
add_test(NAME world_map_view_state_machine COMMAND test_world_map_view_state_machine)
register_test_target(test_world_map_view_state_machine)

# ── test_world_map_exploration_state ──────────────────────────
add_executable(test_world_map_exploration_state
    test_world_map_exploration_state.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/exploration_state.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/coordinate_projection.cpp
)
target_include_directories(test_world_map_exploration_state PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map_exploration_state SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map_exploration_state PRIVATE catch2_main)
wowee_test_link_glm(test_world_map_exploration_state)
add_test(NAME world_map_exploration_state COMMAND test_world_map_exploration_state)
register_test_target(test_world_map_exploration_state)

# ── test_world_map_zone_metadata ──────────────────────────────
add_executable(test_world_map_zone_metadata
    test_world_map_zone_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/world_map/zone_metadata.cpp
)
target_include_directories(test_world_map_zone_metadata PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_world_map_zone_metadata SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_world_map_zone_metadata PRIVATE catch2_main)
wowee_test_link_glm(test_world_map_zone_metadata)
add_test(NAME world_map_zone_metadata COMMAND test_world_map_zone_metadata)
register_test_target(test_world_map_zone_metadata)

# ── test_text_wrap ───────────────────────────────────────────
add_executable(test_text_wrap
    test_text_wrap.cpp
)
target_include_directories(test_text_wrap PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_text_wrap SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_text_wrap PRIVATE catch2_main)
add_test(NAME text_wrap COMMAND test_text_wrap)

# ── test_text_markup ─────────────────────────────────────────
add_executable(test_text_markup
    test_text_markup.cpp
)
target_include_directories(test_text_markup PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_text_markup SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_text_markup PRIVATE catch2_main)
add_test(NAME text_markup COMMAND test_text_markup)

# ── test_link_hit ────────────────────────────────────────────
add_executable(test_link_hit
    test_link_hit.cpp
)
target_include_directories(test_link_hit PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_link_hit SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_link_hit PRIVATE catch2_main)
add_test(NAME link_hit COMMAND test_link_hit)

# ── test_minimap_projection ──────────────────────────────────
add_executable(test_minimap_projection
    test_minimap_projection.cpp
)
target_include_directories(test_minimap_projection PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_minimap_projection SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_minimap_projection PRIVATE catch2_main)
wowee_test_link_glm(test_minimap_projection)
add_test(NAME minimap_projection COMMAND test_minimap_projection)
register_test_target(test_text_wrap)

# ── test_chat_markup_parser ──────────────────────────────────
add_executable(test_chat_markup_parser
    test_chat_markup_parser.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ui/chat/chat_markup_parser.cpp
)
target_include_directories(test_chat_markup_parser PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_chat_markup_parser SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/imgui
)
target_link_libraries(test_chat_markup_parser PRIVATE catch2_main)
add_test(NAME chat_markup_parser COMMAND test_chat_markup_parser)
register_test_target(test_chat_markup_parser)

# ── test_plural_escape ───────────────────────────────────────
# WoW's |4singular:plural; escape, which nothing resolved - so the logout
# prompt read "5 |4Second:Seconds; until logout" and seventy-four other
# strings did the same.
add_executable(test_plural_escape
    test_plural_escape.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/plural_escape.cpp
)
target_include_directories(test_plural_escape PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_plural_escape SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_plural_escape PRIVATE catch2_main)
add_test(NAME plural_escape COMMAND test_plural_escape)
register_test_target(test_plural_escape)

# ── test_macro_evaluator ─────────────────────────────────────
add_executable(test_macro_evaluator
    test_macro_evaluator.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ui/chat/macro_evaluator.cpp
)
target_include_directories(test_macro_evaluator PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_macro_evaluator SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_macro_evaluator PRIVATE catch2_main)
add_test(NAME macro_evaluator COMMAND test_macro_evaluator)
register_test_target(test_macro_evaluator)


# ── test_gm_commands ─────────────────────────────────────────
add_executable(test_gm_commands
    test_gm_commands.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_gm_commands PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_gm_commands SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_gm_commands PRIVATE catch2_main)
add_test(NAME gm_commands COMMAND test_gm_commands)
register_test_target(test_gm_commands)

# ── test_open_formats ────────────────────────────────────────
add_executable(test_open_formats
    test_open_formats.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_building.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_terrain_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wmo_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_open_formats PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_open_formats SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_open_formats PRIVATE catch2_main)
wowee_test_link_glm(test_open_formats)
add_test(NAME open_formats COMMAND test_open_formats)
register_test_target(test_open_formats)

# ── test_open_format_emitter ─────────────────────────────────
# Locks in the asset_extract → wowee open-format conversion path.
add_executable(test_open_format_emitter
    test_open_format_emitter.cpp
    ${CMAKE_SOURCE_DIR}/tools/asset_extract/open_format_emitter.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/blp_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/m2_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wmo_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/adt_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_model.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_building.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/wowee_collision.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_open_format_emitter PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}
)
target_include_directories(test_open_format_emitter SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
)
target_link_libraries(test_open_format_emitter PRIVATE catch2_main)
wowee_test_link_glm(test_open_format_emitter)
add_test(NAME open_format_emitter COMMAND test_open_format_emitter)
register_test_target(test_open_format_emitter)

# ── test_camera ──────────────────────────────────────────────
add_executable(test_camera
    test_camera.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/camera.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_camera PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_camera SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_camera PRIVATE catch2_main)
wowee_test_link_glm(test_camera)
add_test(NAME camera COMMAND test_camera)
register_test_target(test_camera)

# ── test_gltf_terrain_bin ────────────────────────────────────
# The BIN chunk of a terrain .glb, packed byte for byte by two exporters.
# Checked against the glTF 2.0 layout rather than against the old code.
add_executable(test_gltf_terrain_bin
    test_gltf_terrain_bin.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_gltf_terrain_bin PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_gltf_terrain_bin SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
)
target_link_libraries(test_gltf_terrain_bin PRIVATE catch2_main)
wowee_test_link_glm(test_gltf_terrain_bin)
add_test(NAME gltf_terrain_bin COMMAND test_gltf_terrain_bin)
register_test_target(test_gltf_terrain_bin)

# ── test_editor_units (SQL escape, quest validation, …) ─────
add_executable(test_editor_units
    test_editor_units.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/sql_exporter.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/npc_spawner.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/quest_editor.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/content_pack.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/editor_brush.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/object_placer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_editor_units PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_editor_units SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
)
target_link_libraries(test_editor_units PRIVATE catch2_main)
# npc_spawner.hpp pulls in <glm/glm.hpp>; link it when available so the
# test target builds on hosts where glm is system-installed (Homebrew etc).
wowee_test_link_glm(test_editor_units)
add_test(NAME editor_units COMMAND test_editor_units)
register_test_target(test_editor_units)

# ── test_expansion_profile ──────────────────────────────────
add_executable(test_expansion_profile
    test_expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_expansion_profile PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_expansion_profile SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_expansion_profile PRIVATE catch2_main)
add_test(NAME expansion_profile COMMAND test_expansion_profile)
register_test_target(test_expansion_profile)

# ── test_buff_bar_layout ────────────────────────────────────
# Header-only layout arithmetic - no ImGui needed.
add_executable(test_buff_bar_layout
    test_buff_bar_layout.cpp
)
target_include_directories(test_buff_bar_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_buff_bar_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_buff_bar_layout PRIVATE catch2_main)
add_test(NAME buff_bar_layout COMMAND test_buff_bar_layout)
register_test_target(test_buff_bar_layout)

# ── test_dbc_layouts ────────────────────────────────────────
# Reads the layout files shipped in Data/expansions, so it needs the source dir.
add_executable(test_dbc_layouts
    test_dbc_layouts.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/dbc_loader.cpp
)
target_include_directories(test_dbc_layouts PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_dbc_layouts SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_dbc_layouts PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_dbc_layouts PRIVATE catch2_main)
add_test(NAME dbc_layouts COMMAND test_dbc_layouts)
register_test_target(test_dbc_layouts)

# ── test_update_field_table ─────────────────────────────────
add_executable(test_update_field_table
    test_update_field_table.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/game/update_field_table.cpp
)
target_include_directories(test_update_field_table PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_update_field_table SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_update_field_table PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_update_field_table PRIVATE catch2_main)
add_test(NAME update_field_table COMMAND test_update_field_table)
register_test_target(test_update_field_table)

# ── test_pet_action ─────────────────────────────────────────
add_executable(test_pet_action
    test_pet_action.cpp
)
target_include_directories(test_pet_action PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_pet_action SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_pet_action PRIVATE catch2_main)
add_test(NAME pet_action COMMAND test_pet_action)
register_test_target(test_pet_action)

# ── test_spell_classification ───────────────────────────────
add_executable(test_spell_classification
    test_spell_classification.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_spell_classification PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_spell_classification SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_spell_classification PRIVATE catch2_main)
add_test(NAME spell_classification COMMAND test_spell_classification)
register_test_target(test_spell_classification)

# ── test_realm_list ─────────────────────────────────────────
add_executable(test_realm_list
    test_realm_list.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_opcodes.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/big_num.cpp
)
target_include_directories(test_realm_list PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_realm_list SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_realm_list PRIVATE catch2_main OpenSSL::SSL OpenSSL::Crypto)
add_test(NAME realm_list COMMAND test_realm_list)
register_test_target(test_realm_list)

# ── test_quest_query_layout ─────────────────────────────────
# Where the strings start in the quest query response. Pure arithmetic against
# a body built the way the server writes it: reading from the wrong offset does
# not fail, it hands back the next string along and shifts everything after it.
add_executable(test_quest_query_layout test_quest_query_layout.cpp)
target_include_directories(test_quest_query_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_quest_query_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_quest_query_layout PRIVATE catch2_main)
add_test(NAME quest_query_layout COMMAND test_quest_query_layout)
register_test_target(test_quest_query_layout)

# ── test_quest_rewards ──────────────────────────────────────
add_executable(test_quest_rewards
    test_quest_rewards.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/opcode_table.cpp
    ${CMAKE_SOURCE_DIR}/src/game/expansion_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/game/character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_world.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_entity.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_social.cpp
    ${CMAKE_SOURCE_DIR}/src/game/world_packets_economy.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/crypto.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_classic.cpp
    ${CMAKE_SOURCE_DIR}/src/game/packet_parsers_tbc.cpp
)
target_include_directories(test_quest_rewards PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_quest_rewards SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_quest_rewards PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                 OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_quest_rewards)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_quest_rewards PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME quest_rewards COMMAND test_quest_rewards)
register_test_target(test_quest_rewards)

# ── test_inventory_sort ─────────────────────────────────────
add_executable(test_inventory_sort
    test_inventory_sort.cpp
    ${CMAKE_SOURCE_DIR}/src/game/inventory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/logger.cpp
)
target_include_directories(test_inventory_sort PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_inventory_sort SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_inventory_sort PRIVATE catch2_main)
add_test(NAME inventory_sort COMMAND test_inventory_sort)
register_test_target(test_inventory_sort)

# ── test_quest_progress ─────────────────────────────────────
add_executable(test_quest_progress
    test_quest_progress.cpp
)
target_include_directories(test_quest_progress PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_quest_progress SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_quest_progress PRIVATE catch2_main)
add_test(NAME quest_progress COMMAND test_quest_progress)
register_test_target(test_quest_progress)

# ── ASAN / UBSan for test targets ────────────────────────────
if(WOWEE_ENABLE_ASAN AND NOT MSVC)
    foreach(_t IN LISTS ALL_TEST_TARGETS)
        target_compile_options(${_t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${_t} PRIVATE -fsanitize=address,undefined)
    endforeach()
    # catch2_main must also be compiled with the same flags
    target_compile_options(catch2_main PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(catch2_main PRIVATE -fsanitize=address,undefined)
    message(STATUS "Test targets: ASAN + UBSan ENABLED")
endif()

# ── test_m2_classifier_foliage ───────────────────────────────
# Pins the wind-sway classification against substring collisions: foliage
# tokens are matched inside concatenated names, so "thorn" landed inside
# Stranglethorn and made every troll ruin sway like a tree.
add_executable(test_m2_classifier_foliage
    test_m2_classifier_foliage.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/m2_model_classifier.cpp
)
target_include_directories(test_m2_classifier_foliage PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_m2_classifier_foliage SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_m2_classifier_foliage PRIVATE catch2_main)
wowee_test_link_glm(test_m2_classifier_foliage)
add_test(NAME m2_classifier_foliage COMMAND test_m2_classifier_foliage)
register_test_target(test_m2_classifier_foliage)

# ── test_calendar_packet ────────────────────────────────────
add_executable(test_calendar_packet
    test_calendar_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/game/calendar_data.cpp
)
target_include_directories(test_calendar_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_calendar_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_calendar_packet PRIVATE catch2_main SDL2::SDL2 ZLIB::ZLIB
                                                 OpenSSL::SSL OpenSSL::Crypto)
wowee_test_link_glm(test_calendar_packet)
if(TARGET Vulkan::Vulkan)
    target_link_libraries(test_calendar_packet PRIVATE Vulkan::Vulkan)
endif()
add_test(NAME calendar_packet COMMAND test_calendar_packet)
register_test_target(test_calendar_packet)

# ── test_calendar_month ─────────────────────────────────────
add_executable(test_calendar_month test_calendar_month.cpp)
target_include_directories(test_calendar_month PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_calendar_month SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_calendar_month PRIVATE catch2_main)
add_test(NAME calendar_month COMMAND test_calendar_month)
register_test_target(test_calendar_month)

# ── test_catalog_entry_key ───────────────────────────────────
# Which field of a catalog entry is its id. Four commands guessed it from the
# field's name and disagreed; the format table now declares it. The oracle is
# each format's own --info handler, so the test reads the editor sources.
add_executable(test_catalog_entry_key
    test_catalog_entry_key.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/cli_format_table.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_catalog_entry_key PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_catalog_entry_key SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
)
target_compile_definitions(test_catalog_entry_key PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_catalog_entry_key PRIVATE catch2_main)
add_test(NAME catalog_entry_key COMMAND test_catalog_entry_key)
register_test_target(test_catalog_entry_key)

# ── test_catalog_subprocess ──────────────────────────────────
# How the catalog searches reach a file: they re-invoke this executable with
# the format's --info flag. Four commands carried the same four steps. The
# oracle for the quoting is POSIX sh, which the test runs.
add_executable(test_catalog_subprocess
    test_catalog_subprocess.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_catalog_subprocess PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_catalog_subprocess SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
)
target_link_libraries(test_catalog_subprocess PRIVATE catch2_main)
add_test(NAME catalog_subprocess COMMAND test_catalog_subprocess)
register_test_target(test_catalog_subprocess)

# ── test_cli_paths ───────────────────────────────────────────
# Finding the files of one format under a directory. Eleven commands walked
# the tree themselves for .wom alone. The oracle is a tree the test writes.
add_executable(test_cli_paths
    test_cli_paths.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_cli_paths PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_cli_paths SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_cli_paths PRIVATE catch2_main)
add_test(NAME cli_paths COMMAND test_cli_paths)
register_test_target(test_cli_paths)

# ── test_obj_parse ───────────────────────────────────────────
# Reading a Wavefront OBJ, which two importers each implemented. The oracle
# is the OBJ format: 1-based indices, negative indices relative to the pool
# as it stands, and three independently indexed pools.
add_executable(test_obj_parse
    test_obj_parse.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_obj_parse PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_obj_parse SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS}
)
target_link_libraries(test_obj_parse PRIVATE catch2_main)
wowee_test_link_glm(test_obj_parse)
add_test(NAME obj_parse COMMAND test_obj_parse)
register_test_target(test_obj_parse)

# ── test_model_bounds ────────────────────────────────────────
# The box and radius a .wom carries. Eighteen places computed them three
# different ways. The oracle is what the field means: M2Loader sets it from
# the M2 header's boundingRadius, measured from the model origin.
add_executable(test_model_bounds
    test_model_bounds.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_model_bounds PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_model_bounds SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_model_bounds PRIVATE catch2_main)
wowee_test_link_glm(test_model_bounds)
add_test(NAME model_bounds COMMAND test_model_bounds)
register_test_target(test_model_bounds)

# ── test_format_lookup ───────────────────────────────────────
# Finding a format by extension, which three commands each implemented.
# --fix-magic renames on the answer. The oracle is the table itself: every
# row's extension has to find that row.
add_executable(test_format_lookup
    test_format_lookup.cpp
    ${CMAKE_SOURCE_DIR}/tools/editor/cli_format_table.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_format_lookup PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/tools/editor
)
target_include_directories(test_format_lookup SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_format_lookup PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_format_lookup PRIVATE catch2_main)
add_test(NAME format_lookup COMMAND test_format_lookup)
register_test_target(test_format_lookup)

# ── test_wmo_material_class ──────────────────────────────────
# Which WMO materials are glass. The oracle is the shipped WMO data: the
# MOMT flags the artist set, not the texture's file name.
add_executable(test_wmo_material_class
    test_wmo_material_class.cpp
    ${TEST_COMMON_SOURCES})
target_include_directories(test_wmo_material_class PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_wmo_material_class SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_wmo_material_class PRIVATE catch2_main)
add_test(NAME wmo_material_class COMMAND test_wmo_material_class)
register_test_target(test_wmo_material_class)

# ── test_asset_token_name ────────────────────────────────────
# Which part of an asset path a token check may read. A directory name
# poisons every token under it: Item/TextureComponents/LegLowerTexture
# spells "glow", so every leg and boot texture was being colour-keyed.
add_executable(test_asset_token_name
    test_asset_token_name.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/m2_model_classifier.cpp
)
target_include_directories(test_asset_token_name PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_asset_token_name SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_asset_token_name PRIVATE catch2_main)
wowee_test_link_glm(test_asset_token_name)
add_test(NAME asset_token_name COMMAND test_asset_token_name)
register_test_target(test_asset_token_name)
