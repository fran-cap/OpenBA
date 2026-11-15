// ============================================================================
// src/ext/render_pipe/ui_layer.h -- SMOOTHREPLAY: the interface of the last real
// frame, drawn again over each replayed frame
// ============================================================================
// NOT RETAIL. ext.render_pipe, value key render_pipe_replay
// (docs/extensions/handins/SMOOTHREPLAY.md section 3).
//
// A replayed frame (render_pipe.cpp, EngineOps::Replays) is drawn while the main
// thread's steps run, and the interface is drawn from state those steps write. So the
// emit RECORDS the sprite batches of each real frame's save-load and interface draws
// (the engine's UI::SpriteContext::_extRecord seam: the finished vertices and the
// context state of every chunk) and a replayed frame draws that record again: the
// same pixels as the last real frame, with nothing read from the interface.
//
// What the record does not hold: the tool's draw (3D meshes from live tool state) and
// the save-load screen's own draw. A frame in which either drew makes the record
// unusable (Ready answers false) and the next claimed steps get no replayed frames.
//
// The record names each batch's material instance; a step that frees one drops the
// record first (ui_layer.cpp, THE LOCK), and the replayed frames stop for that step.
//
// Every function here runs on the emitting thread, except Install / Remove / Drop
// (the main thread, the render thread idle).
// ============================================================================
#pragma once

#include <cstdint>

namespace Video { class Context; }

namespace Ext {
namespace RenderPipe {

// Sync / Uninstall, the render thread idle: Graphics::MaterialInstance::_extBeforeDestroy set / cleared.
void UiLayerInstall();
void UiLayerRemove();
// Before the frame's save-load draw: the record is emptied and the seam set.
void UiLayerBegin();
// Between the save-load draw and the interface's: what was recorded so far is the save-load
// screen's. True when it drew something (the frame is then not replayable).
bool UiLayerTakeSaveLoad();
// After the interface draw: the seam cleared. `usable` false (the tool or the save-load
// screen drew) leaves the record unusable. `owner` is what the frame belongs to (the game
// loop): Ready answers only for the same one.
void UiLayerEnd(Video::Context& ctx, const void* owner, bool usable);
// The record is dropped (a frame that did not finish, the extension uninstalled).
void UiLayerDrop();
// Whether a replayed frame of `owner` may draw the record.
bool UiLayerReady(const void* owner);
// Draws the record: each chunk under the state it was recorded with. False, and nothing drawn:
// the record was dropped since (a step freed a material it names).
bool UiLayerDraw(Video::Context& ctx);

}  // namespace RenderPipe
}  // namespace Ext
