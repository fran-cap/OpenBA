// ============================================================================
// src/ext/load_notice.h -- which load notice an `ext:` pseudo-mod gets (NOT RETAIL)
// ============================================================================
// Block EXTNOTICE (docs/extensions/handins/EXTNOTICE.md). SaveLoadDialog::DoLoad
// warns about each `ext:<id>` entry of a save's mod list that
// Ext::PseudoModLoadStatus calls not loadable (status 1). Retail's
// Dialog/SaveLoad.rsc:modsMissing says a MOD cannot be found; these are our
// own MessageBox dialogs in Dialog/Extensions.rsc that say EXTENSION, one per
// reason, so the player is told what will happen on Yes:
//
//   NotInBuild    the id is unknown to this build, or names a fix / a class A
//                 extension (neither is ever in a save): on Yes the save's
//                 block-1 manifest fails the load with the fatal error box
//                 (gameupdateloop_serialize.cpp, ExtSave_ReadTail).
//   NewerVersion  this build has the extension, but the save was written by a
//                 newer format version of it: on Yes the load fails the same way.
//   TurnedOff     this build has it and the save's version is readable, but the
//                 player has not requested it: on Yes the manifest locks it ON
//                 for this game (the save keeps the extensions it was made with).
//
// None is exactly PseudoModLoadStatus == 0 (the unit test pins that); the
// checks below are the same as that function's, in the same order. An entry a
// retired row left (registry.h RETIRED ROWS) is None: the row's behaviour is
// gone or unconditional, so there is nothing to warn about and its manifest
// record is dropped on load.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {

enum class PseudoModNotice : uint8_t { None, NotInBuild, NewerVersion, TurnedOff };

PseudoModNotice PseudoModNoticeFor(const char* path, int32_t savedFormatVersion);

// The notice's dialog ("Dialog/Extensions.rsc:<name>"); null for None.
const char* PseudoModNoticeDialog(PseudoModNotice notice);

}  // namespace Ext
