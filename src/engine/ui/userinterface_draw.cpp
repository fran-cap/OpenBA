// ============================================================================
// src/engine/ui/userinterface_draw.cpp — Wave 10 lane UIDRAW
// ?Draw@UserInterface@UI@@QEAAXAEAVContext@Video@@@Z    retail 0x180121890
// ============================================================================
//
// PICKED UP FROM LANE UI1, WHICH LEFT THIS ROW WITH "NOTHING SURVIVES BUT SIZE".
// Read docs/history/runtime_port/WAVE10_LANE_UI1.md §3 first: it retired the three
// recorded blockers ("split brain", "allocator NO-GO", "no oracle") with
// evidence, ported and detoured the sibling `Update` (0x1801213c0), and handed
// this row the prologue bytes, the 13-byte patch length and the persistent
// write set. This file is that row closed. Its sibling is
// src/engine/ui/userinterface_frame.cpp and it deliberately mirrors its shape.
//
// ---------------------------------------------------------------------------
// WHAT THE FUNCTION DOES (614 instructions, read off the DISASSEMBLY)
// ---------------------------------------------------------------------------
//   1. Compute the UI-space screen size from Video::Device::_singleton and
//      `_uiScale`, and reset `_clipRegion` to {0, 0, w-1, h-1}.
//   2. Force the Video::Context's `_color` to opaque white if it is not already,
//      marking kDirtyMaterialConstants.
//   3. Build an ORTHOGRAPHIC Volume::Frustum covering the UI rect, construct a
//      Graphics::Camera from it and an IDENTITY Math::Transform3, and push the
//      camera's world matrix + projection into the context.
//   4. Build a UI::SpriteContext on the stack and `Begin` it.
//   5. Walk the dialog list once per LAYER, painting each dialog's backdrop
//      sprite (letterboxed to the sprite's aspect ratio) on the first layer
//      pass and then calling the dialog's root element's vtable Draw, draining
//      the deferred batch each time.
//   6. Paint the fullscreen fade quad, then the mouse cursor.
//   7. Flush, close the sprite capture, restore the context's scissor to the
//      whole render target, and free the four buffers the SpriteContext owns.
//
// ---------------------------------------------------------------------------
// FACTS GHIDRA'S DECOMPILED C DROPS (reference_ghidra_c_output_vs_disassembly)
// ---------------------------------------------------------------------------
//
//  * **A DROPPED DEREFERENCE THAT CHANGES THE VALUE.** The layer pre-pass at
//    0x180121d33 does
//        MOV ECX,[RDX+0x20] ; SHR ECX,4 ; TEST CL,1 ; CMOVNZ ...
//        MOV RCX,[RDX+0x18] ; TEST RCX,RCX ; JZ +5 ; MOV ECX,[RCX+0x34]
//        CMP ECX,R8D
//    When `dialog->_desc` (+0x18) is NULL the `MOV ECX,[RCX+0x34]` is SKIPPED
//    and ECX still holds `flags >> 4` — so the layer compared is the SHIFTED
//    FLAGS WORD, not zero and not a layer. The very next block re-loads +0x18
//    and, on the same null, assigns `XOR R8D,R8D` = 0 instead. Two different
//    answers for the same null on two consecutive instructions. A readable
//    rewrite unifies them and is wrong.
//
//  * **`isOrthographic` is written as a BYTE and copied as a DWORD.** Draw
//    stores `MOV byte [RBP+0x74],1` (Frustum+0x60) and the Camera ctor copies
//    25 DWORDS (0x1800fe99a..0x1800fe9fd), so the three padding bytes above it
//    are stack garbage in retail as well. Not modelled, not initialised.
//
//  * **The `>> 1` halvings are ARITHMETIC (SAR), not logical.** 0x180121e44,
//    0x180121e48, 0x180121e68, 0x180121e70. On a negative width they matter.
//
//  * **`-(float)width` is a SIGN-BIT XOR** (`XORPS XMM0,[0x1801617c0]`, four
//    copies of 0x80000000), and the plane-4 constant at 0x18016174c is
//    **-0.0f**, not 0.0f. Both preserved bit-exactly by NegBits() below rather
//    than by a C negation, because the width==0 case is the difference between
//    +0.0f and -0.0f in a struct the Camera ctor copies verbatim.
//
//  * **THE `.rdata` CONSTANTS, read out of the shipped bytes:**
//        0x180161694 = 0x3f000000 =  0.5f   (the letterbox rounding term)
//        0x18016169c = 0x3f800000 =  1.0f
//        0x18016174c = 0x80000000 = -0.0f
//        0x18016175c = 0xbf800000 = -1.0f
//        0x180161718 = 0x437f0000 =  255.0f (fade alpha scale)
//        0x1801617c0 = four 0x80000000      (the sign mask)
//
//  * **SpriteContext+0x50 is NEVER INITIALISED.** 0x180121cd8 takes its ADDRESS
//    (`LEA RAX,[RBP-0x30]`) and stores that into a home slot; the qword itself
//    is left as stack garbage. Every other field of the object is written.
//    Reproduced by leaving it uninitialised — see the storage declaration.
//
//  * **Four unguarded dereferences kept faithful** (CLAUDE.md, "Don't 'fix' a
//    faithful crash with a null check"): `[[node]+0x18]+0x40` at 0x180121f28,
//    `[[dialog+0x18]]+0x30` at 0x180121e80, `[[cursor+0x28]+0x20]+0x1c/0x20` at
//    0x180122183, and the element root `[[dialog+0x50]]` at 0x180121f52.
//
// ---------------------------------------------------------------------------
// THE ORACLE, AND — SAID FIRST — WHAT IT DOES NOT COVER
// ---------------------------------------------------------------------------
// `Draw`'s PRODUCT is submitted geometry. A predictive oracle that suppresses
// `AddSpriteDraw` / `Flush` / the element vtable Draw verifies NONE of it. What
// it does verify is the function's entire PERSISTENT SCALAR write set, which is
// exactly ten locations:
//
//   PREDICTED AND COMPARED
//     this+0x0fc..+0x10c  `_clipRegion` — determined by Video::Device + _uiScale
//                          and written BEFORE the first call, so no suppressed
//                          callee can move it.
//     ctx +0x408..+0x417  `_color` = opaque white — also written before the
//                          first call. `Flush` writes `_scissor`, never
//                          `_color` (checked in spritecontext_flush.cpp).
//
//   PREDICTED BUT DECLARED UNVERIFIABLE (`Exclude`, so it counts as such)
//     ctx +0x010          `_dirty`. We predict our own `|= 0x1000`, but
//                          `SetCamera` ORs kDirtyGlobalConstants (0x800) and
//                          `Flush` ORs kDirtyScissor (0x400) into the SAME word
//                          while suppressed, so the final value is not ours.
//
//   ABSTAINED FROM ENTIRELY IN PREDICT MODE
//     ctx +0x1e0..+0x1ef  `_scissor`. The tail's reset is GATED on the current
//                          scissor, and `SpriteContext::Flush` overwrites it
//                          (spritecontext_flush.cpp:502-508) on every batch.
//                          With Flush suppressed the branch condition is
//                          UNKNOWABLE, so the body abstains rather than guessing
//                          — the same rule lane UI1 applied to `Update`'s
//                          drag-drop block. A predictor that guesses turns a
//                          coverage hole into a false red.
//
// EFFECTIVENESS IS THE HONEST WEAKNESS HERE AND IT IS STATED, NOT BURIED.
// Both compared regions are IDEMPOTENT in a steady frame: once `_clipRegion`
// matches the resolution and `_color` is white, every later frame predicts a
// value that is already in memory and `changed=` stops moving. `wrong=0` over
// a thousand such rounds is a weak statement. The lane doc reports `changed=`
// beside `rounds=` for exactly that reason, and the negative control is scored
// against the rounds that HAD an armable subject, never against the total
// (reference_probe_needs_an_effectiveness_control.md).
//
// The complement for the geometry is a live in-game run with the DETOUR ON —
// if the layer walk, the letterbox arithmetic or the batch drain were wrong the
// UI would be visibly wrong or absent. That is a functional check, not an
// oracle, and the lane doc says so in those words.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "ui/UserInterface.h"
#include "ui/SpriteContext.h"
#include "video/Context.h"
#include "graphics/Camera.h"
#include "ui/SpriteCapture.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "ui/Cursor.h"
#include "ui/SpriteSheet.h"
#include "ui/SharpDraw.h"   // NOT RETAIL: ext.sharp_ui's snap origin (SpriteContext::_extSharp)
#include "video/CompanionData.h"   // NOT RETAIL: ext.touch_mode resets ImageBufferData::_extPointSampling
// ENDGAME B.3.6 block E: element_common.h carries the same file-scope
// `Module` / `ResolveAddr` declaration every other runtime/src file uses (the
// address door lives in standalone/), and the RULE B6-5 atomic door caches.
#include "element_common.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "input/Device.h"   // ENDGAME B.3.9 SS1: Input::Device::_singleton
#include "input/Input.h"    // ENDGAME B.3.9 SS1: Input::Configuration::_singleton
#include "collection/Collection.h"


