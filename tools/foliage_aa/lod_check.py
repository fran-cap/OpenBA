"""FAA-SPIKE S6: kMode 5 writes o0.rgb = saturate((iso lod, hardware lod, aniso lod) / 8) for the
transformed cutout programs. For every main-pass draw of a transformed program (a ps_4_1 blob),
read sample 0 of the target after the draw where the draw changed it, and compare the formulas
with the hardware's own `lod` (CalculateLevelOfDetailUnclamped).
  python lod_check.py <cap.rdc>
"""
import os, sys, struct, collections
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
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile(); assert cap.OpenFile(sys.argv[1], "", None) == rd.ResultCode.Succeeded
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None); assert res == rd.ResultCode.Succeeded
    texs = {int(t.resourceId): t for t in ctrl.GetTextures()}
    acts = walk(ctrl.GetRootActions())
    n = 0
    diso = collections.defaultdict(list)   # hw bucket -> |iso - hw|
    dani = collections.defaultdict(list)
    sgn_iso = collections.Counter()
    for i, a in enumerate(acts):
        if not (int(a.flags) & int(rd.ActionFlags.Drawcall)): continue
        ctrl.SetFrameEvent(a.eventId, False)
        refl = ctrl.GetPipelineState().GetShaderReflection(rd.ShaderStage.Pixel)
        if refl is None: continue
        raw = bytes(refl.rawBytes)
        # find the SHDR chunk's version token: ps_4_1 = 0x41
        k = raw.find(b"SHDR")
        if k < 0 or raw[k + 8] != 0x41: continue
        om = ctrl.GetD3D11PipelineState().outputMerger
        rid = om.renderTargets[0].resource
        t = texs[int(rid)]
        if t.msSamp < 2: continue   # the main pass only
        sub = rd.Subresource(0, 0, 0)
        ctrl.SetFrameEvent(acts[i - 1].eventId, True)
        b = bytes(ctrl.GetTextureData(rid, sub))
        ctrl.SetFrameEvent(a.eventId, True)
        f = bytes(ctrl.GetTextureData(rid, sub))
        for p in range(0, len(f), 4):
            if b[p:p + 3] == f[p:p + 3]: continue
            bl, gr, re = f[p], f[p + 1], f[p + 2]   # BGRA8
            iso, hw, ani = re * 8 / 255.0, gr * 8 / 255.0, bl * 8 / 255.0
            if hw <= 0.05 or hw >= 7.95: continue   # clamped by the saturate
            bucket = int(hw)
            diso[bucket].append(iso - hw)
            dani[bucket].append(ani - hw)
            n += 1
    print("pixels compared: %d (lod in (0.05, 7.95))" % n)
    print("hw lod | pixels | iso-hw mean / mean|.| / max|.| | aniso-hw mean / mean|.| / max|.|")
    for k in sorted(diso):
        a, c = diso[k], dani[k]
        print("  %d-%d | %7d | %+.3f / %.3f / %.3f | %+.3f / %.3f / %.3f" % (
            k, k + 1, len(a), sum(a) / len(a), sum(map(abs, a)) / len(a), max(map(abs, a)),
            sum(c) / len(c), sum(map(abs, c)) / len(c), max(map(abs, c))))
    alli = [x for v in diso.values() for x in v]; alla = [x for v in dani.values() for x in v]
    if alli:
        print("ALL: iso-hw mean %+.3f mean|.| %.3f; aniso-hw mean %+.3f mean|.| %.3f (quantum 8/255 = 0.031)" % (
            sum(alli) / len(alli), sum(map(abs, alli)) / len(alli), sum(alla) / len(alla), sum(map(abs, alla)) / len(alla)))
    ctrl.Shutdown(); cap.Shutdown(); rd.ShutdownReplay()


main()
