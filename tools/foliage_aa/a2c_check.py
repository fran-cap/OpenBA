"""FAA-SPIKE S4: inside ONE capture (a deterministic replay), for every draw bound with
AlphaToCoverage, read the render target's samples before and after the draw and count:
  covered    samples whose RGB changed
  alphaChg   samples whose alpha changed (must be 0 with RenderTargetWriteMask = RGB)
  partial    pixels with 1..(n-1) of n samples changed (coverage from o0.w, not all-or-nothing)
  preAlphaNZ covered samples whose alpha was non-zero before the draw (retail writes o0.w = 0
             there, so these are the samples where the main target's alpha now differs from retail)
  python a2c_check.py <cap.rdc> [maxDraws]
"""
import os, sys, collections
# <repo>/tools/renderdoc_mcp/runtime, derived from this file; RENDERDOC_MODULE_PATH overrides.
MOD = os.environ.get("RENDERDOC_MODULE_PATH") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "renderdoc_mcp", "runtime"))
sys.path.insert(0, MOD); os.add_dll_directory(MOD)
import renderdoc as rd


def walk(roots):
    out = []
    def rec(a):
        out.append(a)
        for c in a.children: rec(c)
    for a in roots: rec(a)
    return out


def main():
    path = sys.argv[1]
    maxd = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile(); assert cap.OpenFile(path, "", None) == rd.ResultCode.Succeeded
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None); assert res == rd.ResultCode.Succeeded
    texs = {int(t.resourceId): t for t in ctrl.GetTextures()}
    acts = walk(ctrl.GetRootActions())
    tot = collections.Counter()
    hist_all = collections.Counter()
    n = 0
    for i, a in enumerate(acts):
        if not (int(a.flags) & int(rd.ActionFlags.Drawcall)): continue
        ctrl.SetFrameEvent(a.eventId, False)
        om = ctrl.GetD3D11PipelineState().outputMerger
        if not om.blendState.alphaToCoverage: continue
        if not len(om.renderTargets) or not int(om.renderTargets[0].resource): continue
        rid = om.renderTargets[0].resource
        t = texs[int(rid)]
        ns = max(1, t.msSamp)
        prev = acts[i - 1].eventId
        before, after = [], []
        for s in range(ns):
            sub = rd.Subresource(0, 0, s)
            ctrl.SetFrameEvent(prev, True)
            before.append(bytes(ctrl.GetTextureData(rid, sub)))
            ctrl.SetFrameEvent(a.eventId, True)
            after.append(bytes(ctrl.GetTextureData(rid, sub)))
        px = t.width * t.height
        changed = [bytearray(px) for _ in range(1)][0]
        c = collections.Counter()
        cnt = bytearray(px)
        for s in range(ns):
            b, f = before[s], after[s]
            if b == f: continue
            # BGRA8: compare per pixel
            for p in range(px):
                o = 4 * p
                if b[o:o + 3] != f[o:o + 3]:
                    c["covered"] += 1
                    cnt[p] += 1
                    if b[o + 3] != 0: c["preAlphaNZ"] += 1
                    if f[o + 3] != 0: c["postAlphaNZ"] += 1
                if b[o + 3] != f[o + 3]: c["alphaChg"] += 1
        hist = collections.Counter(cnt)
        del hist[0]
        partial = sum(v for k, v in hist.items() if k < ns)
        c["partial"] += partial
        c["pixels"] += sum(hist.values())
        print("eid %5d %dx%d ms%d covered %8d alphaChg %d preAlphaNZ %7d postAlphaNZ %7d pixels %7d partial %7d hist %s" % (
            a.eventId, t.width, t.height, ns, c["covered"], c["alphaChg"], c["preAlphaNZ"], c["postAlphaNZ"], c["pixels"], partial,
            dict(sorted(hist.items()))), flush=True)
        tot.update(c); hist_all.update(hist)
        n += 1
        if n >= maxd: break
    print("TOTAL over %d A2C draws: %s; samples-changed histogram %s" % (n, dict(tot), dict(sorted(hist_all.items()))))
    ctrl.Shutdown(); cap.Shutdown(); rd.ShutdownReplay()


main()
