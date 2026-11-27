# trace_oracle — verify a Graphics/Video port against the command stream

The replacement for the pixel oracle Wave 23 retired.

## The problem

Wave 23 lane GFXBODY ran five captures at identical seed, camera pin and command
line. They split into two clusters **~98% apart**, and the split was
**independent of the flag under test** — two *armed* runs differed by 97.98%, an
armed and an unarmed run by 0.79%. `BANISHED_SEED` + `BANISHED_CAMERA_PIN` do not
give a reproducible frame. **A pixel diff between two runs of this game measures
nothing.**

This tool never looks at a pixel. It compares **what the renderer told the GPU to
do**: which pipeline configurations it built, which shaders, states, views and
samplers it bound, per draw.

## Three pieces

| | |
|---|---|
| `capture_arm.sh <tag> [--frame N] [--expect-log RE] -- ENV=V ...` | Capture one in-game frame under RenderDoc for an arbitrary arm. Generalises `tools/dx11_state_diff/capture.sh`, whose arms are hard-wired to the DX11 A/B seam. **`--expect-log` is how you find out the arm actually armed** — without it a run that silently did nothing produces a clean comparison. |
| `tools/dx11_state_diff/dump_state.py <cap.rdc> -o t.jsonl` | Unchanged, and re-verified working on this tip. Walks every draw, records the full D3D11 pipeline state, and replaces every RenderDoc `ResourceId` with a content-derived key (shaders by sha1 of DXBC, textures by desc + mip-0 hash, buffers by length) so records compare across two processes. |
| `trace_oracle.py --test A.jsonl B.jsonl [--control C.jsonl C2.jsonl]` | The verdict engine: six screens, scored separately. |
| `android_capture.sh [--serial S] [--wait SECS] --out F.rdc BANISHED_*=V ...` | One in-game Vulkan frame from the attached Android device (debug apk), unattended. The header lists the three device fixes the RenderDoc GUI does not make. Replay with `gpu_timing.py F.rdc out.json --android S`: on Adreno (a tiler) the per-draw ms read ~0, so the capture gives structure, not GPU cost; GPU load comes from kgsl sysfs on a live run. |
| `trace_selftest.py <real-trace.jsonl>` | The control harness. Run it after ANY change to either tool, and on YOUR OWN trace before trusting the oracle's silence. |

## THE MEASURED NOISE FLOOR — read this before quoting any screen

Three captures, **same exe** (md5 `5a909b6df845cf00db7b3237af54acc6`), same seed
`12345`, same pin `0,0,60`, same command line, same frame 150. They drew
**566 / 557 / 509** draws. Between two **identical** runs each screen reported:

| screen | subject | unfloored | floored by one control pair |
|---|---|---|---|
| **S1 configSet** | set of full pipeline configurations | 41–74 | **51** — NOISE |
| **S2 sigSet** | S1 with bound-resource identity removed | 3–6 | **3** — NOISE |
| **S3 fieldDiff** | aligned per-field disagreement rates | suppressed | **0** |
| **S4 invariants** | `maxLOD==mips-1`, `srv.numMips==mips`, `srv.numSlices==arraySize`, each against the bound texture | **0** | **0** |
| **S5 srvPool** | set of textures ever bound as an SRV | 21–43 | **29** — NOISE |
| **S6 strideSet** | set of vb/ib strides, byte offsets excluded | **0** | **0** |

**S3 (floored), S4 and S6 are the only verdict-bearing screens on this game
today.** S1, S2 and S5 are descriptive. Which materials and textures drew this
frame follows what was on screen, and a single control pair does not floor them,
because *the set of run-dependent keys is itself run-dependent* — flooring S1 by
one control pair took 74 findings to 51, not to 0. Their counts are still printed
and are still worth reading (a port that deletes a whole render pass will move
them far outside this band) but a difference of a few dozen keys is the game
moving, not the port.

Also measured: `compare_state.explain_signature_gap()`'s content classifier —
"that shader pair never drew on the other side" — explained **0 of the 3**
one-sided S2 signatures between identical runs. It is not a floor here, and a
lane reading its `UNEXPLAINED` line as a finding would be reading noise.

## S3 is suppressed without `--control`

`trace_selftest`'s negative control **`fewer draws`** — a trace that drew fewer of
the *same* configurations, which is exactly the measured non-determinism — moved
**6016 field values**, and an unfloored S3 flagged it while every set screen
stayed silent. Its rates are still printed (tagged `suppressed`); they cannot
become findings. Supply `--control A.jsonl A2.jsonl` — two captures from the
**same** build — and S3 reports only fields the control proves stable. Floored
that way, S3's residue between identical runs measured **0**.

## What it will not tell you

* **Constant-buffer CONTENTS are not verified by any screen** (`cbContents`,
  100% between identical runs — per-object transforms).
* **`cbBufferSizes` (30.9–37.4%) and `vbStrides` (11.1–15.2%) between identical
  runs** are ring-allocation artefacts and are `non-verdict`. The stride *alone*
  is still checked — that is S6, and its floor is 0.
