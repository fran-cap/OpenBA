---
name: agent-team
description: Run a team of subagents as coordinator - when a team is worth it, model choice (opus / sonnet / haiku), the brief template with the hard rules agents cannot see, worktrees and branches, build jobs, game-dir lanes, file ownership, the hand-in format, serial merges and cleanup. Use when the user says "a team of opus agents", "serve as the director / advisor / orchestrator", "fan out an agent team", "have a sonnet agent ...", "launch an opus agent for ...", or before any Agent call that edits code, builds or runs the game.
---

# Coordinate an agent team

The coordinator plans, briefs, reviews and merges; agents do bounded work in their own worktree.
The user runs the session with the coordinator: relay findings, rulings needed and open choices.

## 1. Team or inline
- Inline: a handful of tool calls, a lookup whose target is known, a one-file fix, anything the
  coordinator must read in full anyway.
- Team: independent items that can run in parallel (several bugs, perf ideas, review angles), or a
  survey across many files where only the conclusion is needed.
- Research first, code second: for a new feature the user often wants a research pass and a
  discussion before any agent writes code. Ask when the request says "first research".
- Cut items small: about 15 files per agent. An agent near the end of its context does its least
  checked work last; a new agent takes over its branch tip on a new branch, unverified claims first.

## 2. Model (pass `model` on EVERY Agent call; never inherit the main model unasked)
| Work | Model |
|---|---|
| Porting, debugging, design, perf work, research and web surveys, integration | `opus` |
| Mechanical edits on an established pattern, Ghidra lookups, icons, renames | `sonnet` |
| File / log searches, greps, lookups | `haiku` |
- A sonnet port that "compiles clean" can still be unfaithful on intricate bodies (tree rotations,
  serialize order, out-params): complex RE goes to opus, and the coordinator re-checks it in Ghidra.

## 3. Before spawning
- One worktree + one branch per code-writing agent (`isolation: "worktree"`). The brief names the
  base: `git fetch` then branch from `origin/main`, or names the unmerged branch and commit to start
  from. Check the base with `git worktree list` after the spawn.
- Build jobs: `--jobs` = 8 / agents building at once, minimum 2 (one builder 8, two 4, three or four 2).
  At most four agents build at once: more has locked up the 32 GB machine. Put the number in the brief.
- Game dirs: two lanes, `perf` (`C:/GOG Games/Banished`, the gate, resave and perf A/Bs) and
  `functional` (`C:/GOG Games/Banished-B`, own user dir; crash, menu, screenshot runs). Assign each
  running agent a lane name and a dir. More than about three parties on one dir starves the lock:
  agents run static checks (builds, unit, acceptance) and the coordinator runs the game on the merged tree.
- Ownership: give each agent a disjoint file set (and disjoint `known_port_issues.md` row ranges or
  names for anything numbered). Tell it who owns the neighbouring files and to SendMessage "main"
  before touching a file it does not own. Track reserved files in the closing checklist: a directory
  sweep skips them.
- A successor for the same files only after the first agent was sent an explicit STOP and acked:
  a queued message resumes a finished agent, and two owners in one file set follow.
- Ghidra MCP has one current program: forbid `switch_program` in briefs when several agents use it.

