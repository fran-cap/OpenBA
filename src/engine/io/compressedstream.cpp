// ============================================================================
// src/engine/io/compressedstream.cpp — Wave 4 lane B: IO::CompressedMemoryStream
// ============================================================================
//
// The six IO stream-family exports on IO::CompressedMemoryStream; one of their
// callers is src/game/Game/gameupdateloop_serialize.cpp
// (GameUpdateLoop::SaveRestore,
// exe 0x1400959c0):
//
//   retail      profile     ordinal  export
//   0x1800a9520 0x18005c240  106     ??0CompressedMemoryStream@IO@@QEAA@XZ
//   0x1800a9560 0x18005c280  551     ??1CompressedMemoryStream@IO@@UEAA@XZ
//   0x1800a9600  --          1874    ?Create@CompressedMemoryStream@IO@@QEAA_NXZ
//   0x1800a9500  --          3302    ?Open@CompressedMemoryStream@IO@@QEAA_NXZ
//   0x1800a9620  --          1719    ?Close@CompressedMemoryStream@IO@@UEAAXXZ
//   0x180024350  --          2784    ?GetMemoryStream@CompressedMemoryStream@IO@@QEAAAEAVMemoryStream@2@XZ
//
// All six manglings are present in the shipped DLL's export directory.
// Authoritative check:
// docs/history/runtime_port/wave4_export_names.txt — the wave-wide pefile sweep, 39 IO
// symbols, 39 ok / 0 MISSING; the addresses and ordinals above are its rows.
// The three vtable symbols and the seven call-through targets below are NOT in
// that sweep and were checked separately by this lane; all present, see
// WAVE4_LANE_B.md §2. Nothing here is a phantom name.
//
// ?GetMemoryStream sits far from the other five, in the 0x18002xxxx COMDAT pool
// of inline-defined and compiler-generated one-liners. It is NOT identical-
// COMDAT-folded with anything: grouping the whole export directory by RVA gives
// exactly one name at 0x180024350, so that address is one function, not a
// folded group.
//
// ORIGINAL SOURCE FILE. The recovered tree (docs/history/runtime_port/
// original_source_tree.txt) has no Stream.cpp and no
// CompressedMemoryStream.cpp: this class lives in IO\CompressedStream.cpp and
// IO::Stream / IO::MemoryStream in IO\MemoryStream.cpp. This file was
// scaffolded as "stream.cpp" and RENAMED to compressedstream.cpp by the
// orchestrator on lane B's recommendation, because the recovered tree has a
// SEPARATE IO\Stream.cpp for the IO::Stream base -- keeping the old name would
// have collided with it when that base is ported. The old name was the
// lane's file name, not the engine's.
//
// WHERE THE CODEC IS
// ------------------
// Every one of these six is a thin shim over IO::CompressedStream, and
// CompressedStream is where the actual compression lives (Commit,
// CompressWindow, DecompressFromSource, SetWindow, and through them
// System::Compression::Compress / Decompress), in compressed_stream_open.cpp,
// compressed_stream_read.cpp, compressed_stream_write.cpp and
// compressed_stream_release.cpp. Each shim calls the same CompressedStream /
// MemoryStream member retail calls, with the same arguments:
//
//   ??0CompressedStream@IO@@QEAA@XZ                     0x1800aa3d0
//   ??1CompressedStream@IO@@UEAA@XZ                     0x1800aa650
//   ?Commit@CompressedStream@IO@@QEAAXXZ                0x1800a9ee0
//   ?Create@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z 0x1800aa4b0
//   ?Open@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z   0x1800aa560
//   ??0MemoryStream@IO@@QEAA@XZ                         0x1800ae710
//   ??1MemoryStream@IO@@UEAA@XZ                         0x1800ae890
//
// VTABLE
// ------
// The class's compiler table stands for retail's ??_7CompressedMemoryStream@IO@@6B@
// (0x180155560): slot 0 is the vector deleting dtor 0x1800244a0 over the dtor
// below, and slot 7 is Close (0x1800a9620), also below.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

#include "io/Stream.h"

// ENDGAME B.3.9 IO (W2): the by-name vtable reads (CompressedMemoryStream,
// CompressedStream, Stream) went with the ??0 doors that stamped them.
//
// B.4 R-IO: the embedded MemoryStream's Close() is called on the member itself,
// `_memoryStream.Close()` -- a virtual call on an object whose dynamic type is
// MemoryStream (retail: `MOV RAX,[RDI]; CALL [RAX+0x38]` on the member's own
// vptr, which is MemoryStream's table).

