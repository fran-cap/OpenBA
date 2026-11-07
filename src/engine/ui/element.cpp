// ============================================================================
// src/engine/ui/element.cpp — Wave 7 lane A — UI::Element + ContainerElement
// ============================================================================
//
// Ports 21 of the 23 `UI::Element` / `UI::ContainerElement` exports our tree
// calls (PLAN.md §4.2 wave 7, §6). The recovered layout and the evidence for
// every offset are in src/engine/ui/Element.h; the per-function write-up,
// the oracle argument for each row and everything this lane could NOT verify
// are in docs/history/runtime_port/WAVE7_LANE_A.md.
//
//   | Export (short form)             | Retail      | Ord  | Ported | Oracle  |
//   |---------------------------------|-------------|------|--------|---------|
//   | ??0Element (ctor)               | 0x180115e50 |  171 | yes    | build   |
//   | ??1Element (dtor)               | 0x1801161a0 |  584 | yes    | none    |
//     ^ Wave 9 lane UIF. `none` is the literal oracle disposition and is
//       argued for at the body; the "TWO HOLD-BACKS" section below is the
//       ORIGINAL Wave 7 text, kept because reason (b) still stands.
//   | ?AddChild@                      | 0x180115df0 | 1537 | yes    | shadow  |
//   | ?Child@                         | 0x180035540 | 1693 | yes    | replay  |
//   | ?ChildCount@                    | 0x180035530 | 1695 | yes    | replay  |
//   | ?ComputeLayout@                 | 0x1801168d0 | 1763 | yes    | thunk   |
//   | ?Draw@                          | 0x1801168f0 | 2243 | yes    | thunk   |
//   | ?FindElement@                   | 0x180115650 | 2347 | yes    | replay  |
//   | ?FindElementType@               | 0x180115760 | 2349 | yes    | replay  |
//   | ?GetAlignedPosition@ (member)   | 0x180115ff0 | 2442 | yes    | outputs |
//   | ?GetPaddedSize@                 | 0x180115f10 | 2830 | yes    | outputs |
//   | ?GetUserData@                   | 0x180115550 | 2997 | yes    | replay  |
//   | ?Hide@                          | 0x180035830 | 3056 | yes    | replay  |
//   | ?RemoveChildren@                | 0x180115570 | 3552 | yes    | none    |
//     ^ Wave 9 lane B3, in src/engine/ui/widgets_layout.cpp, NOT this file.
//   | ?SetElementId@                  | 0x180115820 | 3875 | yes    | replay  |
//   | ?SetFlagsOnOff@                 | 0x180116160 | 3883 | yes    | replay  |
//   | ?SetName@                       | 0x1801154f0 | 3927 | yes    | shadow  |
//   | ?SetNeedsSizeCalc@              | 0x180035470 | 3928 | yes    | replay  |
//   | ?SetPadding@                    | 0x180115480 | 3935 | yes    | replay  |
//   | ?SetToolTip@                    | 0x180035740 | 3989 | yes    | replay  |
//   | ?SetToolTipString@              | 0x1800356e0 | 3990 | yes    | shadow  |
//   | ?SetUserData@                   | 0x180115560 | 3998 | yes    | replay  |
//   | ?SetOffsets@ContainerElement@   | 0x1800cc1e0 | 3932 | yes    | shadow  |
//
// All 23 mangled names were checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with a pefile-style reader BEFORE any
// body was written (reference_resolveexport_phantom_names.md: a name the DLL
// does not contain resolves to NULL in silence, the port still registers, and
// the census still counts it). All 23 are present; the addresses and ordinals
// above are that query's output, not Ghidra's guess. The six Wave 7 phantoms
// are all in lanes B, C and D.
//
// ---------------------------------------------------------------------------
// THE VTABLE QUESTION, WHICH IS THE ONE THAT MATTERS FOR THIS CLASS
// ---------------------------------------------------------------------------
// `Element` is polymorphic with a deep derived hierarchy, so PLAN §4.3 risk 3
// ("port classes whole") is live here in a way it was not for Wave 6's
// non-polymorphic `Video::Context`. It is defused, not ignored:
//
//   * `??_7Element@UI@@6B@` is an EXPORTED data symbol (retail 0x180156160),
//     and the ported ctor stamps THAT address, resolved by name at run time,
//     rather than minting a table of its own. So a half-ported hierarchy never
//     puts two vtables in play: every object built by our ctor and every object
//     built by the DLL's ctor carry the identical pointer, and a virtual call
//     from either side lands on the same twelve slots.
//   * No method in this lane is registered as a vtable slot. Registering
//     redirects OUR direct callers only; the DLL's own internal near-CALLs and
//     its widgets' vtables still reach the DLL's bodies. That is fine here
//     because every ported body is a pure function of the object's fields.
//   * The two functions that would have made this dangerous — the dtor and
//     `RemoveChildren`, which DESTROY objects — are held back (see below).
//     WAVE 9: BOTH HAVE SINCE LANDED. `RemoveChildren` in lane B3, the dtor in
//     lane UIF. The section below is preserved as the Wave 7 record, and its
//     reason (b) for the dtor — "no oracle" — is still true of the port that
//     now exists; what changed is that a cutover with no DLL makes the
//     alternative a NULL CALL rather than a fallback. Read §0 of
//     docs/history/runtime_port/WAVE9_LANE_UIF.md before trusting reason (a).
//
// ---------------------------------------------------------------------------
// THE TWO HOLD-BACKS  (WAVE 7 RECORD — BOTH NOW LANDED, see the note above)
// ---------------------------------------------------------------------------
// `??1Element@UI@@UEAA@XZ` (0x1801161a0). Held back for two independent
// reasons, either of which is sufficient:
//   (a) NOTHING IN OUR TREE CALLS IT. `fn_Element_Dtor`
//       (engine/funcs/engine_funcs.h) is assigned and never invoked; a
//       grep across code/, engine/, standalone/, runtime/ and src/render/dx11/ finds the
//       declaration, the assignment and no call. A registered port on a path
//       nothing executes reports `calls=0 divergences=0` forever, which READS
//       as a pass (reference_uncalled_port_is_not_verified.md — a wrong
//       Quaternion::operator* survived all of Wave 1 exactly that way).
//   (b) It is the most destructive function in the class and it has no oracle:
//       it releases a System::String, destroys every child through vtable slot
//       0, destroys the border element, frees the child array through the
//       allocator, and unregisters the object's handle in
//       `Object::InternalFactory::_singleton`. Replaying any of that runs the
//       destruction twice.
//
// `?RemoveChildren@Element@UI@@QEAAXXZ` (0x180115570). Fully reversed — the
// body is transcribed in WAVE7_LANE_A.md §4 for whoever lands it — and held
// back because it calls each child's vtable slot 0 as `fn(child, 1)`, the
// SCALAR DELETING destructor. That frees the child. There is no oracle for it:
// a replay destroys every child a second time, and a shadow predicate can only
// observe that the array ended up empty, which is true whether or not the
// children were correctly destroyed. Its live caller is `fn_Element_RemoveChildren`
// (nine sites under src/game/Dialog/), so this is a real, deliberate cost:
// nine call sites keep going to the DLL. An unverified port that frees objects
// is a worse outcome than a hold-back (WAVE2 allocator verdict).
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

