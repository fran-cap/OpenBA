#pragma once
// ============================================================================
// src/engine/video/CompanionData.h — Wave 9 lane B2
//
// The twelve `Video::*Data` companion base classes, and the two small owner
// classes whose only unported members dispatch through them.
//
// SHAPE, FROM THE BYTES. Runtime declares one abstract base per renderer
// resource (`DeviceData`, `ContextData`, `VertexBufferData`, ...). The plugin
// — VideoDX11, which in this project is `src/render/dx11/` and therefore already ours —
// derives from each one. Every base is the same two-pointer object and every
// ctor/dtor pair is the same three or four instructions, read at the twelve
// addresses listed in src/engine/video/companion_data.cpp:
//
//     ??0XData(XData* this, X* owner)   this->vtable = ??_7XData; this->owner = owner; return this;
//     ??1XData(XData* this)             this->vtable = ??_7XData;
//
// so ONE struct covers all twelve. That is not an assumption carried over
// from one class to the others: all 24 bodies were disassembled individually
// and each writes its OWN class's exported vtable — the per-class vtable
// addresses are tabulated in companion_data.cpp and each was checked against
// the LEA operand in that class's own ctor and dtor.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // std::has_virtual_destructor (B3X X1 pins)

// ENDGAME B.3.5 block D1: the real `Video::ConstantBuffer`. The partial view
// that used to sit in this file (head[0x38] + device + platformData) is gone —
// see the alias below.
#include "ConstantBuffer.h"
#include "ClearFlags.h"           // Video::ClearFlags (slot 4's first argument)
#include "../object/Internal.h"   // Object::detail::InternalMemFree (the slot-0 bodies)
// B5 RES: `Collection::Array<T>` for `ImageBufferData::Create`'s level array.
// Three template parameters, two defaulted, so it cannot be forward-declared
// (ImageBuffer.h's banner says the same); the header includes only std headers.
#include "../collection/Collection.h"

namespace RtVideo {

// ----------------------------------------------------------------------------
// The shared layout of all twelve `Video::*Data` bases — 0x10 bytes.
//
// `owner` is typed `void*` deliberately: it is a `Device*` for `DeviceData`, a
// `VertexBuffer*` for `VertexBufferData` and so on, and none of the ported
// bodies dereference it. They store it and nothing else.
//
// There is no `sizeof` assertion here because 0x10 is a LOWER bound, not a
// measured total: the ctor writes +0x00 and +0x08 and the derived dx11 object
// continues past that (src/render/dx11/VideoDX11Data.h owns those layouts). The two
// offsets the ported bodies actually touch are asserted.
// ----------------------------------------------------------------------------
struct CompanionData {
    void* vtable;  // +0x00  ??_7<Class>@Video@@6B@, stamped by both ctor and dtor
    void* owner;   // +0x08  the Video-side object this platform record belongs to
};
static_assert(offsetof(CompanionData, vtable) == 0x00, "CompanionData::vtable at +0x00");
static_assert(offsetof(CompanionData, owner) == 0x08, "CompanionData::owner at +0x08");

// ----------------------------------------------------------------------------
// Video::ConstantBuffer — MOVED, ENDGAME B.3.5 block D1 (B4b-1 / RULE B5-2).
//
// This file used to carry a partial view of the class: `head[0x38]` plus
// `device` @+0x38 and `platformData` @+0x40, the two fields `Bind` reads, with
// no `sizeof` assert because the ctor's last write is at +0x40. It was one of
// FOUR views of the same 0x48 bytes (the others: `ConstantBufferOpaque` in
// Context.h, ten `constexpr` offsets in constant_buffer.cpp, and dx11's
// `ConstantBufferOwner`). The real class now lives in
// `src/engine/video/ConstantBuffer.h`, with its interior read out of retail's
// one-line accessors — which closes B35_RULES section 13 item 4 — and this is an
// ALIAS so that `src/engine/video/companion_data.cpp` (block A's file) keeps
// compiling unchanged. There is now ONE definition instead of two.
//
// The ctor transcription that used to be here is in ConstantBuffer.h's banner,
// alongside the accessor that names each field:
//
//   1800eee08  MOV [RCX],RAX        \  RAX = 0   -> _prefix
//   1800eee0b  MOV [RCX+0x08],RAX    |           -> _typeName
//   1800eee0f  MOV [RCX+0x10],-1     |  a QWORD: _ordinalIndex AND _uploadIndex
//   1800eee17  MOV [RCX+0x18],AL     |           -> _bindToVertex (a BYTE)
//   1800eee1a  MOV [RCX+0x1c],RAX    |  unaligned: _registerCount + _bankStride
//   1800eee1e  MOV [RCX+0x28],RAX    |           -> _constantData
//   1800eee22  MOV [RCX+0x30],RAX   /            -> _constantCount/_capacity
//   1800eee26  MOV [RCX+0x38],RDX      device
//   1800eee3c  CALL [RAX+0x58]         Device::CreateConstantBufferData(this)
//   1800eee3f  MOV [RBX+0x40],RAX      platformData
// ----------------------------------------------------------------------------
using ConstantBuffer = ::Video::ConstantBuffer;

// ----------------------------------------------------------------------------
// `Video::ImageBufferData`'s POD view, MOVED HERE from
// src/engine/video/ImageBuffer.h by ENDGAME B.3.5 block A.
//
// RULE B4b-1: the retail name now belongs to the real abstract class
// `Video::ImageBufferData` further down this file, so the POD moves into the
// `Rt*` namespace. `ImageBuffer.h` keeps a global-scope alias to this
// declaration so its one consumer — `src/engine/video/buffers.cpp`, which
// writes `pd->vtable` by hand in three trampolines and in the ported
// `??1ImageBufferData` door — compiles unchanged. There is now exactly ONE
// definition of these two fields instead of two.
//
// It is NOT collapsed into `CompanionData` above even though the bytes are
// identical: `buffers.cpp` names the second field `imageBuffer`, and the whole
// point of B4b-1 is to stop the same layout being spelled twice, not to rename
// a live field in a file this block does not own (block C owns `ImageBuffer.h`
// and `buffers.cpp` after this).
//
// Decompiled from (Wave 6 lane C):
//   Ctor(ImageBuffer*):  0x1800f8c90 (??0ImageBufferData@Video@@QEAA@PEAVImageBuffer@1@@Z)
//   Copy ctor:           0x18007dd80 (??0ImageBufferData@Video@@QEAA@AEBV01@@Z)
//   Dtor:                0x1800f8e10 (??1ImageBufferData@Video@@UEAA@XZ)
// ----------------------------------------------------------------------------

}  // namespace RtVideo

