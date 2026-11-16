// ============================================================================
// src/engine/object/external_reftable.cpp — Wave 11 follow-up 3, lane AVL
//   ?SerializeReferenceTable@ExternalFactory@Object@@QEAAXAEAVStream@IO@@@Z
//   retail 0x1800bcb60 .. 0x1800bcf38   (292 instructions)
// ============================================================================
//
// Byte-faithful port of the disassembly at 0x1800bcb60 (Runtime-x64.dll, image
// base 0x180000000, read 2026-09-10 with `disassemble_function(address=)`).
// The AVL family the READ path needs is reference_set.{h,cpp}; the tree
// teardown the body OPENS with is `RtObjFactoryDtor::ReferenceSubtreeFree`,
// ported in factory_dtors.cpp by Wave 11 lane OBJLIFE.
//
// ---------------------------------------------------------------------------
// WHAT THE FUNCTION IS
// ---------------------------------------------------------------------------
// The save file's id <-> resource-path table. On SAVE it numbers every loaded
// resource 1..N in in-order key order, stamps the number into the resource's
// own `ResourceEntry::referenceId`, and emits (id, path) per resource. On LOAD
// it reads the pairs back, rebuilds `_references` (id -> path) so
// `GetPathForReferenceId` can answer, and — for every path that is ALREADY
// loaded — stamps the id back into that resource's `referenceId`.
//
// Everything downstream of it depends on the numbering: `Object::ExternalPtr`'s
// serializer emits a bare id when reference mode is on, and resolves it through
// this table on the way back in. Getting the ORDER or the COUNT wrong here is
// the silent save-desync this project has paid for repeatedly.
//
// ---------------------------------------------------------------------------
// **THE SECOND PARAMETER IS A `Stream&`, NOT A `Serializer&`.**
// ---------------------------------------------------------------------------
// The mangled name says `AEAVStream@IO@@` and the body confirms it: it reads
// `[RDX+0x08]` as the direction and `[RDX+0x10]` as the `Encoding*`, which are
// Stream fields. The `Serializer` is a LOCAL, built at `[RBP-0x18]` /
// `[RBP-0x10]` — exactly the 16-byte `{Stream*, ReaderWriter*}` of
// src/engine/io/Serializer.h. Passing a Serializer straight through would
// read 16 bytes past the end of a 16-byte object; the local IO::Serializer
// below exists for that reason and not as a workaround for anything.
//
// ---------------------------------------------------------------------------
// THE FIRST THING IT DOES IS A MUTATION, AND NO EMISSION PRECEDES IT
// ---------------------------------------------------------------------------
// Eight instructions in, on BOTH directions, it tears the whole `_references`
// tree down and zeroes root and count:
//
//   1800bcb80  MOV RDX,[RCX+0x20] / ADD RCX,0x20 / CALL 0x18002ebf0
//   1800bcb90  MOV [R14+0x20],R12 / MOV [R14+0x28],R12D
//
// `docs/history/runtime_port/WAVE8_5_PREFIX_MEASUREMENT.md` measured this and its
// consequence, and both findings are load-bearing here:
//
//   * A prefix stop cannot get in front of it — there is no emission index that
//     precedes it — so lane S's `stopAfter` is useless for this subject.
//   * It does not need to. The write path's only other mutation is
//     `ResourceEntry::referenceId = id`, which a replay rewrites with the
//     IDENTICAL value into the identical node (the ids are 1..N over a tree
//     neither call modifies), and the teardown's second run reads
//     `[this+0x20] == 0` and returns immediately. **No double free.**
//   * The teardown is INVISIBLE to an emission compare — it emits nothing — so
//     a port that forgot it, or freed it wrongly, would pass a clean emission
//     trace.
//
// ---------------------------------------------------------------------------
// THE FOUR ENCODING/READERWRITER SLOTS IT DRIVES
// ---------------------------------------------------------------------------
//   Encoding    +0x18 / +0x20   CreateReader / CreateWriter, (this, &ser, 0)
//   ReaderWriter +0xa0          kScalarInt   — the entry COUNT, once
//   ReaderWriter +0x98          kScalarUInt  — the id, once per entry
//   ReaderWriter +0x00          the scalar deleting dtor, flags = 1, once
// plus `String::Serialize` (0x1800c9930) per entry, which is itself +0x98 for
// the length and +0x18 for the char array.
//
// These are RETAIL offsets. The profile build's differ; the constants
// come from `rt::io::rw_vtbl`, which is pinned against retail.
//
// ---------------------------------------------------------------------------
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "reference_set.h"
#include "external_set.h"                 // rtresset::{Node,Set,Elem} — the
                                          //   0x38-byte ResourceEntry node
