#!/usr/bin/env bash
# pre_bash_guard.sh -- PreToolUse hook (Bash|PowerShell): refuse an unscoped find, killing OpenBA.exe by name,
# pacman installs, attribution trailers in commits / PR bodies, and staging retail content.
# Wired in .claude/settings.json: sourced when the hook shell is bash (Git Bash on Windows), else run with bash
# (Claude Code runs a shell-form hook with `sh -c` on Linux / macOS, and dash lacks read -d, shopt, <<< and
# [[ ]] used below); tests: python -I .claude/hooks/tests/test_hooks.py
#
# A bash-builtin prefilter passes most commands without starting python (Git Bash alone costs ~13-18 ms, python
# ~13 ms more): only a command that names one of the trigger words reaches pre_bash_guard.py, which tokenizes
# it, unwraps bash -c / cmd /c / powershell -Command, and applies the rules (listed in the .py).
# Exit 2 + one stderr line refuses the call. A guard fails open on its own errors (python missing, bad JSON):
# the permission deny rules in .claude/settings.json still stop the plainest spellings in that case.
case $BASH_SOURCE in
    */*) HERE=${BASH_SOURCE%/*} ;;
    *) HERE=. ;;  # run by a bare relative name, ${BASH_SOURCE%/*} would be the file name itself
esac
IN=
IFS= read -r -d '' IN || true
shopt -s nocasematch
# gh is matched by the trailer words themselves (R4 needs one of them anyway), so `gh -R o/r pr create` and
# `gh  pr` reach python while the daily `gh pr view` / `gh pr checks` do not; childitem / "gci " catch
# `gci -r C:\` and `Get-ChildItem -Path C:\ -r`.
case "$IN" in
    *find* | *taskkill* | *kill* | *stop-process* | *spps* | *wmic* | *win32_process* | *pacman* | *commit* | \
    *" add "* | *co-authored* | *claude-session* | *"generated with"* | *recurse* | *-rec* | *"ls -"* | \
    *"dir "* | *childitem* | *"gci "*) ;;
    *) exit 0 ;;
esac
shopt -u nocasematch
# python, not python3, on Windows (python3 is the Microsoft Store alias there); python3 elsewhere.
case $OSTYPE in
    msys* | cygwin*) PY=python ;;
    *) PY=python3 ;;
esac
command -v "$PY" > /dev/null 2>&1 || exit 0
# Imported as a module (not run as a script) so python keeps its bytecode in __pycache__/ (gitignored).
# A refusal comes back as 3, not 2: the interpreter itself exits 2 on a command-line error (a python that
# rejects -I, say), which must fail open like every other guard error.
"$PY" -I -S -c 'import sys; sys.path.insert(0, sys.argv[1]); import pre_bash_guard as g
sys.exit(3 if g.main() == 2 else 0)' "$HERE" <<< "$IN"
[[ $? == 3 ]] && exit 2
exit 0