#include "system/String.h"
#include "ui/Element.h"
#include "ui/ContainerElement.h"   // fragment pass F2: SetOffsets
#include "ui/ClipRegionElement.h"  // B.3.9 TI-UI: the two type ids SetOffsets
#include "ui/ScrollElement.h"      // reads (retail `MOV EDX,[0x1801f9d68]`)
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
// `enum class Module` + `ResolveAddr` (declared once there, byte-identical to
// standalone/resolve.h) and `rtuielem::AtomicExport`, the B6-5 door cache.
#include "element_common.h"

// ENDGAME B.3.6 block A: `UI::Element` is the REAL class now (Element.h), so
// every door below receives a typed `Element*` and delegates with a QUALIFIED
// non-virtual call (`self->UI::Element::M(…)`): a live Element wears the MINTED
// table, whose slots resolve back to these doors, so an unqualified call from a
// door into a virtual would re-enter the door (B34_BRIEFS A.1).
using UI::Element;
using ::UIString;   // src/engine/ui/String.h

namespace {

constexpr size_t kElementSize = sizeof(Element);   // 0xC0, static_assert'd in the header

// ============================================================================
// Dependencies this lane CALLS rather than reimplements.
//
// Two different resolution rules, on purpose (Wave 4 lane C's rule, restated in
// achievement_manager.cpp:80):
//
//  * ResolveExport(name) — "whatever is registered". Used where retail called
//    another engine function that a sibling wave may have ported: our body
//    should then use the port, exactly as every other one of our call sites
//    does. That is what Wave 6 lane A did with Math::Matrix::Inverse.
//  * ResolveAddr(Module::Runtime, addr) — "the address door". Used for the
//    Collection::Array helpers and the other unexported internals, which are
//    header-only template instantiations with NO exported symbol to ask for.
//
//    WAVE 9 LANE UIF CORRECTION: this bullet used to read "always the DLL",
//    and that has been FALSE for the whole runtime port. Read the
//    implementation — standalone/resolve_standalone.cpp:471 — `ResolveAddr`
//    consults `RtAddrReg()` BEFORE it touches the module handle and before it
//    applies the ASLR delta, so an address another lane has registered comes
//    back as OUR code (reference_resolveaddr_consults_port_registry_first.md,
//    and lane K's src/engine/object/deleting_dtor.cpp says the same thing in
//    its own words). The correction matters here rather than being pedantic:
//    `??1Element` below reaches RemoveChildren, ~Array and ~Internal through
//    this door precisely BECAUSE it lands on the ports, which is what lets the
//    destructor close its cutover row instead of moving it one frame deeper.
//    Believing the old sentence would have made that port look useless.
//
// Every one of these is looked up ONCE and cached. ResolveExport
// are GetProcAddress and cost like a syscall — two of them in the body of
// System::Time cost 5.6 points of sim time in Wave 2 and were invisible to both
// profilers (reference_rtoriginal_is_getprocaddress.md).
// ============================================================================

// B3R RT-UI: `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) and
// `?IsDerivedFrom@Base@Object@@QEBA_NI@Z` (0x1800c0390) are called directly.
// NOTE for whoever reads a crash log: 0x1800c0390 is the function
// src/app/main.cpp's crash handler recognises by the offset +0xC0390,
// because retail calls it on a possibly-null child (see SetOffsets).

// ENDGAME B.3.2 step 2 — the 0x180076900 forwarder that used to sit here is
// GONE. `Element::_children` is a real `Collection::Array<Element*>` member
// (Element.h, +0xa0), and 0x180076900 is `kSpec_Fill8Z`, trait-equivalent for
// `Element*` (dialog_create.cpp's banner has the full spec comparison), so
// `AddChild` below calls `.SetSizeFill()` on the member directly.

// Video::Device::_singleton (exported, 0x1801fe7a8). GetPaddedSize reads the
// screen size out of the pointed-to Device at +0x04 / +0x08. ENDGAME B.3.9
// SS1 (B39_RULES D7): the class static now, read fresh (B9-10).
const ::Video::Device* VideoDeviceSingleton() {
    return ::Video::Device::_singleton;
}

} // namespace

// ============================================================================
// The member bodies (ENDGAME B.3.6 block A)
// ============================================================================
// Each body below MOVED here from its registered door (`self->x` became `x`),
// and the door, further down, is now a one-line QUALIFIED delegation. Virtual
// calls onto OTHER elements (the border's slot 5/6, a child's slot 2/4) are
// plain virtual calls: they dispatch through the RECEIVER's table — minted or
// GCC's, slot for slot the same — exactly where the old `VSlot<Fn>(p, N)` read.
namespace UI {

// ---- ?SetNeedsSizeCalc@  0x180035470 ---------------------------------------
// The tail every mutator in this class shares. Note the shape: the OR happens
// BEFORE the null test, so it always runs at least once on `this` — a
// do/while, not a while.
void Element::SetNeedsSizeCalc() {
    Element* e = this;
    do {
        e->_flags |= kElementFlagNeedsSizeCalc;
        e = e->_parent;
    } while (e != nullptr);
}

}  // namespace UI

