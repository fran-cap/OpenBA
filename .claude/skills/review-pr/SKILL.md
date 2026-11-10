---
name: review-pr
description: Review a contributor pull request and its later rounds - fetch into a worktree, check the diff against AGENTS.md, verify retail claims in Ghidra, numbered findings for the user's rulings, post the review or comment with gh, push small maintainer fixes to the contributor's branch. Use for "can you review https://github.com/fran-cap/OpenBA/pull/N", "review pr N", "got feedback, please review", "he pushed, give it a look", "post the comment", "leave a comment on the pr", "post as changes requested", "have him fix 1 and 2". The merge is the skill `land-pr`.
---

# Review a contributor PR

The loop: **fetch -> read -> verify -> (run) -> numbered findings to the user -> user rules -> post ->
author pushes -> re-review -> maintainer fixes or another round -> `land-pr`.** Nothing is posted,
pushed or approved before the user has agreed to the exact text in this session.

## 1. Fetch into a worktree
Other sessions often own the main checkout (it may be on a feature branch with uncommitted work):
never check a PR out there, never `git stash`.
```bash
gh pr view N --json title,author,body,baseRefName,headRefName,headRefOid,isCrossRepository,headRepositoryOwner,headRepository,maintainerCanModify,mergeable,commits,files,statusCheckRollup
git fetch -q origin main && git fetch -q origin pull/N/head:pr-N
git worktree add ../wt-prN pr-N            # C:/code/GIT/wt-prN
cd ../wt-prN && git merge-base HEAD origin/main && git log --oneline origin/main -3
```
Note the head sha and base: every comment names the commit it reviewed ("Review of `abc123def` (on
`main` `def456abc`)"). A build in the worktree first needs
`git -c protocol.file.allow=always submodule update --init --recursive` (submodule URLs are local).

## 2. Read the diff
`git diff origin/main...HEAD -- . ':!src/ext/resources/ext_sprites_data.h' ':!*.png'` (three dots:
against the merge base; the sprite header is generated, thousands of lines). Read the consumer of
every seam the PR touches, not only the hunk. Check, in this order:
- **Faithful first** (AGENTS.md §2): game/engine bodies unchanged unless the PR is a port; any new
  behaviour is a `fix.*` or `ext.*` row under `src/ext/<id>/`, with a class, a default and a Names /
  registry entry (`src/ext/registry.{h,cpp}`, `docs/extensions/DESIGN.md` §2-§3). No `#if` in game code.
- **Class**: does the row change the sim or the save? Class A = no sim effect; per-town state that
  must reload as saved = class C with a codec (`src/ext/<id>/save_state.cpp`, `kCodecs` in
  `src/ext/state_codecs.cpp`); B/C bodies are selected once per game, never read in a hot loop
  (DESIGN.md §3 hook rules). Engine-side seam = a null-by-default static pointer, never a vtable.
- **No null guard on a faithful crash** (CLAUDE.md hard rules): a guard needs a stated
  standalone-vs-retail reason in its comment; otherwise ask for the upstream cause.
- **No game content** (AGENTS.md §1): no retail shader/DXBC, textures, `.rsc`/`.pkg` copies or ports.
- **Flags and logs** (AGENTS.md §3): no `getenv`, new flags only in `src/app/options.h`; no stderr /
  `ErrLine` on a per-frame / per-tick path in a shipped build; a `n % 600` throttle is not a gate;
  investigation lines deleted.
- **Seams** (AGENTS.md §4): no `_WIN32` / `__APPLE__` in `src/app|game|engine`; renderer code stays
  in its backend; type headers keep their `static_assert`s.
- **Style** (`.claude/rules/style.md`): one statement per line, only touched lines reformatted.
- **Licensing** (AGENTS.md §6): a new dependency adds a `THIRD_PARTY_NOTICES.md` row and a pin.
- **Docs and tests**: claims in the PR body, the hand-in (`docs/extensions/handins/<ID>.md`),
  `docs/extensions/LIST.MD`, `docs/runtime_port/STATUS.md`, `CHANGELOG.md` match the code (sizes,
  counts, "three sliders"); new pure logic has a `tests/unit/` test.

## 3. Verify what the PR claims
- **Retail addresses / "retail does X"**: decompile the address in Ghidra (skill `ghidra` starts the
  bridge) and quote the instruction or line in the finding. Unverified when Ghidra is down: say so.
