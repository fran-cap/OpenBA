// src/diag/saveload_harness.cpp
// Save/load end-to-end harness. Env BANISHED_SAVE_TEST (unset/0 = off; a value
// > 1 overrides the trigger frame, default 900 ≈ 15s in-game) drives the REAL
// retail autosave path at the trigger frame: AutoSaveDialog::Autosave — the ported
// FUN_14005dec0 — which does SetMinOperationTime(1.0s) + SetSaveSprite +
// SetSaveName("Autosave",2) + SetOperation(4,-4). The engine's
// Save::SaveLoad::UpdateSave then calls the current loop's vtable +0x38
// (SaveRestore) to fill the SavedState and async-writes <SaveDir>/Auto.sav.
//
// After triggering, polls SaveLoad::GetOperation each frame and logs
// transitions; when the operation returns to 0 it stats the save directory so
// the log shows exactly what got written (file, size, age). No pass/fail
// judgement here — the .sav content is verified outside the process.

#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>          // DumpSaveDir's listing (Phase D.4 LINUX-1)
#include <sys/stat.h>
#include <ctime>
#include <string>
#endif
#include "platform_settings.h"   // Platform::DocumentsDir (POSIX listing), Platform::UserDirOverride
#include "save/SaveLoad.h"   // -Iruntime/types: ENDGAME B.3.7 callers (K5)
#include "Game/GameUpdateLoop.h"   // B4: m_userInterface by name

#include "Dialog/AutoSaveDialog.h"   // AutoSaveDialog::Autosave (the ported FUN_14005dec0)
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)
#include "io/Stream.h"   // IO::CompressedMemoryStream (the identity line's decompress)
#include "load_time.h"   // LoadTime::HashBytes
#include <new>
#include <vector>

