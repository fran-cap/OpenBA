# resave gate — the serializer regression check (2026-09-13)

```
tools/resave_gate/resave_gate.sh Gate                # verdict: PASS / DIFF vs golden / NEW-GOLDEN
tools/resave_gate/resave_gate.sh Gate <outdir> <secs>
GATE_MODE=file tools/resave_gate/resave_gate.sh Gate  # smoke test of the on-disk save path only
```

Run it under `tools/game_dir_lock.sh` (or a dir from `tools/game_dirs.sh`). It needs
`<user dir>/Save/Gate.sav` -- `Documents/Banished` or `$BANISHED_USER_DIR` (the
golden was cut against md5 `68f99e06e6cdce6c17a8fb06e867b2c8`, a copy of the 2026-09-12
autosave; copy any save to that name and delete the golden to start over). Exit 0 is PASS.

## The golden was re-cut on 2026-11-03 (review F-186, PR #135) -- same fixture
Golden md5 `971aaec2cf95f5f730bb1cd1a4fa8428` -> `4986645d9649c3e4a6bc9d8aed9f5b03` (23,453,453 bytes, size
unchanged, the same `Gate.sav`). WHY: `EntityList::Serialize` (retail 0x1400c462c) sign-extends the 24-bit bucket
index in `_handle` (`SHL ECX,8 / SAR ECX,8`); the port zero-extended it, so every entity with bucket index -1
saved `FF FF FF 00` where retail saves `FF FF FF FF`. The whole diff is 3,334 single-byte `00 -> FF` runs in
that field (offsets 13,833,058..14,380,719, 20 / 40-byte entity-record strides); nothing else moved. The old
golden carried the port's bytes. Reproduced on two runs before the re-cut.

## The class C golden: GateRestrictedFoods (2026-10-21, block EXTHOSTS)