// ============================================================================
// The ports
// ============================================================================
// Every body is byte-faithful to the retail disassembly — read as
// DISASSEMBLY, not as Ghidra's C, because that C drops dereferences and prints
// MOVSXD as a zero extension (reference_ghidra_c_output_vs_disassembly.md; three
// Wave 6 defects, one a crash, came from trusting it). Where retail derefs
// without a null check, so do we: CLAUDE.md's rule is that retail's unguarded
// derefs are load-bearing, and a guard skips the behaviour rather than
// restoring it.

// ---- ??0Element@UI@@QEAA@XZ  0x180115e50 -----------------------------------
// ENDGAME B.3.9 UI-D: the ctor door `RtUi_Element_Ctor` and its build
// oracle are retired with the minted table. The real constructor
// (Element.h) runs ??0Internal through the compiler's chain and stamps the
// class's own table; its body is `InitFields` below. +0x54, +0x60, +0x64,
// +0x7c and +0x8c stay unwritten, as retail leaves them.

// ============================================================================
// Member bodies, moved from their doors by ENDGAME B.3.6 block A
// ============================================================================
namespace UI {

// ---- ?AddChild@  0x180115df0 -----------------------------------------------
// `child` is an `Object::InternalPtr<Element>` passed BY VALUE, which MSVC
// hands over as a pointer to the caller's 8-byte temporary (the disassembly's
// first act is `MOV RAX,[RDX]`, then `MOV R8,RDX`): RULE B6-11, so the member
// takes that pointer (B.3.6 closing pass; it took the handle by value before)
// and passes it on as SetSizeFill's fill argument exactly as retail does.
//
// retail 0x180076900 — kSpec_Fill8Z, trait-equivalent for `Element*` (see
// dialog_create.cpp's banner): `_children` is the trait-derived
// `Array<Element*>` instantiation already. The fill argument is the address of
// an 8-byte handle holding the child pointer — the same bytes retail hands it.
void Element::AddChild(Object::InternalPtr<Element>* child) {
    Element* const c = child->Get();
    if (c) c->_parent = this;
    _children.SetSizeFill(_children.GetSize() + 1, child);
    SetNeedsSizeCalc();
}

// ---- ?Child@  0x180035540 --------------------------------------------------
// NO BOUNDS CHECK, and the index is SIGN-extended (MOVSXD RCX,R8D) — a
// negative index reads before the buffer, exactly as retail does.
Object::InternalPtr<Element> Element::Child(int32_t index) {
    return Object::InternalPtr<Element>(_children.GetData()[static_cast<intptr_t>(index)]);
}

// ---- ?ChildCount@  0x180035530  (`MOV EAX,[RCX+0xa8]`) ---------------------
int32_t Element::ChildCount() const {
    return _children.GetSize();
}

// ---- ?ComputeLayout@  0x1801168d0  (vtable slot 5) -------------------------
// A tail-jump into the BORDER element's slot 5. `Element` itself lays nothing
// out; every widget that has geometry overrides this. The border call is a
// VIRTUAL call on the border — its own table's slot 5, which is what retail's
// `JMP [RAX+0x28]` reads.
void Element::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    Element* border = _border;
    if (!border) return;
    border->ComputeLayout(x, y, w, h);
}

// ---- ?Draw@  0x1801168f0  (vtable slot 6, const) ---------------------------
void Element::Draw(SpriteContext& context, uint8_t alpha) const {
    Element* border = _border;
    if (!border) return;
    border->Draw(context, alpha);
}

// ---- ?FindElement@  0x180115650 --------------------------------------------
// Depth-first search by name, self first. `name` is a `const System::String&`
// whose one field is the character body; the length lives at body[-8] and the
// refcount at body[-4] (src/engine/system/String.h).
//
// Two faithfulness details that a tidied version would drop:
//   * An EMPTY search string never matches anything, not even an unnamed
//     element: the `lq == 0` test jumps straight to the child scan.
//   * Two elements with equal-length names are compared with StrCompare only
//     when THIS element's name is non-null and non-empty; a null name of
//     length 0 against a length-0 query would have matched, but the query
//     length test above has already excluded that case.
// The recursion is a direct call of this member on each non-null child.
Object::InternalPtr<Element> Element::FindElement(const System::String& name) {
    const char* q = name._data;
    if (q && RtSystem::StringLength(q) != 0) {
        const char* mine = _name;
        const int32_t lm = mine ? RtSystem::StringLength(mine) : 0;
        if (lm == RtSystem::StringLength(q)) {
            bool equal;
            if (!mine || RtSystem::StringLength(mine) == 0) {
                equal = true;
            } else {
                equal = System::String::StrCompare(mine, q) == 0;
            }
            if (equal) return Object::InternalPtr<Element>(this);
        }
    }
    for (int i = 0; i < _children.GetSize(); ++i) {
        Element* c = _children.GetData()[i];
        if (!c) continue;
        Object::InternalPtr<Element> found = c->FindElement(name);
        if (found) return found;
    }
    return Object::InternalPtr<Element>();
}

// ---- ?FindElementType@  0x180115760 ----------------------------------------
// Same shape, keyed on the virtual `Type()` (slot 2) rather than the name.
// EXACT equality, not IsDerivedFrom — a subclass of the requested type is NOT
// a hit. `Type()` is a VIRTUAL call: the dynamic type is the point.
Object::InternalPtr<Element> Element::FindElementType(uint32_t typeId) {
    if (Type() == typeId) return Object::InternalPtr<Element>(this);
    for (int i = 0; i < _children.GetSize(); ++i) {
        Element* c = _children.GetData()[i];
        if (!c) continue;
        Object::InternalPtr<Element> found = c->FindElementType(typeId);
        if (found) return found;
    }
    return Object::InternalPtr<Element>();
}

