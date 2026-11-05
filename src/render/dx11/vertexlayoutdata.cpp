// VideoDX11 VertexLayoutData — vtable 0x180018878, all three slots.
//
//   slot  address      role
//   ----  -----------  ----------------------------------------------------
//     0   0x180003f60  scalar deleting destructor
//     1   0x180003fc0  Bind(Context&)      -> IASetInputLayout
//     2   0x180003ff0  PlatformCreate()    -> CreateInputLayout
//
// Slot names from the Runtime export table:
//     ?Bind@VertexLayout@Video@@QEBAXAEAVContext@2@@Z     (Context&, ignored)
//     ?PlatformCreate@VertexLayout@Video@@AEAAXXZ         (void return — note
//        this differs from VertexProgram/PixelProgram, whose PlatformCreate
//        returns bool. 0x180003ff0 indeed falls off the end with no return
//        value set, so the void signature here is not an oversight.)
//
// This class is the only user of the DLL's single semantic string "TEXCOORD"
// (VideoDX11 .rdata 0x180018868).
//
// ---------------------------------------------------------------------------
// Why every element is TEXCOORD<i>
// ---------------------------------------------------------------------------
// Video::VertexLayout::Element carries a Usage enum and a usage index — the
// Runtime API is
//     ?Add@VertexLayout@Video@@QEAAXHHW4Usage@12@HW4Format@12@@Z
//       void VertexLayout::Add(int stream, int offset, Usage, int usageIndex,
//                              Format)
// — but the DX11 backend reads NEITHER of them. It names element `i`
// "TEXCOORD" with SemanticIndex `i`, so the semantic is purely positional and
// the HLSL must declare every input as TEXCOORD0..TEXCOORDn. That is a
// deliberate design choice in the shipped renderer, not a decompilation gap:
// the two fields it does read out of an Element are the stream and the byte
// offset, and the semantic never enters the picture.
//
// SemanticIndex is written from the LOW BYTE of the loop counter
// (`MOVZX EAX,R10B`), so it wraps at 256. Faithfully reproduced; with a 64-slot
// element buffer it is unreachable, but it costs nothing to be exact.

#include "dx11_backend_access.h"

// The real `Video::VertexLayoutData` abstract base. B5 RES: `VideoDX11::
// VertexLayoutData` derives from it in C++ (src/render/dx11/VideoDX11Data.h), so the
// compiler chains its destructor and builds its vtable.
#include "video/CompanionData.h"

#include <cstdio>