namespace {

using UI::UserInterface;
using UI::SpriteContext;
using UI::SpriteDraw;

// ---- the callees ------------------------------------------------------------
// SEVEN OF THE EIGHT ARE EXPORTED, verified against the shipped export
// directory with pefile rather than assumed — `AEAA` means PRIVATE, not
// unexported, and both `Flush` and `AddSpriteDraw` are `AEAA` names that the DLL
// exports all the same (reference_aeaa_is_private_not_unexported.md).
// B3R RT-UI: all called by name -- the Camera ctor body
// (`??0Camera@Graphics@@QEAA@AEBVFrustum@Volume@@AEBVTransform3@Math@@_N@Z`,
// 0x1800fe960, the Graphics::Camera ctor, constructed directly), the SpriteContext /
// SpriteCapture members, MemoryAllocator::MemFree (0x1800c7ad0) and
// 0x1800783d0, `Collection::Array<UI::SpriteDraw>::SetSize(0)`, the deferred
// batch's clear (not exported: rtcoll::RtColl_ArraySetSize_1800783d0).

// A dialog-list node: element, prev, next. `Draw` only ever walks it forwards
// through `next`.
using Node = Collection::LinkedList<UI::DialogInstancePtr>::Node;
static_assert(sizeof(Node) == 0x18, "LinkedList node is value/prev/next");

// Bit-exact float negation. Retail negates through `XORPS` against four copies
// of 0x80000000 (0x1801617c0), which turns +0.0f into -0.0f; a C `-x` is
// permitted to do the same but the whole point of a byte-faithful port is not
// to depend on that permission. The value lands in a Frustum the Camera ctor
// copies dword by dword, so the sign of a zero is observable.
inline float NegBits(float v) {
    uint32_t b;
    __builtin_memcpy(&b, &v, 4);
    b ^= 0x80000000u;
    float r;
    __builtin_memcpy(&r, &b, 4);
    return r;
}

} // namespace