#include "io/Serializer.h"
#include "io/Stream.h"       // IO::Stream: _encoding / _direction, the Serializer ctor
#include "object/External.h"
#include "system/string_internal.h"
#include "rt_libm.h"      // B3R: rtcrt::StrICmp (0x18007eefc)

// ENDGAME B.3.1 step 2: `String::Serialize` (0x1800c9930, which takes the
// Serializer and not the Stream) and `Filename::AdjustDelimiters` (0x1800c6970)
// used to be forward-declared here as C entry points. They are members now —
// the first called as `s.Serialize(ser)`, the second folded into the two
// Filename constructors that already end in it — so both declarations are gone.

namespace RtObject {
// 0x180006250 — ?GetPath@External@Object@@QEBA?AVFilename@System@@XZ, ported in
// external.cpp. HIDDEN RETURN SLOT: (this in RCX, ret in RDX), returns ret.
System::Filename* RtObject_External_GetPath(const ::Object::External* self, System::Filename* ret);
}

namespace RtObjFactoryDtor {
// 0x18002ebf0 — the post-order `_references` teardown, ported in
// factory_dtors.cpp by Wave 11 lane OBJLIFE. IT RELEASES `node + 0x08` (the
// ReferenceEntry's `path`, which is the VALUE), NOT `node + 0x00`; its sibling
// 0x180008340 in avl_trees.cpp is the same 0x30-byte node releasing +0x00
// instead, and releasing both in either body corrupts the heap. This lane
// re-read both disassemblies rather than trust the comment, and both stand.
void ReferenceSubtreeFree(void* set, RtObject::ReferenceSet::Node* node);
}

namespace {

using RtObject::ExternalFactory;

// The vtable call shapes, all pinned by the disassembly: 1800bcbab the
// Encoding's CreateReader / CreateWriter (inside the inlined Serializer ctor),
// 1800bcbd5 the ReaderWriter's SerializeInt (+0xa0), 1800bcf28 its deleting
// destructor (slot 0, flags 1, inside the inlined ~Serializer). All three are
// IO::Serializer's and IO::ReaderWriter's members, called by name.

// ENDGAME B.3.1 step 2 removed this file's `ReleaseInline` and `AdoptRef`
// helpers. The six inlined releases are `~Filename` (0x180006980); three of the
// four AddRef'd aliases were an Adopt IMMEDIATELY FOLLOWED BY AdjustDelimiters,
// which is `Filename::Filename(const String&)` (0x18000c780) exactly. The
// fourth, at 1800bce41 on the write path, has NO following normalise and so is
// NOT the ctor and NOT operator=; it is spelled out at its call site with
// `System::StringPrivateAccess::Adopt` and a comment saying which address omits
// the normalise (the cookbook's DELIMITERS rule).

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS. Branches taken INSIDE the body under test, not
// proxies observed from outside it.
// ---------------------------------------------------------------------------
struct Counters {
    uint64_t calls, save, load;
    uint64_t entries;            // rows emitted or consumed
    uint64_t teardownNonEmpty;   // entered with a NON-empty _references tree
    uint64_t resFound, resMissed;// read path: the path matched a loaded resource
    uint64_t nullSearchKey, nullNodeKey;   // the two asymmetric null arms
    uint64_t nullEncoding, nullRw;
};
Counters g_c = {};

}  // namespace

