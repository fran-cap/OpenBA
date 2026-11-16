"""FAA-SPIKE S5: for A2C draws into the MSAA main target, per sample, does depth change exactly
where colour changes (depth written only for covered samples)?
  python a2c_depth_check.py <cap.rdc> [maxDraws]
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
    maxd = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile(); assert cap.OpenFile(sys.argv[1], "", None) == rd.ResultCode.Succeeded
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None); assert res == rd.ResultCode.Succeeded
    texs = {int(t.resourceId): t for t in ctrl.GetTextures()}
    acts = walk(ctrl.GetRootActions())
    tot = collections.Counter(); n = 0
    for i, a in enumerate(acts):
        if not (int(a.flags) & int(rd.ActionFlags.Drawcall)): continue
        ctrl.SetFrameEvent(a.eventId, False)
        st = ctrl.GetD3D11PipelineState()
        om = st.outputMerger
        if not om.blendState.alphaToCoverage: continue
        rid = om.renderTargets[0].resource; did = om.depthTarget.resource
        t = texs[int(rid)]
        if t.msSamp < 2 or not int(did): continue
        dt = texs[int(did)]
        c = collections.Counter()
        for s in range(t.msSamp):
            sub = rd.Subresource(0, 0, s)
            ctrl.SetFrameEvent(acts[i - 1].eventId, True)
            cb = bytes(ctrl.GetTextureData(rid, sub)); db = bytes(ctrl.GetTextureData(did, sub))
            ctrl.SetFrameEvent(a.eventId, True)
            cf = bytes(ctrl.GetTextureData(rid, sub)); df = bytes(ctrl.GetTextureData(did, sub))
            dstride = len(df) // (t.width * t.height)
            for p in range(t.width * t.height):
                col = cb[4 * p:4 * p + 3] != cf[4 * p:4 * p + 3]
                dep = db[dstride * p:dstride * p + dstride] != df[dstride * p:dstride * p + dstride]
                if col and dep: c["both"] += 1
                elif col: c["colourOnly"] += 1
                elif dep: c["depthOnly"] += 1
        print("eid %5d depth %s %d bytes/sample: %s" % (a.eventId, dt.format.Name(), dstride, dict(c)), flush=True)
        tot.update(c); n += 1
        if n >= maxd: break
    print("TOTAL %d draws: %s" % (n, dict(tot)))
    ctrl.Shutdown(); cap.Shutdown(); rd.ShutdownReplay()


main()