// ---- ?GetAlignedPosition@ (STATIC, 17 args)  0x1801158e0 -------------------
//
// A pure integer leaf: no calls, no allocation, no globals, four out
// parameters. It places a box of size (outW x outH) inside a container of size
// (containerW x containerH), honouring four pads and a 3x3 alignment, and
// optionally STRETCHES the box to the available space first.
//
// THE ALIGNMENT ENUM IS A ROW-MAJOR 3x3, read off the jump table at
// 0x180115a14 rather than guessed. The X axis switches on `alignment - 1` with
// `JA -> default` for anything above 7, and the eight table entries cycle
// center / right / left; the Y axis is a compare chain (<3 top, 3..5 middle,
// 6..8 bottom, >8 top):
//
//        0 TopLeft      1 TopCenter      2 TopRight
//        3 MiddleLeft   4 MiddleCenter   5 MiddleRight
//        6 BottomLeft   7 BottomCenter   8 BottomRight
//
// and ANY value outside 0..8 lands on left/top.
//
// TWO FAITHFULNESS TRAPS, both of which a tidied port gets wrong:
//  * The centring halves are `SAR reg,1` -- an ARITHMETIC SHIFT, which rounds
//    toward negative infinity. `/ 2` rounds toward zero. `>>` on a signed int is
//    the faithful spelling (GCC guarantees arithmetic shift).
//  * `outW` / `outH` are IN/OUT: read as the box's size, possibly OVERWRITTEN
//    by the stretch, then read AGAIN by the placement. The order is load-bearing.
//
// (B.3.6: the `useDivide` negative-control parameter the old free function
// carried is gone with it; its only user, element_aligned_probe.cpp, no longer
// exists in the tree.)
void Element::GetAlignedPosition(ElementDescription::Alignment alignment,
                                 int32_t x, int32_t y,
                                 int32_t containerW, int32_t containerH,
                                 int32_t offsetX, int32_t offsetY,
                                 int32_t leftPad, int32_t rightPad,
                                 int32_t topPad, int32_t bottomPad,
                                 int32_t& outX, int32_t& outY,
                                 int32_t& outW, int32_t& outH,
                                 bool stretchX, bool stretchY) {
    const int32_t align = static_cast<int32_t>(alignment);
    // Cursor = origin + offset, then pulled back inside the container if the
    // box would overhang. Retail compares against the CURRENT outW/outH.
    int curX = x + offsetX;
    int curY = y + offsetY;

    if (containerW < outW + curX) curX = containerW - outW;
    if (containerH < outH + curY) curY = containerH - outH;

    curX += leftPad;
    curY += topPad;

    // Space left between the cursor and the far pad.
    const int availW = (containerW - rightPad)  - curX;
    const int availH = (containerH - bottomPad) - curY;

    // The stretch, BEFORE the placement reads the size back.
    if (stretchX) outW = availW;
    if (stretchY) outH = availH;

    // ---- X: center for 1/4/7, right for 2/5/8, left otherwise -------------
    switch (align) {
    case ElementDescription::TopCenter:
    case ElementDescription::MidCenter:
    case ElementDescription::BottomCenter:
        outX = ((availW >> 1) - (outW >> 1)) + curX;
        break;
    case ElementDescription::TopRight:
    case ElementDescription::MidRight:
    case ElementDescription::BottomRight:
        outX = (containerW - rightPad) - outW;
        break;
    default:
        outX = curX;
        break;
    }

    // ---- Y: middle for 3..5, bottom for 6..8, top otherwise ---------------
    if (align >= ElementDescription::MidLeft && align <= ElementDescription::MidRight) {
        outY = ((availH >> 1) - (outH >> 1)) + curY;
    } else if (align >= ElementDescription::BottomLeft && align <= ElementDescription::BottomRight) {
        outY = (containerH - bottomPad) - outH;
    } else {
        outY = curY;
    }
}

// ---- ?GetAlignedPosition@ (member)  0x180115ff0 ----------------------------
// Twelve parameters after `this`: six ints, four int& outputs, two bools. The
// body recomputes the element's size through virtual slot 4, publishes it into
// the third and fourth outputs, then forwards everything to the STATIC
// 17-argument overload (0x1801158e0) — now a direct call of the static member.
//
// The two bools are each OR'd with a sticky flag bit before being forwarded:
// `stretchX` with bit 2 and `stretchY` with bit 3. The LAST parameter (+0x100
// in the frame) pairs with flag 0x8 and lands in the LAST outgoing slot; the
// eleventh (+0xf8) pairs with flag 0x4 and lands one slot earlier.
void Element::GetAlignedPosition(int32_t a, int32_t b, int32_t c, int32_t d,
                                 int32_t e, int32_t f,
                                 int32_t& outX, int32_t& outY,
                                 int32_t& outW, int32_t& outH,
                                 bool stretchX, bool stretchY) {
    ComputeSize();                                  // slot 4, virtual

    outW = _layoutWidth;
    outH = _layoutHeight;

    const bool bx = stretchX || (_flags & kElementFlagAlignArg1) != 0;
    const bool by = stretchY || (_flags & kElementFlagAlignArg2) != 0;

    GetAlignedPosition(static_cast<ElementDescription::Alignment>(_alignment),
                       a, b, c, d, e, f,
                       _leftPad, _rightPad, _topPad, _bottomPad,
                       outX, outY, outW, outH, bx, by);
}

