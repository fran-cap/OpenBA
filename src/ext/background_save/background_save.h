// ============================================================================
// src/ext/background_save/background_save.h -- ext.background_save (class A, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/HITCH_SAVESNAP.md.
//
// A save's game state is serialised on a worker thread while the sim is frozen. The main
// thread keeps presenting frames and keeps the camera moving; the sim resumes the frame the
// worker finishes. The saved bytes are retail's: the same SerializeAll sequence over the same
// world state, which nothing changes while the worker runs.
//
//   main thread, in Save::SaveLoad::UpdateSave state 1 (the engine seam _extSaveStart):
//     CompressedMemoryStream::Create, ext.async_save_compress's attach when that is on,
//     GameUpdateLoop::SerializeAllHead (to PlayerController::Serialize), and
//     EntityList::FlushForSerialize (the flush and the removals EntityList::Serialize opens with);
//   worker: SerializeAllBody, the B / C manifest tail, Close, SavedState::Store;
//   main thread, every Update until the worker is done (FrozenUpdate): the camera input on a
//     shadow copy of the tool manager's three camera floats (the body serialises the real ones),
//     the camera transform, and SaveLoad::Update, whose poll (_extSavePoll) joins the worker
//     and runs the rest of UpdateSave's state 1. Draw forces alpha to 1 meanwhile, as it does
//     when the game is paused, so models hold the last tick's pose.
//
// The sim is not stepped while frozen: the fixed-step accumulator is consumed by the frozen
// Updates, so no catch-up burst follows (retail's own stall drops the time the same way once it
// exceeds 1/15 s, MainLoop::Frame's clamp).
//
// What else may run on the main thread while the worker reads the world, and what keeps it off
// the worker's data (docs/extensions/handins/HITCH_BGSAVE.md):
//   * a window close pumped during the frozen frames: Engine::MainLoop runs the loop's Shutdown,
//     which frees the world, so Shutdown first joins the worker, finishes state 1 (the enqueue)
//     and waits for the persistence worker's write (Join);
//   * the device-lost route (Engine::MainLoop::OnVideoError destroys the video Externals; the loop
//     releases the terrain and minimap images; the restore reloads resources): the engine seam
//     Engine::MainLoop::_extVideoErrorBarrier waits for the worker first (the poll still finishes
//     the save at the next Update);
//   * the row's toggle: Sync stops new saves taking the worker but leaves the poll and the barrier
//     installed until the running save is done, so it finishes on the path it started.
//
// VERIFY (COMPPROF level 4, g_verifyOn): the save is first serialised synchronously into a flat
// image on the main thread, and the background save's decompressed image is compared with it
// byte for byte when the worker is done (StressHarness_Verify "BackgroundSave"). Every second
// background save of a verify run also switches the row off on its first frozen Update and back
// on after the join (the toggle exercise).
// ============================================================================
#pragma once

class GameUpdateLoop;
namespace System { class Time; }

namespace Ext {
namespace BackgroundSave {

// Install / remove the Save::SaveLoad seam with the toggle. Ext::InitFromSettings and
// Ext::SetAndRecord call it. Removal waits for nothing: a running save finishes through the
// poll, which stays installed until it has.
void Sync();

// True from the save's start to the poll that joins the worker.
bool Busy();

// GameUpdateLoop::Update's replacement while Busy (gameupdateloop.cpp).
void FrozenUpdate(GameUpdateLoop& loop, const System::Time& time);

// Waits for a running worker, finishes it and (finishSave) the rest of UpdateSave's state 1, and
// waits for the file's write. GameUpdateLoop::Shutdown passes true: never free the world under the
// worker, and never lose a save the player asked for to a quit. The destructor passes false: by
// then Shutdown has freed the SaveLoad (and joined already, so this is normally a no-op).
void Join(bool finishSave);

}  // namespace BackgroundSave
}  // namespace Ext
