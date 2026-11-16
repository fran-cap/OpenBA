// ============================================================================
// src/engine/object/external_class_doors.cpp — ENDGAME B.3.3 step 5 (EXTERNAL)
// ============================================================================
//
// The half of `Object::ExternalList` and the whole of `Object::ExternalFactory`
// whose bodies are DOORS into already-ported C functions. Split out of
// external_class.cpp so that file stays linkable into the unit build (the same
// split type_reflection.cpp has against type_system.cpp).
//
// *** EVERY MEMBER BELOW IS A SPELLING CHANGE AND NOTHING ELSE. ***
// That is the point, not a shortfall. B33_RULES §0 makes the
// `ExternalFactory::Add` loader-thread AVL-rotate race its own investigation:
// the conversion must not add a lock, a retry, an ordering change or a null
// check anywhere on that path, and must not silently remove the race either —
// "a real `ExternalFactory` class changes the SPELLING of the tree mutation,
// never its timing". Taking those bodies over into members would move the
// instructions being investigated into a different file in the same commit that
// changes their surface, which is the one thing the rule forbids. So `Add`,
// `Clean`, `FindOrLoad`, `ForceUnload`, `SerializeReferenceTable`, `PushPath`
// and `PopPath` forward to the UNCHANGED bodies in external_add.cpp,
// external_set.cpp, external_compiled.cpp, external_forceunload.cpp,
// external_reftable.cpp and external_path_stack.cpp. The instruction sequence
// inside each tree mutation is byte-for-byte the one under investigation, with
// one extra (inlinable) call frame in front of it.
//
// The read-only members forward for a weaker but still real reason: their
// bodies sit in external.cpp / external_find.cpp / external_path.cpp
// alongside effectiveness counters and diff-oracle scaffolding that belong to
// those lanes, and moving a body out from under its counters is a second change
// riding on this one. They move when their lane's scaffolding is retired.
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>   // ExternalList::Find's strcmp
#include <new>

#include "object/External.h"
#include "object/Type.h"     // B.4 P2: ExternalList::Find takes Object::Type
#include "io/Stream.h"   // B.3.4 STREAMS (5/6): IO::Stream, the
                                     // parameter of SerializeReferenceTable
                                     // and of the ported entry point.
                                     // External.h only forward-declares the
                                     // class on purpose.
#include "system/string_internal.h"       // RtSystem::RtMemFree (~ExternalList)
#include "external_compiled.h"
#include "external_path_stack.h"

namespace RtObject {
// Declared in the files that define them; repeated here rather than pulled in
// through a header, because several of these have no header of their own.
System::Filename* RtObject_External_GetPath(const ::Object::External* self, System::Filename* ret);
// src/engine/io/external_savetext.cpp — `extern "C"`, so the linker name is unqualified.
extern "C" bool RtObject_External_SaveText(::Object::External* self, const System::Filename* path,
                                           const System::Filename* name);
const ::Object::External* RtObject_ExternalFactory_Find(const ExternalFactory* self,
                                              const System::Filename* path);
const ::Object::External* RtObject_ExternalFactory_FindNext(const ExternalFactory* self, uint32_t typeId,
                                                  const ::Object::External* prev);
uint32_t  RtObject_ExternalFactory_GetReferenceId(const ExternalFactory* self, const ::Object::External* e);
System::Filename* RtObject_ExternalFactory_GetPathForReferenceId(const ExternalFactory* self,
                                                                 System::Filename* ret, uint32_t id);
void      RtObject_ExternalFactory_Clean(ExternalFactory* self);
void      RtObject_ExternalFactory_ForceUnload(ExternalFactory* self, const System::Filename* path);
System::Filename* RtObject_ExternalFactory_GetPathStackFilename(const ExternalFactory* self,
                                                                System::Filename* ret);
System::Filename* RtObject_ExternalFactory_GetPathStackDirectory(const ExternalFactory* self,
                                                                 System::Filename* ret);
// ?ExpandRelativePath@ExternalFactory@Object@@QEBA?AVFilename@System@@AEBV34@_N@Z
// — the `_N` is a FOURTH parameter, `bool appendIdentifier`, which the class
// declaration carries too. Reading the mangled name rather than the three-arg
// shape a reader would expect is what caught it.
System::Filename* RtObject_ExternalFactory_ExpandRelativePath(const ExternalFactory* self,
                                                              System::Filename* ret,
                                                              const System::Filename* path,
                                                              bool appendIdentifier);
// external_add.cpp — declared inside `namespace RtObject` but `extern "C"`,
// so the C++ name lives here and the linker name is unqualified.
extern "C" void RtObject_ExternalFactory_Add(ExternalFactory* self, ::Object::External* external);
// external_reftable.cpp — C++ linkage inside `namespace RtObject`. Its second
// parameter is the real `IO::Stream` (B.4 wave X: the body builds the real
// IO::Serializer over it); both spellings must agree or the link fails.
void RtObject_ExternalFactory_SerializeReferenceTable(ExternalFactory* self,
                                                      ::IO::Stream* stream);
}  // namespace RtObject

