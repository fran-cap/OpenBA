// ============================================================================
// src/ext/save_failure_notice/save_failure_notice.h -- fix.save_failure_notice (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §29.
//
// Retail's save (Save::SaveLoad::UpdateSave, Runtime 0x1800c2a80) queues the write on the
// persistence worker and, in state 2, resets once the spinner time has passed. Nothing reads the
// worker's status (+0x38, set to 2 by the save job 0x1800b6e00 when the file cannot be created or a
// write comes up short), so a save into a read-only folder, one blocked by Windows Controlled Folder
// Access or an antivirus, or one cut short by a full disk says nothing. The job also writes the
// save file in place (CreateFileW CREATE_ALWAYS), so a write that fails part way has already
// truncated the previous save of that name.
//
// With the row on, two engine seams follow its bool:
//   * IO::PersistenceWorker::_extFixSaveTemp: the save job writes `<name>.tmp` and moves it over
//     the save (Platform::FileReplace) only once every byte is written; on failure it deletes the
//     temporary file, and the old save stays.
//   * Save::SaveLoad::_extFixSaveResult / _extSaveResult: state 2 waits for the job's status and
//     hands it to OnSaveResult below, which opens a message box (Dialog/Extensions.rsc:saveFailed)
//     naming the save folder when the save did not reach the disk.
// Every save takes that path: a manual save, the autosave, and the blocks ext.background_save and
// ext.async_save_compress build (both end in the same UpdateSaveTail enqueue and save job).
// ============================================================================
#pragma once

namespace Ext {
namespace SaveFailureNotice {

// Save::SaveLoad::_extSaveResult: `saved` is false when the save job failed or was never queued.
// Opens the notice per ShouldNotify (save_failure_notice_rule.h).
void OnSaveResult(bool saved);

}  // namespace SaveFailureNotice
}  // namespace Ext