DESIGN.md's Gates ask every shipped class C extension for its own golden. `GateRestrictedFoods.sav`
(in the save directory, beside `Gate.sav`) is `Gate.sav` loaded with only `ext.restricted_foods`
requested among the B / C rows, Potato blocked on the Foods page, the extension window open, saved.
Its tail carries the `restricted_foods` record (the blocked template path) and the `toolbar` host
record. Run it under the forced all-on profile, so the golden does not depend on settings.cfg (the
save's own manifest decides the B / C set):

    GATE_PROFILE=/profile:all tools/resave_gate/resave_gate.sh GateRestrictedFoods

Golden md5 `a5a1dc485139a26f3190cfcf89f9d350` (23,440,348 bytes), mods parked; PASS on the run after
NEW-GOLDEN. A change to the restricted_foods payload or the toolbar host record is a DIFF here and a
PASS on `Gate`.

## The golden was re-cut on 2026-10-11 (Phase D.4 close, row 1286) -- same fixture

Golden md5 `994eb184a8e88db9372b5b9cd13a9ed9` -> `9c18dd25f2a84677e33cc61c233943b3` (23,453,453 bytes, +516,
the same `Gate.sav`). WHY: `e9f7a978f` fixed `AmbientEmitterDescription::Serialize` (retail 0x1400431b0),
which had serialized its count INTO the array header's own count field so the on-load SetSize never
allocated: every emitter description loaded with a NULL sound array and no `AmbientEmitterComponent` slot
ever held a sound. With the sounds bound, `AmbientEmitterComponent::Persist` (0x140042f30) writes a real
`ExternalPtr<SoundEffect>` per slot and the save's resource table gains the six `Audio\Effects\*Effect.rsc`
rows (Crowd, Fire, Rain, River, Tornado, Wind: `resource` + `version0`, +11 table entries, +516 bytes).
The payload is byte-identical (`payload delta=+0` in the gate's row analysis), so the component data did
not move; only the table grew. The new-game load count moved 5683 -> 5694 for the same reason (gate.sh
updated). PASS on the run after NEW-GOLDEN, crash log 0.

## The golden was re-cut on 2026-10-09 (B7, the named-prerequisite pass) -- same fixture

Golden md5 `3efc80a84907fbb8e25d64b4f2c99cff` -> `994eb184a8e88db9372b5b9cd13a9ed9` (23,452,937 bytes, the
same `Gate.sav`). WHY: B7 RES row 84 (`184a69e9f`) made `NamedExternalRef::Resolve` find-only, as retail
0x1400265b0 is (import 0x140152460 = `ExternalFactory::Find`). The old body FindOrLoad-ed every miss, and
169 Resolve calls run at factory generation 0 during the goal / achievement description loads, where
`Game\Profession\Profession.rsc:builder` / `:laborer` / `:trader` (and Climate / Terrain / Template refs)
MISS: the tree loaded those professions EARLY. Now they load later from their other referrers (the same
5683 loads), so the three Profession objects get other heap addresses, and `CitizenBehavior`'s profession
bucket tree (+0x100, `CitizenBucketSet<Profession>`, keyed on the raw `Profession*`, walked in order by
`SerializeCompRefTree`, retail 0x140018020) is written in a different order: the one 105-byte block at
0x11b172d of the image (3 buckets, ascending before, descending now; keys, counts and members identical).
Bisected commit by commit in a scratch worktree; the new order is the faithful one. 5/5 PASS after the
NEW-GOLDEN run. The template bucket tree (+0x160) is keyed the same way and may move on other saves.

## The fixture was re-cut on 2026-10-09 (B6, the retail-difference task)

`Gate.sav` is now the frame-661 autosave of a NEW GAME on the B6 COMP-merge exe (`b864cd289`,
seed 12345, medium map, `BANISHED_SAVE_TEST=600`), md5 `b197edf5650119bc55754f523cf0f800`; the
golden was re-cut from it (23,452,937 bytes, md5 in `golden/Gate.resaved.md5`), 4/4 PASS after
the NEW-GOLDEN run, crash log 0. The 2026-09-12 fixture is kept beside it as
`Gate.sav.pre_b6_20260912`. WHY: that save held 11 citizens (ages 0.26-1.7 years, born under the
tree's old birth path) with a NULL profession; retail never creates such a citizen, and once B6
row 1 made `CheckEducation` read the profession unguarded as retail does (0x14010f7f2), every
resave run crashed at frame 60 after a PASSed roundtrip. The upstream null was the fixture, not
the code (known_port_issues row 990). The route's loader line is no longer the old `calls=5691`.

## What it measures

The DLL-absent exe loads the save unattended (`BANISHED_AUTOLOAD`), and in the
LOAD-FROM-SAVE branch of `GameUpdateLoop::Load`, right after `SaveRestore(load)` and before
the first sim tick, `GameUpdateLoop_SaveRoundtripCheck` (src/game/Game/gameupdateloop_serialize.cpp)
calls `SaveRestore(save)` into a fresh `SavedState`, decompresses both images through the
same `CompressedMemoryStream` path the load uses, writes them next to the logs
(`standalone_roundtrip_{loaded,resaved}.bin`, ~21 MB each) and prints one line:

```
[Roundtrip] RESULT=DIFF raw loaded=21074851 resaved=21075114 compressed ... firstDiff=4 diffBytes=... tail=263
```

**That line is informational.** Loaded-vs-resaved is DIFF by the design of retail's format:

- `ExternalFactory::SerializeReferenceTable` numbers every RESIDENT resource 1..N in sorted
  path order and every `ExternalPtr` in the payload is written as that number. The
  resident set depends on the process that writes the save: our load route leaves the
  loading-screen image (`Dialog\Loading.rsc:gameLoadN` + its three sprite resources) and
  the menu scene (`Game\MenuScene\SceneN.rsc`) resident, the process that wrote `Gate.sav`
  did not. Five extra rows shift every id after them, which is the ~15 MB of "differences".
- `InternalFactory::ResetPersistState` writes the internal object table's size, a
  process-history value (123800 in the input, 123514 in ours).
- `N` in both names above is a random pick (`Rand(10)` for the scene, `Rand(12)` for the
  loading image). `BANISHED_MENU_SCENE=<n>` pins both; the gate sets it to 0.

**The verdict is against the golden**: `docs/runtime_port/resave_gate/golden/<save>.resaved.bin`,
the resave of a known-good build for this save and route. With the two picks pinned the
resave is byte-identical run to run (measured: runs 13/14 and the golden run; unpinned runs
differed by exactly the digits of the picks). A Serialize body that changes what it writes
shows up as `DIFF vs golden` with the first offset. When the change is intended, delete the
golden and run twice. The `.bin` files are git-ignored; the golden's md5 is committed.

## What it found on its first run

- The DLL-absent save died in `CompressedStream::SetWindow`: the write-side GROW path
  (extend the header/block arrays on the first write into each new block) had been left
  delegated to the DLL as a "named cutover blocker". Ported 2026-09-13
  (src/engine/io/compressed_stream_read.cpp). Every save takes it; the cutover gate never
  saved, so it never showed.
- Intermittent (2026-09-13, seen once in five full runs on the same exe): `DIFF vs golden`
  with exactly ONE differing byte, the reference-table `count` at +0x18 one higher (5689 vs
  5688) and no extra row, sizes identical. The next four runs of the identical command
  passed. So the count field can include one resource the row loop does not write, and
  which resource is resident is not fully deterministic even with the picks pinned. A
  one-byte diff at +0x18 is this; re-run before reading it as a serializer change.
- The same race, one step further (2026-09-18, after B.3.3 step 6A turned ~158 resolved
  pointer calls on the load path into direct/inline calls): the borderline resource is
  counted AND its row is written. Signature: `count` 5689 (never higher), image +40/+41
  bytes, exactly ONE extra path string versus the golden — `CropField.rsc:resource` —
  and ~14 MB of "differing bytes" that are only the id shift behind it. Measured 2 DIFF in
  10 runs on that exe against 0 in 9 on the commit before it; six consecutive PASS later on
  the same exe. No semantic cause exists in that batch (members return raw pointers, call
  counts match one-for-one, no flag or argument changed). Read a failure with the count and
  the extra row: count <= 5689 and a single extra/missing row is THIS; a count of 5690+, or
  several rows, is a real change. Re-run twice before reading it either way. The extra row
  is NOT always the same resource: after step 6B/6C one failing run in three carried
  `ToolbarRemoval.rsc:resource` (+46 bytes, count 5689) instead — a varying single row is
  what a race looks like; the same NEW row on every run would be a semantic change. The race
  itself is the `ExternalFactory::Add` loader investigation, not a gate defect.
- Intermittent, not fixed, both pre-existing: (1) one run in ~12 crashed in
  `ExternalFactory::Add`'s AVL rotate during the loader, with a `[GUL::Load] redundant
  dispatch` line just before it (two loader dispatchers racing; see the guard comment in
  gameupdateloop_load.cpp); (2) one run hung after the pause-menu Quit had returned, in
  teardown. Re-run on either; a second failure is a finding.

## Mapping an offset to a field

The payload has no field names (BinaryWriter clobbers them). Records are
`[AAAAAAAA][len][version]...[BBBBBBBB]`, the reference table is the first record
(`count` at +0x18, then `{u32 id, u32 len|0x80000000, path}` rows), and the loop's write
order is the body of `GameUpdateLoop_Serialize_95a90`. To localise a DIFF, add a
`SerMark`-style position print around the suspect Serialize call, run once, remove it.