namespace VideoDX11 {

// Video::VertexLayout::Format -> DXGI_FORMAT. Built on the stack at
// [RSP+0x30..0x50] by 0x180003ff0 as nine dwords, in this order:
//
//     0 -> 0x02  DXGI_FORMAT_R32G32B32A32_FLOAT
//     1 -> 0x06  DXGI_FORMAT_R32G32B32_FLOAT
//     2 -> 0x10  DXGI_FORMAT_R32G32_FLOAT
//     3 -> 0x29  DXGI_FORMAT_R32_FLOAT
//     4 -> 0x0e  DXGI_FORMAT_R16G16B16A16_SINT
//     5 -> 0x26  DXGI_FORMAT_R16G16_SINT
//     6 -> 0x57  DXGI_FORMAT_B8G8R8A8_UNORM
//     7 -> 0x1e  DXGI_FORMAT_R8G8B8A8_UINT
//     8 -> 0x1c  DXGI_FORMAT_R8G8B8A8_UNORM
//
// The original indexes this table with the raw int32 from the element and does
// not range-check it. We keep the table verbatim (spelling the DXGI names for
// readability); the port checks the formats before the loop (review S-043).
static const DXGI_FORMAT kElementFormat[9] = {
    DXGI_FORMAT_R32G32B32A32_FLOAT,   // 0x02
    DXGI_FORMAT_R32G32B32_FLOAT,      // 0x06
    DXGI_FORMAT_R32G32_FLOAT,         // 0x10
    DXGI_FORMAT_R32_FLOAT,            // 0x29
    DXGI_FORMAT_R16G16B16A16_SINT,    // 0x0e
    DXGI_FORMAT_R16G16_SINT,          // 0x26
    DXGI_FORMAT_B8G8R8A8_UNORM,       // 0x57
    DXGI_FORMAT_R8G8B8A8_UINT,        // 0x1e
    DXGI_FORMAT_R8G8B8A8_UNORM        // 0x1c
};
static_assert(static_cast<int>(DXGI_FORMAT_R32G32B32A32_FLOAT) == 0x02, "fmt 0");
static_assert(static_cast<int>(DXGI_FORMAT_R32G32B32_FLOAT)    == 0x06, "fmt 1");
static_assert(static_cast<int>(DXGI_FORMAT_R32G32_FLOAT)       == 0x10, "fmt 2");
static_assert(static_cast<int>(DXGI_FORMAT_R32_FLOAT)          == 0x29, "fmt 3");
static_assert(static_cast<int>(DXGI_FORMAT_R16G16B16A16_SINT)  == 0x0e, "fmt 4");
static_assert(static_cast<int>(DXGI_FORMAT_R16G16_SINT)        == 0x26, "fmt 5");
static_assert(static_cast<int>(DXGI_FORMAT_B8G8R8A8_UNORM)     == 0x57, "fmt 6");
static_assert(static_cast<int>(DXGI_FORMAT_R8G8B8A8_UINT)      == 0x1e, "fmt 7");
static_assert(static_cast<int>(DXGI_FORMAT_R8G8B8A8_UNORM)     == 0x1c, "fmt 8");

// The original builds its D3D11_INPUT_ELEMENT_DESC array on the stack:
// `SUB RSP,0x868`, descs start at RSP+0x60 and the frame's saved-register area
// begins at RSP+0x860 — exactly 0x800 bytes, i.e. 64 descriptors of 0x20.
constexpr int kMaxInputElements = 0x800 / 32;
static_assert(kMaxInputElements == 64, "0x180003ff0 stack buffer holds 64 descs");
static_assert(sizeof(D3D11_INPUT_ELEMENT_DESC) == 32,
              "D3D11_INPUT_ELEMENT_DESC must be 32 bytes for the 0x20 stride "
              "the original writes with");

// 0x180003f60 — scalar deleting destructor.
//
//     180003f7d  LEA  RAX,[0x180018878]      ; DX11 vtable
//                MOV  qword ptr [RCX],RAX
//     180003f93  if (this->inputLayout) inputLayout->Release()   ; [vt+0x10]
//     180003f9a  CALL [0x180013328]          ; Video::VertexLayoutData::~VertexLayoutData
//     180003fa9  CALL [0x1800134d0]          ; MemFree, only when (flags & 1)
//                returns `this`
//
// B5 RES: the vptr re-seat, the base-destructor chain and the conditional
// MemFree are the compiler's (its D0 + the base's class-scope `operator
// delete`, B5 W0, companion_operators.cpp). The body is the one Release, not
// followed by a null store (the original makes none).
VertexLayoutData::~VertexLayoutData() {
    if (inputLayout) inputLayout->Release();
}

// 0x180003fc0 — VertexLayout::Bind(Context&).
//
//     488b05f1f40000  MOV RAX,[0x1800134b8]  ; Video::Device**
//     4c8bc1          MOV R8,RCX             ; this
//     488b10          MOV RDX,[RAX]          ; Video::Device*
//     488b8200010000  MOV RAX,[RDX+0x100]    ; DeviceData*
//     498b5010        MOV RDX,[R8+0x10]      ; this->inputLayout
//     488b4828        MOV RCX,[RAX+0x28]     ; ID3D11DeviceContext*
//     488b01          MOV RAX,[RCX]
//     48ffa088000000  JMP [RAX+0x88]         ; index 17 = IASetInputLayout
//
// The Context& in RDX is overwritten before use — ignored, like every other
// resource Bind in this DLL. The layout pointer is passed through unchecked,
// so binding a VertexLayout whose PlatformCreate never ran sets a null input
// layout rather than doing nothing. Preserved.
void VertexLayoutData::Bind(void* /*Video::Context& — ignored*/) {
    Dx11Context()->IASetInputLayout(inputLayout);
}

// 0x180003ff0 — VertexLayout::PlatformCreate().
//
//     180003ffa  MOV R9,[RCX+0x8]                 ; owner = this->vertexLayout
//     180004001  CMP dword ptr [R9+0x40],0x0      ; input-signature length
//     180004006  JZ  return                       ;   zero -> do nothing at all
//     18000400c  MOV R8D,[R9+0x28]                ; element count
//     180004016..180004056  build the 9-entry format table on the stack
//     180004061  TEST R8D,R8D / JLE skip-loop     ; count <= 0 -> no elements,
//                                                 ;   but STILL calls Create
//     180004073  per element i:
//                  desc[i].InputSlot         = (u16)elem[i]+0x08
//                  desc[i].AlignedByteOffset = (u16)elem[i]+0x0C
//                  desc[i].SemanticName      = "TEXCOORD"
//                  desc[i].InputSlotClass    = 0  (D3D11_INPUT_PER_VERTEX_DATA)
//                  desc[i].InstanceDataStepRate = 0
//                  desc[i].Format            = table[(i32)elem[i]+0x18]
//                  desc[i].SemanticIndex     = (u8)i
//     1800040f7  CALL [devicevt+0x58]             ; index 11 = CreateInputLayout
//                    (descs, count, owner->[0x38], (i64)owner->[0x40],
//                     &this->inputLayout)
//
// Two guards worth spelling out, because they are asymmetric:
//   * A zero input-signature length aborts the whole function — no descriptors
//     are built and CreateInputLayout is never called.
//   * A zero (or negative) element count only skips the descriptor loop.
//     CreateInputLayout is still called, with that count widened to UINT. We
//     match this rather than "fixing" it; D3D11 rejects it and returns a
//     failing HRESULT, which the original ignores too (the return value is
//     never tested).
//
// The one deliberate divergence in this file: the original has no bound on the
// element count against its 64-descriptor stack buffer, so a layout with more
// than 64 elements would smash its own frame. We clamp and log instead. This
// is unreachable in practice (a D3D11 input layout is limited to 32 input
// slots and Banished's layouts are single digits), and "reproduce a stack
// overflow exactly" is not a useful fidelity goal.
void VertexLayoutData::PlatformCreate() {
    // ENDGAME B.3.5 block A: was `const VertexLayoutOwner*`, a partial view in
    // dx11_backend_access.h. `Video::VertexLayout` is now a real class
    // (src/engine/video/VertexLayout.h) and this is the same four offsets.
    // B5 RES: the base's `_owner` is typed `Video::VertexLayout*`.
    const Video::VertexLayout* owner = _owner;

    if (owner->_signature.GetSize() == 0) return;

    int count = owner->_elements.GetSize();
    if (count > kMaxInputElements) {
        fprintf(stderr,
                "[Dx11] VertexLayoutData::PlatformCreate: %d elements exceeds "
                "the original's %d-descriptor buffer; clamping\n",
                count, kMaxInputElements);
        count = kMaxInputElements;
    }

    // Left uninitialised on purpose: the original only writes [0, count) and
    // hands the raw stack buffer to D3D11 with that same count.
    D3D11_INPUT_ELEMENT_DESC descs[kMaxInputElements];

    const Video::VertexLayout::Element* elems =
        owner->_elements.GetData();   // B.3.2: was a cast off a void*

    // NOT RETAIL (UB, out-of-bounds index; review S-043): retail 0x180003ff0 indexes its
    // 9-entry table with the raw format of each element, so a format >= 9 (or negative) from
    // a corrupt or mod MeshLayout .rsc read past it. Such a layout is refused the way an empty
    // signature is: no input layout is created. Every retail format is 0..8 and passes.
    for (int i = 0; i < count; ++i) {
        if (!Video::VertexLayout::FormatInRange(elems[i].format)) {
            static bool s_said = false;
            if (!s_said) {
                s_said = true;
                fprintf(stderr, "[Dx11] VertexLayoutData::PlatformCreate: element %d has format %d "
                                "(0..8 are valid); no input layout\n",
                        i, static_cast<int>(elems[i].format));
            }
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        // `format` was checked above (NOT RETAIL, review S-043); the original
        // indexes its 9-entry table with the raw int32 from the element.
        descs[i].SemanticName         = "TEXCOORD";
        descs[i].SemanticIndex        = static_cast<UINT>(
                                            static_cast<uint8_t>(i));
        descs[i].Format               = kElementFormat[elems[i].format];
        // `MOVZX ...,word` in the original: `stream` and `offset` are int32
        // fields and only their low 16 bits reach D3D11. The former
        // `VertexLayoutElement` view spelled both as `uint16_t`, which hid the
        // truncation inside the type; with the real class it is explicit here.
        descs[i].InputSlot            = static_cast<uint16_t>(elems[i].stream);
        descs[i].AlignedByteOffset    = static_cast<uint16_t>(elems[i].offset);
        descs[i].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;  // 0
        descs[i].InstanceDataStepRate = 0;
    }

    Dx11Device()->CreateInputLayout(
        descs,
        // `count` == owner->elements.GetSize() in every reachable case; it differs
        // only on the clamped path above, where passing the unclamped value
        // would hand D3D11 descriptors we never wrote.
        static_cast<UINT>(count),
        owner->_signature.GetData(),
        static_cast<SIZE_T>(owner->_signature.GetSize()),
        &inputLayout);
}

}  // namespace VideoDX11