// ---- ?GetPaddedSize@  0x180115f10 ------------------------------------------
// Size + padding, floored by the minimum size, then optionally REPLACED
// outright by a fraction of the screen when flag bits 4 / 5 are set.
//
// Faithfulness details, all of them observable:
//   * The additions are `layout + right + left` and `layout + bottom + top`,
//     in that order, in 32-bit — an overflowing pad wraps the same way.
//   * The minimum-size floor is applied to the PADDED value.
//   * The screen-size override walks to the ROOT element and reads the root's
//     `_dialogParent`, not this element's.
//   * The second flag test RE-READS `_flags` from memory (retail keeps
//     `LEA RDX,[RBX+0x34]` alive across the root walk, which clobbers RBX).
//   * Both conversions are `CVTTSS2SI` — truncation toward zero, not rounding.
void Element::GetPaddedSize(int32_t& outW, int32_t& outH) {
    ComputeSize();                                  // slot 4, virtual

    outW = _layoutWidth  + _rightPad  + _leftPad;
    outH = _layoutHeight + _bottomPad + _topPad;
    if (outW < _minWidth)  outW = _minWidth;
    if (outH < _minHeight) outH = _minHeight;

    const uint32_t flags = _flags;
    if (flags & (kElementFlagSizeToScreenH | kElementFlagSizeToScreenW)) {
        Element* root = this;
        while (root->_parent != nullptr) root = root->_parent;
        const DialogInstance* dialogParent = root->_dialogParent;
        const ::Video::Device* device = VideoDeviceSingleton();

        if (flags & kElementFlagSizeToScreenW) {
            const int32_t screenW = device->backBufferWidth;            // Device +0x04
            const float scale = dialogParent->_ui->_uiScale;            // +0x10, UI +0xdc
            outW = static_cast<int>(static_cast<float>(screenW) * scale);
        }
        // RE-READ, not the cached `flags` — see the note above.
        if (_flags & kElementFlagSizeToScreenH) {
            const int32_t screenH = device->backBufferHeight;           // Device +0x08
            const float scale = dialogParent->_ui->_uiScale;            // +0x10, UI +0xdc
            outH = static_cast<int>(static_cast<float>(screenH) * scale);
        }
    }
}

// ---- ?GetUserData@  0x180115550 --------------------------------------------
Object::Base* Element::GetUserData() {
    return _userData;
}

// ---- ?Hide@  0x180035830 ---------------------------------------------------
// Sets or clears flag bit 0 (which means HIDDEN — `?IsVisible@` is its
// negation) and runs the dirty walk only if the bit actually changed. Retail
// builds this branchlessly out of SETZ/SETNZ/NOT; the mask is
// `hide ? 0xFFFFFFFF : ~1`, so the AND is a no-op on the hide path.
void Element::Hide(bool hide) {
    const uint32_t old = _flags;
    const uint32_t setMask   = hide ? uint32_t{kElementFlagHidden} : 0u;
    const uint32_t clearMask = hide ? 0xFFFFFFFFu : ~uint32_t{kElementFlagHidden};
    const uint32_t next = (old | setMask) & clearMask;
    _flags = next;
    if ((next ^ old) & kElementFlagHidden) SetNeedsSizeCalc();
}

// ---- ?SetElementId@  0x180115820 -------------------------------------------
void Element::SetElementId(int32_t id) {
    _elementId = id;
}

// ---- ?SetFlagsOnOff@  0x180116160 ------------------------------------------
// `flags = (flags | on) & ~off`, then the dirty walk if BIT 0 changed — only
// bit 0, not any other bit. (setFlags, clearFlags), not "hide, show".
void Element::SetFlagsOnOff(uint32_t on, uint32_t off) {
    const uint32_t old = _flags;
    const uint32_t next = (old | on) & ~off;
    _flags = next;
    if ((next ^ old) & kElementFlagHidden) SetNeedsSizeCalc();
}

}  // namespace UI

namespace {
// The System::String release/acquire SetName and SetToolTipString share, on
// the field each passes. The release path is retail's, including the detail
// that the FIELD is used as the scratch for the pointer arithmetic:
// `field -= 8` and then `MemFree(field)`, which leaves a freed pointer in the
// field for the two instructions before it is overwritten. Reproduced.
void AssignStringField(char*& field, char* incoming) {
    char* const previous = field;
    if (previous != incoming) {
        if (previous) {
            if (--RtSystem::StringRefCount(previous) == 0) {
                field = previous - 8;
                System::MemoryAllocator::MemFree(field);
            } else {
                field = nullptr;
            }
        }
        field = incoming;
        if (incoming) ++RtSystem::StringRefCount(incoming);
    }
}
}  // namespace