// ############################################################################
// THE TWELVE `Video::*Data` ABSTRACT BASES, as real C++ classes.
// ENDGAME B.3.5 block A, RULE B5-1.
// ############################################################################
//
// THE BOUNDARY, AND WHICH HALF THIS IS. Runtime declares one ABSTRACT base per
// renderer resource; VideoDX11 — `src/render/dx11/`, ours — derives the concrete override
// set from it. Confirmed byte-wise for all twelve (B35_RULES 1.1): the Runtime
// vtable has a CONCRETE slot 0 (a deleting dtor in Runtime's
// 0x180058590..0x180058b10 band) and `_purecall` (0x18007ec28) in EVERY other
// slot, and the object is `{vptr, owner}`.
//
//   class                 ??_7 (Runtime)  slots   VideoDX11 ??_7 (the subclass's)
//   DeviceData            0x180158c30      20     0x180018bb0
//   ContextData           0x180158b90      20     0x1800186f0
//   ImageBufferData       0x180158b50       7     0x180018790
//   IndexBufferData       0x180158af8       7     0x1800187c8
//   VertexBufferData      0x180158ac0       7     0x180018830
//   ConstantBufferData    0x180158b30       4     0x1800185d0
//   RenderBufferData      0x180158a28       4     0x180018588
//   VertexLayoutData      0x180158aa8       3     0x180018878
//   VertexProgramData     0x180158a90       3     0x180018890
//   PixelProgramData      0x180158a78       3     0x180018800
//   RenderStateData       0x180158a60       3     0x180018818
//   VideoProgramData      0x180158a48       3     0x1800185a8
//
// Two independent instruments agree on all twelve slot counts: the Runtime
// `.rdata` read (B35_SLOTS.md) and the shipped plugin's twelve tables walked
// 2026-07-24 (src/render/dx11/README.md status board); tests/unit/b39_vid_data_test.cpp
// pins every slot by retail number.
//
// NO `sizeof` ASSERT, DELIBERATELY (RULE B5-1.1). 0x10 is a LOWER bound, not a
// measured total: the ctor writes +0x00 and +0x08 and the DERIVED dx11 object
// continues past that (`src/render/dx11/VideoDX11Data.h` owns those layouts and
// keeps its own size asserts — `DeviceData` is 0x70). The two offsets the
// ported bodies actually touch are what is asserted, once, on
// `RtVideo::CompanionData` above; `offsetof` on these polymorphic classes would
// add nothing it does not already say, so each class below asserts only that it
// is still the two-pointer object.
//
// ENDGAME B.3 step 9, block VID (B39_RULES.md D1, section 5 item 4): CONCRETE.
// Each class has a REAL public constructor (retail's `??0XData(owner)`: the typed
// owner at +0x08, the vptr -- the compiler's) and a body in every slot but the
// destructor that is retail's: `_purecall` (0x18007ec28), spelled
// `detail::CompanionPureCall()`. The twelve minted `??_7XData@Video@@6B@` rows,
// their RT_DELDTOR rows and the `??1XData` doors are retired.
//
// ENDGAME B5, THE DX11 SEAM (2026-10-05, B5_RULES.md D1-D3): each class declares
// `virtual ~XData()` (retail slot 0, the deleting destructor; GCC spends two
// compiler slots on it, so retail slot N >= 1 is compiler slot N + 1 -- nothing
// reads these tables by index). `src/render/dx11/`'s twelve `VideoDX11::XData` are real
// `final` SUBCLASSES (src/render/dx11/VideoDX11Data.h) overriding every slot, built by
// `new XData(owner)` through the base's class-scope `operator new` (retail's
// `MemAlloc(size, 4)`) and destroyed by `delete` through its `operator delete`
// (retail's `MemFree`; src/engine/video/companion_operators.cpp). The base
// table is therefore never the vptr of a live object -- the compiler stores the
// derived table on top of it in the derived constructor, as MSVC did.
//
// SLOT NAMES. Retail gives none: every slot but 0 is `_purecall`, and a pure
// slot's name can only be read off an override. The overrides are ours -- the
// bodies recovered from the shipped VideoDX11 vtables lane by lane -- so the
// names below are those bodies' and the signatures come from the CALLERS
// (the slot decides, B35_RULES B5-14). Where a lane could not recover a name
// (four `DeviceData` stubs, one `RenderBufferData` and two `VideoProgramData`
// slots, all of which the shipped plugin implements as a constant return) the
// slot is spelled `SlotN` rather than given an invented name.

