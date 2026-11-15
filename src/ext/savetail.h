// ============================================================================
// src/ext/savetail.h -- the class B / C manifest at the tail of block 1
// ============================================================================
// NOT RETAIL. docs/extensions/DESIGN.md §2, handins/SAVETAIL.md §1 / §4,
// handins/EXTSAVE.md.
//
// Every ACTIVE class B / C extension is recorded in a small chunk written
// into the save's game-state block (block 1) AFTER the last faithful
// Serialize and before the compressed stream closes
// (GameUpdateLoop::SaveRestore, src/game/Game/gameupdateloop_serialize.cpp).
// Retail's reader stops where it always did and never sees it; the block's
// size and the file CRC cover it automatically (SAVETAIL §1). Our loader
// reads it back and locks the game's B / C set from it
// (Ext::LockForGameWithSet), which is the one route every load takes -- the
// scenario / tutorial / autoload routes never read block 0's mod list.
//
// NO B / C EXTENSION ACTIVE = ZERO BYTES. Encode of an empty set is an empty
// vector: no header, nothing. The resave gate byte-compares the whole
// decompressed block 1, so the faithful profile must not write one byte.
//
// THE WIRE FORMAT (little-endian; version 2, written since 2026-10-21; version 1 is still read):
//   u32  magic          0x53545845 ("EXTS" as bytes)
//   u32  version        2 (kVersion)
//   u32  count
//   count x {
//     u8   kind         kKindExtension or kKindHost (version 1: absent, every record an extension's)
//     u8   flags        kFlagMustUnderstand: a reader without this kind / host id must refuse the save
//                       (version 1: absent, set)
//     u32  idLength     1..kMaxIdLength, the bytes of the id, no NUL
//     u8   id[idLength] [a-z0-9_]+ (Ext::ValidId)
//     i32  formatVersion  the extension's (or the host's) format version, >= 1
//     u32  payloadLength  a class C extension's or a host's own state; 0 for a class B row
//     u8   payload[payloadLength]
//   }
//   u32  bodyLength     the byte count of everything above (magic .. last payload)
//   u32  crc            CRC-32 (IEEE, reflected 0xEDB88320) of those bytes
// The 8-byte trailer is fixed for every version: a reader finds it from the
// END, so a cut tail (bodyLength != what is there) reads as Truncated and a
// damaged one as BadChecksum before a single record is trusted.
//
// CLASS C STATE (DESIGN.md "Class C state: the contract", ruled 2026-10-20). A class C extension's per-town
// state travels in its own record's payload. The extension registers a StateCodec beside its Entry
// (src/ext/state_codecs.cpp, one line per class C extension): `encode` appends the state at the save arm,
// `decode` reads it when the loader locks the game's set from the manifest, `reset` empties it for a new game
// and when a game ends. This file never learns what a payload means. A class B row has no codec and an empty
// payload; the faithful profile still writes zero bytes.
//
// TWO KINDS OF RECORD (ruled 2026-10-21, DESIGN.md "Host records").
//   * An EXTENSION record (kKindExtension, always kFlagMustUnderstand): one per active class B / C
//     extension. These ARE the game's lock set: an unknown id or a newer formatVersion is fatal, because the
//     town would play under rules this build does not have.
//   * A HOST record (kKindHost, no flag): the per-town state of a shared surface (the extension toolbar
//     window: open, where, which page). PRESENTATION ONLY: anything a reload must restore for the town to
//     play on as saved is an extension record (the class C test, DESIGN.md §2). Never in the lock set; an
//     unknown host, a newer host version or a payload its codec refuses is dropped with a log line and the
//     host keeps its defaults. Host records are written only beside at least one extension record, so a
//     game with only class A (or no) extensions still writes zero bytes.
// The kind byte is the room for a later kind: an unknown kind without kFlagMustUnderstand is dropped, one
// with it is fatal, so no future kind needs another chunk version.
//
// ENCODE RUNS ON THE SAVE WORKER (ext.background_save). A codec's `encode` reads only plain state its own
// extension or host owns, never a UI object, and that state is not written during a frozen update.
//
// Standard C++ only: tests/unit/ext_savetail_test.cpp links this file with
// registry.cpp (and state_codecs.cpp / host_codecs.cpp for the codec tables).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace SaveTail {

constexpr uint32_t kMagic        = 0x53545845u;   // 'E' 'X' 'T' 'S'
constexpr uint32_t kVersion      = 2;
constexpr uint8_t  kKindExtension = 1;
constexpr uint8_t  kKindHost      = 2;
constexpr uint8_t  kFlagMustUnderstand = 0x01;
constexpr uint32_t kMaxIdLength  = 64;
constexpr uint32_t kTrailerBytes = 8;

// What a class C extension gives the save tail (see CLASS C STATE above).
struct StateCodec {
    const char* id;                                      // the Entry id
    void (*encode)(std::vector<uint8_t>& payload);       // the save arm: append this town's state
    bool (*decode)(const uint8_t* bytes, size_t size, int32_t savedFormatVersion);
                                                         // the load arm: false = malformed (fatal)
    void (*reset)();                                     // a new game, or the game ends: empty state
};
// The codec registered for `id`; null for a class B row (no state). Defined in state_codecs.cpp.
const StateCodec* FindCodec(const char* id);
// Every class C codec's and every host codec's `reset` (a new game; the game ends). Safe to call at any time.
void ResetAllState();

