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
	int _imageHeight = 512;
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
		{ String _name = "ExtensionsEntry_icon_slide";	String _source = "Sprite/Extensions/Entry_icon_slide.png"; }
		{ String _name = "ExtensionsEntry_residence_goto";	String _source = "Sprite/Extensions/Entry_residence_goto.png"; }
		{ String _name = "ExtensionsEntry_tavern_counter";	String _source = "Sprite/Extensions/Entry_tavern_counter.png"; }
		{ String _name = "ExtensionsEntry_start_barn_height";	String _source = "Sprite/Extensions/Entry_start_barn_height.png"; }
		{ String _name = "ExtensionsEntry_ui_scale_layout";	String _source = "Sprite/Extensions/Entry_ui_scale_layout.png"; }
		{ String _name = "ExtensionsEntry_rock_seat";	String _source = "Sprite/Extensions/Entry_rock_seat.png"; }
		{ String _name = "ExtensionsEntry_terrain_resort";	String _source = "Sprite/Extensions/Entry_terrain_resort.png"; }
		{ String _name = "ExtensionsEntry_large_packages";	String _source = "Sprite/Extensions/Entry_large_packages.png"; }
		{ String _name = "ExtensionsEntry_boot_device_lost";	String _source = "Sprite/Extensions/Entry_boot_device_lost.png"; }
		{ String _name = "ExtensionsEntry_slider_drag_clamp";	String _source = "Sprite/Extensions/Entry_slider_drag_clamp.png"; }
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
		{ String _name = "ExtensionsEntry_max_shadow_res";	String _source = "Sprite/Extensions/Entry_max_shadow_res.png"; }
		{ String _name = "ExtensionsEntry_seamless_loops";	String _source = "Sprite/Extensions/Entry_seamless_loops.png"; }
		{ String _name = "ExtensionsEntry_spread_warmth";	String _source = "Sprite/Extensions/Entry_spread_warmth.png"; }
		{ String _name = "ExtensionsEntry_async_save_compress";	String _source = "Sprite/Extensions/Entry_async_save_compress.png"; }
		{ String _name = "ExtensionsEntry_drain_split";	String _source = "Sprite/Extensions/Entry_drain_split.png"; }
		{ String _name = "ExtensionsEntry_background_save";	String _source = "Sprite/Extensions/Entry_background_save.png"; }
		{ String _name = "ExtensionsEntry_better_camera";	String _source = "Sprite/Extensions/Entry_better_camera.png"; }
		{ String _name = "ExtensionsEntry_no_fog";	String _source = "Sprite/Extensions/Entry_no_fog.png"; }
		{ String _name = "ExtensionsEntry_high_limits";	String _source = "Sprite/Extensions/Entry_high_limits.png"; }
		{ String _name = "ExtensionsEntry_restricted_foods";	String _source = "Sprite/Extensions/Entry_restricted_foods.png"; }
		{ String _name = "ExtensionsEntry_item_priority";	String _source = "Sprite/Extensions/Entry_item_priority.png"; }
		{ String _name = "ExtensionsEntry_terrain_edit";	String _source = "Sprite/Extensions/Entry_terrain_edit.png"; }
		{ String _name = "ExtensionsGlyph_terrain_raise";	String _source = "Sprite/Extensions/Glyph_terrain_raise.png"; }
		{ String _name = "ExtensionsGlyph_terrain_lower";	String _source = "Sprite/Extensions/Glyph_terrain_lower.png"; }
		{ String _name = "ExtensionsGlyph_terrain_flatten";	String _source = "Sprite/Extensions/Glyph_terrain_flatten.png"; }
		{ String _name = "ExtensionsGlyph_terrain_mountain";	String _source = "Sprite/Extensions/Glyph_terrain_mountain.png"; }
		{ String _name = "ExtensionsGlyph_terrain_smooth";	String _source = "Sprite/Extensions/Glyph_terrain_smooth.png"; }
		{ String _name = "ExtensionsGlyph_terrain_undo";	String _source = "Sprite/Extensions/Glyph_terrain_undo.png"; }
		{ String _name = "ExtensionsGlyph_terrain_redo";	String _source = "Sprite/Extensions/Glyph_terrain_redo.png"; }
		{ String _name = "ExtensionsGlyph_terrain_plant";	String _source = "Sprite/Extensions/Glyph_terrain_plant.png"; }
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
	]
}