namespace UI {

// ---- ?SetName@  0x1801154f0 ------------------------------------------------
// System::String release/acquire on +0x10.
void Element::SetName(const System::String& name) {
    AssignStringField(_name, name._data);
}

// ---- ?SetPadding@  0x180115480 ---------------------------------------------
// *** THE PARAMETER ORDER IS (left, RIGHT, TOP, bottom). ***
// Retail stores EDX->+0x18, R8D->+0x1c, R9D->+0x20, [RSP+0x28]->+0x24, and the
// one-line accessors pin which offset is which. SetPadding does NOT run the
// dirty walk (SetMinSize does); the walk is simply not in the shipped body.
void Element::SetPadding(int32_t left, int32_t right, int32_t top, int32_t bottom) {
    _leftPad   = left;
    _rightPad  = right;
    _bottomPad = bottom;   // retail writes +0x24 BEFORE +0x20; same bytes
    _topPad    = top;
}

// ---- ?SetToolTip@  0x180035740 ---------------------------------------------
// `tip` is a `UI::String` BY VALUE — 16 bytes, NOT refcounted, so this is a
// plain 16-byte copy.
//   * The `_toolTipDialog != 0` test is evaluated FIRST, before the copy.
//   * The second store is a full QWORD at +0x50, so it also writes the 4 bytes
//     at +0x54 that the ctor leaves alone. *** DO NOT "FIX" THIS COPY TO 0x0C
//     BYTES ***: `MOV RAX,[RDX+0x8] ; MOV [RCX+0x50],RAX` (0x180035748..50).
//   * Both branches end with `if ((flags ^ old) & 1) walk`, but neither
//     `|= 0x90000` nor `&= ~0x80000` can change bit 0, so that walk is
//     UNREACHABLE in the shipped code. Kept, because it is in the shipped code.
void Element::SetToolTip(::UIString tip) {
    const bool hadDialog = (_toolTipDialog != nullptr);
    std::memcpy(&_toolTip, &tip, sizeof(::UIString));

    const uint32_t old = _flags;
    uint32_t next;
    if (hadDialog && _toolTip.table != nullptr && _toolTip.index >= 0 &&
        (old & kElementFlagDisabled) == 0) {
        next = old | (kElementFlagRaisesEvents | kElementFlagToolTipValid);
    } else {
        next = old & ~static_cast<uint32_t>(kElementFlagToolTipValid);
    }
    _flags = next;
    if ((next ^ old) & kElementFlagHidden) SetNeedsSizeCalc();
}

// ---- ?SetToolTipString@  0x1800356e0 ---------------------------------------
// Byte-for-byte the same release/acquire as SetName, on +0x58 instead of +0x10.
void Element::SetToolTipString(const System::String& text) {
    AssignStringField(_toolTipString, text._data);
}

// ---- ?SetUserData@  0x180115560 --------------------------------------------
void Element::SetUserData(Object::Base* userData) {
    _userData = userData;
}

// ---- ??0Element@UI@@QEAA@XZ 0x180115e50 MINUS ??0Internal and the vptr store
// The body of `Element::Element()` (Element.h); moved verbatim from the
// retired door (`self->x` -> `x`). +0x54, +0x60, +0x64, +0x7c, +0x8c stay
// unwritten.
void Element::InitFields() {
    _name             = nullptr;
    _leftPad          = 0;
    _rightPad         = 0;
    _topPad           = 0;
    _bottomPad        = 0;
    _minWidth         = 0;
    _minHeight        = 0;
    _alignment        = ElementDescription::MidLeft;        // literal 3 at 0x180115e88
    _flags            = UI::kElementFlagNeedsSizeCalc;      // 0x08000000
    _color            = System::kColorWhite;
    _mouseCursorIndex = 0;
    _toolTipDialog    = nullptr;
    _toolTip.table    = nullptr;
    _toolTip.index    = -1;                                 // +0x54 deliberately untouched
    _toolTipString    = nullptr;
    _layoutLeft       = 0;
    _layoutTop        = 0;
    _layoutRight      = 0;
    _layoutBottom     = 0;
    _unknown78        = -1;                                 // +0x7c deliberately untouched
    _userData         = nullptr;
    _elementId        = -1;                                 // +0x8c deliberately untouched
    _dialogParent     = nullptr;
    _parent           = nullptr;
    _children._data   = nullptr;
    _children._size   = 0;
    _children._capacity = 0;
    _border           = nullptr;
    _description      = nullptr;
}

// ---- ??1Element@UI@@UEAA@XZ 0x1801161a0 MINUS the restamp and the ~Internal tail
// The body of `Element::~Element()` (Element.h); moved verbatim from the
// retired door `RtUi_Element_Dtor` (`self->x` -> `x`); the evidence is the
// banner that stands where that door was.
void Element::ReleaseFields() {

    // 0x180115570 RemoveChildren — a member since fragment pass F1 (body in
    // widgets_layout.cpp); called as one instead of by ADDRESS.
    UI::Element::RemoveChildren();

    // The border element, destroyed through ITS OWN slot 0 with flag 1 —
    // scalar delete. Not `??1Element`: slot 0 is `??_E`, and picking the wrong
    // one of the three spellings resolves null behind a guard
    // (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md).
    if (_border != nullptr) {
        Element* border = _border;
        if (border != nullptr)             // retail's second, redundant test
            delete border;
        _border = nullptr;
    }

    // ENDGAME B.3.2 step 2 — the 0x180037240 forwarder is GONE. `_children` is
    // the real `Collection::Array<Element*>` member, and 0x180037240 is a J1
    // `~Array` (destroy == null): trait-equivalent, since `Element*` is
    // trivially destructible. `.~Array()` IS that instantiation.
    // In the member it is ~Array's own body (`ClearCallback`) rather than an
    // explicit `~Array()` call: the real destructor chain runs ~Array again
    // after this body, a no-op on the header this zeroes, and ending the
    // member's lifetime twice would be undefined.
    _children.DestroyAndFree();

    // The two System::String releases, in retail's order (+0x58 then +0x10) and
    // with retail's asymmetry: on the FREEING branch the field is left holding
    // `data - 8`, a dangling pointer; only the SHARING branch nulls it. Both
    // reproduced — see src/engine/system/String.h, "RETAIL'S RELEASE IDIOM,
    // AND WHY IT MUST BE COPIED EXACTLY".
    if (char* p = _toolTipString) {
        if (--RtSystem::StringRefCount(p) == 0) {
            _toolTipString = p - 8;
            System::MemoryAllocator::MemFree(_toolTipString);
        } else {
            _toolTipString = nullptr;
        }
    }
    if (char* p = _name) {
        if (--RtSystem::StringRefCount(p) == 0) {
            _name = p - 8;
            System::MemoryAllocator::MemFree(_name);
        } else {
            _name = nullptr;
        }
    }
}


// ---- ?SetOffsets@ContainerElement@UI@@QEAAXHH@Z  0x1800cc1e0 ---------------
// ENDGAME B.3.6 fragment pass F2: block C4's class, its body in block A's file
// (the evidence, the UNGUARDED first write and the re-load of `_children` per
// read are documented at the door `RtUi_ContainerElement_SetOffsets` below).
void ContainerElement::SetOffsets(int32_t x, int32_t y) {

    Element* child0 = _children.GetData()[0];
    ClipRegionElement* clip = child0->IsDerivedFrom(UI::ClipRegionElement::TypeId())
                                  ? static_cast<ClipRegionElement*>(child0) : nullptr;
    clip->_offsetX = x;                                     // +0xd0
    clip->_offsetY = y;                                     // +0xd4

    Element* child1 = _children.GetData()[1];
    ScrollElement* vscroll = nullptr;
    if (child1 && child1->IsDerivedFrom(UI::ScrollElement::TypeId()))
        vscroll = static_cast<ScrollElement*>(child1);

    Element* child2 = _children.GetData()[2];
    ScrollElement* hscroll = nullptr;
    if (child2 && child2->IsDerivedFrom(UI::ScrollElement::TypeId()))
        hscroll = static_cast<ScrollElement*>(child2);

    if (vscroll) {
        vscroll->_currentValue = static_cast<float>(y);     // +0x160
    }
    if (hscroll) {
        hscroll->_currentValue = static_cast<float>(x);     // +0x160
    }

}

}  // namespace UI