// ============================================================================
// IO::CompressedMemoryStream -- ENDGAME B.3.9 IO (W2)
// ============================================================================
// The six doors that stood here (??0 / ??1 / ?Create / ?Open / ?Close /
// ?GetMemoryStream) are the class's own members now: the constructor and
// GetMemoryStream are inline in Stream.h, Close is below, and every builder
// (Save::SaveLoad::SaveMetaData / LoadMetaData, PackageFile::ReadPackfile,
// the exe's GameUpdateLoop::SaveRestore and its roundtrip check) spells
// `::new (p) IO::CompressedMemoryStream()` and ends the object with a
// qualified `T::~T()` at the position the door call had.

namespace IO {

// ?Open@CompressedMemoryStream@IO@@QEAA_NXZ      retail 0x1800a9500
// Whole body: `return CompressedStream::Open(_memoryStream, 1);` -- 0x1800aa560
// sets _direction = 0 (reading), stores _source, SetWindowCount(1), then parses
// the window header table out of the source; false when the totals disagree.
bool CompressedMemoryStream::Open() {
    return CompressedStream::Open(_memoryStream, 1);
}

// ?Create@CompressedMemoryStream@IO@@QEAA_NXZ    retail 0x1800a9600
// Whole body: `return CompressedStream::Create(_memoryStream, 2);` -- 0x1800aa4b0
// sets _direction = 1 (writing), stores _source, news a System::Compression and
// SetWindowCount(2). Writing uses two windows, reading one: both literals are
// read off the two call sites.
bool CompressedMemoryStream::Create() {
    return CompressedStream::Create(_memoryStream, 2);
}

// ??1CompressedMemoryStream@IO@@UEAA@XZ          retail 0x1800a9560
// The profile build (0x18005c280) gives the source shape:
//     CompressedStream::Close();          // inlined in retail: guard + Commit
//     _memoryStream.Close();              // virtual, vtable +0x38
//     _memoryStream.~MemoryStream();      // the compiler's member dtor
//     ~CompressedStream();                // the compiler's base dtor
// The member's Close is dispatched through ITS OWN vptr (a different object at
// +0x68), exactly as retail's `CALL [RAX+0x38]`. No guard added.
CompressedMemoryStream::~CompressedMemoryStream() {
    if (_headerSize > 0 && _direction == 1)
        Commit();
    _memoryStream.Close();   // slot 7 +0x38, through the member's own vtable
}

}  // namespace IO

// ?Open@MemoryStream@IO@@QEAA_NH@Z — 0x1800ae670.
//
// Retail, in order: MemAlloc(size,4) -> +0x18; size -> +0x28; 0 -> +0x24
// (_cursor); 0 -> +0x08 (_direction, i.e. READING); 1 -> +0x20 (_ownsData);
// return true UNCONDITIONALLY — including when the allocation failed. That last
// part is retail's and it is reproduced: a caller that trusts the `true` and
// writes into a null buffer is retail's behaviour, and a guard here would be
// the one CLAUDE.md warns against rather than a repair.
//
// The lock/Allocate/unlock trio the decompile shows inlined at this call site is
// instruction-for-instruction `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`
// (0x1800c7a50: EnterCriticalSection(_singleton+0x640); Allocate(_singleton, n,
// align); LeaveCriticalSection) — compared, not assumed — so calling MemAlloc
// (RtSystem::RtMemAlloc) is the faithful spelling.
// ENDGAME B.3.9 IO (W2): the member; the ?Open door that stood here is gone
// with its by-name callers (SavedState::Retrieve, LoadMetaData). The allocation
// is the same ?MemAlloc@MemoryAllocator (RtSystem::RtMemAlloc), the size
// sign-extended exactly as `MOVSXD` does.
bool IO::MemoryStream::Open(int size) {
    _data = RtSystem::RtMemAlloc(static_cast<uint64_t>(size), 4);
    _size = static_cast<uint32_t>(size);
    _cursor = 0;
    _direction = 0;
    _ownsData = 1;
    return true;
}