// ENDGAME B.3.6 block E: the body is the member.
void UI::UserInterface::Draw(Video::Context& context) {
    // ---- 0x180121907: UI-space screen size -------------------------------
    const ::Video::Device* const vdev = ::Video::Device::_singleton;
    const float    scale = _uiScale;
    const int32_t  width = static_cast<int32_t>(
        static_cast<float>(vdev->backBufferWidth) * scale);    // +0x04
    const int32_t  height = static_cast<int32_t>(
        static_cast<float>(vdev->backBufferHeight) * scale);   // +0x08

    // ---- 0x180121942: force the context colour to opaque white ------------
    // Four UCOMISS/JP/JNZ pairs: ANY inequality, and an unordered compare
    // (a NaN in the colour), takes the write path. `!(a==1 && b==1 && c==1 &&
    // d==1)` reproduces that including the NaN case, because `NaN == 1.0f` is
    // false and so the negation is true.
    {
        const Math::Vector4& col = context._color;             // +0x408
        if (!(col.x == 1.0f && col.y == 1.0f && col.z == 1.0f && col.w == 1.0f)) {
            context._color.x = 1.0f;
            context._color.y = 1.0f;
            context._color.z = 1.0f;
            context._color.w = 1.0f;
            context._dirty |= RtVideo::kDirtyMaterialConstants;    // +0x10
        }
    }

    // ---- 0x1801219c0: the clip region ------------------------------------
    // One QWORD clears x and y together (0x1801219c0), then two DWORDs.
    _clipRegion.left   = 0;
    _clipRegion.top    = 0;
    _clipRegion.right  = width - 1;
    _clipRegion.bottom = height - 1;

    // ---- 0x180121a3f: the orthographic UI frustum -------------------------
    // Six planes {normal.xyz, d}. Together they are the box
    //   0 <= x <= width,  0 <= y <= 1,  0 <= z <= height
    // i.e. a screen-sized slab one unit deep, and `isOrthographic` says so.
    // The bool is stored BEFORE the floats in retail; irrelevant for a local,
    // recorded because the write order is the only thing that could ever make
    // it relevant.
    Frustum fr;
    fr.isOrthographic = true;                                   // +0x60, byte
    fr.planes[0] = {  0.0f,  1.0f,  0.0f,  0.0f };
    fr.planes[1] = {  0.0f, -1.0f,  0.0f, -1.0f };
    fr.planes[2] = {  1.0f,  0.0f,  0.0f,  0.0f };
    fr.planes[3] = { -1.0f,  0.0f,  0.0f,  NegBits(static_cast<float>(width)) };
    fr.planes[4] = {  0.0f,  0.0f, -1.0f,  NegBits(0.0f) };     // -0.0f, 0x18016174c
    fr.planes[5] = {  0.0f,  0.0f,  1.0f,  static_cast<float>(height) };

    // ---- 0x180121ae8: identity camera-to-world ----------------------------
    Transform3 t3;
    t3.axisX       = { 1.0f, 0.0f, 0.0f };
    t3.axisY       = { 0.0f, 1.0f, 0.0f };
    t3.axisZ       = { 0.0f, 0.0f, 1.0f };
    t3.translation = { 0.0f, 0.0f, 0.0f };

    // Uninitialised, exactly like retail's `RBP+0x80` local: the ctor fills
    // 0x00..0xd4 and `SetFrustum` fills the projection.
    // `??0Camera@Graphics@@QEAA@AEBVFrustum@Volume@@AEBVTransform3@Math@@_N@Z`.
    ::Graphics::Camera cam(fr, t3, false);          // 0x1800fe960

    // ---- 0x180121b00: Transform3::AsMatrix, inlined -----------------------
    // Rows from the three basis vectors and the translation, w = 0/0/0/1.
    // The 1.0f in m[15] is the SAME XMM14 as the frustum's, not a fresh
    // constant load — noted because it is why the constant appears once.
    {
        const Transform3& ctw = cam._cameraToWorld;
        Matrix view;
        view.m[0]  = ctw.axisX.x;        view.m[1]  = ctw.axisX.y;        view.m[2]  = ctw.axisX.z;        view.m[3]  = 0.0f;
        view.m[4]  = ctw.axisY.x;        view.m[5]  = ctw.axisY.y;        view.m[6]  = ctw.axisY.z;        view.m[7]  = 0.0f;
        view.m[8]  = ctw.axisZ.x;        view.m[9]  = ctw.axisZ.y;        view.m[10] = ctw.axisZ.z;        view.m[11] = 0.0f;
        view.m[12] = ctw.translation.x;  view.m[13] = ctw.translation.y;  view.m[14] = ctw.translation.z;  view.m[15] = 1.0f;

        // Retail copies the projection into its own scratch and passes THAT,
        // so the argument is a copy, not a pointer into the camera.
        Matrix proj = cam._projection;

        // `?SetCamera@Context@Video@@QEAAXAEBVMatrix@Math@@0@Z` (0x18001a150).
        context.SetCamera(view, proj);
    }

    // ---- 0x180121cad: the SpriteContext, built on the stack ---------------
    // B4 Y RT-UI: a real `UI::SpriteContext`. Retail inlines
    // ??0SpriteContext(Context&) (0x18011d1e0) here store for store
    // (0x180121cad..0x180121cef: +0x00 1.0f, +0x08, the _draws header, the
    // scissor as one MOVDQA, +0x30 &context, BYTE +0x38, the _deferred
    // header, +0x58..+0x77 the Layout's two arrays; +0x50 never written --
    // only its address is taken at 0x180121cd8). Its ~SpriteContext, inlined
    // at the end of the body (0x1801222e0..), is the implicit member-wise
    // destructor at the closing brace: +0x68, +0x58, +0x40, +0x10.
    SpriteContext scObj(context);
    SpriteContext* const sc = &scObj;

    // ---- 0x180121d0e: Begin ------------------------------------------------
    // ENDGAME B.3.6: `SpriteContext::Begin` / `Flush` / `AddSpriteDraw` are
    // members of the real class (block D).
    sc->Begin(_uiScale, _spriteCapture, &_captureViewport);   // +0x118

    // A local lambda for retail's repeated "switch the batch key" preamble,
    // which appears FIVE times verbatim (0x180121e96, 0x180121f79, 0x180122084,
    // 0x180122168, and inside the deferred drain). Written once because five
    // copies of the same four instructions is how a transcription error hides.
    auto selectSheet = [&](SpriteSheet* sheet) {
        if (sc->_currentSheet != sheet) {
            if (sc->_currentSheet != nullptr) {
                sc->Flush();                            // batch key change
            }
            sc->_currentSheet = sheet;
        }
    };

    // ---- 0x180121d1b: the layer pre-pass -----------------------------------
    // Two things at once: find the LAST dialog whose flags bit 4 is set (the
    // "topmost" barrier below which only backdrop-flagged dialogs draw), and
    // find the highest layer index (`Dialog::_drawLevel`, +0x34).
    int32_t maxLayer = 0;
    Node*   topmost  = _dialogs._head;
    for (Node* n = topmost; n != nullptr; n = n->next) {
        const DialogInstance* const d = n->element;

        const uint32_t shifted = d->_flags >> 4;
        if (shifted & 1u) topmost = n;

        // THE DROPPED DEREFERENCE. On a null `_dialog` (+0x18) the compared
        // value is `shifted`, not zero — the load is skipped and ECX is not
        // reset.
        const Dialog* const desc  = d->_dialog;
        const int32_t       probe = desc ? desc->_drawLevel
                                         : static_cast<int32_t>(shifted);
        if (probe > maxLayer) {
            // ...and here the SAME null yields 0 instead, from `XOR R8D,R8D`.
            const Dialog* const desc2 = d->_dialog;
            maxLayer = desc2 ? desc2->_drawLevel : 0;
        }
    }

    // ---- 0x180121d71: the layer loops --------------------------------------
    // `JS` at 0x180121d7e — signed, so a negative maxLayer skips the whole
    // walk. It cannot go negative today (the pre-pass only ever assigns a value
    // strictly greater than the running max, or 0), which makes the branch
    // dead; kept because a dead branch that costs nothing is cheaper than a
    // claim about reachability.
    int32_t curLayer = 0;
    if (maxLayer >= 0) {
        for (int64_t pass = 0;; ++pass, ++curLayer) {
            bool pastTopmost = false;

            for (Node* n = _dialogs._head; n != nullptr; n = n->next) {
                DialogInstance* const d = n->element;

                // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, this dialog's quads snap
                // against its own upper-left (ui/SharpDraw.h SnapDevice), so a
                // dialog that moves -- a panel following its entity while the
                // camera turns -- moves whole, with no glyph-to-glyph shimmer.
                // Null `_extSharp` (always in the faithful profile): nothing.
                if (const SharpDraw* const sharp = SpriteContext::_extSharp) {
                    const Element* const root = d->_root.Get();
                    const int32_t ox = root != nullptr ? root->_layoutLeft : 0;
                    const int32_t oy = root != nullptr ? root->_layoutTop : 0;
                    // Quads still pending (a skipped dialog's backdrop) belong
                    // to the previous origin: flush them under it first.
                    if ((ox != sharp->originX || oy != sharp->originY) && sc->_draws._size > 0)
                        sc->Flush();
                    sharp->originX = ox;
                    sharp->originY = oy;
                }

                // ---- 0x180121dbe: the backdrop, FIRST PASS ONLY -----------
                // `TEST R12,R12; JNZ` — the whole block is gated on the OUTER
                // pass index being zero, so a dialog's backdrop is painted once
                // no matter how many layers exist.
                if (pass == 0) {
                    int32_t x1 = 0, y1 = 0, x2 = width, y2 = height;
                    uint32_t tint = 0;

                    const int32_t spriteIndex = d->_spriteIndex;       // +0x60
                    void* const   sheet0      = d->_spriteSheet;       // +0x58
                    if (spriteIndex >= 0 && sheet0 != nullptr) {
                        // Letterbox the backdrop to the sprite's aspect ratio.
                        const float screenAspect =
                            static_cast<float>(width) / static_cast<float>(height);

                        // Both size reads re-do the `idx < 0 -> 0` test, and
                        // both re-load the sheet. Kept as two reads because
                        // that is two loads of `[d+0x58]` in retail. The sheet's
                        // sprite table is `SpriteSheet::m_sprites` (+0x20).
                        // B.4 LEFT: `DialogInstance::_spriteSheet` is `void*`
                        // (R-UI-a).
                        int32_t sw = 0, sh = 0;
                        {
                            const int64_t idx = d->_spriteIndex;
                            if (idx >= 0) {
                                const SpriteInfo* const tbl =
                                    d->_spriteSheet->m_sprites._data;
                                sw = tbl[idx].width;                   // +0x1c
                            }
                        }
                        {
                            const int64_t idx = d->_spriteIndex;
                            if (idx >= 0) {
                                const SpriteInfo* const tbl =
                                    d->_spriteSheet->m_sprites._data;
                                sh = tbl[idx].height;                  // +0x20
                            }
                        }

                        const float spriteAspect =
                            static_cast<float>(sw) / static_cast<float>(sh);

                        // `COMISS; JBE` — an unordered compare takes the ELSE
                        // arm, which is what `>` gives on a NaN.
                        if (spriteAspect > screenAspect) {
                            const int32_t half = static_cast<int32_t>(
                                spriteAspect * static_cast<float>(height) + 0.5f) >> 1;
                            const int32_t mid = width >> 1;   // SAR, arithmetic
                            x1 = mid - half;
                            x2 = mid + half;
                        } else {
                            const int32_t half = static_cast<int32_t>(
                                static_cast<float>(width) / spriteAspect + 0.5f) >> 1;
                            const int32_t mid = height >> 1;  // SAR, arithmetic
                            y1 = mid - half;
                            y2 = mid + half;
                        }

                        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu
                        // may move the menu's backdrop so its logo is not cut off; retail's fit otherwise.
                        if (_extDialogPlace != nullptr) {
                            int32_t rect[4] = {x1, y1, x2, y2};
                            _extDialogPlace(kExtPlaceBackdrop, d, width, height, rect);
                            x1 = rect[0];
                            y1 = rect[1];
                            x2 = rect[2];
                            y2 = rect[3];
                        }

                        // 0x180121e7c — unguarded on +0x18, but INSIDE the
                        // sprite test: ComboDialog's ctor leaves `_dialog` NULL
                        // by design and has spriteIndex -1, so hoisting this out
                        // of the block (as this port once did) faulted on the
                        // first dropdown drawn DLL-absent (2026-09-12).
                        tint = d->_dialog->_backgroundColor;           // +0x30
                    }

                    SpriteSheet* const sheet = d->_spriteSheet;
                    if (sheet != nullptr && d->_spriteIndex >= 0) {
                        selectSheet(sheet);

                        SpriteDraw sd;
                        sd.sheet       = d->_spriteSheet;
                        sd.spriteIndex = d->_spriteIndex;
                        sd.x1 = x1; sd.y1 = y1; sd.x2 = x2; sd.y2 = y2;
                        sd.rotation    = 0.0f;
                        sd.color       = tint;
                        sd.isLine      = 0;
                        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu
                        // may draw the menus' backdrop once more BEFORE retail's draw, bottom-aligned,
                        // so its bottom border shows under the moved picture (retail's draw then covers
                        // the extra copy's top with its own opaque top border). The hook gets retail's
                        // rectangle and answers the extra draw's; none unless it is non-empty. Null
                        // pointer: retail's one draw.
                        if (_extDialogPlace != nullptr) {
                            int32_t lower[4] = {x1, y1, x2, y2};
                            _extDialogPlace(kExtPlaceBackdropLower, d, width, height, lower);
                            if (lower[2] > lower[0] && lower[3] > lower[1] &&
                                (lower[0] != x1 || lower[1] != y1 || lower[2] != x2 || lower[3] != y2)) {
                                SpriteDraw extra = sd;
                                extra.x1 = lower[0];
                                extra.y1 = lower[1];
                                extra.x2 = lower[2];
                                extra.y2 = lower[3];
                                sc->AddSpriteDraw(extra);
                            }
                        }
                        sc->AddSpriteDraw(sd);          // dialog backdrop
                        // EXT SEAM (NOT RETAIL, P9, docs/extensions/handins/PHONEMENU.md §3):
                        // ext.phone_menu may draw a sprite (the backdrop's, or another the hook
                        // names) over the backdrop, clipped (the loading picture's torn bottom
                        // border). Null pointer: retail's one draw.
                        if (_extBackdropOver != nullptr) {
                            int32_t r[4] = {x1, y1, x2, y2};
                            int32_t clip[4] = {0, 0, 0, 0};
                            SpriteSheet* overSheet = sheet;
                            int32_t overSprite = d->_spriteIndex;
                            if (_extBackdropOver(d, width, height, r, clip, &overSheet, &overSprite) &&
                                r[2] > r[0] && r[3] > r[1] && overSheet != nullptr && overSprite >= 0) {
                                SpriteDraw over = sd;
                                over.sheet = overSheet;
                                over.spriteIndex = overSprite;
                                over.x1 = r[0];
                                over.y1 = r[1];
                                over.x2 = r[2];
                                over.y2 = r[3];
                                if (clip[2] > 0 && clip[3] > 0) {
                                    Video::Viewport v;
                                    v.x = clip[0];
                                    v.y = clip[1];
                                    v.width = clip[2];
                                    v.height = clip[3];
                                    // A scissor change flushes and forgets the batch's sheet:
                                    // the sheet is bound again on each side of the clipped draw.
                                    const Video::Viewport saved = sc->SetScissor(v);
                                    selectSheet(overSheet);
                                    sc->AddSpriteDraw(over);
                                    (void)sc->SetScissor(saved);
                                    selectSheet(sheet);
                                } else {
                                    selectSheet(overSheet);
                                    sc->AddSpriteDraw(over);
                                    selectSheet(sheet);
                                }
                            }
                        }
                    }
                }

                // ---- 0x180121f0e: is this dialog drawn at all? ------------
                // Everything from the topmost dialog onward draws. Everything
                // BEFORE it draws only if its description carries flag 0x40.
                if (n == topmost) {
                    pastTopmost = true;
                } else if (!pastTopmost) {
                    const Dialog* const desc = d->_dialog;
                    if ((desc->_defaultFlags & 0x40) == 0) continue;   // UNGUARDED deref, +0x40
                }

                // ---- 0x180121f32: layer test then paint ------------------
                {
                    const Dialog* const desc  = d->_dialog;
                    const int32_t       layer = desc ? desc->_drawLevel : 0;
                    if (layer != curLayer) continue;
                }

                // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): ext.touch_mode, a dialog with its own
                // scale k. Its quads are in its own units: the pending quads are flushed under the
                // global scale, then the context's world transform scales by k and the scissor scale
                // is _uiScale / k until this dialog's last flush. ext.sharp_ui's snap is off for it
                // (retail's filtered quads). Null `_extDialogScale` (the faithful profile): nothing.
                const float dialogScale = _extDialogScale != nullptr ? _extDialogScale(d) : 1.0f;
                const SharpDraw* const sharpBefore = SpriteContext::_extSharp;
                if (dialogScale != 1.0f) {
                    sc->Flush();
                    Matrix scaled;
                    scaled.m[0] = dialogScale;
                    scaled.m[5] = dialogScale;
                    scaled.m[10] = 1.0f;
                    scaled.m[15] = 1.0f;
                    context.SetTransform(0, scaled);
                    sc->_scale = _uiScale / dialogScale;
                    SpriteContext::_extSharp = nullptr;
                    if (::Video::ImageBufferData::_extPointSampling) {
                        ::Video::ImageBufferData::_extPointSampling = false;
                        context._imageBufferDirty |= 0xffffu;
                        context._dirty |= RtVideo::kDirtyImageBuffers;
                    }
                }

                sc->_deferring = 1;
                {
                    // Element vtable slot 6 (+0x30) `Draw(SpriteContext&, alpha)`
                    // — VIRTUAL, dispatched through the root's own table. `_root`
                    // (+0x50) through the dialog's one-load accessor (0x180045580).
                    d->GetRoot()->Draw(*sc, 0xff);
                }

                // The element pushed its geometry into `_deferred` while
                // `_deferring` was set. Drain it in order, flushing whenever
                // the sheet changes, then flush and clear.
                if (sc->_deferring != 0) {
                    for (int32_t i = 0; i < sc->_deferred._size; ++i) {
                        SpriteDraw* rec = sc->_deferred._data + i;
                        selectSheet(rec->sheet);
                        rec = sc->_deferred._data + i;   // re-load: Flush may
                                                         // have moved the buffer
                        sc->AddSpriteDraw(*rec);        // deferred drain
                    }
                    sc->Flush();                        // end of dialog

                    // Array<SpriteDraw>::SetSize(0) (clear _deferred), 0x1800783d0
                    rtcoll::RtColl_ArraySetSize_1800783d0(&sc->_deferred);
                }
                sc->_deferring = 0;

                // EXT SEAM (NOT RETAIL, docs/android/TOUCH_MODE.md): back to the global scale.
                if (dialogScale != 1.0f) {
                    sc->Flush();
                    Matrix identity;
                    identity.m[0] = 1.0f;
                    identity.m[5] = 1.0f;
                    identity.m[10] = 1.0f;
                    identity.m[15] = 1.0f;
                    context.SetTransform(0, identity);
                    sc->_scale = _uiScale;
                    SpriteContext::_extSharp = sharpBefore;
                }
            }

            if (curLayer >= maxLayer) break;   // `CMP R14D,R8D; JLE` at the tail
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, the fade and the cursor snap
    // absolutely.
    if (const SharpDraw* const sharp = SpriteContext::_extSharp) {
        if ((sharp->originX != 0 || sharp->originY != 0) && sc->_draws._size > 0)
            sc->Flush();
        sharp->originX = 0;
        sharp->originY = 0;
    }

    // ---- 0x18012201c: the fullscreen fade quad -----------------------------
    // `UCOMISS XMM0,XMM15; JP +8; JZ skip` — a NaN amount FADES. `!(x == 0.0f)`
    // reproduces that; `x != 0.0f` would too, but the negation is written out
    // because the JP/JZ pair is the only reason to think about it.
    {
        const float amount = _fadeAmount;
        if (!(amount == 0.0f) &&
            _fadeSpriteIndex >= 0 &&
            _fadeSpriteSheet != nullptr) {

            // alpha = (uint8)(int64)(amount * 255.0f), placed in the top byte
            // over the fade colour's low 24 bits.
            const int64_t a64 = static_cast<int64_t>(amount * 255.0f);
            const uint32_t color =
                (static_cast<uint32_t>(static_cast<uint8_t>(a64)) << 24) |
                (_fadeColor & 0x00ffffffu);

            SpriteSheet* const sheet = _fadeSpriteSheet;
            if (sheet != nullptr && _fadeSpriteIndex >= 0) {
                selectSheet(sheet);

                SpriteDraw sd;
                sd.sheet       = _fadeSpriteSheet;
                sd.spriteIndex = _fadeSpriteIndex;
                sd.x1 = 0; sd.y1 = 0;            // one qword at 0x1801220b9
                sd.x2 = width; sd.y2 = height;
                sd.rotation    = 0.0f;
                sd.color       = color;
                sd.isLine      = 0;
                sc->AddSpriteDraw(sd);                  // fullscreen fade
            }
        }
    }

    // ---- 0x1801220ec: the mouse cursor -------------------------------------
    {
        const Cursor* const cursor = _cursor;
        const ::Input::Configuration* const cfg = ::Input::Configuration::_singleton;
        if (cursor != nullptr && _cursorEnabled != 0 && cfg->_mouseDisplay) {   // +0x0a
            const ::Input::Device* const in = ::Input::Device::_singleton;

            // MOVSX — SIGNED 16-bit mouse coordinates, which Ghidra prints as a
            // zero extension. A negative X off the left edge is real.
            const int32_t rawY = in->_mouse[1].value;                     // +0x10b4
            const float   uiScale = _uiScale;
            const int32_t rawX = in->_mouse[0].value;                     // +0x109c

            const int32_t x1 = static_cast<int32_t>(static_cast<float>(rawX) * uiScale)
                             - cursor->_hotSpotX;                         // +0x20
            const int32_t y1 = static_cast<int32_t>(static_cast<float>(rawY) * uiScale)
                             - cursor->_hotSpotY;                         // +0x24

            SpriteSheet* const sheet = cursor->_spriteSheet.Get();        // +0x28
            if (sheet != nullptr) {
                selectSheet(sheet);

                // UNGUARDED, and sprite index 0 is implicit: the size comes
                // from the FIRST table entry, with no index arithmetic.
                const SpriteInfo* const tbl = sheet->m_sprites._data;           // +0x20
                const int32_t sh = tbl->height;                           // +0x20
                const int32_t sw = tbl->width;                            // +0x1c

                SpriteDraw sd;
                sd.sheet       = sheet;
                sd.spriteIndex = 0;
                sd.x1 = x1; sd.y1 = y1;
                sd.x2 = sw + x1; sd.y2 = sh + y1;
                sd.rotation    = 0.0f;
                sd.color       = 0xffffffffu;
                sd.isLine      = 0;
                sc->AddSpriteDraw(sd);                  // mouse cursor
            }
        }
    }

    // ---- 0x1801221cf: the tail ---------------------------------------------
    sc->Flush();                                        // final

    if (UI::SpriteCapture* const capture = sc->_spriteCapture) {
        capture->EndAddingSprites();                    // a member of the real class (block D)
    }

    // ---- 0x1801221e6: restore the context scissor to the whole target ------
    // Retail reads the context back through the SpriteContext (+0x30).
    {
        const ::Video::Device* const dev = ::Video::Device::_singleton;
        const int32_t devW = dev->backBufferWidth;
        const int32_t devH = dev->backBufferHeight;

        ::Video::Context* const cc = sc->_context;
        ::Video::Viewport& vp = cc->_scissor;                              // +0x1e0
        if (vp.x != 0 || vp.y != 0 || vp.width != devW || vp.height != devH) {
            cc->_dirty |= RtVideo::kDirtyScissor;                          // +0x10
            vp.x      = 0;                                                 // } one QWORD
            vp.y      = 0;                                                 // }
            vp.width  = devW;
            vp.height = devH;
        }
    }

    // ---- 0x180122242: free the four buffers the SpriteContext owns ---------
    // `scObj`'s ~SpriteContext at the closing brace (B4 Y RT-UI): reverse
    // declaration order runs `_layout._lineWidths` (+0x68), `_layout._entries`
    // (+0x58), `_deferred` (+0x40), `_draws` (+0x10), each a trivial-element
    // ~Array -- the J1 body: `if (data)` free under the allocator's lock, then
    // the header zeroed. Retail's order and retail's four frees.
}
