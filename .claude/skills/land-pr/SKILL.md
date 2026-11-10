---
name: land-pr
description: Take finished work (own branch, or an approved contributor PR) to merged on protected main - sync, unit + gate + resave, the PR body, the four required CI checks, squash merge, pull, remove the worktree. Use for "pr it", "open a pr", "lgtm can pr and merge", "merge once ci passes", "ci looks good can merge", "run the gate and resave then merge", "approve and squash merge", "can push after everything passes", "merge it, no need for a pr".
---

# Land a PR on main

`main` is protected: four required checks, squash merge, no direct push. **The merge happens only on
the user's go-ahead for THAT PR** ("merge once ci passes" counts; a go-ahead for one PR does not cover
the next). A direct push to `main` only when the user says so in this session; `--admin` is for an
emergency the user names. No release build for a PR (release builds and apks are for releases).

## 1. Sync the branch with main
```bash
git fetch -q origin && git log --oneline -1 origin/main      # this sha is the "base" in the PR body
git rebase origin/main        # own branch; or `git merge origin/main` when the branch is already pushed / shared
```
- New work starts in a worktree off main: `git worktree add -b <kind>/<name> ../openba-<name> origin/main`,
  then `git -c protocol.file.allow=always submodule update --init --recursive` (local submodule URLs).
- A contributor's PR: the review, and pushing maintainer changes to THEIR branch ("push the changes to
  the branch"), is the skill `review-pr` (§7: push by URL to the fork, never force). Note the head sha
  that was reviewed: it goes to `--match-head-commit`.

## 2. Local checks (Windows; CI never runs the game)
```bash
tools/build.sh unit                       # "N tests, 0 failures" -- the count goes in the PR body
tools/game_dirs.sh status                 # who holds which dir
unset BANISHED_DIR; eval "$(tools/game_dirs.sh env <lane> perf 600)"; [ -n "$BANISHED_DIR" ] || exit 1
ls "$BANISHED_DIR/WinData" | grep -i '\.pkm$'   # must print nothing: mods mounted = not a faithful gate
tools/build.sh run                        # THE GATE: deploys and launches /profile:faithful autostart
tools/boot_state.sh --wait 300            # RUNNING; then give autostart ~60 s
grep -c "\[AutoStart\] sequence fired" "$BANISHED_DIR/standalone_error.log"   # 1
ls "$BANISHED_DIR"/standalone_{crash,hang}.log 2>/dev/null                     # none
taskkill //PID <pid from standalone_boot_state.txt> //F
tools/resave_gate/resave_gate.sh Gate     # verdict: PASS (resave == golden)
tools/game_dirs.sh release <lane>
```
- Class C rows with their own golden: `GATE_PROFILE=/profile:all tools/resave_gate/resave_gate.sh <GateX>`
  when the PR touches that row's save record (tools/resave_gate/README.md).
- A PR that touches registration, factories or anything resolved by name or address:
  `tools/acceptance/acceptance.sh` (the no-doors check; CI runs it too, locally it saves a round trip).
- A gameplay / rendering change also gets a row-on run where the user asked for one (skill `menu-driver`
  for settings routes); say in the body which rows were on.
- Anything that fails: skill `triage-run`. Do not open the PR over a red local check without saying so.

## 3. Commit and open the PR
- Title prefix by kind: `behaviour:` (a PORT GAP arm or a faithful body changes what runs), `fix.<id>:`
  / `ext.<id>:` (or `ext:` / `fix:` for several rows), `perf:`, `cleanup:`, `test:`, `docs:`, `build:`,
  `tooling:`, `Android:`. Prose subject, no trailing period.
- Write the commit message and PR body with the Write tool into the scratchpad, then
  `git commit -F <file>` and `gh pr create --body-file <file>`.