// ============================================================================
// void Object::ExternalFactory::SerializeReferenceTable(IO::Stream&)
// ============================================================================
void RtObjectExternalRefTableSummary();

namespace RtObject {

void RtObject_ExternalFactory_SerializeReferenceTable(ExternalFactory* self,
                                                      ::IO::Stream* stream) {
    ++g_c.calls;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): `_references` is rebuilt.
    ::Object::NotifyResourceEvent(::Object::ResourceEvent::RefTable, nullptr, nullptr, nullptr);

    // ---- MUTATION A: tear the reference table down --------------------------
    // 1800bcb80. `this` for the teardown is the SET (`ADD RCX,0x20`), not the
    // factory: retail loads RCX = this+0x20. The callee ignores it, but a port
    // that passed the factory would be relying on that.
    RtObject::ReferenceSet& refs = self->_references;   // this + 0x20
    if (refs._root) ++g_c.teardownNonEmpty;
    RtObjFactoryDtor::ReferenceSubtreeFree(&refs, refs._root);
    refs._root  = nullptr;                       // 1800bcb90, qword
    refs._count = 0;                             // 1800bcb94, DWORD

    // ---- the local Serializer and its codec ---------------------------------
    // 1800bcb98 .. 1800bcbbc is `??0Serializer@IO@@QEAA@AEAVStream@1@H@Z`
    // (0x1800b9960) inlined with version 0: `_stream = stream`, `_readerWriter
    // = 0`, the Encoding at [stream+0x10], CreateReader (+0x18) on a read or
    // CreateWriter (+0x20) on a write, the result stored at +0x08. It is the
    // real IO::Serializer (B.4 wave X); its destructor (0x1800b99b0, inlined
    // at 1800bcf17) closes the block below.
    if (!stream->_encoding) ++g_c.nullEncoding;  // counted, NOT guarded: retail
                                                 //   derefs it unconditionally
    {
    ::IO::Serializer ser(*stream, 0);
    ::IO::ReaderWriter* const rw = ser._readerWriter;   // 1800bcbbc
    if (!rw) ++g_c.nullRw;

    // ---- the entry count ----------------------------------------------------
    // 1800bcbc0: the value emitted is `_resources._count` on BOTH directions.
    // On the reading side the slot OVERWRITES it with what the stream holds, so
    // the local is the loop bound either way. It is read as a SIGNED int
    // (`kScalarInt`, +0xa0) and the read-path loop is `JLE` / `JL` on it, so a
    // negative count is a zero-iteration loop rather than a wild one.
    int32_t count = self->_resources._count;     // this + 0x18
    rw->SerializeInt(count, nullptr);            // +0xa0

    if (stream->_direction == rt::io::kDirectionReading) {
        // ====================================================================
        // READ PATH — 1800bcbe5 .. 1800bcdef
        // ====================================================================
        ++g_c.load;

        // Its 1800bcdc8 release is the destructor at the end of this block.
        System::Filename scratch;                // [RBP-0x40], and RBX caches it
                                                 // 1800bcbe8

        for (int32_t i = 0; i < count; ++i) {    // 1800bcbef JLE / 1800bcdbf JL
            uint32_t id = 0;                     // [RBP+0x48]
            rw->SerializeUInt(id, nullptr);      // 1800bcc1e, +0x98

            // 1800bcc2c — reads the path into `scratch`, replacing whatever it
            // held. `RBX` is reloaded from the slot AFTER this call, which is
            // why the port re-reads `scratch.data` here and not before.
            scratch.AsString().Serialize(ser);
            ++g_c.entries;                       // 1800bcc35 reloads the slot

            // Temporary A: an AddRef'd alias, delimiter-normalised, handed to
            // the Set insert as the VALUE. 1800bcc31 .. 1800bcc49 — an Adopt of
            // the buffer plus ONE AdjustDelimiters IS
            // `Filename::Filename(const String&)`, 0x18000c780. (The normalise
            // rewrites the SHARED buffer in place, so `scratch`'s characters
            // change too; that is retail's, and the ctor keeps it.)
            {
                System::Filename keyPath(scratch.AsString());

                uint32_t key = id;               // 1800bcc4f [RBP+0x50]
                rtrefset::Node* outSlot = nullptr;   // [RBP-0x20]
                rtrefset::Insert(reinterpret_cast<rtrefset::Set*>(&refs), &outSlot, &key,  // CAST-OK: elem_stride
                                 &keyPath);
            }                                    // 1800bcc6b — keyPath's release

            // Temporary B: a SECOND AddRef'd alias, normalised again, then
            // copy-assigned into a third slot which is normalised TWICE more.
            // 1800bcc94 .. 1800bcd01. Retail really does call AdjustDelimiters
            // at 1800bccf4 AND at 1800bccfd on the same object — the inlined
            // `Filename::operator=` carries one and the statement carries the
            // other. It is idempotent, and it is transcribed rather than
            // folded because "obviously redundant" is how a port stops being a
            // port.
            RtObject::ResourceSet::Node* found;
            {
            System::Filename tmpB(scratch.AsString());   // as keyPath above

            // 1800bccae null-init, 1800bccbd release-then-adopt with retail's
            // own `!=` guard, then AdjustDelimiters at 1800bccf4 AND 1800bccfd:
            // that whole sequence IS the copy ctor (0x1800067d0), whose ported
            // body normalises twice for exactly this reason. Both calls are
            // still spent.
            System::Filename lookup(tmpB);

            // ---- the INLINED find over `_resources` ------------------------
            // 1800bcd07 .. 1800bcd5d. This is the same descent
            // `rtresset::Find` (0x18002dd70) makes, open-coded here by MSVC,
            // over the 0x38-byte ResourceEntry node: left +0x20, right +0x28,
            // key = element.path at +0x00, compared with `_stricmp`.
            //
            // THE TWO NULL ARMS ARE ASYMMETRIC and are retail's:
            //   node key null  -> fall through to the second test, then RIGHT
            //   search key null with a non-null node key -> LEFT
            //   both null      -> FOUND
            found = self->_resources._root;      // this + 0x10
            while (found) {
                char* searchKey = lookup._data;
                char* nodeKey   = found->element.path._data;
                if (nodeKey) {
                    if (!searchKey) {
                        ++g_c.nullSearchKey;
                        found = found->left;     // 1800bcd31, +0x20
                        if (!found) break;
                        continue;
                    }
                    if (rtcrt::StrICmp(searchKey, nodeKey) < 0) {
                        found = found->left;
                        if (!found) break;
                        continue;
                    }
                } else {
                    ++g_c.nullNodeKey;
                }
                // second test — 1800bcd37
                if (!searchKey) break;           // FOUND (both keys null)
                nodeKey = found->element.path._data;
                if (nodeKey && rtcrt::StrICmp(nodeKey, searchKey) >= 0) break;  // FOUND
                found = found->right;            // 1800bcd54, +0x28
            }

            }   // 1800bcd60 lookup's release, then 1800bcd89 tmpB's — reverse
                // declaration order is retail's order

            if (found) {                         // 1800bcdb2
                ++g_c.resFound;
                found->element.referenceId = id;   // +0x14
            } else {
                ++g_c.resMissed;
            }
        }
        // 1800bcdc8 — scratch's release, its destructor at the end of this block
    } else {
        // ====================================================================
        // WRITE PATH — 1800bcdf4 .. 1800bcf11
        // ====================================================================
        ++g_c.save;

        uint32_t id = 1;                         // 1800bcdf4, ids start at ONE
        RtObject::ResourceSet::Node* n = self->_resources._root;   // this + 0x10

        if (n) {
            while (n->left) n = n->left;         // 1800bce10, leftmost

            while (n) {
                // MUTATION B — 1800bce30. Idempotent under replay: the ids run
                // 1..N in in-order key order over a tree this body never
                // modifies, so a second pass writes the identical value into
                // the identical node.
                n->element.referenceId = id;        // +0x14

                // 1800bce37: NO NULL CHECK on `element.external`. Faithful —
                // retail relies on the pointer always being there, and a guard
                // would skip the emission rather than restore the behaviour.
                {
                    System::Filename path;
                    {
                        // GetPath's hidden return slot: constructed into, so
                        // the null local must not be constructed twice.
                        System::Filename ret;
                        RtObject_External_GetPath(
                            n->element.external, &ret);  // 1800bce3b

                        // 1800bce41 IS AN ADOPT WITH NO FOLLOWING
                        // AdjustDelimiters, so it is neither the String ctor
                        // (0x18000c780) nor operator= (0x180026d40) — both of
                        // those normalise. Spelled out rather than silently
                        // upgraded to one that adds a pass retail does not make.
                        System::StringPrivateAccess::Adopt(path.AsString(),
                                                           ret._data);
                    }                                        // 1800bce54
                    ++g_c.entries;

                    rw->SerializeUInt(id, nullptr);          // +0x98
                    path.AsString().Serialize(ser);          // 1800bce99
                }                                            // 1800bce9f

                // ---- in-order successor, 1800bcec0 .. 1800bcf02 ------------
                if (n->right) {
                    n = n->right;
                    while (n->left) n = n->left;
                } else {
                    RtObject::ResourceSet::Node* p = n->parent;           // +0x18
                    if (!p) {
                        n = n->parent;                       // 1800bcf02 -> null
                    } else if (p->left == n) {               // 1800bcee0, +0x20
                        n = p;
                    } else {
                        // 1800bcef0: climb while we are the RIGHT child.
                        while (p && p->right == n) {         // +0x28
                            n = p;
                            p = p->parent;
                        }
                        n = n->parent;                       // 1800bcf02
                    }
                }
                ++id;                                        // 1800bcf09
            }
        }
    }

    // ---- destroy the codec the factory allocated ----------------------------
    // 1800bcf17: `~Serializer` (0x1800b99b0), `if (rw) delete rw` (slot 0,
    // flags 1). Skipping it leaks 0x30 bytes per call.
    }