// ============================================================================
// The DOORS. Registered names and addresses unchanged; each is now a one-line
// QUALIFIED delegation. The retslot ones keep retail's `(this, ret, …)` shape
// and placement-new the member's prvalue into the caller's slot (B5-12).
// ============================================================================

// (ENDGAME B.3.9 UI-D: the ComputeLayout / Draw slot doors are retired with
// the minted table.)

// ---- ?SetOffsets@ContainerElement@UI@@QEAAXHH@Z  0x1800cc1e0 ---------------

// ============================================================================
// Registration
// ============================================================================
// Each ported function is registered TWICE, under its mangled name and under
// its retail address, even though today every live call site in our tree uses
// the name form (verified by grep across code/, engine/, standalone/, runtime/
// and src/render/dx11/: all of them go through engine/funcs/engine_funcs.cpp or
// standalone/address_registry.cpp, both of which call ResolveExport). The
// address registration costs nothing and closes the Wave 5 lane D failure mode
// where one live object is split between two implementations because a later
// call site used the other door.
//
// The four exe PE-import slots that carry lane-A methods —
// 0x1401518d8 (ctor), 0x1401518e0 (dtor), 0x1401518e8 (ComputeLayout),
// 0x1401518f0 (GetAlignedPosition member) and 0x1401518f8 (Draw) — are filled
// by `RegisterDispatchSlot(slot, ResolveExport(name))` in
// standalone/address_registry.cpp, and `AddressRegistryInit()` runs AFTER
// `RuntimePortInit()` (src/app/main.cpp:1614 then :1626). So those slots
// pick this port up too, without this file touching them. CHECKED, because a
// slot filled BEFORE the port registered would have kept the DLL pointer and
// quietly halved the lane. (WAVE 9 LANE UIF: this used to end "0x1401518e0
// keeps the DLL's dtor, which is correct: the dtor is held back." The dtor is
// no longer held back, so that slot now picks up `RtUi_Element_Dtor` by the
// same after-the-fact ResolveExport the other four use — no change needed
// here, but the sentence had to go rather than stay and be wrong.)
//
// src/engine/ui/rt_ui_port.cpp before this function is called.
// ============================================================================

// ============================================================================
// ??1Element@UI@@UEAA@XZ  retail 0x1801161a0  ord 584   (WAVE 9 LANE UIF)
// ============================================================================
//
// WHY THE WAVE 7 HOLD-BACK NO LONGER STANDS. It rested on two reasons, and
// BOTH were re-read rather than assumed (reference_holdback_blockers_are_stale
// _check_them_first.md — three stated blockers in one session turned out to be
// facts the tree already had):
//
//   (a) "NOTHING IN OUR TREE CALLS IT." That was true of DIRECT callers and is
//       still true of them. It is no longer the whole picture, because it was
//       a statement about our C++ call graph and the thing that reaches this
//       function is a VTABLE SLOT. `??_7Element@UI@@6B@` slot 0 is not `??1`
//       and never was — it is the VECTOR deleting destructor
//       `??_EElement@UI@@UEAAPEAXI@Z` at 0x180035c10 — and Wave 9 lane K
//       ported all 37 of those slot-0 bodies. 0x180035c10 was disassembled for
//       this lane: it reaches `??1Element` at 0x1801161a0 on BOTH branches
//       (`LEA R9,[0x1801161a0]` into the array iterator at 0x180035c28, and
//       `CALL 0x1801161a0` in the scalar branch at 0x180035c61). Lane K calls
//       `~Class` by ADDRESS through ResolveAddr, which consults the port
//       registry first, so the registration below is what makes that edge land
//       in our code. Until it did, lane K's ported slot 0 called into the DLL —
//       and with the DLL absent, into NULL. That is the shape the brief warned
//       about: a null reached through a LIVE MINTED VTABLE, faulting far from
//       its cause.
//
//   (b) "It is the most destructive function in the class and it has no
//       oracle." Still exactly true, and nothing below pretends otherwise —
//       see THE ORACLE QUESTION. What changed is only that (a) turned this
//       from an optional port into a cutover blocker, so the choice is no
//       longer "unverified port vs. hold-back" but "unverified port vs. a null
//       call". The Wave 2 allocator verdict that an unverified destroying port
//       is worse than a hold-back assumed a working DLL underneath it. There
//       is none after cutover.
//
// THE BODY, read from the disassembly at 0x1801161a0 and not from Ghidra's C
// (reference_ghidra_c_output_vs_disassembly.md):
//
//     this->_vtable = &??_7Element@UI@@6B@;         // 0x180156160
//     RemoveChildren(this);                         // 0x180115570
//     if (this->_border != 0 && this->_border != 0) // yes, retail tests twice
//         this->_border->vtbl[0](this->_border, 1); //   scalar deleting dtor
//         this->_border = nullptr;
//     ~Array<Element*>(&this->_children);           // 0x180037240, this+0xa0
//     <release this->_toolTipString>                // +0x58
//     <release this->_name>                         // +0x10
//     JMP ~Internal(this);                          // 0x1800bfdb0, TAIL call
//
// THE DOUBLED NULL TEST ON `_border` IS REPRODUCED. Retail emits both
// `CMP qword ptr [RBX+0xb0],0x0` (0x1801161c5) and `TEST RCX,RCX` (0x1801161d6)
// on the same loaded value. It is redundant and it is kept: this is a
// byte-faithful port and "tidying" a test out of a destructor is how a
// difference gets introduced that nothing then looks for.
//
// EVERY CALLEE IS SATISFIED ON OUR SIDE — which is the whole admission test
// (a port only CLOSES a cutover row if it does not merely move it one frame
// deeper). Checked one at a time, not assumed:
//   * 0x180115570 RemoveChildren  — PORTED, Wave 9 lane B3, widgets_layout.cpp
//   * slot 0 on `_border`         — PORTED, Wave 9 lane K's deleting-dtor family
//   * 0x180037240 ~Array<Element*> — PORTED BY THIS LANE as the twelfth address
//                                    of lane Q's J1 family; see the byte-level
//                                    argument in src/engine/collection/array.cpp
//   * 0x1800c7ad0 MemFree         — PORTED, lane Q
//   * 0x1800bfdb0 ~Internal       — PORTED, Wave 3, src/engine/object/internal.cpp
// So this row closes rather than relocating.
//
// THE TAIL CALL TO ~Internal IS LOAD-BEARING AND IS NOT OPTIONAL. 0x1800bfdb0
// is the InternalFactory swap-remove that
// reference_internalfactory_swapremove_wild_write.md is about: an Element freed
// without it leaves a stale handle-table entry that a later swap-remove writes
// through. Reached here by ADDRESS, exactly where retail reaches it, so
// whichever variant internal.cpp has live (plain or shadowed) is what runs.
//
// THE ORACLE QUESTION — THERE IS NO ORACLE, AND THIS PORT SHIPS WITHOUT ONE.
// Stated plainly because a quiet site reads as a pass
// (reference_uncalled_port_is_not_verified.md). A replay here would not be a
// comparison but a SECOND DESTRUCTION: it would destroy every child again
// through slot 0, decrement two String refcounts our body may already have
// taken to zero and FREED, free the child array again, and run the handle
// swap-remove twice on a handle that no longer exists. Any one of those is
// heap corruption; the last is this project's worst failure family. So this is
// a NO-ORACLE PORT in the exact disposition Wave 9 lane M established and lane
// Z reused four rows over in src/engine/ui/userinterface.cpp: a real body,
// registered, with NO call into the shipped body at all.
//
// Nothing in this file is evidence that the body is right; the evidence is the
// disassembly transcribed above.
// ENDGAME B.3.9 UI-D: the door `RtUi_Element_Dtor` that stood here is
// retired with the minted table and its RT_DELDTOR row. The body above is
// `UI::Element::~Element` (ReleaseFields); the restamp and the ~Internal
// tail are the compiler's.

