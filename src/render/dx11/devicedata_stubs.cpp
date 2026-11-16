// Phase 1 — VideoDX11 DeviceData slots 4, 5, 6, 7.
//
// The four one-liner stubs on the DeviceData vtable. These are pure virtuals on
// the Runtime-side Video::DeviceData that the DX11 backend declines to
// implement, so their *names* are unknown, but their bodies are three or four bytes
// each and fully decoded:
//
//   slot 4  0x180001190   c2 00 00        ret            -> void
//   slot 5  0x180005430   0f 57 c0 c3     xorps xmm0,xmm0; ret -> 0.0f
//   slot 6  0x180005440   32 c0 c3        xor al,al; ret -> false
//   slot 7  0x180001060   b0 01 c3        mov al,1; ret  -> true
//
// Ported first on purpose: they were the smallest possible change that
// exercised the whole seam — static registration, install-time patch, live
// dispatch — with a blast radius of nothing.
//
// B5 DEV-A (the dx11 seam): they are `VideoDX11::DeviceData` members overriding
// the base's `Slot4`..`Slot7` (src/engine/video/CompanionData.h), so each
// carries its base slot's signature — the CALLER's dispatch shape. The originals ignore
// what they are passed; Slot4 reads its bool only for the NOT RETAIL calls below. The four
// DX11_REGISTER_SLOT lines that installed them in the hand table are gone. (The
// original slot 4 is `ret 0`, not `ret N` — it pops nothing, so there is no
// stack adjustment to reproduce either.)
//
// Return registers match the originals exactly: `bool` returns in AL like
// `mov al,1` / `xor al,al`, and `float` returns in XMM0 like `xorps xmm0,xmm0`.
//
// Slots 4 and 7 are shared code in the original — 0x180001190 is also
// VideoProgramData slot 2, and 0x180001060 is also RenderBufferData slot 1 and
// VideoProgramData slot 1. A vtable holds a *slot*, not a *function*: those
// classes have their own members (smallclasses.cpp) and are untouched by this
// file.

#include "VideoDX11Data.h"
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "compress_ao/bc4_chain.h"           // NOT RETAIL: ext.compress_ao (the load's summary)
#include "share_gpu_data/share_create.h"     // NOT RETAIL: ext.share_gpu_data (the load's summary)
#include "create_on_demand/lazy_create.h"    // NOT RETAIL: ext.create_on_demand (the load's summary)
#include "texture_quality/texture_quality.h" // NOT RETAIL: ext.texture_quality (the load's summary)

namespace VideoDX11 {

// 0x180001190 — `ret`. The Runtime's one caller (Engine::Loader::Run
// 0x1800dbfc0) passes a bool in DL (1 around the load, 0 after); the body
// ignores it (B3R owed 3).
//
// NOT RETAIL (ext.async_gpu_create, docs/extensions/handins/ASYNCGPUCREATE.md): the bracket's two
// calls are the loader's own thread opening and closing a load, which is exactly the span whose
// seeded creates the extension may defer: `true` arms it for this thread (row on, driver with
// concurrent creates), `false` waits for every deferred create and disarms it. Off: two calls that
// test a bool and return, as the `ret` did nothing.
//
// NOT RETAIL (ext.compress_ao, docs/extensions/handins/COMPRESSAO.md): the close also prints the
// load's BC4 summary (when a texture was converted since the last) and asks the derived cache to
// persist its keep list, without waiting.
//
// NOT RETAIL (ext.share_gpu_data, docs/extensions/handins/SHAREGPUDATA.md): and, after the drain, the
// run's sharing summary (when anything was shared since the last).
//
// NOT RETAIL (ext.create_on_demand, docs/extensions/handins/CREATEONDEMAND.md): and the deferred
// creates' summary (when anything changed since the last).
//
// NOT RETAIL (ext.texture_quality, docs/extensions/handins/TEXQUALITY.md): and the dropped levels'
// summary (when anything changed since the last; nothing while the row is off).
void DeviceData::Slot4(bool loading) {
    if (loading) {
        AsyncCreate::Begin();
    } else {
        AsyncCreate::End();
        ::CompressAo::LoadEnd();
        ShareGpuData::LoadEnd();
        CreateOnDemand::LoadEnd();
        ::TextureQuality::PrintIfChanged("load end (run total)");
    }
}

// 0x180005430 — `xorps xmm0,xmm0; ret`
float DeviceData::Slot5() {
    return 0.0f;
}

// 0x180005440 — `xor al,al; ret`
bool DeviceData::Slot6() {
    return false;
}

// 0x180001060 — `mov al,1; ret`
bool DeviceData::Slot7() {
    return true;
}

}  // namespace VideoDX11