    // ---- report, HERE, inside the subject's live window ---------------------
    // Not only from the exit summary. This body runs at most twice per session
    // and the run that exercises it usually has to be killed rather than quit:
    // `BANISHED_AUTOQUIT` OVERRIDES `BANISHED_AUTOLOAD`'s path, so the one run
    // that reaches a clean exit is the one that never loaded, and an exit-only
    // instrument prints a plausible ZERO on it. This line is what a
    // killed run can still be read from.
    RtObjectExternalRefTableSummary();
}

}  // namespace RtObject

// The counters, for the exit summary.
void RtObjectExternalRefTableSummary() {
    if (g_c.calls == 0) {
        std::fprintf(stderr,
            "[RefTable] calls=0 -- the body never ran. That is EXPECTED on a run "
            "that neither saved nor loaded; it is NOT evidence the port is "
            "correct and it is NOT evidence it is dead.\n");
        return;
    }
    std::fprintf(stderr,
        "[RefTable] calls=%llu save=%llu load=%llu entries=%llu "
        "teardownNonEmpty=%llu resFound=%llu resMissed=%llu "
        "nullSearchKey=%llu nullNodeKey=%llu nullEncoding=%llu nullRw=%llu\n",
        (unsigned long long)g_c.calls, (unsigned long long)g_c.save,
        (unsigned long long)g_c.load, (unsigned long long)g_c.entries,
        (unsigned long long)g_c.teardownNonEmpty,
        (unsigned long long)g_c.resFound, (unsigned long long)g_c.resMissed,
        (unsigned long long)g_c.nullSearchKey, (unsigned long long)g_c.nullNodeKey,
        (unsigned long long)g_c.nullEncoding, (unsigned long long)g_c.nullRw);
}
