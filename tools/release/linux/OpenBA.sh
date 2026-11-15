#!/usr/bin/env bash
# OpenBA.sh -- start OpenBA on Linux from the directory this script is in.
#
# Put every file of the release package next to the game's WinData folder (or make WinData
# a symlink to the retail install's WinData, see README-linux.txt) and run ./OpenBA.sh.
# The renderer is Vulkan (a Vulkan 1.1 driver is needed); SDL3 (libSDL3.so.0, beside the exe)
# opens the system's Vulkan loader.
# Logs (standalone_*.log) are written to the working directory; settings and saves go to
# ~/.local/share/Banished.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
export LD_LIBRARY_PATH="$HERE${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/OpenBA" "$@"
