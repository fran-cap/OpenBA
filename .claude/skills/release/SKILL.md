---
name: release
description: Cut a versioned OpenBA release (CMake VERSION bump, CHANGELOG section, PGO retrain, release PR, Release workflow dry run, tag v<version>, verify the published assets), or build a one-off signed tester apk from a Release workflow dispatch. Use when the user says "generate a 1.0.x release", "bump the version and tag/push the release", "build the release zip package", "lets discuss the next release", "get a release apk so I can hand it off to <tester>", "prepare the pgo for our next release".
---

# Cut a release

The Release workflow (`.github/workflows/release.yml`) does the building, packaging and publishing:
a `v*` tag push builds Windows / Linux / macOS / Android, runs `unit release`, and publishes the GitHub
release with the CHANGELOG section as its notes. A manual dispatch runs every job except `release`
(a dry run; the packages are the run's artifacts). Pull requests never run it.
**Pushing the tag, merging the release PR and any `gh release edit` are outward-facing: each needs the
user's explicit go-ahead in this session.** Nothing is published by hand: no local zip upload, no sha file.

## 1. Pre-flight
- `main` green on the four CI checks; ask which open PRs the user wants in first (`gh pr list`), and land
  them with skill `land-pr` before the release branch is cut.
- Last release and what changed since: `git tag --list 'v*' | tail -3`,
  `git log --oneline v<prev>..origin/main --merges`, `gh release view v<prev> --json assets -q '.assets[].name'`.
- Branch from fresh main: `git switch -c release/<version> origin/main` (main is protected; the release
  lands by PR).

## 2. Version bump
- `CMakeLists.txt` line `project(banished_reimpl VERSION X.Y.Z LANGUAGES CXX)` is the only number to change.
  It becomes `OPENBA_VERSION` (main menu build line: `OpenBA vX.Y.Z <commit> <date>`, the commit from
  `cmake/build_info.cmake`, `+` when the tree was dirty) and the apk's versionName / versionCode
  (`android/app/build.gradle` reads the same line). README "Versioning" explains it; no other file carries it.
- The tag must equal `v<CMake VERSION>`: the workflow's `version` job refuses anything else.

## 3. CHANGELOG section
- Rename `## Unreleased` to `## vX.Y.Z (YYYY-MM-DD)` in `CHANGELOG.md`; check it parses:
  `python tools/release/changelog_section.py X.Y.Z > /dev/null && echo SECTION_OK` (exit 1 = no notes =
  the workflow refuses to publish).
- Open the section with a short numbered **Major Changes:** list, then the grouped bullets
  (**Platforms and packages**, **Build**, **Fixes**, **Extensions**, **Android**): short itemized bullets,
  one change each, ext rows named by id. Project voice, no "we" / "I".
- Credits: every PR author by GitHub login with their PR numbers
  (`gh pr list --state merged --base main --limit 200 --json number,author,title,mergedAt`), and a closing
  `Thanks to ... for their pull requests, and to ... for ...` line. **Ask the user for the bug reporters and
  testers to thank, and how to name them; never infer one from a login** (a tester "Karl" is not the
  contributor kralyerg). A PR number in a credit is checked against `gh pr view N`.
- Show the section to the user and wait: the user edits the notes before anything is pushed.

## 4. PGO retrain (every release; also after any change to a profiled hot file)
- Windows: park the mods, quiet machine, then `perf/pgo/train.sh --build` (instrumented build, the 10x and
  50x stress towns, release rebuild on the new set). Commit `perf/pgo/profile/` on the release branch.
- Android (phone over adb; skill `android-device` for the bed): the recipe in `perf/pgo/android/README.md`
  (`tools/android.sh build release --pgo generate`, the two towns, `tools/android.sh pgo-merge`), commit
  `perf/pgo/android/openba.profdata`. There is no `train_android.sh`; the README is the recipe.
- Linux and macOS have no profile set (plain -O3).

## 5. Local gates (CI has no game data)
Under the game-dir lock (`tools/game_dirs.sh`), in order:
`tools/build.sh unit release`, `tools/build.sh run` (skill `triage-run` if it dies),
`tools/resave_gate/resave_gate.sh Gate`. State the results in the PR body.

## 6. Release PR and dry run
- Commit (`release: vX.Y.Z -- ...`), push, `gh pr create` (body: what changed in the bump, the gates run).
- Dry run on the branch: `gh workflow run release.yml --ref release/<version>`, then
  `gh run list --workflow release.yml --limit 1`, and watch it in chunks under the 10-minute command limit:
  `timeout 560 gh run watch <id> --interval 30 --exit-status`. A cold dry run takes about 45 minutes.
- Inspect: `gh run download <id> -n package-windows -n release-notes -D <scratch>`; list each archive and
  confirm the texts (README, LICENSE, THIRD_PARTY_NOTICES, the FrancophilSans fonts + README) and no
  `.pkg` / `.pkm` / `WinData` (package.sh and make_release_zip.sh also refuse them).

## 7. Merge, tag, publish (each on the user's go-ahead)
```bash
gh pr checks <N>                         # all four green on the head commit
gh pr merge <N> --merge                  # the release PRs were merge commits
git switch main && git pull --ff-only
grep -n "VERSION X.Y.Z" CMakeLists.txt   # on the merged commit
git tag -a vX.Y.Z -m "OpenBA X.Y.Z" && git push origin vX.Y.Z
gh run list --workflow release.yml --limit 2   # the tag run (about 20 min warm)
```

## 8. Verify the published release
```bash
gh release view vX.Y.Z --json isDraft,assets -q '.isDraft, (.assets[] | "\(.size) \(.name)")'
gh api repos/fran-cap/OpenBA/releases/tags/vX.Y.Z -q '.assets[] | "\(.name) \(.digest)"'
```
Four assets: `OpenBA-vX.Y.Z-win-x64.zip`, `OpenBA-vX.Y.Z-linux-x64.tar.gz`, `OpenBA-vX.Y.Z-mac-arm64.zip`,
`OpenBA-vX.Y.Z-android-arm64.apk`. The digests are GitHub's SHA-256; do not add sha files.
A notes fix after publish: `gh release edit vX.Y.Z --notes-file <f>` (go-ahead first) and the same change
in `CHANGELOG.md` on main.

## Tester apk (no version bump, no release)
For an issue reporter: the commands, the smoke run on the attached phone and the ask-before-posting rule
are in skill `android-device` ("An apk for an issue reporter"). The file carries the current CMake
version (the last release's number); say so when handing it off, with the run id. The dispatch builds
every platform.

## Traps
- **Windows zip name:** `OpenBA-vX.Y.Z-win-x64.zip` (`tools/release/package.sh`); releases up to 1.0.2
  had no suffix, so a script or note that looks for the old name misses the asset. release.yml uploads
  `dist/*.zip` and does not name it.
- **A stale PGO set can break the release build**, not just warn: GCC 16.2 hit an ICE ("during IPA pass:
  profile") and a 3.5 GB allocation on files changed since the last retrain. Retrain (step 4); meanwhile
  `CMAKE_EXTRA="-DBANISHED_PGO=off" tools/build.sh build release` (a cache variable: it sticks in
  `build/cmake-release`). Debug / og / perf never read the set.
- **Android profile only taken at configure:** an existing release `.cxx` dir configured before
  `pgo-merge` keeps building plain; remove it (`perf/pgo/android/README.md`).
- **New scripts from a Windows checkout are mode 644**: the Linux job fails "Permission denied".
  `git update-index --chmod=+x <script>` before the dry run.
- **The version bump recompiles every object of every flavour** (`OPENBA_VERSION` is a global define): the
  next gate is a full debug rebuild.
- **`unit release` locally fails `ext_sheet_kit_crosscheck`** on stale local ToolKit sprite output; it
  skips on CI and is not a release finding.
- **Checksums:** never hand-make `.sha256` files; GitHub records each asset's digest.
- **Apk users need the data route:** the notes point Android players to `docs/android/README.md`
  "Installing the game files" (their own `WinData`, the first-run screen); no game content in any package.
- **Release notes are user-reviewed**: never push the tag on a section the user has not seen.
