"""FAA-SPIKE capture summariser: per pixel shader (keyed like the exe's [FoliageAA] blob lines,
the DXBC checksum's first two dwords), the draws, PSInvocations and SamplesPassed GPU counters,
the render target (size / samples) and the blend state's A2C / write mask.

  python capture_summary.py <cap.rdc> [index.txt] [--events]
"""
import os, struct, sys, collections

# <repo>/tools/renderdoc_mcp/runtime, derived from this file; RENDERDOC_MODULE_PATH overrides.
MOD = os.environ.get("RENDERDOC_MODULE_PATH") or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "renderdoc_mcp", "runtime"))
sys.path.insert(0, MOD)
os.add_dll_directory(MOD)
import renderdoc as rd


def walk(roots):
    out = []
    def rec(a):
        out.append(a)
        for c in a.children:
            rec(c)
    for a in roots:
        rec(a)
    return out


def main():
    cap_path = sys.argv[1]
    index = {}
    if len(sys.argv) > 2 and os.path.isfile(sys.argv[2]):
        for line in open(sys.argv[2]):
            p = line.split()
            if len(p) >= 4:
                index.setdefault(p[0], (p[1], p[2], p[3]))
    events = "--events" in sys.argv
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    assert cap.OpenFile(cap_path, "", None) == rd.ResultCode.Succeeded
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    assert res == rd.ResultCode.Succeeded
    texs = {int(t.resourceId): t for t in ctrl.GetTextures()}
    draws = [a for a in walk(ctrl.GetRootActions()) if int(a.flags) & int(rd.ActionFlags.Drawcall)]
    avail = ctrl.EnumerateCounters()
    want = [c for c in (rd.GPUCounter.PSInvocations, rd.GPUCounter.SamplesPassed) if c in avail]
    vals = collections.defaultdict(dict)
    for r in ctrl.FetchCounters(want):
        vals[r.eventId][r.counter] = r.value.u64
    groups = collections.OrderedDict()
    cache = {}
    for a in draws:
        ctrl.SetFrameEvent(a.eventId, False)
        pipe = ctrl.GetPipelineState()
        st = ctrl.GetD3D11PipelineState()
        refl = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
        key = "-"
        if refl is not None:
            rid = int(refl.resourceId)
            if rid not in cache:
                raw = bytes(refl.rawBytes)
                cache[rid] = "%08x%08x" % struct.unpack_from("<II", raw, 4) if len(raw) > 20 else "-"
            key = cache[rid]
        om = st.outputMerger
        rt = "-"
        if len(om.renderTargets) and int(om.renderTargets[0].resource):
            t = texs.get(int(om.renderTargets[0].resource))
            if t:
                rt = "%dx%d ms%d" % (t.width, t.height, t.msSamp)
        elif int(om.depthTarget.resource):
            t = texs.get(int(om.depthTarget.resource))
            if t:
                rt = "depth-only %dx%d ms%d" % (t.width, t.height, t.msSamp)
        b = om.blendState
        a2c = int(b.alphaToCoverage)
        mask = b.blends[0].writeMask if len(b.blends) else -1
        inv = vals[a.eventId].get(rd.GPUCounter.PSInvocations, 0)
        smp = vals[a.eventId].get(rd.GPUCounter.SamplesPassed, 0)
        g = groups.setdefault((key, rt, a2c, mask), [0, 0, 0, a.eventId, a.eventId])
        g[0] += 1; g[1] += inv; g[2] += smp; g[4] = a.eventId
        if events:
            print("eid %5d ps %s rt %-18s a2c %d mask %x inv %8d samples %8d" % (a.eventId, key, rt, a2c, mask, inv, smp))
    print("capture %s: %d draws; counters %s" % (os.path.basename(cap_path), len(draws), [str(c) for c in want]))
    print("%-16s %-20s %-3s %-4s %5s %10s %10s %-11s %s" % ("ps", "target", "a2c", "mask", "draws", "PSinvoc", "samples", "eids", "blob (index.txt)"))
    for (key, rt, a2c, mask), g in groups.items():
        info = index.get(key, ("", "", ""))
        print("%-16s %-20s %-3d %-4x %5d %10d %10d %5d-%-5d %s %s" % (key, rt, a2c, mask, g[0], g[1], g[2], g[3], g[4], info[2], info[1]))
    ctrl.Shutdown(); cap.Shutdown(); rd.ShutdownReplay()


main()