// ?Create@MemoryStream@IO@@QEAA_NH@Z — 0x1800ae7f0.   WAVE 23 lane IOPORT.
//
// THE WRITE-SIDE TWIN OF Open ABOVE, and the ONLY difference is the direction.
// The full retail disassembly (Ghidra MCP, retail Runtime-x64.dll,
// not the profile build) is 26 instructions and the five stores are:
//
//     1800ae852  MOV [RBP+0x18],RBX     _data    = MemAlloc(size, 4)
//     1800ae856  MOV [RBP+0x28],ESI     _size    = size
//     1800ae859  MOV [RBP+0x24],0       _cursor  = 0
//     1800ae860  MOV [RBP+0x8],1        _direction = 1   <-- WRITING (Open: 0)
//     1800ae867  MOV [RBP+0x20],1       _ownsData  = 1
//     1800ae86b  MOV AL,1               return true, UNCONDITIONALLY
//
// STORE ORDER IS RETAIL'S, not tidied: _size before _cursor before _direction
// before _ownsData. Nothing here reads what it wrote, so the order is not
// load-bearing, but reordering would make a future byte-level diff lie.
//
// `MOVSXD RSI,EDX` at 1800ae80e sign-extends the int before it reaches
// Allocate, so a negative size reaches the allocator as a huge unsigned value
// rather than being clamped. `static_cast<uint64_t>(size)` reproduces exactly
// that bit pattern (a negative int converts to uint64_t as 2^64+v), which is
// why it is spelled this way and not with an intermediate size_t.
//
// THE UNCONDITIONAL `true` IS RETAIL'S AND IS REPRODUCED. Allocation failure is
// not reported; a caller that trusts the return and writes into a null buffer
// is behaving exactly as it does against the shipped DLL. Per CLAUDE.md this is
// the deref NOT to guard — a guard here would skip retail's behaviour, not
// restore it.
//
// WHY THIS ROW MATTERS BEYOND ITS SIZE. `?Open@MemoryStream@IO@@QEAA_NH@Z` is
// the WRONG overload for a writer — it sets _direction = 0 — and the failure
// mode when the two are confused is a `REP MOVSB` with RDI = 0 inside the DLL's
// memcpy, two calls away from the mistake. With both
// overloads now ported under their own mangled names, the distinction is
// visible in our source instead of only in the DLL.
//
// ENDGAME B.3.9 IO (W2): the member; the ?Create door that stood here is gone
// with its by-name caller (CompressedMemoryStream::PreCommit).
bool IO::MemoryStream::Create(int size) {
    _data = RtSystem::RtMemAlloc(static_cast<uint64_t>(size), 4);   // 1800ae852
    _size = static_cast<uint32_t>(size);                               // 1800ae856
    _cursor = 0;                                                       // 1800ae859
    _direction = 1;          // 1800ae860 — WRITING. Open stores 0 here.
    _ownsData = 1;                                                     // 1800ae867
    return true;
}

// ??1MemoryStream@IO@@UEAA@XZ — 0x1800ae890.
//
// Retail: vptr = MemoryStream's; if (_ownsData && _data) MemFree(_data);
// vptr = Stream's; if (_encoding) MemFree(_encoding). _data and _encoding are
// left holding freed pointers.
//
// ENDGAME B.3.9 IO: that is `IO::MemoryStream::~MemoryStream` followed by
// `~Stream` (Stream.h).
// ENDGAME B.3.9 IO (W2): the ??1MemoryStream door that stood here is gone --
// Object::Internal::Clone and the dialog-settings load end their stack
// streams with qualified `T::~T()`.