- **Merge with current main**: `git merge-tree --write-tree --name-only origin/main pr-N` lists
  conflicting files. A PR that adds an ext sprite: compare `kBitmapCount` in the merged
  `src/ext/resources/ext_sprites_data.h` with `grep -c _source src/ext/ui/Dialog/ExtensionsSprites.rsc`
  (the header merges "clean" but stale when two PRs each add an icon).
- **CI**: `gh pr checks N`. A red check that also fails on main (`gh run list --branch main --limit 5`)
  is not the PR's; one on a platform the PR did not touch is still fixed in the PR (AGENTS.md §7).

## 4. Run it (when the user asks, or the change is visible in game)
The recipe is `land-pr` §2 (lock a game dir and check the lock was granted, unit, the gate, the
resave gate, release the lane), run from the PR worktree. The worktree has no resave golden, so copy
main's in first and restore the md5 after:
```bash
mkdir -p docs/runtime_port/resave_gate/golden
cp ../untitled_rev_eng_project/docs/runtime_port/resave_gate/golden/Gate.resaved.bin docs/runtime_port/resave_gate/golden/
# ... land-pr §2 ...
git checkout -- docs/runtime_port/resave_gate/golden/Gate.resaved.md5
```
The gate runs the faithful profile, so a fix/ext row stays OFF: a row-on run is separate and is a
smoke check only. A by-hand look for the user is the skill `hand-test`. Report what ran and what did not.

## 5. Findings, rulings, posting
- Show the user numbered findings (blocking first, then should-fix, then nits), each with
  `path:line`, the evidence (quoted source / Ghidra line) and the requested change, plus what was
  checked and found fine. Open questions are marked "for a ruling"; the user answers by number.
- Draft the post in the scratchpad, show it, wait for "agree / post". Wording is impersonal: no
  "we / I / our / us / me" ("Three changes are needed before the merge", "Checked on Windows: ...").
  No `Co-Authored-By`, `Claude-Session` or "Generated with" lines anywhere.
- Post the form the user chose:
  `gh pr review N --request-changes --body-file F` · `gh pr comment N --body-file F` ·
  `gh pr review N --approve --body-file F`.

## 6. Re-review ("got feedback", "he pushed")
In the worktree (the `pr-N` branch is checked out there, so fetch without a local ref):
```bash
git fetch -q origin pull/N/head && git checkout -q --detach FETCH_HEAD
git diff <old-head> HEAD -- src tests docs      # rebased: git range-diff origin/main <old-head> HEAD
```
Check each earlier numbered point (fixed / not / partly), read new code as in §2, re-run what §4
ran, and read the author's replies (`gh pr view N --comments`) for questions back.

## 7. Maintainer fixes on the contributor's branch
For small leftovers the author cannot test, the user usually prefers a maintainer commit over another
round. Needs `maintainerCanModify: true`. Commit in the worktree (merge `origin/main` in, not a rebase,
when conflicts must be resolved), then push to the fork, never `origin`:
`git push https://github.com/<headRepositoryOwner>/<headRepository>.git HEAD:<headRefName>`.
Never force-push a contributor's branch. Then the approving review says what was pushed ("Accepted,
with one change pushed to the branch (`sha`): ..."), and `land-pr` takes over (CI, gate + resave,
squash merge, worktree cleanup).

## Traps
- `git push origin` from a PR worktree goes to fran-cap/OpenBA, not the fork: push by URL.
- `git fetch origin main pull/N/head` then `FETCH_HEAD` is **main** (first refspec): fetch the PR alone.
- A fork PR's CI can sit at `action_required`: `gh run list --branch <headRefName>`, then
  `gh api -X POST repos/fran-cap/OpenBA/actions/runs/<id>/approve`.
- A worktree has no resave golden: the first run prints NEW-GOLDEN, which is no check; copy main's.
- Resave on the functional dir shows a pre-existing 12-byte DIFF: take the verdict on the perf dir.
- `eval "$(tools/game_dirs.sh env ...)" && build` proceeds when the lock is denied (empty eval exits 0).
- `boot_state.sh --wait` returns RUNNING at the menu; check `[AutoStart] sequence fired` in
  `standalone_error.log` before killing, and kill by PID, never by image name (the user may be playing).
- A new sprite: `python tools/ext_sprites_bake.py` re-bakes the header; `tools/ext_resources.sh` does
  not, it only re-cuts `build/ext_resources` (needed before `ext_sheet_kit_crosscheck` passes locally).
- Pinned counts move on a merge (`tests/unit/ext_rsc_text_test.cpp` string blocks, registry tests):
  build `unit` on the merged tree, not only on the PR head.
- `unit` does not relink the exe: build or `run` before any game check.
