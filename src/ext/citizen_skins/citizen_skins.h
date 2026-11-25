// ============================================================================
// src/ext/citizen_skins/citizen_skins.h -- ext.citizen_skins (class A, default OFF): every mod's citizen
// skins at once
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CITIZENSKINS.md; skin_math.h has the engine-free half.
//
// Retail draws a citizen with one of 8 shirt colours, cut from a 1024 x 128 strip
// (Models/Citizen/MaterialInstance/ CitizenTexture, and CitizenTextureF for the women). A mod that retextures
// the citizens ships its own copy of that file and the last one loaded replaces the others: only that mod's 8
// colours are ever drawn. With the row on, every mounted package's copy is read, the strips are put side by
// side in one wider texture (up to 16; vanilla's own only when no mod has one) and each citizen wears the strip that scores highest
// for its name: 8 colours become 16, 24 ... 128. Drawing only: the citizen's saved variant (0..7) is
// untouched, the strip is a function of the name, nothing is saved, the simulation does not know.
//
// HOW. The character vertex program divides the texture coordinate by 8 (an `l(0.125)` immediate in its
// compiled code); it becomes 1 / (8 N) and the mesh colour's fourth component (the citizen's variant, written
// in CitizenComp_SetupAgeMesh) becomes set * 8 + variant. Retail draws nothing but citizens with that
// program.
//
// WHEN. OnGameStart (loader thread, the head of GameUpdateLoop::Load, before any citizen exists) builds and
// installs everything once per process and OnVideoRestored again after a video device restart (unless the
// row has been switched off since). The row is read at those two places: switching it on takes effect at the
// next game start, switching it off at the next device restart or the next run of the game; the Extensions
// row says to restart the game after changing it.
// ============================================================================
#pragma once

namespace Ext {
namespace CitizenSkins {

// The row (ext.citizen_skins), as the Extensions dialog leaves it.
bool Enabled();

// Loader thread, GameUpdateLoop::Load before the entities: the combined textures and the patched program
// installed (once per process); a no-op while the row is off or when fewer than two different skin sets are
// mounted.
void OnGameStart();

// The mesh colour number a citizen wears: its variant (0..7) with its strip's first cell added. The variant
// itself when no combined texture is installed. Any thread.
int SlotFor(const char* citizenName, int variant);

// Engine::MainLoop::_extVideoLost / _extVideoRestored (src/ext/registry_boot.cpp): a device restart reloads
// the citizen texture and the program from their files; the combined ones are made again from what was kept.
void OnVideoLost();
void OnVideoRestored();

}  // namespace CitizenSkins
}  // namespace Ext
