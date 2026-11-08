// src/game/Game/citizen_names.cpp
//
// Faithful port of the citizen name generator that the retail deferred citizen
// init (FUN_140117080) runs to fill CitizenComponent+0x28 (the name String):
//
//   FUN_1400a4c60(retbuf, rng, femaleFlag)   -- pick Male.ndb / Female.ndb, load it
//     -> FUN_1400a4b30(retbuf, db, rng, 3, 10)  -- retry generate until len >= 3,
//                                                   rejecting names in the db reject list
//        -> FUN_1400a4880(db, rng, 3, 10, buf)   -- 4-gram Markov walk over the .ndb
//
// The whole retail init is guarded off in standalone (it dereferences housing /
// animation sub-objects that are NULL), so founding colonists never get a name and
// the CitizenUI dialog's textName is skipped (CitUI_OkString rejects the garbage
// String at +0x28). This reproduces just the name-generation slice and constructs a
// real System::String directly at the citizen's +0x28, so the dialog shows a name.
//
// The .ndb name database is an Object::External. Field layout (from FUN_1400a4880 /
// FUN_1400a4b30 disassembly):
//   +0x20  record table base (12 bytes per record)
//   +0x28  record count (int)
//   +0x30  start-index array base (uint16 per entry -> index into record table)
//   +0x38  start count (uint)
//   +0x40  reject-list string pool base (C-strings)
//   +0x50  reject-list offset array (int per entry -> offset into +0x40 pool)
//   +0x58  reject-list count (int)
// Record (12 bytes):
//   [0..3]  4-char context key (compared big-endian; the sorted key of the table)
//   [4]     word-boundary flag (nonzero => this transition can end the name)
//   [8..11] bitmask of possible next chars (bits 0..25 = 'a'..'z', bit 26 = end)

#include "system/Filename.h"
#include "system/String.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <new>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"
#include "Game/NameDatabase.h"   // the .ndb record the generator walks (B4 P2 GAME-1)

// System::Random::Rand(int min, int max)  (Runtime 0x1800c7d70) returns
//   min + Rand() % ((max - min) + 1)   -- inclusive [min, max].
// Built from the (self, N) overload, which is Rand() % N.
static inline int RandRange(System::Random* rng, int lo, int hi) {
    if (hi < lo) return lo;
    return lo + (int)rng->Rand((uint32_t)(hi - lo + 1));
}

