---
name: cleanup-branches
description: Clean up stale local and remote branches, git worktrees, stashes and leftover folders safely - inventory with a merged / squash-merged / unmerged / open-PR / dirty verdict per item, skip what another session uses or the user names as WIP, show the delete and keep lists, bundle a backup, remove in a fixed order. Use for "clean up the branches", "clean up our unused local and remote branches", "clean up all of our stale branches and worktrees", "clean up my old worktrees (other than X, still a wip)", "remove those stashes as well". One finished PR's own worktree is `land-pr`; a team's, `agent-team`.
---

# Clean up branches, worktrees and stashes

Destructive and partly unrecoverable (remote branches, stashes, untracked files). Run from the main
checkout (`C:/code/GIT/untitled_rev_eng_project`) in Git Bash. Nothing is deleted before step 4.
Helper output goes to `<scratchpad>/cleanup/` (`S` below), never into the repo.

## 1. Inventory
```bash
cd /c/code/GIT/untitled_rev_eng_project && git fetch --prune origin
git worktree list --porcelain                     # path, HEAD, branch, and a `locked` line if any
git for-each-ref --format='%(refname:short) %(objectname:short) %(committerdate:short) %(upstream:track)' refs/heads
git for-each-ref --format='%(refname:short) %(committerdate:short)' refs/remotes/origin
git stash list
gh pr list --state all --limit 400 --json number,state,headRefName,headRefOid \
  --jq '.[]|"\(.headRefName) \(.state) #\(.number) \(.headRefOid)"' > "$S/prs.txt"
ls -d /c/code/GIT/openba-* /c/code/GIT/openba_* /c/code/GIT/wt-* /c/code/GIT/oba-* .claude/worktrees/* 2>/dev/null
```
Per branch (local and `origin/*`), a verdict, in this order:
```bash
for b in $(git for-each-ref --format='%(refname:short)' refs/heads); do
  f=$(git diff --name-only origin/main...$b)
  echo "$b ahead=$(git rev-list --count origin/main..$b) cherry+=$(git cherry origin/main $b | grep -c '^+')" \
       "content=$( [ -z "$f" ] || git diff --quiet $b origin/main -- $f && echo in-main || echo differs)" \
       "pr=$(grep "^$b " "$S/prs.txt" | cut -d' ' -f2,3 | tr '\n' ' ')"
done
```
The same loop over `refs/remotes/origin` (skip `origin/HEAD`, `origin/main`) grep `prs.txt` for `${b#origin/}`.
- `ahead=0` -> **merged**. `cherry+=0` -> **rebased in** (same patches, new hashes).
- PR `MERGED` and the branch tip is that PR's `headRefOid` (or an ancestor of it) -> **squash-merged**.
  Commits past the PR head are unmerged work. `git branch --merged` cannot see squash merges.
- `content=in-main` -> the files the branch touched match main now: content landed. `differs` with a
  merged PR is usually main moving on since; check the commits by hand before calling it stale.
- PR `OPEN` -> **keep**. Anything else -> **unmerged**: list its commits (`git log --oneline origin/main..$b`).

Per worktree: who is using it, then whether it is dirty (this order: `git status` may refresh the
index, which wipes the activity signal).
```bash
stat -c '%y' "$(git -C "$w" rev-parse --git-dir)/index"   # touched in the last hours = someone is in it
git -C "$w" status --porcelain --ignore-submodules=none     # `??` = untracked, lost on removal
git -C "$w" submodule foreach --quiet 'git status --short'
cmd //c "dir /AL /B $(cygpath -w "$w/third_party")"           # junctions into the main checkout
```

## 2. Keep, never delete
- `main`, and the **main checkout's current branch** when it is not main or is dirty: another session
  (or the user) works there. Do not `git checkout main` in it to pull; say it was left.
- Anything the user names as WIP, any branch with an **open PR**, contributor forks.
- A worktree that is `locked` (a live Claude agent), has a recent index mtime, has an `OpenBA.exe`
  running from it (`powershell -c "Get-Process OpenBA | Select Id,Path,StartTime"`), or is this
  session's own cwd. Ask about it; do not force it.