// ---- ?AdjustUpperLeft@Element@UI@@QEAAXHH@Z  0x180115840  ord 1596 ---------
//                                                        WAVE 9 LANE UIC
//
// Translate this element's screen rectangle by (dx, dy), then every descendant,
// then everything on its border chain. Thirty-two instructions, ZERO calls
// other than the recursion into itself — the cheapest enabling port in the
// dialog cluster, and the only thing standing between
// `?SetUpperLeft@DialogInstance@` (WAVE9_LANE_UIF.md row 8) and admission.
//
//     180115860  ADD [RBX+0x68],EDX      ; _layoutLeft   += dx
//     180115863  ADD [RBX+0x70],EDX      ; _layoutRight  += dx
//     180115866  ADD [RBX+0x6c],EBP      ; _layoutTop    += dy
//     180115869  ADD [RBX+0x74],EBP      ; _layoutBottom += dy
//     18011586c  XOR EDI,EDI             ; i = 0   <- INSIDE the border loop
//     18011586e  CMP [RBX+0xa8],EDI / JLE
//     180115880  MOV RAX,[RBX+0xa0] / MOV RCX,[RSI+RAX] ; children[i]
//     18011588b  TEST RCX,RCX / JZ                      ; null children skipped
//     180115893  CALL 0x180115840                       ; recurse
//     180115898  MOV EDX,[RSP+0x38]      ; dx reloaded — EDX is volatile
//     1801158aa  MOV RBX,[RBX+0xb0] / TEST RBX,RBX / JNZ 0x180115860
//
// THE BORDER IS A LOOP, NOT A RECURSION. The tail at 0x1801158aa jumps back to
// 0x180115860, so the border chain is walked ITERATIVELY while children are
// walked recursively. Written as a `while` here for that reason; a recursive
// `AdjustUpperLeft(self->_border, dx, dy)` computes the same answer but is a
// different stack profile on a long border chain, and this body is reached from
// a UI frame.
//
// `_children._data` IS RE-LOADED ON EVERY ITERATION (0x180115880 is inside the
// loop). Nothing in the body can move it — but the load is in the shipped code,
// and hoisting it is the kind of tidy-up that turns a transcription into an
// assumption. Reproduced.
//
// No null test on `self`: retail has none, and the two callers
// (`?SetUpperLeft@DialogInstance@` 0x180114a60 and this body itself) both test
// before calling. CLAUDE.md's rule — the deref is load-bearing.
namespace UI {
void Element::AdjustUpperLeft(int32_t dx, int32_t dy) {
    Element* e = this;
    while (e) {
        e->_layoutLeft   += dx;
        e->_layoutRight  += dx;
        e->_layoutTop    += dy;
        e->_layoutBottom += dy;
        for (int32_t i = 0; i < e->_children.GetSize(); ++i) {
            Element* child = e->_children.GetData()[i];
            if (child) child->AdjustUpperLeft(dx, dy);
        }
        e = e->_border;
    }
}
}  // namespace UI

// ?CreateNewObject@Element@UI@@SAPEAVBase@Object@@XZ  0x180035450
// `xor eax,eax; ret`: the class is abstract, so its factory makes nothing.
Object::Base* UI::Element::CreateNewObject() { return nullptr; }