* **A field empty in every record is NOT-EXAMINED**, never counted as agreeing.
  On these captures that was `ds, gs, hs, uav` — this renderer binds no
  hull/domain/geometry shader and no UAV, so four of twenty-four fields carry
  nothing. A verdict that quietly included them would claim more than it examined.
* **Zero draws on either side aborts before any screen reports.** A dead trace is
  not a clean one.
* **Nothing here compares against the shipped Runtime/VideoDX11 code.** It
  compares two runs of *our* exe. It answers "did arming this port change what
  the renderer does, beyond the noise?" — not "is our output byte-identical to
  retail's".

## Usage

```bash
# 0. the game directory is one resource
tools/game_dir_lock.sh acquire lane-NAME 1500      # BARE, never piped

# 1. THREE captures: the two arms, plus a repeat of one arm as the control pair.
#    The third is not optional if you want S3, and S3 is where most of the
#    verdict-bearing power is.
bash tools/trace_oracle/capture_arm.sh armed   --frame 150 \
     --expect-log '\[MYPORT\] .*calls=[1-9]' -- BANISHED_MYPORT=1
bash tools/trace_oracle/capture_arm.sh armed2  --frame 150 \
     --expect-log '\[MYPORT\] .*calls=[1-9]' -- BANISHED_MYPORT=1
bash tools/trace_oracle/capture_arm.sh unarmed --frame 150 \
     --expect-log '\[MYPORT\] .*calls=0'      -- BANISHED_MYPORT=0

tools/game_dir_lock.sh release lane-NAME

# 2. extract (no lock needed; replay is read-only)
PY="<main checkout>/tools/renderdoc_mcp/.venv/Scripts/python.exe"
G="C:/GOG Games/Banished/rdoc_trace"
for t in armed armed2 unarmed; do
  $PY tools/dx11_state_diff/dump_state.py "$G/$t.rdc" -o docs/history/notes/$t.jsonl
done

# 3. prove the instrument is alive on YOUR trace before trusting its silence
$PY tools/trace_oracle/trace_selftest.py docs/history/notes/armed.jsonl

# 4. the verdict
$PY tools/trace_oracle/trace_oracle.py \
    --test docs/history/notes/armed.jsonl docs/history/notes/unarmed.jsonl \
    --control docs/history/notes/armed.jsonl docs/history/notes/armed2.jsonl \
    --label-test-a armed --label-test-b unarmed
```

Exit codes: `0` no findings · `1` findings on at least one screen · `3` refused
(a dead trace, or a non-zero identity baseline in the selftest).

**Reading the result.** S3/S4/S6 non-zero is a finding. S1/S2/S5 inside the bands
above is the game; far outside them is worth looking at, and the residue keys are
in the `--json` output.

## The interpreter

`renderdoc.pyd` is Python 3.10 and lives in `tools/renderdoc_mcp/runtime`, with
the matching venv at `tools/renderdoc_mcp/.venv`. **That directory is
gitignored** — it ships with the RenderDoc MCP server, not with this repo, so it
is present in the main checkout and *absent from a git worktree*. Use the
absolute path to the main checkout's copy, or set `RENDERDOC_MODULE_PATH`.
`dump_state.py` already defaults to it. (`tools/dx11_state_diff/README.md` tells
you to use that venv without saying it is not in your tree; that cost this lane
a detour.)

## The control harness

```bash
$PY tools/trace_oracle/trace_selftest.py <any real trace.jsonl>
```

28 mutations of a real trace, in three categories:

* **positive** (19) — a subject the oracle *does* compare; must be CAUGHT.
* **negative** (9) — a subject it *must not* flag: event ids, record indices,
  action names, constant-buffer contents, draw counts, dynamic buffer identity,
  dynamic buffer byte offsets, constant-buffer allocation size, and fewer draws
  of the same configurations.
* **open** (0 today) — stability not known, settleable only by a real same-build
  control pair. Never scored as a pass or a failure.

Current score: **19/19 positives CAUGHT, 9/9 negatives clean, 0 open**, with an
identity baseline of 0 on all six screens.

Every mutation carries an **effectiveness counter** (records and field values
actually altered). A mutation that changed nothing is reported `INERT` and
excluded — "the oracle did not notice a change that never happened" is not
evidence about the oracle.

**Four of the six screens exist, or were corrected, because this harness found a
hole in the first draft:**

1. `vb.stride` was caught by S3 alone, and S3 is suppressed in normal use — so
   **S6 was added**.
2. The first S6 keyed on strides *and* offsets, which made the `buffer byte
   offsets` negative control trip it — so **S6 now drops offsets**.
3. `cb buffer size` began as an `open` control on the argument that a D3D11
   constant buffer's byte length is fixed at creation. The three same-build
   captures measured it differing on 30.9–37.4% of aligned pairs between
   identical runs. **It is NEGATIVE**, and the first guess was wrong.
4. Moving `cbBufferSizes` and `vbStrides` to `non-verdict` left S3 with **no
   positive control at all**, which the harness printed as
   "`S3_fieldDiff caught 0/18 <- CAUGHT NOTHING: this screen is unvalidated`" —
   so **`cb.bindSize` was added**. Every screen now has at least one.
