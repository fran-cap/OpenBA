// Dialog/ExtensionsSprites.rsc -- the Extensions sprites (NOT RETAIL): the menu button's icon and
// the per-entry row icons (and the older per-tier glyphs). Programmer art (src/ext/ui/Dialog/Sprite/Extensions/*.png, 16 x 16 like
// the Mods button's "Mod" sprite, Dialog/Sprite/ButtonIcons/Scenario.png); the user authors the real
// art later by replacing the PNGs. The kit's sprite-sheet form (Dialog/SpriteSheet.rsc,
// example/building/UI/ApiarySpriteSheet.rsc): the compiler packs the sources into _imageName.

SpriteSheet resource
{
	String _materialName = "ExtensionsSpritesMaterial.rsc";
	String _imageName = "Build/ExtensionsSprites.png";
	int _imageWidth = 512;		// 512 since 2026-10-18: 256 x 256 held 28 sprites (the 29th did not fit)
	int _imageHeight = 1024;	// 1024 since 2026-11-04: 512 x 512 held 115 sprites (the 116th did not fit)
	bool _padForFiltering = true;

	Sprite _sprites
	[
		{ String _name = "ExtensionsIcon";		String _source = "Sprite/Extensions/ExtensionsIcon.png"; }
		{ String _name = "OptionsEnhanced";		String _source = "Sprite/Extensions/OptionsEnhanced.png"; }	// the Options dialog's Enhanced tab (AoOptions.rsc)
		{ String _name = "ExtensionsTierFix";	String _source = "Sprite/Extensions/TierFix.png"; }
		{ String _name = "ExtensionsTierA";		String _source = "Sprite/Extensions/TierA.png"; }
		{ String _name = "ExtensionsTierB";		String _source = "Sprite/Extensions/TierB.png"; }
		{ String _name = "ExtensionsTierC";		String _source = "Sprite/Extensions/TierC.png"; }
		// one icon per registry entry (tools/ext_icons/*.py), named ExtensionsEntry_<id>
		{ String _name = "ExtensionsEntry_recipe_skip";	String _source = "Sprite/Extensions/Entry_recipe_skip.png"; }
		{ String _name = "ExtensionsEntry_icon_slide";	String _source = "Sprite/Extensions/Entry_icon_slide.png"; }
		{ String _name = "ExtensionsEntry_residence_goto";	String _source = "Sprite/Extensions/Entry_residence_goto.png"; }
		{ String _name = "ExtensionsEntry_tavern_counter";	String _source = "Sprite/Extensions/Entry_tavern_counter.png"; }
		{ String _name = "ExtensionsEntry_start_barn_height";	String _source = "Sprite/Extensions/Entry_start_barn_height.png"; }
		{ String _name = "ExtensionsEntry_ui_scale_layout";	String _source = "Sprite/Extensions/Entry_ui_scale_layout.png"; }
		{ String _name = "ExtensionsEntry_rock_seat";	String _source = "Sprite/Extensions/Entry_rock_seat.png"; }
		{ String _name = "ExtensionsEntry_town_screenshot";	String _source = "Sprite/Extensions/Entry_town_screenshot.png"; }
		{ String _name = "ExtensionsEntry_terrain_resort";	String _source = "Sprite/Extensions/Entry_terrain_resort.png"; }
		{ String _name = "ExtensionsEntry_large_packages";	String _source = "Sprite/Extensions/Entry_large_packages.png"; }
		{ String _name = "ExtensionsEntry_boot_device_lost";	String _source = "Sprite/Extensions/Entry_boot_device_lost.png"; }
		{ String _name = "ExtensionsEntry_slider_drag_clamp";	String _source = "Sprite/Extensions/Entry_slider_drag_clamp.png"; }
		{ String _name = "ExtensionsEntry_dialog_position_drift";	String _source = "Sprite/Extensions/Entry_dialog_position_drift.png"; }
		{ String _name = "ExtensionsEntry_outline_with_panel";	String _source = "Sprite/Extensions/Entry_outline_with_panel.png"; }
		{ String _name = "ExtensionsEntry_alloc_tiebreak";	String _source = "Sprite/Extensions/Entry_alloc_tiebreak.png"; }
		{ String _name = "ExtensionsEntry_foundation_piles";	String _source = "Sprite/Extensions/Entry_foundation_piles.png"; }
		{ String _name = "ExtensionsEntry_path_query_first";	String _source = "Sprite/Extensions/Entry_path_query_first.png"; }
		{ String _name = "ExtensionsEntry_crop_planting_exit";	String _source = "Sprite/Extensions/Entry_crop_planting_exit.png"; }
		{ String _name = "ExtensionsEntry_firefighter_wells";	String _source = "Sprite/Extensions/Entry_firefighter_wells.png"; }
		{ String _name = "ExtensionsEntry_unresolved_seeds";	String _source = "Sprite/Extensions/Entry_unresolved_seeds.png"; }
		{ String _name = "ExtensionsEntry_commute_cost";	String _source = "Sprite/Extensions/Entry_commute_cost.png"; }
		{ String _name = "ExtensionsEntry_nomad_arrival";	String _source = "Sprite/Extensions/Entry_nomad_arrival.png"; }
		{ String _name = "ExtensionsEntry_mods_persist";	String _source = "Sprite/Extensions/Entry_mods_persist.png"; }
		{ String _name = "ExtensionsEntry_save_failure_notice";	String _source = "Sprite/Extensions/Entry_save_failure_notice.png"; }
		{ String _name = "ExtensionsEntry_boat_reach_notice";	String _source = "Sprite/Extensions/Entry_boat_reach_notice.png"; }
		{ String _name = "ExtensionsEntry_cancel_refund";	String _source = "Sprite/Extensions/Entry_cancel_refund.png"; }
		{ String _name = "ExtensionsEntry_autotrade_free_goods";	String _source = "Sprite/Extensions/Entry_autotrade_free_goods.png"; }
		{ String _name = "ExtensionsEntry_input_fetch_room";	String _source = "Sprite/Extensions/Entry_input_fetch_room.png"; }
		{ String _name = "ExtensionsEntry_stranded_wade";	String _source = "Sprite/Extensions/Entry_stranded_wade.png"; }
		{ String _name = "ExtensionsEntry_debug_spawn_origin";	String _source = "Sprite/Extensions/Entry_debug_spawn_origin.png"; }
		{ String _name = "ExtensionsEntry_statusbar_empty_town";	String _source = "Sprite/Extensions/Entry_statusbar_empty_town.png"; }
		{ String _name = "ExtensionsEntry_stream_free_wait";	String _source = "Sprite/Extensions/Entry_stream_free_wait.png"; }
		{ String _name = "ExtensionsEntry_minimap_outline_clip";	String _source = "Sprite/Extensions/Entry_minimap_outline_clip.png"; }
		{ String _name = "ExtensionsEntry_decal_fog_order";	String _source = "Sprite/Extensions/Entry_decal_fog_order.png"; }
		{ String _name = "ExtensionsEntry_shadow_zoom_pulse";	String _source = "Sprite/Extensions/Entry_shadow_zoom_pulse.png"; }
		{ String _name = "ExtensionsEntry_eye_camera";	String _source = "Sprite/Extensions/Entry_eye_camera.png"; }
		{ String _name = "ExtensionsEntry_sharp_ui";	String _source = "Sprite/Extensions/Entry_sharp_ui.png"; }
		{ String _name = "ExtensionsEntry_fast_paths";	String _source = "Sprite/Extensions/Entry_fast_paths.png"; }
		{ String _name = "ExtensionsEntry_path_workers";	String _source = "Sprite/Extensions/Entry_path_workers.png"; }
		{ String _name = "ExtensionsEntry_render_culling";	String _source = "Sprite/Extensions/Entry_render_culling.png"; }
		{ String _name = "ExtensionsEntry_anim_batch";	String _source = "Sprite/Extensions/Entry_anim_batch.png"; }
		{ String _name = "ExtensionsEntry_draw_gather";	String _source = "Sprite/Extensions/Entry_draw_gather.png"; }
		{ String _name = "ExtensionsEntry_draw_batch";	String _source = "Sprite/Extensions/Entry_draw_batch.png"; }
		{ String _name = "ExtensionsEntry_walk_batch";	String _source = "Sprite/Extensions/Entry_walk_batch.png"; }
		{ String _name = "ExtensionsEntry_walk_batch_grid";	String _source = "Sprite/Extensions/Entry_walk_batch_grid.png"; }
		{ String _name = "ExtensionsEntry_render_pipe";	String _source = "Sprite/Extensions/Entry_render_pipe.png"; }
		{ String _name = "ExtensionsEntry_gather_threads";	String _source = "Sprite/Extensions/Entry_gather_threads.png"; }
		{ String _name = "ExtensionsEntry_quick_screens";	String _source = "Sprite/Extensions/Entry_quick_screens.png"; }
		{ String _name = "ExtensionsEntry_ambient_occlusion";	String _source = "Sprite/Extensions/Entry_ambient_occlusion.png"; }
		{ String _name = "ExtensionsEntry_better_trees";	String _source = "Sprite/Extensions/Entry_better_trees.png"; }
		{ String _name = "ExtensionsEntry_better_water";	String _source = "Sprite/Extensions/Entry_better_water.png"; }
		{ String _name = "ExtensionsEntry_winter_ice";	String _source = "Sprite/Extensions/Entry_winter_ice.png"; }
		{ String _name = "ExtensionsEntry_walk_on_ice";	String _source = "Sprite/Extensions/Entry_walk_on_ice.png"; }
		{ String _name = "ExtensionsEntry_better_clouds";	String _source = "Sprite/Extensions/Entry_better_clouds.png"; }
		{ String _name = "ExtensionsEntry_fancy_terrain";	String _source = "Sprite/Extensions/Entry_fancy_terrain.png"; }
		{ String _name = "ExtensionsEntry_depth_of_field";	String _source = "Sprite/Extensions/Entry_depth_of_field.png"; }
		{ String _name = "ExtensionsEntry_leaf_lod";	String _source = "Sprite/Extensions/Entry_leaf_lod.png"; }
		{ String _name = "ExtensionsEntry_boat_lane";	String _source = "Sprite/Extensions/Entry_boat_lane.png"; }
		{ String _name = "ExtensionsEntry_max_shadow_res";	String _source = "Sprite/Extensions/Entry_max_shadow_res.png"; }
		{ String _name = "ExtensionsEntry_seamless_loops";	String _source = "Sprite/Extensions/Entry_seamless_loops.png"; }
		{ String _name = "ExtensionsEntry_spread_warmth";	String _source = "Sprite/Extensions/Entry_spread_warmth.png"; }
		{ String _name = "ExtensionsEntry_async_save_compress";	String _source = "Sprite/Extensions/Entry_async_save_compress.png"; }
		{ String _name = "ExtensionsEntry_drain_split";	String _source = "Sprite/Extensions/Entry_drain_split.png"; }
		{ String _name = "ExtensionsEntry_background_save";	String _source = "Sprite/Extensions/Entry_background_save.png"; }
		{ String _name = "ExtensionsEntry_better_camera";	String _source = "Sprite/Extensions/Entry_better_camera.png"; }
		{ String _name = "ExtensionsEntry_camera_angle";	String _source = "Sprite/Extensions/Entry_camera_angle.png"; }
		{ String _name = "ExtensionsEntry_no_fog";	String _source = "Sprite/Extensions/Entry_no_fog.png"; }
		{ String _name = "ExtensionsEntry_show_grid";	String _source = "Sprite/Extensions/Entry_show_grid.png"; }
		{ String _name = "ExtensionsEntry_fps_counter";	String _source = "Sprite/Extensions/Entry_fps_counter.png"; }
		{ String _name = "ExtensionsEntry_building_finder";	String _source = "Sprite/Extensions/Entry_building_finder.png"; }
		{ String _name = "ExtensionsEntry_dynamic_professions";	String _source = "Sprite/Extensions/Entry_dynamic_professions.png"; }
		{ String _name = "ExtensionsEntry_tool_names";	String _source = "Sprite/Extensions/Entry_tool_names.png"; }
		{ String _name = "ExtensionsEntry_high_limits";	String _source = "Sprite/Extensions/Entry_high_limits.png"; }
		{ String _name = "ExtensionsEntry_restricted_foods";	String _source = "Sprite/Extensions/Entry_restricted_foods.png"; }
		{ String _name = "ExtensionsEntry_item_priority";	String _source = "Sprite/Extensions/Entry_item_priority.png"; }
		{ String _name = "ExtensionsEntry_item_limits";	String _source = "Sprite/Extensions/Entry_item_limits.png"; }
		{ String _name = "ExtensionsEntry_family_tree";	String _source = "Sprite/Extensions/Entry_family_tree.png"; }
		{ String _name = "ExtensionsEntry_map_overlays";	String _source = "Sprite/Extensions/Entry_map_overlays.png"; }
		{ String _name = "ExtensionsEntry_terrain_edit";	String _source = "Sprite/Extensions/Entry_terrain_edit.png"; }
		{ String _name = "ExtensionsEntry_staggered_passes";	String _source = "Sprite/Extensions/Entry_staggered_passes.png"; }
		{ String _name = "ExtensionsGlyph_town_screenshot_view";	String _source = "Sprite/Extensions/Glyph_town_screenshot_view.png"; }
		{ String _name = "ExtensionsGlyph_town_screenshot_take";	String _source = "Sprite/Extensions/Glyph_town_screenshot_take.png"; }
		{ String _name = "ExtensionsGlyph_terrain_raise";	String _source = "Sprite/Extensions/Glyph_terrain_raise.png"; }
		{ String _name = "ExtensionsGlyph_terrain_lower";	String _source = "Sprite/Extensions/Glyph_terrain_lower.png"; }
		{ String _name = "ExtensionsGlyph_terrain_flatten";	String _source = "Sprite/Extensions/Glyph_terrain_flatten.png"; }
		{ String _name = "ExtensionsGlyph_terrain_mountain";	String _source = "Sprite/Extensions/Glyph_terrain_mountain.png"; }
		{ String _name = "ExtensionsGlyph_terrain_smooth";	String _source = "Sprite/Extensions/Glyph_terrain_smooth.png"; }
		{ String _name = "ExtensionsGlyph_terrain_undo";	String _source = "Sprite/Extensions/Glyph_terrain_undo.png"; }
		{ String _name = "ExtensionsGlyph_terrain_redo";	String _source = "Sprite/Extensions/Glyph_terrain_redo.png"; }
		{ String _name = "ExtensionsGlyph_professions_one_column";	String _source = "Sprite/Extensions/Glyph_professions_one_column.png"; }
		{ String _name = "ExtensionsGlyph_professions_two_columns";	String _source = "Sprite/Extensions/Glyph_professions_two_columns.png"; }
		{ String _name = "ExtensionsGlyph_terrain_plant";	String _source = "Sprite/Extensions/Glyph_terrain_plant.png"; }
		{ String _name = "ExtensionsGlyph_pointer_shift";	String _source = "Sprite/Extensions/Glyph_pointer_shift.png"; }	// ext.touch_pointer's Shift toggle (TouchPointer.rsc)
		{ String _name = "ToolbarExtensions";	String _source = "Sprite/Extensions/Toolbar_extensions.png"; }	// the toolbar's one extension button (ExtensionsWindow.rsc, src/ext/toolbar)
		{ String _name = "ExtensionsEntry_terrain_batch";	String _source = "Sprite/Extensions/Entry_terrain_batch.png"; }
		{ String _name = "ExtensionsEntry_decode_ahead";	String _source = "Sprite/Extensions/Entry_decode_ahead.png"; }
		{ String _name = "ExtensionsEntry_load_progress";	String _source = "Sprite/Extensions/Entry_load_progress.png"; }
		{ String _name = "ExtensionsEntry_async_gpu_create";	String _source = "Sprite/Extensions/Entry_async_gpu_create.png"; }
		{ String _name = "ExtensionsEntry_compress_ao";	String _source = "Sprite/Extensions/Entry_compress_ao.png"; }
		{ String _name = "ExtensionsEntry_share_gpu_data";	String _source = "Sprite/Extensions/Entry_share_gpu_data.png"; }
		{ String _name = "ExtensionsEntry_create_on_demand";	String _source = "Sprite/Extensions/Entry_create_on_demand.png"; }
		{ String _name = "ExtensionsEntry_stream_cache";	String _source = "Sprite/Extensions/Entry_stream_cache.png"; }
		{ String _name = "ExtensionsEntry_package_cache";	String _source = "Sprite/Extensions/Entry_package_cache.png"; }
		{ String _name = "ExtensionsEntry_menu_entry";	String _source = "Sprite/Extensions/Entry_menu_entry.png"; }
		{ String _name = "ExtensionsEntry_visit_replay";	String _source = "Sprite/Extensions/Entry_visit_replay.png"; }
		{ String _name = "ExtensionsEntry_find_fastpath";	String _source = "Sprite/Extensions/Entry_find_fastpath.png"; }
		{ String _name = "ExtensionsEntry_heap_slack";	String _source = "Sprite/Extensions/Entry_heap_slack.png"; }
		{ String _name = "ExtensionsEntry_texture_quality";	String _source = "Sprite/Extensions/Entry_texture_quality.png"; }
		{ String _name = "ExtensionsEntry_phone_menu";	String _source = "Sprite/Extensions/Entry_phone_menu.png"; }
		{ String _name = "ExtensionsEntry_fps_cap";	String _source = "Sprite/Extensions/Entry_fps_cap.png"; }
		{ String _name = "ExtensionsEntry_touch_mode";	String _source = "Sprite/Extensions/Entry_touch_mode.png"; }
		{ String _name = "ExtensionsEntry_touch_pointer";	String _source = "Sprite/Extensions/Entry_touch_pointer.png"; }
		{ String _name = "TreeLink1";	String _source = "Sprite/Extensions/TreeLink1.png"; }
		{ String _name = "TreeLink2";	String _source = "Sprite/Extensions/TreeLink2.png"; }
		{ String _name = "TreeLink3";	String _source = "Sprite/Extensions/TreeLink3.png"; }
		{ String _name = "TreeLink4";	String _source = "Sprite/Extensions/TreeLink4.png"; }
		{ String _name = "TreeLink5";	String _source = "Sprite/Extensions/TreeLink5.png"; }
		{ String _name = "TreeLink6";	String _source = "Sprite/Extensions/TreeLink6.png"; }
		{ String _name = "TreeLink7";	String _source = "Sprite/Extensions/TreeLink7.png"; }
		{ String _name = "TreeLink8";	String _source = "Sprite/Extensions/TreeLink8.png"; }
		{ String _name = "TreeLink9";	String _source = "Sprite/Extensions/TreeLink9.png"; }
		{ String _name = "TreeLink10";	String _source = "Sprite/Extensions/TreeLink10.png"; }
		{ String _name = "TreeLink11";	String _source = "Sprite/Extensions/TreeLink11.png"; }
		{ String _name = "TreeLink12";	String _source = "Sprite/Extensions/TreeLink12.png"; }
		{ String _name = "TreeLink13";	String _source = "Sprite/Extensions/TreeLink13.png"; }
		{ String _name = "TreeLink14";	String _source = "Sprite/Extensions/TreeLink14.png"; }
		{ String _name = "TreeLink15";	String _source = "Sprite/Extensions/TreeLink15.png"; }
		{ String _name = "TreeGap";	String _source = "Sprite/Extensions/TreeGap.png"; }
		{ String _name = "TreeGapStem";	String _source = "Sprite/Extensions/TreeGapStem.png"; }
		{ String _name = "GameFilesIcon";		String _source = "Sprite/Extensions/GameFilesIcon.png"; }	// ext.phone_menu's Game files menu button (issue #96)
		{ String _name = "ExtensionsEntry_fullscreen_mode";	String _source = "Sprite/Extensions/Entry_fullscreen_mode.png"; }
		{ String _name = "ExtensionsEntry_render_backend";	String _source = "Sprite/Extensions/Entry_render_backend.png"; }
	]
}