// ============================================================================
// Wave 9 lane W, round 2 — ??0MemoryStream and ??0CompressedStream
//
//   0x1800ae710  ??0MemoryStream@IO@@QEAA@XZ        ord 273
//   0x1800aa3d0  ??0CompressedStream@IO@@QEAA@XZ    ord 108
//
// Both ctors install a BinaryEncoding (Serializer.h), retail's unexported
// shared encoding table 0x1801554a8. Its five slots (0x1800a81e0, 0x1800a81d0,
// 0x1800a81c0, 0x1800a8120, 0x1800a8080) are BinaryEncoding's members, in
// Serializer.h and encoding.cpp.
//
// TRANSCRIBED FROM THE LISTINGS (0x1800ae710..0x1800ae7c7 and
// 0x1800aa3d0..0x1800aa4a5), which are the same function with a different field
// set. Details the decompiled C would have lost:
//
//   * `MOV byte ptr [RCX+0x20],BPL` — `_ownsData` is a BYTE store, not the
//     `undefined8` Ghidra prints around it.
//   * `MOV qword ptr [RCX+0x24],RBP` — ONE 8-byte store spanning `_cursor`
//     (+0x24) and `_size` (+0x28), and deliberately UNALIGNED. Writing the two
//     u32 fields separately is byte-equivalent and is what is done here.
//   * CompressedStream writes `+0x1c` and `+0x20` as DWORDs (`+0x20` = -1) and
//     leaves `+0x18` and `+0x24` untouched.
//   * `MOV RAX,RDI` before the return: MSVC constructors return `this`.
//   * The allocation is `CALL 0x1800c6e60` with RCX=`_singleton`, EDX=8, R8D=4
//     between an `EnterCriticalSection(_singleton+0x640)` / `LeaveCriticalSection`
//     pair. 0x1800c6e60 is `?Allocate@MemoryAllocator@System@@QEAAPEAX_KH@Z`, so
//     that trio is `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50)
//     inlined — compared instruction by instruction, not assumed, exactly as for
//     `MemoryStream::Open` above.
//   * The `if (_encoding) MemFree(_encoding)` before the final store is retail's
//     and is reproduced even though `_encoding` was set to 0 eleven instructions
//     earlier and the branch is therefore dead in every reachable call. It is one
//     `TEST`/`JZ` in the shipped bytes; removing it would be a divergence for no
//     gain, and `FUN_180006a10` — the un-exported second copy of MemFree retail
//     calls there — is the same code as the exported name used here.
//   * An allocation FAILURE leaves `_encoding` NULL and the constructor returns
//     normally. Faithful, and not guarded: retail's callers rely on it never
//     failing, and a guard would hide the upstream problem rather than fix it.
//
// ============================================================================

// ??0MemoryStream@IO@@QEAA@XZ (0x1800ae710). ENDGAME B.3.9 IO: the body is
// `IO::MemoryStream`'s real constructor (Stream.h).
// ENDGAME B.3.9 IO (W2): the ??0MemoryStream door that stood here is gone --
// every builder spells `::new (p) IO::MemoryStream()`.

// ENDGAME B.3.9 IO (W2): the ??0CompressedStream door (0x1800aa3d0) that stood
// here is gone with its last caller; the body is IO::CompressedStream's real
// protected ctor (Stream.h), run by every CompressedMemoryStream /
// CompressedFileStream construction.

// ============================================================================
// `IO::CompressedMemoryStream::Close` — B.3.4 block 4 (STREAMS)
// ============================================================================
namespace IO {

// ?Close@CompressedMemoryStream@IO@@UEAAXXZ                  retail 0x1800a9620
//     if (headerSize > 0 && direction == 1) Commit(this);    // CompressedStream::Close
//     _memoryStream.Close();                                 // virtual, +0x38
// Retail tail-JMPs the virtual call, which is why Ghidra reports "could not
// recover jumptable" at 0x1800a9647; it is an ordinary tail call, not a switch.
//
// This is the function that turns the written windows into the compressed byte
// image sitting in _memoryStream, so SaveRestore must call it before Store.
//
// THE MEMBER'S `Close` IS DISPATCHED VIRTUALLY, DELIBERATELY, and that is not
// the recursion hazard the qualified-call rule is about. `_memoryStream` is a
// DIFFERENT OBJECT at +0x68 carrying its OWN vptr — retail does
// `MOV RAX,[RDI]; CALL [RAX+0x38]` on it — so the dispatch lands on
// MemoryStream's table, not on this class's. Qualifying it would change what
// retail calls, which is the opposite of what the rule asks for.
void CompressedMemoryStream::Close() {

    const bool committed = _headerSize > 0 && _direction == 1;
    if (committed)
        Commit();

    _memoryStream.Close();   // slot 7 +0x38, through the member's own vtable

}

}  // namespace IO

// ENDGAME B.3.9 IO (W2): RtIoCompressedStreamRegister is gone -- every door it
// registered (the six CompressedMemoryStream doors, ??0CompressedStream and the
// four MemoryStream doors) is a class member now, reached directly.