- `origin/pr-assets/*`: images linked from PR descriptions. Deleting them breaks those PRs' pictures.
- `refs/archive/*` and earlier backup bundles in `C:/code/GIT/`.

## 3. Show the lists, get the go-ahead
One table: item, verdict, last commit date, dirty/untracked count, delete or keep and why. Merged and
squash-merged **local** branches and clean worktrees of merged branches may go on a general "clean
up" request. **Unmerged branches, dirty worktrees, remote branches and stashes need an explicit yes**
naming them. A stash is shared by every worktree: list each with `git stash show --stat stash@{N}`.

## 4. Backup, then remove in this order
```bash
D=/c/code/GIT/branch_cleanup_$(date +%Y%m%d); mkdir -p "$D/worktree_patches"
git for-each-ref --format='%(objectname) %(refname)' refs/heads refs/remotes/origin > "$D/refs.txt"
git bundle create "$D/branches_$(date +%Y%m%d).bundle" refs/heads/<each to delete> refs/remotes/origin/<each to delete>
git bundle verify "$D/branches_$(date +%Y%m%d).bundle"
# each dirty worktree w named n:
git -C "$w" diff HEAD --binary > "$D/worktree_patches/$n.patch"
git -C "$w" ls-files -o --exclude-standard -z | tar -C "$w" --null -T - -czf "$D/worktree_patches/${n}_untracked.tgz"
echo "$n base=$(git -C "$w" rev-parse --short HEAD)" >> "$D/worktree_patches/BASES.txt"
# each stash to drop:
git stash show -p --include-untracked stash@{N} > "$D/stash_N.patch"
```
Re-run `git worktree list` now: a worktree that was not in the step 1 list belongs to another session
since; leave it. Then, **only the approved names** (never "everything but main"):
1. Worktrees: unlink junctions first (`cmd //c rmdir "<junction>"`), then `git worktree remove "$w"`;
   `--force` only for "working trees containing submodules cannot be moved or removed" after step 1
   showed the submodules clean; `--force --force` for a locked one the user released.
2. Leftover folders: a `C:/code/GIT/openba-*` dir where `git -C <dir> rev-parse` fails is an orphan
   (build output), `rm -rf` it by exact path. `C:/code/GIT` holds unrelated projects: no globs.
3. `git worktree prune`, then local branches: `git branch -d` for merged, `-D` for the approved rest.
4. Remote: `git push origin --delete <b1> <b2> ...`.
5. Stashes: `git stash drop stash@{N}` from the highest N down (`git stash clear` only when all go).
6. `git fetch --prune origin`; `git pull --ff-only` only if the main checkout is on a clean `main`.

## 5. Report
Deleted (worktrees, local, remote, stashes, folders, with the verdict for each unmerged one); kept and
why; the backup dir, its bundle and patch files, and the restore line
`git fetch <bundle> refs/heads/<name>:refs/heads/<name>`; anything left for the user to rule on.
Record the backup dir in memory (a `reference_branch_cleanup_bundle_<date>.md` entry).

## Traps that bit
- Unmerged `perf/likely-*` branches were deleted without a bundle: the only copy of a study was lost.
- `pr-assets/*` remote branches were deleted: screenshots in two merged PRs' descriptions broke.
- Worktrees that appeared between inventory and removal were another session's new work; a delete
  loop over "every worktree but main" would have taken them. Re-list and act on names only.
- A branch appeared in the main checkout mid-cleanup: another session switched it. Leave it.
- `git worktree remove` refuses any worktree with submodules; `--force` is safe only once checked clean.
- Agent worktrees once junctioned `third_party/` to the main checkout's submodules: unlink first so
  a forced removal cannot delete through the junction.
- `git push origin --delete` with a ref already gone prints "remote ref does not exist" and exits 1:
  fetch --prune first so the list is current.
- `git stash` is shared across all worktrees; `git stash clear` drops other sessions' stashes too.
