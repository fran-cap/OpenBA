# tests/unit/sources.cmake -- HAND-MAINTAINED (it was generated from sources.mk until the Makefile
# was deleted on 2026-10-11). The tree sources the unit tests link against; every family a test
# file depends on is listed here. A body a test calls but whose source is not on this list fails
# at LINK time (undefined reference), not at compile time: add the .cpp here, or a stub in
# tests/unit/link_stubs.cpp when the real file cannot link on the host (tests/unit/README.md).
set(UNIT_TREE_SRCS
  ${CMAKE_SOURCE_DIR}/src/engine/math/vector.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/math/transform.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/math/from_lookat.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/math/wave9_lane_p.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/math/wave9_lane_gfxr.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/rt_libm.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/volume/frustum.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/graphics/draw_sort.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Game/cell_stamp.cpp   # cell_stamp_test.cpp (block AMBMEMO)
  ${CMAKE_SOURCE_DIR}/src/engine/crt/msvc_libm.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/crt/msvc_qsort.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Component/citizenai_sortbypriority.cpp   # citizenai_sort_test.cpp (row 290)
  ${CMAKE_SOURCE_DIR}/src/engine/system/string.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/system/filename.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/object/expand_memo.cpp   # block SAVELOAD: the ExpandRelativePath memo
  ${CMAKE_SOURCE_DIR}/src/engine/collection/array.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/collection/array_destroy.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/collection/linkedlist.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/collection/introsort.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/object/type_reflection.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/object/external_class.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/object/internal_class.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/io/serializer_class.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/io/serialize_family.cpp       # decode_exact_test.cpp (issue #13): the reader's array slots
  ${CMAKE_SOURCE_DIR}/src/engine/io/compressed_stream_read.cpp #   CompressedStream::Read / SetWindow / ReadArray
  ${CMAKE_SOURCE_DIR}/src/engine/system/compression.cpp        #   Compress / Decompress
  ${CMAKE_SOURCE_DIR}/src/engine/system/crc.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/picking/picking_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/pathfinding/pathfinding_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/io/io_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/audio/audio_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/collision2d/collision2d_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/achievement/achievement_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/video/video_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/graphics/graphics_type_records.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/object/root_operator_delete.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/video/companion_operators.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/system/allocator.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/allocator_win.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/system/error.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/video/device_enum_types.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/sync_win.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/thread_win.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/time_win.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/event_std.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/io/settings_file.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/io/settings_store.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/user_dir_override.cpp   # DefaultPath's BANISHED_USER_DIR arm
  ${CMAKE_SOURCE_DIR}/src/ext/registry.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/savetail.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/load_notice.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/resources/text_parser.cpp   # ext_rsc_text_test.cpp (block RSCTEXT)
  ${CMAKE_SOURCE_DIR}/src/ext/resources/text_enums.cpp    #   its identifier tables
  ${CMAKE_SOURCE_DIR}/src/ext/eye_camera/eye_math.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/rock_seat/rock_seat_math.cpp   # ext_rock_seat_test.cpp (ROCKSEAT)
  ${CMAKE_SOURCE_DIR}/src/ext/large_packages/large_packages_unwrap.cpp   # ext_large_packages_test.cpp (LARGEPKM)
  ${CMAKE_SOURCE_DIR}/src/ext/package_cache/index_codec.cpp      # ext_package_cache_test.cpp (PKGCACHE)
  ${CMAKE_SOURCE_DIR}/src/ext/menu_entry/fingerprint.cpp         # ext_menu_entry_test.cpp (MENUENTRY)
  ${CMAKE_SOURCE_DIR}/src/ext/sharp_ui/sharp_math.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/phone_menu/phone_math.cpp   # ext_phone_menu_test.cpp (PHONEMENU)
  ${CMAKE_SOURCE_DIR}/src/ext/phone_menu/touch_math.cpp   # ext_phone_touch_test.cpp (PHONEMENU, P9)
  ${CMAKE_SOURCE_DIR}/src/ext/phone_menu/size_math.cpp    # ext_phone_size_test.cpp (PHONEMENU, P11)
  ${CMAKE_SOURCE_DIR}/src/ext/phone_menu/hold_math.cpp    # ext_phone_hold_test.cpp (PHONEMENU, P11c)
  ${CMAKE_SOURCE_DIR}/src/ext/dialog_position_drift/drift_math.cpp   # ext_dialog_position_drift_test.cpp (DIALOGPOSITIONDRIFT)
  ${CMAKE_SOURCE_DIR}/src/ext/touch_mode/touch_math.cpp   # ext_touch_mode_test.cpp (TOUCHMODE)
  ${CMAKE_SOURCE_DIR}/src/ext/touch_pointer/touch_pointer_math.cpp   # ext_touch_pointer_test.cpp (P11)
  ${CMAKE_SOURCE_DIR}/src/ext/resources/font_tables.cpp         # ext_font_raster_test.cpp (block RSCFONT)
  ${CMAKE_SOURCE_DIR}/src/platform/win/font_raster_win.cpp      #   the GDI rasteriser (the posix twin is FreeType over the next file)
  ${CMAKE_SOURCE_DIR}/src/platform/common/font_raster_freetype.cpp   #   the FreeType rasteriser (block FREETYPE; GDI comparison on Windows)
  ${CMAKE_SOURCE_DIR}/src/ext/ambient_occlusion/ao_math.cpp   # ext_ambient_occlusion_test.cpp (block AOCORE)
  ${CMAKE_SOURCE_DIR}/src/ext/seamless_loops/seamless_loops.cpp   # ext_seamless_loops_test.cpp (block SEAMLESS)
  ${CMAKE_SOURCE_DIR}/src/ext/high_limits/high_limits.cpp             # ext_high_limits_test.cpp (HIGHLIMITS)
  ${CMAKE_SOURCE_DIR}/src/ext/restricted_foods/restricted_foods.cpp            # ext_restricted_foods_test.cpp (RESTRICTEDFOODS)
  ${CMAKE_SOURCE_DIR}/src/ext/restricted_foods/save_state.cpp             #   its class C codec
  ${CMAKE_SOURCE_DIR}/src/ext/toolbar/toolbar.cpp                    # ext_toolbar_test.cpp (the page list; the page is stubbed there)
  ${CMAKE_SOURCE_DIR}/src/ext/item_priority/item_priority.cpp         # ext_item_priority_test.cpp (ITEMPRIO)
  ${CMAKE_SOURCE_DIR}/src/ext/item_priority/save_state.cpp            #   its class C codec
  ${CMAKE_SOURCE_DIR}/src/ext/tool_names/tool_names.cpp                # ext_tool_names_test.cpp (TOOLNAMES): the note table and its class C codec
  ${CMAKE_SOURCE_DIR}/src/ext/state_codecs.cpp                       # savetail.cpp's FindCodec (the class C codec table)
  ${CMAKE_SOURCE_DIR}/src/ext/host_codecs.cpp                        # savetail.cpp's FindHostCodec (the host codec table, EXTHOSTS)
  ${CMAKE_SOURCE_DIR}/src/ext/toolbar/window_state.cpp               #   the toolbar window's host record
  ${CMAKE_SOURCE_DIR}/src/ext/spread_warmth/spread_warmth.cpp     # ext_spread_warmth_test.cpp (HITCH_PFBURST)
  ${CMAKE_SOURCE_DIR}/src/ext/drain_split/drain_split.cpp         # ext_drain_split_test.cpp (HITCH_DRAINSPLIT)
  ${CMAKE_SOURCE_DIR}/src/render/common/better_trees/dxbc.cpp            # ext_better_trees_test.cpp (block FAA-CORE)
  ${CMAKE_SOURCE_DIR}/src/render/common/better_trees/cutout_rewrite.cpp
  ${CMAKE_SOURCE_DIR}/src/render/common/better_trees/receiver_rewrite.cpp   # ext_better_trees_test.cpp (block TS-CORE)
  ${CMAKE_SOURCE_DIR}/src/render/common/better_trees/edge_dither.cpp        # ext_better_trees_test.cpp (BETTER-TREES)
  ${CMAKE_SOURCE_DIR}/src/ext/fast_paths/fast_search.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Action/action_pf_region_search.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Game/worldgen_f390_erosion.cpp   # worldgen_erosion_test.cpp (block EROSION)
  ${CMAKE_SOURCE_DIR}/src/ext/path_workers/pool.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/cpu/roster.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/draw_gather/gather_side.cpp
  ${CMAKE_SOURCE_DIR}/src/ext/draw_batch/batch_fill.cpp   # ext_draw_batch_test.cpp (DRAWBATCH)
  ${CMAKE_SOURCE_DIR}/src/render/common/draw_batch/smoke_vs_rewrite.cpp   # ext_draw_batch_test.cpp (DRAWBATCH, Idea B)
  ${CMAKE_SOURCE_DIR}/src/ext/gather_threads/gather_threads.cpp
  ${CMAKE_SOURCE_DIR}/src/diag/diag_threads.cpp   # gather_threads.cpp registers its helpers (block LOADSTAMP)
  ${CMAKE_SOURCE_DIR}/src/platform/win/settings_win.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/system/widestring.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/file_win.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/dir_chain.cpp   # EnsureDirChain: settings twins, ext_derived_cache_test.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/win/file_map_win.cpp   # file_map_read_test.cpp (MAPREAD)
  ${CMAKE_SOURCE_DIR}/src/platform/win/file_rw_win.cpp    # ext_derived_cache_test.cpp (COMPRESSAO)
  ${CMAKE_SOURCE_DIR}/src/ext/derived_cache/derived_cache.cpp   # ext_derived_cache_test.cpp (COMPRESSAO)
  ${CMAKE_SOURCE_DIR}/src/ext/derived_cache/hash64.cpp          #   its content hash
  ${CMAKE_SOURCE_DIR}/src/render/common/bc4/bc4.cpp             # bc4_test.cpp (COMPRESSAO)
  ${CMAKE_SOURCE_DIR}/src/render/common/share_gpu_data/share_table.cpp   # ext_share_gpu_data_test.cpp (SHAREGPUDATA)
  ${CMAKE_SOURCE_DIR}/src/render/common/create_on_demand/payload.cpp      # ext_create_on_demand_test.cpp (CREATEONDEMAND)
  ${CMAKE_SOURCE_DIR}/src/render/common/create_on_demand/lazy_table.cpp   #   the deferred-create table
  ${CMAKE_SOURCE_DIR}/src/render/common/create_on_demand/use_tracker.cpp  #   its use stamps (eviction)
  ${CMAKE_SOURCE_DIR}/src/ext/decode_ahead/block_cache.cpp   # ext_decode_ahead_test.cpp (DECODEAHEAD)
  ${CMAKE_SOURCE_DIR}/src/engine/system/compression.cpp       # ext_decode_ahead_test.cpp: the real Decompress
  ${CMAKE_SOURCE_DIR}/src/ext/stream_cache/stream_record.cpp   # ext_stream_cache_test.cpp (STREAMCACHE)
  ${CMAKE_SOURCE_DIR}/src/ext/stream_cache/segment_stream.cpp  #   the record stream and its fallback
  ${CMAKE_SOURCE_DIR}/src/ext/heap_slack/heap_slack.cpp   # ext_heap_slack_test.cpp (HEAPSLACK)
  ${CMAKE_SOURCE_DIR}/src/platform/common/window_messages.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/display_geometry.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/cursor_planes.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/open_url.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/audio/xwma_decode.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/audio/load_decode_batch.cpp   # block SNDPAR: the load's decode batch
  ${CMAKE_SOURCE_DIR}/src/engine/system/parallel_for.cpp   # block WGPAR: the fork-join pool
  ${CMAKE_SOURCE_DIR}/src/platform/common/cpu/topology.cpp             # parallel_for.cpp sizes its pool from Cpu::Get()
  ${CMAKE_SOURCE_DIR}/src/platform/win/cpu_win.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/utility/perlin.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Game/worldgen_cells.cpp     # the e5d0 / cf440 cell loops
  ${CMAKE_SOURCE_DIR}/src/engine/audio/audio_pcm.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/common/audio_feed.cpp
  ${CMAKE_SOURCE_DIR}/src/game/Component/statemachine_tick.cpp   # sm_memo_test.cpp: the real StateMachine::Tick
  ${CMAKE_SOURCE_DIR}/src/game/Game/mapquery.cpp                  # still_terrain_test.cpp: the real Map::GetTerrainPos
  ${CMAKE_SOURCE_DIR}/src/game/Game/worldgeometry_ray.cpp          # its height: the terrain ray cast (terrain_ray_test.cpp)
  ${CMAKE_SOURCE_DIR}/src/game/Game/maparea.cpp                   # clear_area_road_test.cpp: Map::IsGroundClear / IsAreaEmpty
  ${CMAKE_SOURCE_DIR}/src/engine/collection/set_core.cpp          # set_ported_test.cpp: the four ported AVL cores
  ${CMAKE_SOURCE_DIR}/src/engine/collection/set_avl.cpp           #   element 0x10 (0x18000adc0 / aba0)
  ${CMAKE_SOURCE_DIR}/src/engine/collection/set_avl_e24.cpp       #   element 0x18 (0x180023170)
  ${CMAKE_SOURCE_DIR}/src/engine/collection/avl_trees.cpp         #   element 0x30 (0x180030540)
  ${CMAKE_SOURCE_DIR}/src/engine/crt/msvc_stricmp.cpp             #   avl_trees.cpp's group-B comparator (rtcrt::StrICmp)
  ${CMAKE_SOURCE_DIR}/src/engine/io/modification_avl8.cpp         #   element 8    (0x1800274c0)
  ${CMAKE_SOURCE_DIR}/src/engine/system/value_types.cpp           # random_test.cpp: the real System::Random (MT19937); the erosion tests draw from it too
  ${CMAKE_SOURCE_DIR}/src/engine/system/timer.cpp                 #   value_types.cpp's System::Time bodies call Timer::SecondsToTicks
  ${CMAKE_SOURCE_DIR}/src/engine/io/filename_extension.cpp        # filename_test.cpp: the real Filename::GetPathExtension body
  # ext_sheet_builder_test.cpp (block RSCSPRITE, KITFREE.md): the packer and the SpriteSheet fill.
  # (The ported writer family cannot join the host build: ui_resources_abi_test.cpp and
  # b39_io_classes_test.cpp define SpriteSheet's slots and the encodings' / BinaryWriter's bodies
  # as slot probes; the test mirrors the record format and the exe logs the real writer's CRC.)
  ${CMAKE_SOURCE_DIR}/src/ext/resources/sheet_builder.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/ui/sprite.cpp                     #   AddSpriteInfo / SetSpriteInfo
)