- No `Co-Authored-By`, `Claude-Session`, "Generated with" lines. Impersonal text: no we / I / our.
```bash
git push -u origin <branch>
gh pr create --base main --head <branch> --title "<prefix>: <subject>" --body-file <scratchpad>/pr_body.md
```
PR body template (AGENTS.md section 7):
```markdown
<What changes and why, 2-6 lines: the row / function, retail address where it applies, the default.>

**Checks** (base <origin/main short sha>)
- Windows: `tools/build.sh unit` <N> / 0 / <skips>; gate (`tools/build.sh run`) PASS; resave gate PASS.
- In game: <rows on / off, backend(s), seed or save> -- or "not run".
- Not run: <each check skipped and why, e.g. "resave gate (docs-only change)">.
- Linux / macOS: CI only.

Docs: <known_port_issues.md / docs/extensions/... rows touched, or none>.
```
State every check that was NOT run. "Gate + resave not run" is a valid body; an implied PASS is not.

## 4. Watch CI
```bash
gh pr checks <N> --watch --interval 60      # foreground; CI takes ~5 min warm
gh pr checks <N> --required                 # the four that gate the merge
```
Required (exact names): `Windows (MSYS2 UCRT64)`, `Linux (Ubuntu 24.04, dxvk-native)`,
`macOS (arm64, Metal)`, `Acceptance (no doors)`. `Android (arm64, debug apk)` and `label` are not
required, but a red Android job is a seam the PR broke: report it. A red check on a platform the PR did
not touch is fixed in the PR (a missed seam), never waived or re-run until green. Read the failure with
`gh run view <run-id> --log-failed`.

## 5. Merge (on the go-ahead) and clean up
```bash
gh pr view <N> --json mergeable,mergeStateStatus -q '.mergeable+" "+.mergeStateStatus'   # MERGEABLE CLEAN
gh pr merge <N> --squash --subject "<PR title> (#<N>)" --body-file <scratchpad>/squash.md
#   own PR from the main checkout: add --delete-branch
#   contributor PR: add --match-head-commit <reviewed sha>; keep their branch unless told
gh pr view <N> --json state,mergeCommit -q '.state+" "+.mergeCommit.oid'
git switch main && git pull -q --ff-only && git log --oneline -1
git worktree remove ../openba-<name> && git branch -D <branch> && git push origin --delete <branch>
```
Squash body: the PR's "what changes" paragraph plus the checks line (or `--body ""`). Report the merge sha.

## Traps
- `eval "$(tools/game_dirs.sh env ...)" && run` proceeds on a DENIED lock (empty eval = exit 0): test
  `BANISHED_DIR` after the eval, and `unset` it first so an inherited value cannot pass the test.
- Never pipe the acquire (`... | tail`): the pipe discards the refusal (game_dir_lock.sh header).
- One game run at a time per dir; overlapping runs splice the logs. A denied perf lock may mean the
  user is playing: ask, or use the `functional` dir for the gate, but never kill a run you did not start.
- Kill by PID, never `taskkill //IM OpenBA.exe`: the user may be playing in the other game dir.
- A fresh worktree has no resave golden: the first run prints NEW-GOLDEN, which is no check. Copy
  `docs/runtime_port/resave_gate/golden/Gate.resaved.bin` from the main checkout first, and
  `git checkout -- docs/runtime_port/resave_gate/golden/Gate.resaved.md5` after (line-ending rewrite).
- A one-row resave DIFF (`count` <= 5689, one extra resource row) is the known loader race: re-run twice
  before reading it either way; a repeat is a finding.
- `boot_state.sh --wait` returns RUNNING at the menu; autostart fires ~60 frames later. Check the
  `[AutoStart]` line in `standalone_error.log` (not the debug log) before killing.
- `tools/build.sh unit` does not relink the exe: never hand-copy an exe after a unit-only build
  (`run` / `deploy` build it themselves).
- Inline quoting rewrites text: backticks in a heredoc or `-m "..."` executed and replaced words in a
  commit body. Write the file, pass `-F` / `--body-file`.
- A new extension sprite fails unit (`no icon for registry row`) until `python tools/ext_sprites_bake.py`
  re-bakes `src/ext/resources/ext_sprites_data.h`; commit the regenerated header.
- A fork PR whose checks never start is waiting at `action_required`: the approve call is in `review-pr`
  (Traps).
- Step 5 removes this PR's own worktree and branch only; a sweep of everything stale is the skill
  `cleanup-branches`.
- A push to the PR cancels the running CI (`cancel-in-progress`): watch the new run, not the old one.
- From a worktree, `--delete-branch` makes gh switch that worktree to `main`, which the main checkout
  already holds: merge without it and delete the branches by hand (step 5).