## 4. Brief template (agents do not see CLAUDE.md context; paste the rules)
```
Repo: OpenBA (C:/code/GIT/untitled_rev_eng_project), a clean-room C++ reimplementation of Banished.
Read CLAUDE.md and AGENTS.md first; they are binding. You work in your own git worktree.

## Goal
<one paragraph: the bug / idea / port, the user's words where they matter, the expected deliverable>

## Known and ruled out
<facts already established, hypotheses already rejected, user rulings, related commits / PRs>

## Files
<files you own; docs worth reading; files owned by <agent> -- SendMessage "main" before touching them>

## Base
`git fetch`, then branch `<type>/<name>` from origin/main (or: from `<branch>` at `<sha>`).
A fresh worktree: `git -c protocol.file.allow=always submodule update --init --recursive`.

## Hard rules
- Don't fix a faithful crash with a null check: find what handed in the null upstream.
- Never getenv (flags live in src/app/options.h, read Opt().field); no new BANISHED_* flags; no
  log output on a hot path; style per .claude/rules/style.md; no game content in the tree.
- No `find /` or unscoped filesystem search: Grep/Glob inside your worktree only.
- Build only with tools/build.sh, `--jobs <N>`. Block in the FOREGROUND on builds, lock acquires
  and runs (Bash timeout 600000); never a background job waiting for a notification.
- Game runs: `eval "$(tools/game_dirs.sh env <lane> functional)"` as its OWN command, never chained
  with &&; check BANISHED_DIR is set; `tools/game_dirs.sh release <lane>` after each run block.
  Back up that dir's settings.cfg and exe at lock time and restore them after.
- Poll with `tools/boot_state.sh --wait 300`, never a grep loop. [AutoStart] lines are in
  standalone_error.log; on a crash read standalone_crash.log first (skill triage-run).
- Kill a run only by the PID you launched, never `taskkill /IM OpenBA.exe`: the user may be playing.
- Helper scripts go in <scratchpad>/<team>/<agent>/ and take the worktree path explicitly.
- Never `git stash` (shared across worktrees); no amend, rebase, push or PR; no sub-agents.
  Commit on your branch, no attribution lines.
- When editing an existing file, ADD to it; never rewrite it or drop existing functions.
- Port the prerequisite rather than skip an item "for scope"; if unsure of retail, leave it out and say so.

## Hand-in (relayed to the user, keep it tight)
<the hand-in format below, plus task-specific items>
```
- Perf work: add "The A/B needs a quiet machine: SendMessage "main" for the go, record
  tools/machine_load.ps1 (or stress_run's `machine load` line) in the report."
- When the dir is busy (the user's check run, another agent): "Do not launch a game until the
  coordinator messages you; build and read meanwhile, SendMessage "main" when ready."

## 5. Hand-in format (ask for it in every brief)
- Branch, tip commit, base commit, commit list.
- What ran, with results: build flavours, `tools/build.sh unit`, `tools/acceptance/acceptance.sh`,
  game runs (lane, dir, route), and for ports the retail address checked against.
- What did NOT run and why; what needs the user's eye (a route per body: agents often cannot see it).
- Shared files touched outside the file set, and conflicts expected at the merge.
- Open choices needing a ruling, each with a recommendation.

## 6. Review and merge (coordinator only)
- Read the diff and the FINAL files, not the report: `git diff --stat <base>..<tip>` against the
  claimed size and file set; a mismatch is the tell. Agents keep refining after reporting.
- Check that no existing function was dropped from an edited file; revert anything outside the set.
- "No behaviour change" claims are proven, not trusted: ordered calls, store widths, loop bounds.
- Merge one branch at a time. Build the merged tree BEFORE the merge commit; on a failure fix
  inside the merge or send the block back to `git merge main` on its own branch.
- Append-only conflicts: keep both sides; match `^=======$` as a whole line.
- No commit on the target branch while a merge is in progress, docs included: prepare files in
  the scratchpad and commit between merges.
- Never `git checkout --theirs` to resolve: it silently drops the other side's code.
- Parallel renames collide: each branch builds alone, the merge does not. Merge a block's shared
  headers early.
- One game run at a time from the coordinator: the lock is re-entrant for the same holder, and two
  overlapping runs splice logs. An impossible result: check for an overlap and re-run alone.
- Perf A/Bs only after every agent has reported and nothing builds; a non-CLEAN machine-load line
  is not evidence (skill stress-test).
- Before the merged PR: the gate (`tools/build.sh run`) and `tools/resave_gate/resave_gate.sh Gate`
  on the perf dir; a fresh worktree has no golden, so NEW-GOLDEN there is no check.

## 7. Lock and lockup recovery
- `tools/game_dir_lock.sh status` shows the holder; empty exe pids and an old last write = an
  idle holder: a message resumes the agent. `tools/game_dir_lock.sh orphans` names an unlocked run.
- A killed lock acquire or a TaskStop'd gate can orphan the lock: verify the holder is dead, then
  `tools/game_dir_lock.sh release <lane>`.
- After a machine lockup: `git status` in every worktree, `git fsck`, release stale locks, restore
  any tracked file written as NUL bytes from the index, then resume each agent by SendMessage.

## 8. Close
- Collect the user-visible "look owed" routes from the hand-ins into the close note.
- Remove the team's worktrees (`git worktree remove`) and delete merged branches, including the
  `worktree-agent-*` branches; ask before deleting anything unmerged. Anything beyond the team's own
  items is the skill `cleanup-branches`.
- A phone shared by agents has one driver at a time (skill `android-device`, "One driver per phone").
- Pointers: `tools/game_dirs.sh` and `tools/game_dir_lock.sh` headers, skills `triage-run`,
  `stress-test`, `menu-driver`, `ghidra`; CLAUDE.md "Subagents".
