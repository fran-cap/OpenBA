#!/bin/bash
# tools/kill_game.sh <game dir> -- end the OpenBA.exe that runs FROM that game dir, and no other.
#
# The harness scripts (stress_run.sh, harness_route.sh, device_sim.sh) call this when a run passes its
# deadline. A kill by image name (`taskkill //IM OpenBA.exe`) also closes the user's own game in the other
# game dir, and the pid bash holds for a launched native exe is a wrapper's, not the game's: killing it
# leaves the game up. The image PATH is the one thing that names this run.
#
# Exit 0 when a process was ended, 1 when none ran from that dir.
set -u
[ $# -eq 1 ] || { echo "usage: tools/kill_game.sh <game dir>" >&2; exit 2; }
DIR="$1"
command -v cygpath >/dev/null 2>&1 && DIR="$(cygpath -w "$DIR")"
EXE="${DIR%\\}\\OpenBA.exe"
export KILL_GAME_EXE="$EXE"
powershell -NoProfile -Command '
    $hit = @(Get-Process OpenBA -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $env:KILL_GAME_EXE })
    foreach ($p in $hit) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    if ($hit.Count -gt 0) { exit 0 } else { exit 1 }'