// Faithful port of FUN_1400a4880: one 4-gram Markov walk. Writes a C-string into
// `out` (caller supplies a >=0x100 byte buffer, memset to 0). No allocation.
static void MarkovGenerate(const NameDatabase* db, System::Random* rng, int minLen, int maxLen, char* out) {
    const NameDatabaseEntry* table = db->_array1._data;   // +0x20
    int       recCount = db->_array1._size;              // +0x28
    const uint16_t* startArr = db->_array2._data;        // +0x30
    uint32_t  startCnt = db->_array2._size;              // +0x38 (read as uint)
    if (!table || recCount <= 0 || !startArr || startCnt == 0) { out[0] = 0; return; }

    // --- Seed: pick a random starting 4-gram and copy it into out[0..3] ---
    uint32_t s = rng->Rand(static_cast<uint32_t>(startCnt));
    uint16_t startIdx = startArr[(int)s];
    if ((int)startIdx >= recCount) { out[0] = 0; return; }
    uint32_t seedKey = table[startIdx].context;
    *(uint32_t*)out = seedKey;   // CAST-OK: byte_io (the 4-gram stored into the caller's char buffer as one dword, as retail does)
    out[0] = (char)(out[0] - 0x20);   // uppercase the first letter ('a'-0x20 = 'A')

    // Context window = the 4 seed bytes, in memory (little-endian) order.
    unsigned char c0 = (unsigned char)(seedKey);
    unsigned char c1 = (unsigned char)(seedKey >> 8);
    unsigned char c2 = (unsigned char)(seedKey >> 16);
    unsigned char cCur = (unsigned char)(seedKey >> 24);

    int  targetLen  = RandRange(rng, minLen, maxLen - 4);
    int32_t loopBound  = (int32_t)maxLen * 2 - 3;
    if (loopBound <= 0) return;

    int32_t charCount   = 0;   // chars appended past the seed
    int  outIdx      = 0;
    int  lastBoundary = -1;

    do {
        // Context key packed big-endian (the table's sort order).
        uint32_t ctxKey = ((uint32_t)c0 << 24) | ((uint32_t)c1 << 16)
                        | ((uint32_t)c2 << 8) | (uint32_t)cCur;

        // Binary search the record table for the record whose key == ctxKey.
        int lo = 0, hi = recCount, foundIdx = -1;
        for (;;) {
            int mid = (hi + lo) >> 1;
            if (mid < 0 || mid >= recCount) return;   // context not found -> stop
            // The 4-gram's bytes in memory order (little-endian: byte 0 is the
            // dword's low byte), most significant first.
            const uint32_t rk = table[mid].context;
            uint32_t rkey = ((rk & 0xFFu) << 24) | (((rk >> 8) & 0xFFu) << 16)
                          | (((rk >> 16) & 0xFFu) << 8) | (rk >> 24);
            if (rkey == ctxKey) { foundIdx = mid; break; }
            if (rkey < ctxKey) lo = mid + 1; else hi = mid - 1;
            if (hi < lo) return;                       // not found -> stop
        }

        const NameDatabaseEntry* rec = &table[foundIdx];
        uint32_t recKey  = rec->context;
        uint32_t mask    = rec->nextCharMask;
        // Slide the context window: drop the oldest byte (recKey byte0), keep 1..3.
        c0 = (unsigned char)(recKey >> 8);
        c1 = (unsigned char)(recKey >> 16);
        c2 = (unsigned char)(recKey >> 24);
        if (mask == 0) return;                         // no continuations
        unsigned char flag = rec->isWordEnd;   // the low byte of the old dword read at +4
        if (maxLen <= charCount && flag != 0) return;  // too long, at a word boundary
        if (flag != 0) lastBoundary = outIdx + 4;

        // Pick a uniformly random set bit among the 27 possible next-chars. The
        // original counts bits in three interleaved groups; the sum is the popcount
        // of the low 27 bits, so a single popcount + one Rand call is faithful.
        int total = __builtin_popcount(mask & 0x07FFFFFFu);
        if (total <= 0) return;
        uint32_t pick = rng->Rand((uint32_t)total);
        int chosenBit = 0;
        uint32_t seen = 0;
        for (int bit = 0; bit < 0x1b; ++bit) {
            if ((mask >> bit) & 1u) {
                chosenBit = bit;
                if (seen == pick) break;
                ++seen;
            }
        }

        // Stop if we've reached the target length and 'end' is allowed, or the
        // chosen transition IS the end marker (bit 0x1a).
        if (((targetLen <= charCount) && ((mask >> 0x1a) & 1u)) || chosenBit == 0x1a) {
            out[outIdx + 4] = 0;
            return;
        }

        char nc = (char)(chosenBit + 'a');
        out[charCount + 4] = nc;
        cCur = (unsigned char)nc;                      // new current char feeds next context
        ++charCount;
        ++outIdx;
    } while (charCount < loopBound);

    if (lastBoundary > 0) out[lastBoundary] = 0;       // truncate at last word boundary
}

// Faithful port of FUN_1400a4b30: retry the generator up to 50 times until it
// yields a name of at least `minLen`, rejecting any candidate that appears in the
// db's reject list (a sorted C-string table at +0x40 / +0x50 / +0x58). Writes the
// accepted C-string into `out` (>=0x100 bytes).
static void GenerateName(const NameDatabase* db, System::Random* rng, int minLen, int maxLen, char* out) {
    const char* pool = db->_array3._data;      // +0x40
    const int32_t* offs = db->_array4._data;   // +0x50
    int       rejN  = db->_array4._size;      // +0x58

    for (int attempt = 0; attempt < 0x32; ++attempt) {
        std::memset(out, 0, 0x100);
        MarkovGenerate(db, rng, minLen, maxLen, out);
        int len = (int)std::strlen(out);
        if (len < minLen) continue;                    // too short -> regenerate

        // Reject if the generated name is present in the reject list. A missing name
        // (search exhausts) is accepted. Empty list -> accept immediately.
        bool rejected = false;
        if (pool && offs && rejN > 0) {
            int lo = 0, hi = rejN;
            while (true) {
                int mid = (lo + hi) >> 1;
                if (mid < 0 || mid >= rejN) break;     // not found -> accept
                const char* cand = pool + offs[mid];
                int cmp = std::strcmp(out, cand);
                if (cmp == 0) { rejected = true; break; }
                if (cmp < 1) hi = mid - 1; else lo = mid + 1;
                if (hi < lo) break;                    // not found -> accept
            }
        }
        if (!rejected) return;                         // accepted
    }
    // 50 failed attempts: keep whatever the last generate produced (retail leaves
    // the buffer as-is and constructs from it).
}

