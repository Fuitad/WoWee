#include "cli_dispatch.hpp"

#include "cli_gen_audio.hpp"
#include "cli_zone_packs.hpp"
#include "cli_audits.hpp"
#include "cli_readmes.hpp"
#include "cli_zone_inventory.hpp"
#include "cli_project_inventory.hpp"
#include "cli_gen_texture.hpp"
#include "cli_gen_mesh.hpp"
#include "cli_mesh_io.hpp"
#include "cli_mesh_edit.hpp"
#include "cli_wom_info.hpp"
#include "cli_format_validate.hpp"
#include "cli_convert.hpp"
#include "cli_format_info.hpp"
#include "cli_pack.hpp"
#include "cli_content_info.hpp"
#include "cli_zone_info.hpp"
#include "cli_data_tree.hpp"
#include "cli_diff.hpp"
#include "cli_spawn_audit.hpp"
#include "cli_items.hpp"
#include "cli_extract_info.hpp"
#include "cli_export.hpp"
#include "cli_bake.hpp"
#include "cli_migrate.hpp"
#include "cli_validate_interop.hpp"
#include "cli_glb_inspect.hpp"
#include "cli_wom_io.hpp"
#include "cli_world_io.hpp"
#include "cli_info_tree.hpp"
#include "cli_info_bytes.hpp"
#include "cli_info_extents.hpp"
#include "cli_info_water.hpp"
#include "cli_info_density.hpp"
#include "cli_info_audio.hpp"
#include "cli_world_info.hpp"
#include "cli_world_map.hpp"
#include "cli_sound_catalog.hpp"
#include "cli_spawns_catalog.hpp"
#include "cli_items_catalog.hpp"
#include "cli_loot_catalog.hpp"
#include "cli_creatures_catalog.hpp"
#include "cli_quests_catalog.hpp"
#include "cli_objects_catalog.hpp"
#include "cli_factions_catalog.hpp"
#include "cli_locks_catalog.hpp"
#include "cli_skills_catalog.hpp"
#include "cli_spells_catalog.hpp"
#include "cli_achievements_catalog.hpp"
#include "cli_trainers_catalog.hpp"
#include "cli_gossip_catalog.hpp"
#include "cli_taxi_catalog.hpp"
#include "cli_talents_catalog.hpp"
#include "cli_maps_catalog.hpp"
#include "cli_chars_catalog.hpp"
#include "cli_tokens_catalog.hpp"
#include "cli_triggers_catalog.hpp"
#include "cli_titles_catalog.hpp"
#include "cli_events_catalog.hpp"
#include "cli_mounts_catalog.hpp"
#include "cli_battlegrounds_catalog.hpp"
#include "cli_mail_catalog.hpp"
#include "cli_gems_catalog.hpp"
#include "cli_guilds_catalog.hpp"
#include "cli_conditions_catalog.hpp"
#include "cli_pets_catalog.hpp"
#include "cli_auction_catalog.hpp"
#include "cli_channels_catalog.hpp"
#include "cli_cinematics_catalog.hpp"
#include "cli_glyphs_catalog.hpp"
#include "cli_vehicles_catalog.hpp"
#include "cli_holidays_catalog.hpp"
#include "cli_liquids_catalog.hpp"
#include "cli_list_formats.hpp"
#include "cli_info_magic.hpp"
#include "cli_animations_catalog.hpp"
#include "cli_spell_visuals_catalog.hpp"
#include "cli_summary_dir.hpp"
#include "cli_rename_magic.hpp"
#include "cli_world_state_ui_catalog.hpp"
#include "cli_player_conditions_catalog.hpp"
#include "cli_trade_skills_catalog.hpp"
#include "cli_creature_equipment_catalog.hpp"
#include "cli_item_sets_catalog.hpp"
#include "cli_touch_tree.hpp"
#include "cli_game_tips_catalog.hpp"
#include "cli_companions_catalog.hpp"
#include "cli_spell_mechanics_catalog.hpp"
#include "cli_keybindings_catalog.hpp"
#include "cli_tree_summary_md.hpp"
#include "cli_spell_schools_catalog.hpp"
#include "cli_lfg_catalog.hpp"
#include "cli_catalog_grep.hpp"
#include "cli_diff_headers.hpp"
#include "cli_audit_tree.hpp"
#include "cli_magic_fix.hpp"
#include "cli_bulk_validate.hpp"
#include "cli_bulk_json.hpp"
#include "cli_diff_tree.hpp"
#include "cli_orphan_jsons.hpp"
#include "cli_list_by_magic.hpp"
#include "cli_catalog_stats.hpp"
#include "cli_macros_catalog.hpp"
#include "cli_char_features_catalog.hpp"
#include "cli_pvp_catalog.hpp"
#include "cli_bags_catalog.hpp"
#include "cli_runes_catalog.hpp"
#include "cli_loading_screens_catalog.hpp"
#include "cli_item_suffixes_catalog.hpp"
#include "cli_combat_ratings_catalog.hpp"
#include "cli_unit_movement_catalog.hpp"
#include "cli_quest_sorts_catalog.hpp"
#include "cli_spell_ranges_catalog.hpp"
#include "cli_spell_cast_times_catalog.hpp"
#include "cli_spell_durations_catalog.hpp"
#include "cli_spell_cooldowns_catalog.hpp"
#include "cli_creature_families_catalog.hpp"
#include "cli_spell_power_costs_catalog.hpp"
#include "cli_glyph_slots_catalog.hpp"
#include "cli_creature_difficulties_catalog.hpp"
#include "cli_item_materials_catalog.hpp"
#include "cli_player_spawn_profiles_catalog.hpp"
#include "cli_talent_tabs_catalog.hpp"
#include "cli_currency_types_catalog.hpp"
#include "cli_spell_reagents_catalog.hpp"
#include "cli_achievement_criteria_catalog.hpp"
#include "cli_spell_effect_types_catalog.hpp"
#include "cli_spell_aura_types_catalog.hpp"
#include "cli_item_qualities_catalog.hpp"
#include "cli_skill_costs_catalog.hpp"
#include "cli_item_flags_catalog.hpp"
#include "cli_npc_services_catalog.hpp"
#include "cli_token_rewards_catalog.hpp"
#include "cli_spell_procs_catalog.hpp"
#include "cli_creature_patrols_catalog.hpp"
#include "cli_boss_encounters_catalog.hpp"
#include "cli_instance_lockouts_catalog.hpp"
#include "cli_stable_slots_catalog.hpp"
#include "cli_stat_curves_catalog.hpp"
#include "cli_action_bars_catalog.hpp"
#include "cli_group_compositions_catalog.hpp"
#include "cli_hearth_binds_catalog.hpp"
#include "cli_server_broadcasts_catalog.hpp"
#include "cli_combat_maneuvers_catalog.hpp"
#include "cli_realm_list_catalog.hpp"
#include "cli_emotes_catalog.hpp"
#include "cli_buff_book_catalog.hpp"
#include "cli_tabards_catalog.hpp"
#include "cli_spell_markers_catalog.hpp"
#include "cli_learning_notifications_catalog.hpp"
#include "cli_creature_resists_catalog.hpp"
#include "cli_pet_talents_catalog.hpp"
#include "cli_heroic_scaling_catalog.hpp"
#include "cli_reputation_rewards_catalog.hpp"
#include "cli_minimap_levels_catalog.hpp"
#include "cli_pet_care_catalog.hpp"
#include "cli_movie_credits_catalog.hpp"
#include "cli_spell_variants_catalog.hpp"
#include "cli_voiceovers_catalog.hpp"
#include "cli_trade_rules_catalog.hpp"
#include "cli_word_filters_catalog.hpp"
#include "cli_raid_markers_catalog.hpp"
#include "cli_loot_modes_catalog.hpp"
#include "cli_sky_params_catalog.hpp"
#include "cli_server_config_catalog.hpp"
#include "cli_anniversary_events_catalog.hpp"
#include "cli_pvp_ranks_catalog.hpp"
#include "cli_localization_catalog.hpp"
#include "cli_global_channels_catalog.hpp"
#include "cli_addon_manifest_catalog.hpp"
#include "cli_spell_pack_catalog.hpp"
#include "cli_player_movement_anim_catalog.hpp"
#include "cli_transit_schedule_catalog.hpp"
#include "cli_mage_portals_catalog.hpp"
#include "cli_combat_stats_catalog.hpp"
#include "cli_guild_bank_catalog.hpp"
#include "cli_quest_graph_catalog.hpp"
#include "cli_crafting_recipes_catalog.hpp"
#include "cli_world_locations_catalog.hpp"
#include "cli_soulbind_rules_catalog.hpp"
#include "cli_creature_behavior_catalog.hpp"
#include "cli_random_property_catalog.hpp"
#include "cli_spell_proc_rules_catalog.hpp"
#include "cli_auction_houses_catalog.hpp"
#include "cli_battleground_rewards_catalog.hpp"
#include "cli_sound_swap_catalog.hpp"
#include "cli_tutorial_steps_catalog.hpp"
#include "cli_chat_commands_catalog.hpp"
#include "cli_camera_presets_catalog.hpp"
#include "cli_combat_formulas_catalog.hpp"
#include "cli_chat_links_catalog.hpp"
#include "cli_catalog_pluck.hpp"
#include "cli_catalog_find.hpp"
#include "cli_catalog_by_name.hpp"
#include "cli_catalog_id_range.hpp"
#include "cli_quest_objective.hpp"
#include "cli_quest_reward.hpp"
#include "cli_clone.hpp"
#include "cli_remove.hpp"
#include "cli_add.hpp"
#include "cli_random.hpp"
#include "cli_items_export.hpp"
#include "cli_items_mutate.hpp"
#include "cli_zone_create.hpp"
#include "cli_tiles.hpp"
#include "cli_zone_mgmt.hpp"
#include "cli_strip.hpp"
#include "cli_repair.hpp"
#include "cli_makefile.hpp"
#include "cli_zone_list.hpp"
#include "cli_tilemap.hpp"
#include "cli_deps.hpp"
#include "cli_for_each.hpp"
#include "cli_check.hpp"
#include "cli_introspect.hpp"
#include "cli_texture_helpers.hpp"
#include "cli_mesh_info.hpp"
#include "cli_zone_data.hpp"
#include "cli_project_actions.hpp"
#include "cli_zone_export.hpp"