namespace Object {
namespace {


// A `System::Filename` returned by value out of an entry point that writes a
// HIDDEN RETURN SLOT: the slot is raw, uninitialised bytes, so the callee
// CONSTRUCTS into it and the local must not be constructed twice.
// `char storage[8]` + placement is
// what the C bodies already do; the move out is a pure pointer steal.
template <class Fn>
inline System::Filename ByHiddenSlot(Fn call) {
    System::Filename out;                       // born null
    // The callee placement-constructs; `out` is null, so nothing is leaked by
    // overwriting its one pointer field.
    call(&out);
    return out;
}

}  // namespace

// ============================================================================
// Object::External — SaveText, whose body is in src/engine/io/external_savetext.cpp
// ============================================================================

// ?SaveText@External@Object@@QEAA_NAEBVFilename@System@@0@Z  retail 0x1800bb9d0
//
// A spelling over the UNCHANGED body in external_savetext.cpp, which drives
// the object's own vtable slot +0x28 (`Serialize`) with an IO::TextFileStream
// and keeps its effectiveness counters. Declared on the class by B.3.3 step 6A
// because a caller batch needed it; nothing about the body moved.
bool External::SaveText(const System::Filename& path, const System::Filename& name) {
    return RtObject::RtObject_External_SaveText(this, &path, &name);
}

// ============================================================================
// Object::ExternalFactory
// ============================================================================

// ??0ExternalFactory@Object@@QEAA@XZ   retail 0x1800be7e0 — the field-by-field
// pin for the whole layout: a zero qword at +0x00, an int32 at +0x08, a qword
// at +0x10, an int32 at +0x18, a qword at +0x20, an int32 at +0x28, qwords at
// +0x30 and +0x38, an int16 at +0x40 and a byte at +0x42.
//
// It also publishes `_singleton`, which this constructor does NOT:
// Ctor_ExternalFactory (src/engine/engine/singleton_ctors.cpp) does.
ExternalFactory::ExternalFactory()
    : _cleanBitIndex(0), _cleanBitMask(0), _generation(0), _pad0c(0),
      _resources(), _references(), _pathStack(),
      _referenceModeEnabled(false), _loadEnabled(false), _simpleLoadEnabled(false) {}

// ?_singleton@ExternalFactory@Object@@0PEAV12@EA — retail 0x1801fc6c8. The
// class static (ENDGAME B.3.9 OBJ, B39_RULES D7); the language zero-initialises
// it and Ctor_ExternalFactory (src/engine/engine/singleton_ctors.cpp) publishes
// into it.
ExternalFactory* ExternalFactory::_singleton = nullptr;

// ?Get@ExternalFactory@Object@@SAPEAV12@XZ — `MOV RAX,[_singleton]; RET`.
ExternalFactory* ExternalFactory::Get() {
    return _singleton;
}

const External* ExternalFactory::Find(const System::Filename& path) const {
    return RtObject::RtObject_ExternalFactory_Find(this, &path);
}

const External* ExternalFactory::FindNext(uint32_t typeId, const External* prev) const {
    return RtObject::RtObject_ExternalFactory_FindNext(this, typeId, prev);
}

// *** RETAIL DEREFERENCES THE FOUND NODE WITHOUT A NULL CHECK *** and the
// ported body keeps it that way. An External the factory never Add()ed faults
// reading +0x14; a guard here would not restore the behaviour, it would skip it
// (CLAUDE.md's hard rule, and external.cpp's GetReferenceId banner has the disassembly).
uint32_t ExternalFactory::GetReferenceId(const External* external) const {
    return RtObject::RtObject_ExternalFactory_GetReferenceId(this, external);
}

System::Filename ExternalFactory::GetPathForReferenceId(uint32_t id) const {
    const ExternalFactory* self = this;
    return ByHiddenSlot([self, id](System::Filename* slot) {
        RtObject::RtObject_ExternalFactory_GetPathForReferenceId(self, slot, id);
    });
}

System::Filename ExternalFactory::GetPathStackFilename() const {
    const ExternalFactory* self = this;
    return ByHiddenSlot([self](System::Filename* slot) {
        RtObject::RtObject_ExternalFactory_GetPathStackFilename(self, slot);
    });
}

System::Filename ExternalFactory::GetPathStackDirectory() const {
    const ExternalFactory* self = this;
    return ByHiddenSlot([self](System::Filename* slot) {
        RtObject::RtObject_ExternalFactory_GetPathStackDirectory(self, slot);
    });
}

System::Filename ExternalFactory::ExpandRelativePath(const System::Filename& path,
                                                     bool appendIdentifier) const {
    const ExternalFactory* self = this;
    const System::Filename* p = &path;
    return ByHiddenSlot([self, p, appendIdentifier](System::Filename* slot) {
        RtObject::RtObject_ExternalFactory_ExpandRelativePath(self, slot, p, appendIdentifier);
    });
}

void ExternalFactory::PushPath(const System::Filename& path) {
    RtObject::RtObject_ExternalFactory_PushPath(this, &path);
}
void ExternalFactory::PopPath() {
    RtObject::RtObject_ExternalFactory_PopPath(this);
}

// THE ADD PATH. See the file banner: spelling only, body untouched, race
// neither papered over nor removed.
void ExternalFactory::Add(External* external) {
    RtObject::RtObject_ExternalFactory_Add(this, external);
}

const External* ExternalFactory::FindOrLoad(const System::Filename& path) {
    return RtObject::RtObject_ExternalFactory_FindOrLoad(this, &path);
}

void ExternalFactory::Clean() {
    RtObject::RtObject_ExternalFactory_Clean(this);
}
void ExternalFactory::ForceUnload(const System::Filename& path) {
    RtObject::RtObject_ExternalFactory_ForceUnload(this, &path);
}
// ---------------------------------------------------------------------------
// `SerializeReferenceTable` (0x1800bcb60) — LANDS HERE, B.3.4 STREAMS (5/6).
// ---------------------------------------------------------------------------
// The paragraph this replaces said the method was held back by a TYPE and not
// by a decision about the method: retail's parameter is `IO::Stream&`, and
// `RtObject_ExternalFactory_SerializeReferenceTable`'s second parameter is
// `rt::io::Stream` — the DUPLICATE POD declared in src/engine/io/Serializer.h,
// the one `RtIo::Stream` (Stream.h) shadows. Block 3 landed `IO::Serializer`
// but not `IO::Stream`; block 4 landed `IO::Stream` and it replaces BOTH PODs
// (B34_RULES §1.5.2). So the signature is now spellable and the method lands.
//
// IT FORWARDS, AND THAT IS THE POINT — TWICE OVER.
//
//  1. It is in the MUTATING HALF. Its body tears `_references` down
//     (`ReferenceSubtreeFree`, then root = 0 and count = 0) and rebuilds it row
//     by row through `rtrefset::Insert` — the SAME tree the
//     `ExternalFactory::Add` loader-thread AVL-rotate race runs in. The file
//     banner's rule applies unchanged: a real class changes the SPELLING of the
//     tree mutation, never its timing.
//  2. B34_BRIEFS §D binds it independently. `external_reftable.cpp` is on the
//     loader-race path and its bodies stay byte-faithful — no lock, no retry,
//     no reordering, no null check, and no silent removal of whatever makes the
//     race reproduce. **This commit changes ZERO lines of external_reftable.cpp.**
//
// NO QUALIFIED CALL IS NEEDED ANYWHERE HERE. `Object::ExternalFactory` has NO
// VTABLE (`??0ExternalFactory@Object@@` writes no vptr and the class has no
// exported `??_7`), so the recursion hazard B34_BRIEFS §A.1 describes for the
// stream classes cannot arise: there is no table to dispatch through and no
// door for a virtual call to re-enter. Said out loud because a reader arriving
// from the STREAMS commits will look for the `Class::` and wonder.
void ExternalFactory::SerializeReferenceTable(IO::Stream& stream) {
    RtObject::RtObject_ExternalFactory_SerializeReferenceTable(
        this,
        &stream);
}

// ?GetCompiledPath@ExternalFactory@Object@@SA?AVFilename@System@@AEBV34@@Z
//   retail 0x1800bec10 — a STATIC returning by value, so RCX is the hidden
//   return slot and RDX is the path (the arguments do NOT shift the way a
//   member's do; the C entry point's `(ret, path)` order is that shape).
System::Filename ExternalFactory::GetCompiledPath(const System::Filename& path) {
    const System::Filename* p = &path;
    return ByHiddenSlot([p](System::Filename* slot) {
        RtObject::RtObject_ExternalFactory_GetCompiledPath(slot, p);
    });
}

// ============================================================================
// Object::ExternalList — the registry-free half
// ============================================================================

// (The constructor is inline in External.h. The class's KEY FUNCTION is its
// destructor below, the first virtual declared out of line, so GCC emits the
// vtable here.)

// ??1ExternalList@Object@@UEAA@XZ   retail 0x1800076c0
//
//   FUN_180009460(this + 0x20);        // Array<ExternalPtr<External>>::~Array
//   Object::External::~External(this); // tail call in retail
//
// The second line is the base destructor, which the chain runs for us. The
// first is the +0x20 array's teardown: `_resources`/`_count`/`_pad2c` are the
// three fields of a `Collection::Array<ExternalPtr<External>>` whose element is
// one bare non-owning pointer, so 0x180009460 is the trivial-element `~Array`
// — free the buffer, zero the header — with no per-element work at all
// (Object::ExternalPtr<T> is asserted trivially destructible in External.h, and
// External.h's own banner records that ownership lives in the factory, never in
// the pointer). Spelled out rather than reached through the container because
// the three fields are declared here as plain members: External.h left +0x2c
// UNPINNED, and declaring an `Array` to get a free would be asserting a
// capacity field no decompiled body touches.
ExternalList::~ExternalList() {
    if (_resources != nullptr)
        RtSystem::RtMemFree(_resources);
    _resources = nullptr;
    _count     = 0;
    _pad2c     = 0;
}

// slot 1 — ?TypeName@ExternalList@Object@@UEBAPEBDXZ   0x180007370  MOV RAX,[0x1801f9be8]
// slot 2 — ?Type@ExternalList@Object@@UEBAIXZ          0x180007350  MOV EAX,[0x1801f9be0]
// ?TypeId@ExternalList@Object@@SAIXZ                   0x180007360  the same load
// All three are DECLARE_TYPE's over the class record (ENDGAME B.3.9 OBJ W3).

// slot 3 — ?GetFileVersion@ExternalList@Object@@UEBAHXZ  0x1800bfa10
//          `MOV EAX,3; RET` — a literal of its own, not ElementDescription's.
int32_t ExternalList::GetFileVersion() const { return 3; }

// ============================================================================
// Object::ExternalList — slots 6-8 (ENDGAME B.3.9 OBJ: the class's own bodies;
// the extern "C" doors they forwarded to are retired with the minted table).
// Slot 5, Serialize, is in external_list.cpp with the factory pair.
// ============================================================================

// slot 6 — ?Reconstruct@ExternalList@Object@@UEAAPEAVExternal@2@XZ  0x180007570
//          `slot0(this, 0)` dispatched VIRTUALLY — destroy without freeing —
//          then the ctor body again, with NO null test.
External* ExternalList::Reconstruct() {
    this->~ExternalList();
    return ::new (this) ExternalList();
}

// slot 7 — ?Destruct@ExternalList@Object@@UEAAXXZ   0x180007640
//          `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void ExternalList::Destruct() { this->~ExternalList(); }

// slot 8 — ?Construct@ExternalList@Object@@UEAAPEAVExternal@2@XZ   0x1800075e0
//          ExternalList's OWN new virtual; External's table stops at eight.
//          The inlined constructor, i.e. placement construction (retail's
//          `TEST RCX,RCX` sat in the retired door; a virtual call always has a
//          `this`).
External* ExternalList::Construct() {
    return ::new (this) ExternalList();
}

// ?Find@ExternalList@Object@@QEBA?AV?$ExternalPtr@VExternal@Object@@@2@AEBVFilename@System@@VType@2@@Z
//   retail 0x1800bf700.  *** THE `type` PARAMETER IS NEVER READ *** — R9 is
//   dead in the whole body, so a caller that passes a type expecting it to
//   filter gets no filtering. The comparison is a LINEAR scan and is
//   case-SENSITIVE, where `ExternalFactory::Find` over the same kind of path is
//   case-INSENSITIVE; both are transcribed as found and must not be harmonised.
//
// HIDDEN RETURN SLOT: (this in RCX, ret in RDX, path in R8, type in R9); the
// slot is zeroed first and written again on a hit, as retail does.
//
// Two more things worth naming:
//   * The `":resource"` suffix convention again, but through the String&/String&
//     operator+ (0x1800c9440) rather than the char* overload Find uses.
//   * The comparison is length first (the `str-8` header), then a plain byte
//     strcmp.
// B.4 P2 wave X: the body of the retired `RtObject_ExternalList_Find`
// (external.cpp), moved here unchanged; `self->` became `this`.
ExternalPtr<External> ExternalList::Find(const System::Filename& path, uint32_t /*type — unused by retail*/) const {
    bool noIdentifier;
    {
        System::Filename ident = path.GetPathIdentifer();
        noIdentifier = (ident.GetLength() == 0);
    }   // retail releases `ident` here, before the key is built

    // `key` was a raw `char*` holding ONE reference that retail takes by hand
    // and gives back at the tail. That hand-rolled reference IS a String, and
    // the tail's `--refcount / MemFree` is its destructor.
    System::String key;
    if (noIdentifier) {
        // Set + AdjustDelimiters on the same object is the `const char*` ctor
        // (0x180026cf0). Retail builds a whole Filename for the literal so that
        // the concatenation goes through the String&/String& operator+
        // (0x1800c9440) rather than the char* overload.
        System::Filename suffix(":resource");
        key = path.AsString() + suffix.AsString();
    } else {
        key = path.AsString();
    }

    ExternalPtr<External> ret(nullptr);
    for (int i = 0; i < _count; ++i) {
        bool equal;
        {
            System::Filename got = _resources[i]->GetPath();   // 0x180006250

            const int keyLen = key.GetLength();
            const int gotLen = got.GetLength();

            if (keyLen != gotLen) {
                equal = false;
            } else if (keyLen == 0) {
                equal = true;                       // two empty paths match
            } else if (got._data == nullptr) {
                equal = false;                      // retail's `iVar8 = 1` arm
            } else {
                equal = (std::strcmp(key._data, got._data) == 0);
            }
        }   // `got` releases here, as retail does, before the hit is recorded

        if (equal) { ret = _resources[i]; break; }
    }

    // key's reference goes back in ~String.
    return ret;
}

}  // namespace Object