// What a shared host gives the save tail (see TWO KINDS OF RECORD above). Its own format version: a host
// has no registry Entry.
struct HostCodec {
    const char* id;                                      // [a-z0-9_]+, its own namespace (never an Entry id)
    int32_t     formatVersion;                           // >= 1; bumped when the payload's layout changes
    void (*encode)(std::vector<uint8_t>& payload);       // the save arm (the worker: plain state only)
    bool (*decode)(const uint8_t* bytes, size_t size, int32_t savedFormatVersion);
                                                         // the load arm: false = refused (dropped, reset)
    void (*reset)();                                     // defaults: a new game, the game ends, a refusal
};
// The host codec for `id`; null when this build has no such host. Defined in host_codecs.cpp.
const HostCodec* FindHostCodec(const char* id);
// The host codec at `index` in table order; null past the last one.
const HostCodec* HostCodecAt(int index);
// Every host codec's `reset`. ResetAllState runs it too.
void ResetAllHostState();

// Little-endian helpers for a payload, so a codec spells its wire format the way the manifest does.
struct Writer {
    std::vector<uint8_t>& out;
    explicit Writer(std::vector<uint8_t>& o) : out(o) {}
    void U32(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
    }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void Bytes(const uint8_t* data, size_t n) { out.insert(out.end(), data, data + n); }
    void String(const std::string& s) {                   // u32 length + the bytes, no NUL
        U32(static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};
struct Reader {
    const uint8_t* p;
    size_t         left;
    Reader(const uint8_t* bytes, size_t size) : p(bytes), left(size) {}
    bool U32(uint32_t& v) {
        if (left < 4) {
            return false;
        }
        v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        p += 4;
        left -= 4;
        return true;
    }
    bool I32(int32_t& v) {
        uint32_t u = 0;
        if (!U32(u)) {
            return false;
        }
        v = static_cast<int32_t>(u);
        return true;
    }
    // the next n bytes in place; false when fewer are left
    bool Bytes(size_t n, const uint8_t*& at) {
        if (left < n) {
            return false;
        }
        at = p;
        p += n;
        left -= n;
        return true;
    }
    bool String(std::string& s) {                         // false when the length overruns the payload
        uint32_t n = 0;
        if (!U32(n) || left < n) {
            return false;
        }
        s.assign(reinterpret_cast<const char*>(p), n);
        p += n;
        left -= n;
        return true;
    }
    bool AtEnd() const { return left == 0; }
};

struct Record {
    uint8_t              kind  = kKindExtension;
    uint8_t              flags = kFlagMustUnderstand;
    std::string          id;
    int32_t              formatVersion = 0;
    std::vector<uint8_t> payload;   // empty for a class B row; a class C extension's state (its StateCodec)
};

enum class Status : uint8_t {
    NoManifest,          // nothing there, or bytes that do not start with kMagic: not ours, ignored
    Ok,
    Truncated,           // starts with kMagic but shorter than its trailer says
    BadChecksum,         // the trailer's CRC does not match the body
    NewerChunkVersion,   // the chunk's version is newer than kVersion
    Malformed,           // a record overruns the body, a bad id, bytes left over, formatVersion < 1
    UnknownId,           // Validate: no registered extension has this id
    NotLockedClass,      // Validate: the id is a fix or a class A extension
    NewerFormatVersion,  // Validate: the save's formatVersion is newer than ours
    DuplicateId,         // Validate: an id named twice
    UnknownKind,         // SplitKinds: a record kind this build does not have, marked must-understand
};
const char* StatusName(Status s);

// The chunk for `records`; EMPTY (zero bytes) when `records` is empty.
std::vector<uint8_t> Encode(const std::vector<Record>& records);

// Parse `size` bytes (everything after the last faithful Serialize). On Ok
// `out` holds the records; on anything else it is cleared.
Status Decode(const uint8_t* data, size_t size, std::vector<Record>& out);

// Check decoded records against the registry: every id a registered class B /
// C extension, its formatVersion not newer than ours, no id twice. On failure
// `offending` (when given) names the record.
Status Validate(const std::vector<Record>& records, std::string* offending);

// Sort decoded records by kind: `extensions` (the lock set: Validate, the lock, ApplyState) and `hosts`
// (ApplyHostState). A record of a kind this build does not have is dropped (its id named in `dropped`) unless
// it is marked must-understand: then UnknownKind, `offending` names it, and the outputs are cleared.
Status SplitKinds(const std::vector<Record>& all, std::vector<Record>& extensions, std::vector<Record>& hosts,
                  std::vector<std::string>* dropped, std::string* offending);

// The load arm for host records: each to its codec. Never fails the load: an unknown host, a newer host
// version or a refused payload is noted in `notes` (one line each) and that host is reset to its defaults.
void ApplyHostState(const std::vector<Record>& hosts, std::vector<std::string>* notes);

// The load arm: hand every record's payload to its codec (after the lock took the saved set). False when a
// codec found its payload malformed; `offending` names it.
bool ApplyState(const std::vector<Record>& records, std::string* offending);

// The registry's ACTIVE class B / C extensions as records, in table order
// (the same set Ext::PseudoModsForSave reports).
std::vector<Record> ActiveRecords();
// One host record per host codec, in table order (written beside the extension records).
std::vector<Record> HostRecords();
// Encode(ActiveRecords() + HostRecords()): zero bytes while no B / C extension is active (the host records
// are written only beside an extension record).
std::vector<uint8_t> EncodeActive();

// CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF). "123456789" -> 0xCBF43926.
uint32_t Crc32(const uint8_t* data, size_t size);

}  // namespace SaveTail
}  // namespace Ext