// ---- .ndb load + gender selection (FUN_1400a4c60) ---------------------------
// FindOrLoad on EVERY call, exactly as retail does. This used to cache the two
// Externals in process-lifetime statics behind a one-shot "tried" flag, to avoid
// a FindOrLoad per colonist. That deviation crashed the SECOND new game of a
// session once quit-to-main-menu became reachable (2026-08-29): the teardown
// frees the .ndb Externals, the cache kept the dangling pointers, and the
// one-shot flag meant they were never reloaded -- so StartTown's first colonist
// walked a freed, poison-filled table and faulted reading 0xFFFFFFFFFFFFFFFF
// inside MarkovGenerate. FindOrLoad is a lookup-or-load, so the repeat cost is
// the hash lookup retail already pays.
//
// The IsDerivedFrom check retail does on the result is still skipped: its type-id
// .rdata (DWORD_1401c5a60) is NULL in standalone, and FindOrLoad by exact
// filename is authoritative.
static NameDatabase* LoadNameDb(const char* path) {
    if (!path || !::Object::ExternalFactory::_singleton)
        return nullptr;

    System::Filename fname(path);
    // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
    // The singleton is still read through the EXISTING door, so the guard
    // above still gates the same cell and the same registered body still runs.
    // (retail's IsDerivedFrom on the result is skipped, as before: see above)
    return ::Object::ExternalFactory::_singleton
        ->FindOrLoadAs<NameDatabase>(fname);
}

// Public entry: generate a citizen name for `female` into `out` (>=0x100 bytes,
// caller owns). Returns true if a name of length >= minLen was produced. Faithful
// minLen=3 / maxLen=10 (retail FUN_1400a4c60 passes 3, 10).
extern "C" bool GenerateCitizenName(System::Random* rng, bool female, char* out, int cap) {
    if (!out || cap < 0x100 || !rng) return false;
    NameDatabase* db = LoadNameDb(female ? "Game/Names/Female.ndb" : "Game/Names/Male.ndb");
    if (!db) { out[0] = 0; return false; }
    GenerateName(db, rng, 3, 10, out);
    return std::strlen(out) >= 3;
}


// ---- NameDatabase::GenerateName (exe 0x1400a4b30) ---------------------------
// The retail entry point, registered at its exe address so every caller reaches
// this port through ResolveAddr instead of a NULL function pointer. Retail
// signature: String* GenerateName(String* retStr, NameDatabase* db, Random*,
// int minLen, int maxLen) -- constructs the result String in `retStr` (caller
// owns and destructs it).
//
// Callers: NewGameDialog's town-name button (via FUN_140099ac0) and
// MapParamsDialog's in-game rename button. `db` may be NULL when the caller's
// FindOrLoad missed; retail would walk the NULL table, so return an empty String
// rather than faulting -- an empty name is visible in the edit box and keeps the
// OK button's enable check honest.
extern "C" System::String* NameDB_GenerateName(System::String* retStr, const NameDatabase* db,
                                              System::Random* rng, int minLen, int maxLen) {
    char buf[0x100];
    buf[0] = 0;
    if (db && rng)
        GenerateName(db, rng, minLen, maxLen, buf);
    // Retail constructs the result String in the caller's return slot.
    new (retStr) System::String(buf);
    return retStr;
}

// ---- NameDatabase gender picker (exe 0x1400a4c60) ---------------------------
// Retail: String* fn(String* retStr, Random* rng, char female) -- picks
// "Game/Names/Female.ndb" or "Game/Names/Male.ndb" by the flag, FindOrLoad's it,
// then tail-calls 0x1400a4b30 (NameDB_GenerateName) with minLen=3 / maxLen=10.
// Same body as GenerateCitizenName above, but with retail's String return so the
// address can be registered and reached through ResolveAddr.
//
// Registering this matters: TradeComponent::Setup (tradecomponent.cpp:326)
// resolves 0x1400a4c60 and, while it was null, left its 8-byte String scratch
// zeroed and assigned an EMPTY name into every merchant slot -- so every trader
// rendered blank in the trade UI. reg_namedb.cpp used to say this address needed
// no registration because "nothing resolves them by address"; that stopped being
// true when the trade lane added the resolve.
extern "C" System::String* NameDB_PickAndGenerate(System::String* retStr, System::Random* rng, char female) {
    char buf[0x100];
    buf[0] = 0;
    NameDatabase* db = LoadNameDb(female ? "Game/Names/Female.ndb" : "Game/Names/Male.ndb");
    if (db && rng)
        GenerateName(db, rng, 3, 10, buf);

    new (retStr) System::String(buf);
    return retStr;
}