extern "C" void _purecall();   // the CRT's (the exe's src/app/main.cpp logs it)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"   // the _purecall bodies below

namespace IO { class Serializer; }   // B5 BUF: Index/VertexBufferData::Serialize's argument

namespace Video {

// The owners and the companions the factory slots below name (B.4 R-VIDEO, H4).
class Device;
class Context;
class ImageBuffer;
class IndexBuffer;
class VertexBuffer;
class VertexLayout;
class VertexProgram;
class PixelProgram;
class RenderState;
class VideoProgram;
class RenderBuffer;
class ContextData;
class ImageBufferData;
class ConstantBufferData;
class IndexBufferData;
class VertexBufferData;
class VertexLayoutData;
class VertexProgramData;
class PixelProgramData;
class RenderStateData;
class VideoProgramData;
class RenderBufferData;

namespace detail {
// Retail's body in every Video::*Data slot but 0: `_purecall` (0x18007ec28).
// Never reached -- a live companion wears dx11's table -- and never returns.
[[noreturn]] inline void CompanionPureCall() { _purecall(); __builtin_trap(); }
}  // namespace detail


// ---- 20 slots -------------------------------------------------------------
class DeviceData {
public:
    // ??0DeviceData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVDevice@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = Device;
    explicit DeviceData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058b10 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~DeviceData). B5 DEV-A (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1DeviceData@Video@@UEAA@XZ  0x1800f8cf0.
    virtual ~DeviceData();
    virtual bool EnumerateInterfaces() { detail::CompanionPureCall(); }                      // [ 1]
    virtual bool CreateDevice() { detail::CompanionPureCall(); }                             // [ 2]
    virtual void Shutdown() { detail::CompanionPureCall(); }                                 // [ 3]
    // [ 4] name unknown. Retail's one caller, Engine::Loader::Run 0x1800dbfc0,
    // passes a bool in DL: 0x1800dbfe7 MOV DL,1 before the load, 0x1800dc028
    // XOR EDX,EDX before the closing tail JMP [RAX+0x20]. dx11's body
    // (0x180001190) is a bare `ret`.
    virtual void  Slot4(bool loading) { (void)loading; detail::CompanionPureCall(); }          // [ 4] dx11: void stub
    virtual float Slot5() { detail::CompanionPureCall(); }                                   // [ 5] dx11: returns 0.0f
    virtual bool  Slot6() { detail::CompanionPureCall(); }                                   // [ 6] dx11: returns false
    virtual bool  Slot7() { detail::CompanionPureCall(); }                                   // [ 7] dx11: returns true
    // HCURSOR-shaped: (imageBuffer, hotspotX, hotspotY).
    // B5 CLOSE: the image is typed -- retail's `Video::Device::SetCursor` passes an
    // `ExternalPtr<ImageBuffer>` and the one caller (UI::UserInterface::SetCursor)
    // reads a `MaterialTexture::image`, itself a `Video::ImageBuffer*`.
    virtual void SetImageBuffer(ImageBuffer* imageBuffer, int hotX, int hotY) { detail::CompanionPureCall(); }  // [ 8]
    // [ 9]..[19] — the eleven companion factories, in retail slot order. Each
    // takes the owning Video-side object and returns the new `*Data`; B.4
    // (R-VIDEO) types both, as `Video::Device`'s eleven `Create*Data` forwarders'
    // mangled names spell them (`?CreateImageBufferData@Device@Video@@QEAAPEAVImageBufferData@2@PEAVImageBuffer@2@@Z`
    // and its ten siblings). Layout-neutral: pointer parameters and returns.
    // B5 DEV-A: `VideoDX11::DeviceData` overrides all eleven with these exact
    // signatures (base-typed returns; dx11's table is the compiler's now).
    virtual ContextData*        CreateContextData(Context* owner) { detail::CompanionPureCall(); }                   // [ 9] +0x48
    virtual ImageBufferData*    CreateImageBufferData(ImageBuffer* owner) { detail::CompanionPureCall(); }           // [10] +0x50
    virtual ConstantBufferData* CreateConstantBufferData(ConstantBuffer* owner) { detail::CompanionPureCall(); }     // [11] +0x58
    virtual IndexBufferData*    CreateIndexBufferData(IndexBuffer* owner) { detail::CompanionPureCall(); }           // [12] +0x60
    virtual VertexBufferData*   CreateVertexBufferData(VertexBuffer* owner) { detail::CompanionPureCall(); }         // [13] +0x68
    virtual VertexLayoutData*   CreateVertexLayoutData(VertexLayout* owner) { detail::CompanionPureCall(); }         // [14] +0x70
    virtual VertexProgramData*  CreateVertexProgramData(VertexProgram* owner) { detail::CompanionPureCall(); }       // [15] +0x78
    virtual PixelProgramData*   CreatePixelProgramData(PixelProgram* owner) { detail::CompanionPureCall(); }         // [16] +0x80
    virtual RenderStateData*    CreateRenderStateData(RenderState* owner) { detail::CompanionPureCall(); }           // [17] +0x88
    virtual VideoProgramData*   CreateVideoProgramData(VideoProgram* owner) { detail::CompanionPureCall(); }         // [18] +0x90
    virtual RenderBufferData*   CreateRenderBufferData(RenderBuffer* owner) { detail::CompanionPureCall(); }         // [19] +0x98

    Owner* _owner;           // +0x08  Video::Device*
};
static_assert(offsetof(DeviceData, _owner) == 0x8, "DeviceData::_owner");

// ---- 20 slots -------------------------------------------------------------
class ContextData {
public:
    // ??0ContextData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVContext@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = Context;
    explicit ContextData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058a90 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~ContextData). B5 CTX (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1ContextData@Video@@UEAA@XZ  0x1800f8e30.
    virtual ~ContextData();
    virtual bool BeginCommands() { detail::CompanionPureCall(); }                            // [ 1]
    virtual void EndCommands() { detail::CompanionPureCall(); }                              // [ 2]
    virtual void Finish() { detail::CompanionPureCall(); }                                   // [ 3]
    virtual void Clear(ClearFlags flags, unsigned color,
                       float depth, int stencil) { detail::CompanionPureCall(); }            // [ 4]
    virtual void BindRenderBuffer() { detail::CompanionPureCall(); }                         // [ 5]
    virtual void BindViewport() { detail::CompanionPureCall(); }                             // [ 6]
    virtual void BindScissor() { detail::CompanionPureCall(); }                              // [ 7]
    virtual void BindTransforms() { detail::CompanionPureCall(); }                           // [ 8]
    virtual void BindTextureNone(int slot) { detail::CompanionPureCall(); }                  // [ 9]
    virtual void BindIndexNone() { detail::CompanionPureCall(); }                            // [10]
    virtual void BindVertexNone(int slot) { detail::CompanionPureCall(); }                   // [11]
    virtual void DrawIndexed(int primitiveType, int baseVertex, int vertexCount,
                             int startIndex, int indexCount,
                             int instanceCount) { detail::CompanionPureCall(); }             // [12]
    virtual int  GetMaxDynamicInstances(int a, int b, int c) { detail::CompanionPureCall(); } // [13]
    virtual bool BeginVertices(int vertexCount, int vertexStride) { detail::CompanionPureCall(); }        // [14]
    virtual void EndVertices(int primitiveType) { detail::CompanionPureCall(); }                          // [15]
    virtual bool BeginIndexedVertices(int vertexCount, int vertexStride,
                                      int indexCount) { detail::CompanionPureCall(); }                    // [16]
    virtual void EndIndexedVertices(int primitiveType) { detail::CompanionPureCall(); }                   // [17]
    virtual bool BeginInstancedIndexedVertices(int vertexCount, int vertexStride,
                                               int indexCount,
                                               int instanceCount) { detail::CompanionPureCall(); }        // [18]
    virtual void EndInstancedIndexedVertices(int primitiveType) { detail::CompanionPureCall(); }          // [19]

    Owner* _owner;           // +0x08  Video::Context*
};
static_assert(offsetof(ContextData, _owner) == 0x8, "ContextData::_owner");

// ---- 7 slots --------------------------------------------------------------
class ImageBufferData {
public:
    // ??0ImageBufferData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVImageBuffer@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = ImageBuffer;
    explicit ImageBufferData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058a10 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~ImageBufferData). B5 RES (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1ImageBufferData@Video@@UEAA@XZ  0x1800f8e10.
    virtual ~ImageBufferData();
    virtual void AttachColorBuffer() { detail::CompanionPureCall(); }                        // [1]
    virtual void AttachDepthBuffer() { detail::CompanionPureCall(); }                        // [2]
    // B5 RES, PARAMETER TYPED FROM THE CALLERS (was `const void* levels`). Both
    // dispatchers pass their own `LevelArray* levels` argument straight through:
    // `?Create@ImageBuffer@Video@@UEAA_N...` (0x1800f2680, buffers.cpp) and
    // `Graphics::FrameCopyImageBuffer::Create` (0x1800a3b87.., wave23_lane_gfxbody
    // .cpp), whose mangled parameter is `PEAV?$Array@V?$Array@E@Collection@@@
    // Collection@@` -- i.e. `Video::ImageBuffer::LevelArray`, a
    // `Collection::Array<Collection::Array<uint8_t>>`. VideoDX11 (0x180002870)
    // reads only `_data` / `_size` of the outer array and the leading `_data` of
    // each 0x10-byte element, which is exactly that type's layout. `const`: the
    // slot never writes through it. (A nested typedef cannot be forward-declared,
    // so the template is spelled out; Collection.h includes only std headers.)
    virtual bool Create(const Collection::Array<Collection::Array<uint8_t> >* levels) { detail::CompanionPureCall(); }  // [3]
    // ENDGAME B.3.5 block J, SIGNATURE CORRECTED. `unknownArg` was not unknown:
    // it is the `Video::Context*`. Read as DISASSEMBLY this pass from the only
    // caller, `?BindTextures@Context@Video@@` 0x1800ef330 --
    //   1800ef360  MOV RCX,[RCX+0x60]   ; img->_data, the companion
    //   1800ef364  MOV R8D,EDI          ; the slot index
    //   1800ef367  MOV RDX,RSI          ; `this`, the Context
    //   1800ef36d  CALL [RAX+0x20]      ; slot 4
    // -- so RDX carries the Context, exactly as it does on the six other Bind
    // slots. VideoDX11's body ignores it (src/render/dx11/imagebufferdata.cpp says so at
    // the parameter); the CALLER still sets it, and the name should say what the
    // register holds rather than that one implementation does not read it.
    virtual void Bind(void* context, unsigned slot) { detail::CompanionPureCall(); }         // [4]
    virtual bool Update(void* data, int width, int height) { detail::CompanionPureCall(); }  // [5]
    virtual bool Read(void* dst, int width, int height) { detail::CompanionPureCall(); }     // [6]

    Owner* _owner;           // +0x08  Video::ImageBuffer*

    // NOT RETAIL (ext.sharp_ui, docs/extensions/DESIGN.md §3 rule 5): false by
    // default and in the faithful profile. Only set inside a UI pass that the
    // game side's Ext::SharpUI::Draw wraps, at an integer device-pixel ratio >= 2:
    // SpriteContext::Flush sets it per batch (ui/SharpDraw.h) and Ext::SharpUI::Draw
    // clears it on exit. While it is set, a backend's Bind (slot 4) binds a
    // POINT sampler with the image's own address modes instead of the one built
    // at creation. Every image bound inside that window is a UI sheet's
    // (SpriteContext::Flush's material Setup is the only binder there). A
    // static: the layout above is untouched.
    static bool _extPointSampling;
};
static_assert(offsetof(ImageBufferData, _owner) == 0x8, "ImageBufferData::_owner");

// ---- 7 slots --------------------------------------------------------------
class IndexBufferData {
public:
    // ??0IndexBufferData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVIndexBuffer@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = IndexBuffer;
    explicit IndexBufferData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058910 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~IndexBufferData). B5 BUF (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1IndexBufferData@Video@@UEAA@XZ  0x1800f8df0.
    virtual ~IndexBufferData();
    virtual bool Create(uint32_t sizeBytes, uint8_t dynamic,
                        const void* data) { detail::CompanionPureCall(); }                   // [1]
    virtual bool SetData(int32_t sizeBytes, int32_t offsetBytes,
                         const void* src) { detail::CompanionPureCall(); }                   // [2]
    // `Video::IndexBuffer::Lock`/`Unlock` (companion_data.cpp) are three-
    // instruction tail-call trampolines into these two, +0x18 and +0x20.
    virtual void* Lock(int32_t sizeBytes, int32_t* outByteOffset) { detail::CompanionPureCall(); }  // [3] +0x18
    virtual void  Unlock() { detail::CompanionPureCall(); }                                  // [4] +0x20
    // ENDGAME B.3.5 block J, SIGNATURE CORRECTED -- this slot takes the Context.
    // It was declared `Bind()` because VideoDX11's body (0x180003380) reads only
    // `this`. But the ABSTRACT slot is what the CALLER dispatches through, and
    // `?BindResources@Context@Video@@`'s index-buffer arm sets RDX, read as
    // disassembly this pass --
    //   1800ef1de  MOV RCX,[RCX+0x8]    ; ib->platformData
    //   1800ef1e2  MOV RDX,RBX          ; `this`, the Context
    //   1800ef1e8  CALL [RAX+0x28]      ; slot 5
    // -- so the argument exists and one implementation declining to read it is not
    // the slot's arity. RULE B5-14 / the caller brief: THE SLOT DECIDES, and the
    // arity comes from retail. (VertexBufferData's slot 5, below, takes four; that
    // is still the one slot where the two shapes differ.)
    virtual void  Bind(void* context) { detail::CompanionPureCall(); }                       // [5]
    // B5 BUF: the serializer is TYPED. `?Serialize@IndexBuffer@Video@@QEAAXAEAV
    // Serializer@IO@@@Z` (0x1800f38c0) hands its `IO::Serializer&` straight to this
    // slot, so the `void*` was an erasure, not a different argument (same register,
    // same bits); VideoDX11's body (0x1800033b0) reads it as the IO::Serializer.
    virtual void  Serialize(IO::Serializer* serializer) { detail::CompanionPureCall(); }     // [6]

    Owner* _owner;           // +0x08  Video::IndexBuffer*
};
static_assert(offsetof(IndexBufferData, _owner) == 0x8, "IndexBufferData::_owner");

// ---- 7 slots --------------------------------------------------------------
class VertexBufferData {
public:
    // ??0VertexBufferData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVVertexBuffer@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = VertexBuffer;
    explicit VertexBufferData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058890 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~VertexBufferData). B5 BUF (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1VertexBufferData@Video@@UEAA@XZ  0x1800f8d50.
    virtual ~VertexBufferData();
    virtual bool Create(uint32_t sizeBytes, uint8_t dynamic,
                        const void* data) { detail::CompanionPureCall(); }                   // [1]
    virtual bool SetData(int32_t sizeBytes, int32_t offsetBytes,
                         const void* src) { detail::CompanionPureCall(); }                   // [2]
    virtual void* Lock(int32_t sizeBytes, int32_t* outByteOffset) { detail::CompanionPureCall(); }  // [3] +0x18
    virtual void  Unlock() { detail::CompanionPureCall(); }                                  // [4] +0x20
    // The one slot where the vertex and index shapes differ.
    virtual void  Bind(void* context, uint32_t startSlot, uint32_t offsetBytes,
                       uint32_t strideBytes) { detail::CompanionPureCall(); }                // [5]
    // B5 BUF: typed as IndexBufferData's is -- `?Serialize@VertexBuffer@Video@@
    // QEAAXAEAVSerializer@IO@@@Z` (0x1800f5a20) forwards its `IO::Serializer&` here.
    virtual void  Serialize(IO::Serializer* serializer) { detail::CompanionPureCall(); }     // [6]

    Owner* _owner;           // +0x08  Video::VertexBuffer*
};
static_assert(offsetof(VertexBufferData, _owner) == 0x8, "VertexBufferData::_owner");

// ---- 4 slots --------------------------------------------------------------
class ConstantBufferData {
public:
    // ??0ConstantBufferData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVConstantBuffer@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = ConstantBuffer;
    explicit ConstantBufferData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058990 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~ConstantBufferData). B5 BUF (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1ConstantBufferData@Video@@UEAA@XZ  0x1800f8e50.
    virtual ~ConstantBufferData();
    virtual bool Create() { detail::CompanionPureCall(); }                                   // [1]
    // `Video::ConstantBuffer::Bind` (0x1800eef70) tail-jumps into this one
    // through +0x10, off the owner's +0x40.
    // ENDGAME B.3.5 block J, SIGNATURE CORRECTED IN TWO PLACES. `unused` is the
    // `Video::Context*`, and `unused2` is NOT A POINTER -- it is a 32-bit int.
    // Two independent proofs:
    //   * retail's mangling of the owner-side trampoline,
    //     `?Bind@ConstantBuffer@Video@@QEAAXAEAVContext@2@PEAXHH@Z`
    //     = (Context&, void*, int, int), which tail-jumps straight into this slot;
    //   * the five call sites in `?BindResources@Context@Video@@`, read as
    //     disassembly this pass (0x1800ef249, ef274, ef29f, ef2ca, ef2f5), each of
    //     which is
    //       MOV  RCX,[RBX+<buf>+0x40]   ; the companion
    //       LEA  R8,[RBX+<staging>]     ; the payload
    //       XOR  R9D,R9D                ; <-- a DWORD zero, i.e. an int 0
    //       MOV  RDX,RBX                ; `this`, the Context
    //       MOV  dword [RSP+0x20],<len>
    //       CALL [RAX+0x10]             ; slot 2
    // A `void*` fourth parameter would have been `XOR R9,R9`. Declaring it as one
    // is the kind of quiet type error a caller conversion has to pass an ugly cast
    // through, which is how it was found. VideoDX11 reads neither argument.
    virtual void Bind(void* context, const void* srcData, int32_t first,
                      int32_t sizeBytes) { detail::CompanionPureCall(); }                    // [2] +0x10
    virtual int32_t SlotIndexPlusOne() { detail::CompanionPureCall(); }                      // [3]

    Owner* _owner;           // +0x08  Video::ConstantBuffer*
};
static_assert(offsetof(ConstantBufferData, _owner) == 0x8, "ConstantBufferData::_owner");

// ---- 4 slots --------------------------------------------------------------
class RenderBufferData {
public:
    // ??0RenderBufferData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVRenderBuffer@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = RenderBuffer;
    explicit RenderBufferData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058590 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~RenderBufferData). B5 PILOT (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1RenderBufferData@Video@@UEAA@XZ  0x1800f8db0.
    virtual ~RenderBufferData();
    virtual bool Slot1() { detail::CompanionPureCall(); }                                    // [1] dx11: returns true
    virtual void Bind(void* context) { detail::CompanionPureCall(); }                        // [2]
    // B5 CLOSE: `other` is typed from retail's mangling of the owner-side member,
    // `?Resolve@RenderBuffer@Video@@QEBAXAEAVContext@2@PEAV12@_N@Z` (RenderBuffer*).
    virtual void Resolve(void* context, RenderBuffer* other, bool flag) { detail::CompanionPureCall(); }  // [3]

    Owner* _owner;           // +0x08  Video::RenderBuffer*
};
static_assert(offsetof(RenderBufferData, _owner) == 0x8, "RenderBufferData::_owner");

// ---- 3 slots --------------------------------------------------------------
class VertexLayoutData {
public:
    // ??0VertexLayoutData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVVertexLayout@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = VertexLayout;
    explicit VertexLayoutData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058810 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~VertexLayoutData). B5 RES (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1VertexLayoutData@Video@@UEAA@XZ  0x1800f8d30.
    virtual ~VertexLayoutData();
    virtual void Bind(void* context) { detail::CompanionPureCall(); }                        // [1]
    // void, unlike VertexProgramData's and PixelProgramData's bool — VideoDX11's
    // 0x180003ff0 falls off its end with no return value set.
    virtual void PlatformCreate() { detail::CompanionPureCall(); }                           // [2] +0x10

    Owner* _owner;           // +0x08  Video::VertexLayout*
};
static_assert(offsetof(VertexLayoutData, _owner) == 0x8, "VertexLayoutData::_owner");

// ---- 3 slots --------------------------------------------------------------
class VertexProgramData {
public:
    // ??0VertexProgramData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVVertexProgram@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = VertexProgram;
    explicit VertexProgramData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058790 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~VertexProgramData). B5 RES (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1VertexProgramData@Video@@UEAA@XZ  0x1800f8d10.
    virtual ~VertexProgramData();
    virtual bool PlatformCreate() { detail::CompanionPureCall(); }                           // [1]
    virtual void Bind(void* context, int permutation) { detail::CompanionPureCall(); }        // [2]

    Owner* _owner;           // +0x08  Video::VertexProgram*
};
static_assert(offsetof(VertexProgramData, _owner) == 0x8, "VertexProgramData::_owner");

// ---- 3 slots --------------------------------------------------------------
class PixelProgramData {
public:
    // ??0PixelProgramData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVPixelProgram@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = PixelProgram;
    explicit PixelProgramData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058710 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~PixelProgramData). B5 RES (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1PixelProgramData@Video@@UEAA@XZ  0x1800f8dd0.
    virtual ~PixelProgramData();
    virtual bool PlatformCreate() { detail::CompanionPureCall(); }                           // [1]
    virtual void Bind(void* context, int permutation) { detail::CompanionPureCall(); }        // [2]

    Owner* _owner;           // +0x08  Video::PixelProgram*
};
static_assert(offsetof(PixelProgramData, _owner) == 0x8, "PixelProgramData::_owner");

// ---- 3 slots --------------------------------------------------------------
class RenderStateData {
public:
    // ??0RenderStateData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVRenderState@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = RenderState;
    explicit RenderStateData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058690 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~RenderStateData). B5 RES (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1RenderStateData@Video@@UEAA@XZ  0x1800f8d70 (its old
    // registered door in device.cpp is retired, B.3.9 VID).
    virtual ~RenderStateData();
    virtual bool Create() { detail::CompanionPureCall(); }                                   // [1]
    virtual void Bind(void* context) { detail::CompanionPureCall(); }                        // [2]

    Owner* _owner;           // +0x08  Video::RenderState*
};
static_assert(offsetof(RenderStateData, _owner) == 0x8, "RenderStateData::_owner");

// ---- 3 slots --------------------------------------------------------------
class VideoProgramData {
public:
    // ??0VideoProgramData(owner) -- the REAL constructor: the owner at +0x08 (retail
    // stores it first, then the vptr; nothing can observe the order).
    // B5 (the dx11 seam) W0: the owner is TYPED -- the eleven `Device::Create*Data`
    // forwarders' mangled names spell it (`PEAVVideoProgram@2@`); `Owner` is for the
    // subclass and the tests.
    using Owner = VideoProgram;
    explicit VideoProgramData(Owner* owner) : _owner(owner) {}
    // B5 W0 (B5_RULES D2): each of the twelve bases is its OWN root, so each owns its
    // allocation -- retail's factory `MemAlloc(size, 4)` and slot 0's `MemFree`
    // (src/engine/video/companion_operators.cpp; host arm under BANISHED_UNIT_TEST).
    static void* operator new(size_t n);
    static void  operator delete(void* p);

    // [0] 0x180058610 -- MSVC's VECTOR deleting destructor (element 0x10, over
    // ~VideoProgramData). B5 PILOT (B5_RULES D1): a real virtual destructor -- the
    // compiler's D0/D1 pair is slot 0 now, and the base's class-scope `operator
    // delete` above is retail's MemFree on `flags & 1`. The body (empty) stays in
    // companion_data.cpp: ??1VideoProgramData@Video@@UEAA@XZ  0x1800f8d90.
    virtual ~VideoProgramData();
    virtual bool Slot1() { detail::CompanionPureCall(); }                                    // [1] dx11: returns true
    // ENDGAME B.3.5 block J, SLOT NAMED AND SIGNATURE CORRECTED. It was `Slot2()`
    // because VideoDX11 implements it as a bare `RET` (0x180001190), so the label
    // block A copied from the seam said nothing. The CALLER says what it is:
    // `?BindResources@Context@Video@@`'s video-program arm, read as disassembly
    // this pass --
    //   1800ef0dd  MOV RAX,[RBX+0x200]  ; the Video::VideoProgram
    //   1800ef0e4  MOV R8D,[RBX+0xc]    ; the Context's shader-detail level
    //   1800ef0e8  MOV RDX,RBX          ; `this`, the Context
    //   1800ef0eb  MOV RCX,[RAX+0x40]   ; the companion -- +0x40, NOT the
    //                                   ; +0x50 a Vertex/PixelProgram uses
    //   1800ef0f2  CALL [RAX+0x10]      ; slot 2
    // -- which is the SAME three-argument shape as `VertexProgramData::Bind` and
    // `PixelProgramData::Bind` at the same slot index. So slot 2 is `Bind`, and
    // this backend simply implements no video-program binding (src/render/dx11/
    // smallclasses.cpp: "VideoProgramData really is inert").
    virtual void Bind(void* context, int detail) { detail::CompanionPureCall(); }             // [2] dx11: inert stub

    Owner* _owner;           // +0x08  Video::VideoProgram*
};
static_assert(offsetof(VideoProgramData, _owner) == 0x8, "VideoProgramData::_owner");

// The two offsets, once per class, as the two things every one of the twelve
// really claims: the vptr is the first qword and `owner` is the second. A
// `sizeof` assert would be the false witness RULE B5-1.1 names.
#define RT_VIDEO_COMPANION_PINS(CLASS)                                          \
    static_assert(sizeof(CLASS) == sizeof(RtVideo::CompanionData),              \
                  #CLASS " is the two-pointer object CompanionData pins");      \
    static_assert(alignof(CLASS) == alignof(void*), #CLASS " is qword-aligned");\
    static_assert(offsetof(CLASS, _owner) == 0x08, #CLASS "::_owner at +0x08")

RT_VIDEO_COMPANION_PINS(DeviceData);
RT_VIDEO_COMPANION_PINS(ContextData);
RT_VIDEO_COMPANION_PINS(ImageBufferData);
RT_VIDEO_COMPANION_PINS(IndexBufferData);
RT_VIDEO_COMPANION_PINS(VertexBufferData);
RT_VIDEO_COMPANION_PINS(ConstantBufferData);
RT_VIDEO_COMPANION_PINS(RenderBufferData);
RT_VIDEO_COMPANION_PINS(VertexLayoutData);
RT_VIDEO_COMPANION_PINS(VertexProgramData);
RT_VIDEO_COMPANION_PINS(PixelProgramData);
RT_VIDEO_COMPANION_PINS(RenderStateData);
RT_VIDEO_COMPANION_PINS(VideoProgramData);

#undef RT_VIDEO_COMPANION_PINS

// ENDGAME B3X (the normal-virtual cutover) EXEMPTED the twelve `*Data` classes
// (B3X_RULES X1) while every live companion wore dx11's hand-built table in
// retail slot order. B5 (the dx11 seam) ended that: the subclasses are real and
// the compiler builds every table, so the twelve are on `virtual ~XData()` like
// the 449 classes of the cutover.
static_assert(std::has_virtual_destructor<DeviceData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<ContextData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<ImageBufferData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<IndexBufferData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<VertexBufferData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<ConstantBufferData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<RenderBufferData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<VertexLayoutData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<VertexProgramData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<PixelProgramData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<RenderStateData>::value,
              "B5: the dx11 subclass wears the compiler's table");
static_assert(std::has_virtual_destructor<VideoProgramData>::value,
              "B5: the dx11 subclass wears the compiler's table");

}  // namespace Video

#pragma GCC diagnostic pop