namespace wowee {
namespace editor {
namespace cli {

namespace {

// Each handler family takes (int& i, int argc, char** argv,
// int& outRc) and returns true if it claimed the flag. The
// table is walked in order until one returns true. Order
// rarely matters - flags are exact-string-matched, so two
// families can't both claim the same flag - but families with
// shorter/cheaper checks still come first by convention.
using DispatchFn = bool (*)(int&, int, char**, int&);

constexpr DispatchFn kDispatchTable[] = {
    handleGenAudio,
    handleZonePacks,
    handleAudits,
    handleReadmes,
    handleZoneInventory,
    handleProjectInventory,
    handleGenTexture,
    handleGenMesh,
    handleMeshIO,
    handleMeshEdit,
    handleWomInfo,
    handleFormatValidate,
    handleConvert,
    handleFormatInfo,
    handlePack,
    handleContentInfo,
    handleZoneInfo,
    handleDataTree,
    handleDiff,
    handleSpawnAudit,
    handleItems,
    handleExtractInfo,
    handleExport,
    handleBake,
    handleMigrate,
    handleValidateInterop,
    handleGlbInspect,
    handleWomIo,
    handleWorldIo,
    handleInfoTree,
    handleInfoBytes,
    handleInfoExtents,
    handleInfoWater,
    handleInfoDensity,
    handleInfoAudio,
    handleWorldInfo,
    handleWorldMap,
    handleSoundCatalog,
    handleSpawnsCatalog,
    handleItemsCatalog,
    handleLootCatalog,
    handleCreaturesCatalog,
    handleQuestsCatalog,
    handleObjectsCatalog,
    handleFactionsCatalog,
    handleLocksCatalog,
    handleSkillsCatalog,
    handleSpellsCatalog,
    handleAchievementsCatalog,
    handleTrainersCatalog,
    handleGossipCatalog,
    handleTaxiCatalog,
    handleTalentsCatalog,
    handleMapsCatalog,
    handleCharsCatalog,
    handleTokensCatalog,
    handleTriggersCatalog,
    handleTitlesCatalog,
    handleEventsCatalog,
    handleMountsCatalog,
    handleBattlegroundsCatalog,
    handleMailCatalog,
    handleGemsCatalog,
    handleGuildsCatalog,
    handleConditionsCatalog,
    handlePetsCatalog,
    handleAuctionCatalog,
    handleChannelsCatalog,
    handleCinematicsCatalog,
    handleGlyphsCatalog,
    handleVehiclesCatalog,
    handleHolidaysCatalog,
    handleLiquidsCatalog,
    handleListFormats,
    handleInfoMagic,
    handleAnimationsCatalog,
    handleSpellVisualsCatalog,
    handleSummaryDir,
    handleRenameMagic,
    handleWorldStateUICatalog,
    handlePlayerConditionsCatalog,
    handleTradeSkillsCatalog,
    handleCreatureEquipmentCatalog,
    handleItemSetsCatalog,
    handleTouchTree,
    handleGameTipsCatalog,
    handleCompanionsCatalog,
    handleSpellMechanicsCatalog,
    handleKeybindingsCatalog,
    handleTreeSummaryMd,
    handleSpellSchoolsCatalog,
    handleLFGCatalog,
    handleCatalogGrep,
    handleDiffHeaders,
    handleAuditTree,
    handleMagicFix,
    handleBulkValidate,
    handleBulkJson,
    handleDiffTree,
    handleOrphanJsons,
    handleListByMagic,
    handleCatalogStats,
    handleMacrosCatalog,
    handleCharFeaturesCatalog,
    handlePVPCatalog,
    handleBagsCatalog,
    handleRunesCatalog,
    handleLoadingScreensCatalog,
    handleItemSuffixesCatalog,
    handleCombatRatingsCatalog,
    handleUnitMovementCatalog,
    handleQuestSortsCatalog,
    handleSpellRangesCatalog,
    handleSpellCastTimesCatalog,
    handleSpellDurationsCatalog,
    handleSpellCooldownsCatalog,
    handleCreatureFamiliesCatalog,
    handleSpellPowerCostsCatalog,
    handleGlyphSlotsCatalog,
    handleCreatureDifficultiesCatalog,
    handleItemMaterialsCatalog,
    handlePlayerSpawnProfilesCatalog,
    handleTalentTabsCatalog,
    handleCurrencyTypesCatalog,
    handleSpellReagentsCatalog,
    handleAchievementCriteriaCatalog,
    handleSpellEffectTypesCatalog,
    handleSpellAuraTypesCatalog,
    handleItemQualitiesCatalog,
    handleSkillCostsCatalog,
    handleItemFlagsCatalog,
    handleNPCServicesCatalog,
    handleTokenRewardsCatalog,
    handleSpellProcsCatalog,
    handleCreaturePatrolsCatalog,
    handleBossEncountersCatalog,
    handleInstanceLockoutsCatalog,
    handleStableSlotsCatalog,
    handleStatCurvesCatalog,
    handleActionBarsCatalog,
    handleGroupCompositionsCatalog,
    handleHearthBindsCatalog,
    handleServerBroadcastsCatalog,
    handleCombatManeuversCatalog,
    handleRealmListCatalog,
    handleEmotesCatalog,
    handleBuffBookCatalog,
    handleTabardsCatalog,
    handleSpellMarkersCatalog,
    handleLearningNotificationsCatalog,
    handleCreatureResistsCatalog,
    handlePetTalentsCatalog,
    handleHeroicScalingCatalog,
    handleReputationRewardsCatalog,
    handleMinimapLevelsCatalog,
    handlePetCareCatalog,
    handleMovieCreditsCatalog,
    handleSpellVariantsCatalog,
    handleVoiceoversCatalog,
    handleTradeRulesCatalog,
    handleWordFiltersCatalog,
    handleRaidMarkersCatalog,
    handleLootModesCatalog,
    handleSkyParamsCatalog,
    handleServerConfigCatalog,
    handleAnniversaryEventsCatalog,
    handlePvPRanksCatalog,
    handleLocalizationCatalog,
    handleGlobalChannelsCatalog,
    handleAddonManifestCatalog,
    handleSpellPackCatalog,
    handlePlayerMovementAnimCatalog,
    handleTransitScheduleCatalog,
    handleMagePortalsCatalog,
    handleCombatStatsCatalog,
    handleGuildBankCatalog,
    handleQuestGraphCatalog,
    handleCraftingRecipesCatalog,
    handleWorldLocationsCatalog,
    handleSoulbindRulesCatalog,
    handleCreatureBehaviorCatalog,
    handleRandomPropertyCatalog,
    handleSpellProcRulesCatalog,
    handleAuctionHousesCatalog,
    handleBattlegroundRewardsCatalog,
    handleSoundSwapCatalog,
    handleTutorialStepsCatalog,
    handleChatCommandsCatalog,
    handleCameraPresetsCatalog,
    handleCombatFormulasCatalog,
    handleChatLinksCatalog,
    handleCatalogPluck,
    handleCatalogFind,
    handleCatalogByName,
    handleCatalogIdRange,
    handleQuestObjective,
    handleQuestReward,
    handleClone,
    handleRemove,
    handleAdd,
    handleRandom,
    handleItemsExport,
    handleItemsMutate,
    handleZoneCreate,
    handleTiles,
    handleZoneMgmt,
    handleStrip,
    handleRepair,
    handleMakefile,
    handleZoneList,
    handleTilemap,
    handleDeps,
    handleForEach,
    handleCheck,
    handleIntrospect,
    handleTextureHelpers,
    handleMeshInfo,
    handleZoneData,
    handleProjectActions,
    handleZoneExport,
};

}  // namespace

bool tryDispatchAll(int& i, int argc, char** argv, int& outRc) {
    for (DispatchFn fn : kDispatchTable) {
        if (fn(i, argc, argv, outRc)) return true;
    }
    return false;
}

} // namespace cli
} // namespace editor
} // namespace wowee