namespace {

int TriggerFrame() {
    static int s = -2;
    if (s == -2) {
        const char* e = Opt().saveTest;
        if (!e || !e[0] || (e[0] == '0' && !e[1]) ||
            rtcrt::StrICmp(e, "false") == 0 ||
            rtcrt::StrICmp(e, "roundtrip") == 0) {   // the in-process gate, not a frame trigger
            s = -1;                       // off
        } else {
            int v = HarnessAtoi(e);
            s = (v > 1) ? v : 900;        // "1"/non-numeric = default frame
        }
    }
    return s;
}

// GetSaveDirectory returns a System::String; avoid the ABI dance and just stat
// the known per-user save dir the engine uses (Persistence::GetSaveDirectory →
// Documents/Banished).
#if defined(_WIN32)
void DumpSaveDir(const char* tag) {
    char path[MAX_PATH];
    // BANISHED_USER_DIR (tools/game_dirs.sh) is the whole "<Documents>\Banished" folder.
    if (const char* user = Platform::UserDirOverride()) {
        snprintf(path, sizeof(path), "%s\\Save\\*", user);
    } else {
        const char* prof = Opt().userProfile;
        if (!prof) return;
        snprintf(path, sizeof(path), "%s\\Documents\\Banished\\Save\\*", prof);
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    fprintf(stderr, "[SaveTest] %s — save dir listing:\n", tag);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[SaveTest]   (dir empty or missing)\n");
        fflush(stderr);
        return;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER sz;
        sz.LowPart  = fd.nFileSizeLow;
        sz.HighPart = fd.nFileSizeHigh;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER wt, nt;
        wt.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        wt.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        nt.LowPart = now.dwLowDateTime;
        nt.HighPart = now.dwHighDateTime;
        long long ageSec = (long long)((nt.QuadPart - wt.QuadPart) / 10000000ULL);
        fprintf(stderr, "[SaveTest]   %-24s %9llu bytes  written %llds ago\n",
                fd.cFileName, (unsigned long long)sz.QuadPart, ageSec);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    fflush(stderr);
}
#else
// POSIX (Phase D.4 LINUX-1): the same listing of <Documents>/Banished/Save, where Documents
// is Platform::DocumentsDir (settings_posix.cpp: ~/.local/share), over opendir / stat.
void DumpSaveDir(const char* tag) {
    std::string dir;
    if (!Platform::DocumentsDir(dir)) return;
    dir += "/Banished/Save";
    DIR* d = opendir(dir.c_str());
    fprintf(stderr, "[SaveTest] %s — save dir listing:\n", tag);
    if (d == nullptr) {
        fprintf(stderr, "[SaveTest]   (dir empty or missing)\n");
        fflush(stderr);
        return;
    }
    const time_t now = time(nullptr);
    while (dirent* e = readdir(d)) {
        const std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
        fprintf(stderr, "[SaveTest]   %-24s %9llu bytes  written %llds ago\n",
                e->d_name, (unsigned long long)st.st_size, (long long)(now - st.st_mtime));
    }
    closedir(d);
    fflush(stderr);
}
#endif

// The save's identity (HITCH_BGSAVE): the stored game-state block (SaveLoad::_saveState, block
// 1 of the file) as soon as state 1 has filled it, hashed compressed and decompressed. Two runs'
// saves of the same world compare on this line; the file itself cannot, because block 0 carries
// the thumbnail and the time stamp. Not in a stress run: the decompress is a hitch of its own
// there, and the stress run's own check is COMPPROF 4's in-process verify.
void LogSavedStateIdentity(int frameNum) {
    const Save::SaveLoad* sl = ::Save::SaveLoad::_singleton;
    Save::SavedState* st = sl->_saveState;
    const uint64_t compressedHash = LoadTime::HashBytes(static_cast<const unsigned char*>(st->_data), st->_size);
    alignas(16) uint8_t storage[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cms = ::new (static_cast<void*>(storage)) IO::CompressedMemoryStream();
    st->Retrieve(cms->GetMemoryStream());
    uint32_t rawSize = 0;
    uint64_t rawHash = 0;
    bool ok = cms->Open();
    if (ok) {
        rawSize = cms->IO::CompressedStream::GetSize();
        std::vector<uint8_t> raw(rawSize ? rawSize : 1);
        ok = rawSize == 0 || cms->IO::CompressedStream::Read(static_cast<int>(rawSize), raw.data());
        rawHash = LoadTime::HashBytes(raw.data(), rawSize);
        cms->IO::CompressedMemoryStream::Close();
    }
    cms->IO::CompressedMemoryStream::~CompressedMemoryStream();
    fprintf(stderr, "[SaveTest] frame %d: saved state compressed=%u hash=%016llx raw=%u rawHash=%016llx%s\n",
            frameNum, st->_size, static_cast<unsigned long long>(compressedHash), rawSize,
            static_cast<unsigned long long>(rawHash), ok ? "" : " (DECOMPRESS FAILED)");
    fflush(stderr);
}

} // namespace

// gameupdateloop.cpp hands `this`; its local declaration still spells the parameter
// void* (C linkage, the same symbol).
extern "C" void SaveLoadHarness_Tick(GameUpdateLoop* gul, int frameNum) {
    const int trigger = TriggerFrame();
    if (trigger < 0) return;

    static int  s_state = 0;      // 0=armed, 1=waiting for op!=0, 2=op running, 3=done
    static int  s_lastOp = -999;
    static int  s_triggeredFrame = 0;

    // Save::SaveLoad::_singleton, the class static (written by the SaveLoad ctor
    // in GameUpdateLoop::Load step 25).
    static bool s_armed = false;
    if (!s_armed) {
        s_armed = true;
        fprintf(stderr, "[SaveTest] armed (trigger frame %d)\n", trigger);
        fflush(stderr);
    }
    const Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    if (!saveLoad) return;

    if (s_state == 0) {
        if (frameNum < trigger) return;
        DumpSaveDir("pre-save");

        // The live AutoSaveDialog (GameUpdateLoop::Startup adds it): its
        // Autosave reads only its own _ui (+0x10), the game's UserInterface.
        // (B.4 Y GAMEB: was a stack fake carrying only that pointer.)
        AutoSaveDialog* autoSave = nullptr;
        const uint32_t autoSaveTid = AutoSaveDialog::TypeId();
        for (auto it = gul->m_userInterface->_dialogs.GetFirst(); it; ++it) {
            UI::DialogInstance* dialog = it.node->element;
            if (dialog && ::Object::IsDerivedFromNullSafe(dialog, autoSaveTid)) {
                autoSave = static_cast<AutoSaveDialog*>(dialog);   // the dialog IS the AutoSaveDialog (a DialogInstance)
                break;
            }
        }
        if (!autoSave) {
            fprintf(stderr, "[SaveTest] frame %d: no AutoSaveDialog open; not triggered\n", frameNum);
            fflush(stderr);
            s_state = 1;
            return;
        }

        fprintf(stderr, "[SaveTest] frame %d: invoking retail autosave handler "
                        "(SetOperation(4,-4))\n", frameNum);
        fflush(stderr);
        autoSave->Autosave();

        int op = saveLoad->GetOperation();
        fprintf(stderr, "[SaveTest] handler returned; GetOperation()=%d "
                        "(4=save expected; 0=trigger was refused)\n", op);
        fflush(stderr);
        s_triggeredFrame = frameNum;
        s_lastOp = op;
        s_state = (op != 0) ? 2 : 1;
        return;
    }

    int op = saveLoad->GetOperation();
    if (op != s_lastOp) {
        fprintf(stderr, "[SaveTest] frame %d: operation %d -> %d\n",
                frameNum, s_lastOp, op);
        fflush(stderr);
        s_lastOp = op;
    }

    if (s_state == 1) {
        // Trigger was refused (paused / blocked / op busy). Retry every 120 frames.
        if ((frameNum - s_triggeredFrame) % 120 == 0) {
            s_state = 0;
        }
        return;
    }

    if (s_state == 2) {
        static int s_identityFor = -1;   // the trigger frame whose save was hashed
        if (op == Save::SaveLoad::kOpSave && saveLoad->_progress == 2 && saveLoad->_saveState != nullptr &&
            s_identityFor != s_triggeredFrame && !Opt().stress) {
            s_identityFor = s_triggeredFrame;
            LogSavedStateIdentity(frameNum);
        }
        if (op == 0) {
            fprintf(stderr, "[SaveTest] frame %d: SAVE OPERATION COMPLETE "
                            "(%d frames elapsed)\n",
                    frameNum, frameNum - s_triggeredFrame);
            DumpSaveDir("post-save");
            // BANISHED_SAVE_TEST_REPEAT=<frames>: keep saving on an interval.
            // A save only crashes once the world has produced the offending
            // state (e.g. a stale Work in the goals tree), so one early save
            // proves nothing — re-arm and keep probing as the town ages.
            static int s_repeat = -2;
            if (s_repeat == -2) {
                s_repeat = Opt().saveTestRepeat;
            }
            if (s_repeat > 0) {
                s_triggeredFrame = frameNum + s_repeat;
                s_state = 4;
            } else {
                s_state = 3;
            }
        } else if ((frameNum - s_triggeredFrame) % 300 == 0) {
            fprintf(stderr, "[SaveTest] frame %d: still saving (op=%d)\n",
                    frameNum, op);
            fflush(stderr);
        }
        return;
    }

    if (s_state == 4) {
        if (frameNum >= s_triggeredFrame) {
            fprintf(stderr, "[SaveTest] frame %d: re-arming (repeat save)\n", frameNum);
            fflush(stderr);
            s_state = 0;
        }
        return;
    }
}
