| Type | Name | Desc |
|---|---|---|
| Fix | Status icons appear in place | New status icons no longer slide in from the map origin. |
| Fix | House "go to" button | Centers on the resident instead of rare case when it would not. |
| Fix | Tavern statistics | Counts the good actually consumed (edge case). |
| Fix | Grounded starting buildings | Lower starter barn to actually sit h terrain |
| Fix | Steady dialogs on UI rescale | Open dialogs no longer flash to a corner, instead stay centered. |
| Fix | Sliders follow the mouse | A slider or scroll bar being dragged keeps following the mouse and stops at its ends, instead of jumping back when the pointer strays off it. |
| Fix | Steady panels at the edge | A building or citizen panel opened near the bottom of the screen no longer jumps for a moment when it first appears (or grows against the edge). |
| Fix | Graphics reset during start-up | A graphics driver reset while the game is starting up recovers instead of crashing or hanging. Off under the faithful profile, where retail's crash or hang remains. |
| Ext A | Eye-level camera | Follow a citizen at eye level (replaces overhead follow). |
| Ext B | Better camera | Zoom in much closer and out much farther (6 to 200 instead of 20 to 130) and keep the camera tilt when zoomed out instead of flattening to top-down. Off by default; locked for the game and recorded in saves. |
| Ext B | Higher limit values | Lets the limit boxes in the Town Hall, the Limits window and the production buildings take values up to 999,999,999 (retail stops at 999,999), typed or with the arrows. Off by default; locked for the game and recorded in saves. |
| Ext A | Sharp UI | Scale text with UI so it remains sharp. Note this requires the release TTF files be installed |
| Ext A | Skip unseen work | Skip offscreen animations, particles to save cycles. |
| Ext A | Faster scene gathering | Skips animations/pathing of gathering offscreen objects. |
| Ext A | Batched small draws | Draws runs of decals, roads and smoke as one draw each. Same picture. |
| Ext A | Batched walking | Batches walking operations above 1x speed. |
| Ext A | Render thread | Renders on a 2nd thread. |
| Ext A | Parallel culling | Take the ofscreeen work culling algo and spread across multiple threads. |
| Ext A | Quick loading screens | Loading and save screens close/open faster. |
| Ext A | Ambient occlusion | Screen-space AO. |
| Ext A | Better trees | Smoothes leaf edges, adds ability to adjust tree billboard shadows.|
| Ext A | Max shadow resolution | Adjustable shadow max resolution. |
| Ext A | Seamless ambient loops | When loops (ambient) end cleanly cycle into the next one. |
| Ext A | Background save compression | Compresses the save on a worker thread; identical save files. |
| Ext A | Batched terrain drawing | Draws each row of visible terrain chunks in one call and skips shadow work for terrain whose shadow cannot reach the screen. Same picture. |
| Ext A | Parallel resource decoding | While a resource loads, the next compressed parts of its file are decoded on other cores. Same game data, faster loading of large mods. Takes effect at the next start. |
| Ext A | Parallel graphics loading | While content loads, its GPU buffers and textures are created on other cores. Same resources, all made before the load ends; faster loading of large mods. Takes effect at the next load. |
| Ext A | Compressed AO maps | Keeps the models' baked shading maps compressed (BC4): half their memory (-1.1 GB with MegaMod), the same look. The first start after a mod change prepares them; later starts reuse them from a cache beside settings.cfg (Options > Enhanced: size + Clear cache). Takes effect at the next start. |
| Ext A | Shared graphics data | Meshes and textures that several models or mods load byte for byte identically are kept once on the GPU and shared: -3.1 GB of memory and -2.6 GB of GPU memory with MegaMod, the same look. Takes effect at the next start. |
| Ext A | Load graphics on demand | A model's buffers and textures are created when the game first draws it (or when the world places it) instead of at start-up, read again from the mod package: with MegaMod 9.6 -> 2.3 GB of memory and 7.4 -> 0.65 GB of GPU memory, the menu 2.6 s sooner, the same look; optionally (ext.create_on_demand_idle_min, minutes, default 0 = never) what has not been drawn that long gives its memory back until it is shown again. Takes effect at the next start. |
| Ext A | Cache decoded resources | Keeps the small part of every resource file the game reads at start-up (all of a description, a model's or texture's header) decoded in the cache beside settings.cfg, so the next start skips opening and decompressing ~362k resource streams: with MegaMod the big load 8.7 -> 7.8 s, the menu ~0.5-1 s sooner, 150 MB of cache; the first start after a change takes ~3 s longer while it records. Takes effect at the next start. |
| Ext A | Load graphics on demand | A model's buffers and textures are created when the game first draws it (or when the world places it) instead of at start-up, read again from the mod package: with MegaMod 9.6 -> 2.3 GB of memory and 7.4 -> 0.65 GB of GPU memory, the menu 2.6 s sooner, the same look. Takes effect at the next start. |
| Ext A | Remembered resource links | Each loading step after the first walks every resource the loaded ones reference, to mark them in use; with the row on, the rows each resource's walk reaches are remembered the first time and reached again from that record. Same rows marked, same save. With MegaMod the four later loading steps of a boot and new game drop from ~0.2 s each to ~0.04 s (the first records, ~0.05-0.1 s dearer). Takes effect at the next start. |
| Ext A | Faster resource lookups | A resource reference read by a load (or a save's reference id) that already named a loaded resource goes straight to it, skipping the path expansion and two tree descents. Same resources, same save. A vanilla save load's entity stage ~0.2 -> ~0.08 s. Takes effect at the next start. |
| Ext A | Fewer memory round trips | The allocator keeps the pages it would hand back to the system at the top of a heap range (up to 64 MB in all) and zeroes them itself where the game would have received fresh pages: every allocation sees the same bytes. A MegaMod first start after a mod change 15.75 -> 14.35 s to the menu; warm starts and the stress town unchanged. Takes effect at the next start. |
| Ext A | Remembered mod file lists | Keeps the combined file list of the mounted mods in the cache folder, so starting the game or reloading mods skips rebuilding it (MegaMod: 0.15 s per mount). Same game data. |
| Ext A | Quick return to the menu | Skips rescanning and remounting the mod files when returning to the main menu if none of them changed (MegaMod: about 1.3 s per return to the menu, 1.2 s at boot). Same game data. |
| Ext A | Mod loading screen | While mods load at start-up, shows the loading screen and which mod is loading instead of a black window. |
| Ext A | Background saving | Writes the save on a worker thread while the town holds still for a moment; the screen and camera keep moving. Identical save files. |
| Ext A | No fog | Removes the fog that hazes the far map when the camera is zoomed far out. Off by default; live. |
| Ext A | Phone screen fit | On a screen wider than 16:9 the main menu's logo sits whole on the left and the buttons on the right; the menu stays inside a 3.4 % safe area (rounded corners, camera cut-outs); the UI scale slider goes down to 25 %. Off by default (on in the Android first run); live. |
| Ext A | Touch controls | A camera pad (pan in any direction), rotate and zoom buttons at the bottom left of the game view, and the in-game toolbar at its own scale (up to 2x, shrunk to fit beside the pad) along the bottom; panels and dialogs keep the UI scale. The camera zooms in closer than the game allows, to about two houses across the screen (distance 8 instead of 20, `ext.touch_mode_zoom_min`, settings.cfg only). Off by default (on in the Android first run); live. `docs/android/TOUCH_MODE.md`. |
| Ext A | Touch pointer | Fingers drive the game's own tools: one finger aims a building, road or area above the finger (an action bar on the right places, rotates, flips and cancels; roads and areas use the game's tap-start / tap-end mode), two fingers pan, pinch-zoom and spin the view, and a tap with no tool selects the nearest person or building (tap again to cycle). Off by default; the phone turns it on (docs/android/P11_TOUCH_POINTER.md). |
| Ext B | Faster pathfinding | Looks for same length routes to save pathfinding work. |
| Ext B | Parallel pathfinding | Spread pathfinding to multiple cores. |
| Ext B | Batched animation | Batch animations together above 1x speed. |
| Ext B | Batched walking, faster | Precache walking to save animation cycles. |
| Ext B | Spread warmth searches | At high speed, cold citizens pick a warm house a few frames later instead of all in one frame. |
| Ext B | Split the job reassignment | At high speed, the five-minute job assignment runs one frame after the housing pass. |
| Ext C | Restricted foods | Stop citizens eating foods you choose, from the Foods page of the extension window (the toolbar's cog button, in a game). They can still be stored, sold and cooked. Off by default; locked for the game, and the choice is recorded in the town's save. |
| Ext C | Item priority | Rank the tools, fuels, clothing and health items your citizens take first, from the Priority page of the extension window (the toolbar's cog button, in a game); a citizen takes the best-ranked item the storages hold, even from a farther storage. Off by default; locked for the game, and the ranking is recorded in the town's save. |
| Ext B | Terrain editor | A brush, started from the Terrain page of the extension window (the toolbar's cog button, in a game), that raises the ground under the mouse; ground raised high enough becomes an unwalkable mountain. Proof of concept. Off by default; locked for the game. |
