#!/usr/bin/env bash
# tools/ext_resources.sh -- compile the extension resources with the official mod kit's compiler.
#
# usage: tools/ext_resources.sh [--deploy <game-dir>] [--kit <ToolKit-dir>] [--pkg <WinData-dir>]
#
# INPUTS
#   src/ext/ui/**              our resource sources (NOT RETAIL), laid out as under the kit's
#                              resource/: Dialog/Extensions.rsc (the dialog + its menu button),
#                              Dialog/ExtensionsStringTable.rsc, Dialog/ExtensionsSprites*.rsc,
#                              Dialog/Sprite/Extensions/*.png (programmer art, tools/ext_sprites.py),
#                              Font/SharpFont*.rsc (ext.sharp_ui's font variants, tools/ext_fonts.py)
#   the typeface FrancophilSans  installed for the user running this (per-user or machine-wide:
#                              Settings > Fonts). The compiler RASTERISES the Font resources from the
#                              installed face named by `_fontName`; without it Windows substitutes
#                              another face SILENTLY and the compile "succeeds". A machine dependency
#                              of this step, like the kit. Checked before the compile (the Fonts
#                              registry keys) and after it (tools/ext_font_check.py: the kit's own
#                              Font/Font.rsc must compile byte-identical to the game's shipped glyph
#                              tables); either failing fails this script, naming the font.
#   ToolKit/                   the Banished Modding ToolKit 1.0.7 (git-ignored; README.html section 3):
#                              bin/x64/Tools-x64.exe (+ Runtime/CompileWin/VideoCompiler -profile DLLs)
#                              and resource/ (the game's .rsc sources). Looked up at the repo root,
#                              then at the main checkout's root when run from a git worktree, or --kit.
#   <game>/WinData/*.pkg       the game's packages (--pkg <dir>; default $BANISHED_DIR/WinData, with
#                              BANISHED_DIR as tools/build.sh: C:/GOG Games/Banished). READ ONLY:
#                              hard-linked (copied when a link fails) into the /pathdat directory,
#                              and /pathpkg is that same directory. Found by trial: /pathpkg at the
#                              game's WinData (a path with a space) or at a junction to it, and the
#                              packages in /pathdat with /pathpkg left at its default, all gave
#                              "compiled resource not found in pack"; packages in /pathdat AND
#                              /pathpkg = /pathdat works. The kit's README (section 4) copies the .pkg files beside the
#                              tool for the same reason: a base resource the kit ships no source for
#                              (MeshLayout/UILayout.rsc, which Material/UI/uiMaterial.rsc needs) is
#                              then taken compiled from data1.pkg. Only *.pkg: no .pkm mod leaks in.
# WORK
#   build/ext_resources/res/   the compiler's /pathres: a copy of the kit's resource/ (copied once,
#                              refreshed by timestamp) with src/ext/ui/ laid over it, so references to
#                              the game's resources (Dialog/SharedElements.rsc, Dialog/Mod.rsc,
#                              Dialog/MainMenu.rsc, Font/..., Material/UI/uiMaterial.rsc) resolve
#                              and inherit. The kit itself is never written to.
# OUTPUTS
#   build/ext_resources/WinData/  the compiler's /pathdat: every compiled object, one .crs per named
#                              object (`Dialog/Extensions.rsc:buttonExtensions` ->
#                              Dialog/Extensions_buttonExtensions.crs, an unnamed `resource` ->
#                              <file>_resource.crs), OURS AND the base dependencies the compiler
#                              built on the way, and the linked *.pkg. Not committed (build/ is
#                              git-ignored).
#   --deploy <game-dir>        copies only OUR outputs loose into <game-dir>/WinData/ (loose files
#                              load before the .pkg): Dialog/Extensions*.crs, Dialog/EnhancedOptions_*.crs, and per font variant
#                              Font/SharpFont*_{resource,material,texture}.crs and
#                              Build/SharpFont*Sheet_resource.crs (the glyph image is inside the
#                              texture .crs; the PNGs are compiler intermediates and are not deployed).
#
# The compiler: `Tools-x64.exe /build <resource> /pathres <dir> /pathdat <dir>`, run from the kit's
# directory (bin/x64/cmd.txt sets `/bin ..\`); /pathres and /pathdat are taken RELATIVE TO the kit's
# bin/ (an absolute path is appended to it), so this script passes relative paths. One /build per
# top-level object: the dialog, its button (nothing references it, so the dialog's build does not
# reach it), the string table, the sprite sheet, each font variant (a variant whose four outputs are
# newer than its source is skipped: 54 launches cost ~1 min). The tool exits 0 even when it failed and shows a
# modal "Assert" box for a missing resource, so every call runs under a timeout and its log is
# checked for Warning / Error / Failed lines.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY=""
KIT=""
PKG="${BANISHED_DIR:-C:/GOG Games/Banished}/WinData"
while [ $# -gt 0 ]; do
  case "$1" in
    --deploy) DEPLOY="$2"; shift 2;;
    --kit) KIT="$2"; shift 2;;
    --pkg) PKG="$2"; shift 2;;
    *) echo "ext_resources: unknown argument $1"; exit 2;;
  esac
done

if [ -z "$KIT" ]; then
  KIT="$ROOT/ToolKit"
  if [ ! -f "$KIT/bin/x64/Tools-x64.exe" ]; then
    # A git worktree has no copy of the git-ignored kit: use the main checkout's.
    COMMON="$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)"
    [ -n "$COMMON" ] && KIT="$(cd "$COMMON/.." && pwd)/ToolKit"
  fi
fi
TOOL="$KIT/bin/x64/Tools-x64.exe"
if [ ! -f "$TOOL" ] || [ ! -d "$KIT/resource" ]; then
  echo "ext_resources: FAILED -- the mod kit is absent: unpack the Banished Modding ToolKit 1.0.7 into <repo root>/ToolKit/ (ToolKit/bin/x64/Tools-x64.exe and ToolKit/resource/)."
  exit 1
fi
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *)
  echo "ext_resources: FAILED -- Tools-x64.exe is a Windows program; run this on Windows (Git Bash / MSYS2)."; exit 1;;
esac

B="$ROOT/build/ext_resources"
RES="$B/res"
DAT="$B/WinData"
LOGS="$B/logs"
mkdir -p "$RES" "$DAT" "$LOGS"

# The /pathres tree: the kit's sources, then ours over them (timestamps kept: the compiler rebuilds
# an object when its source is newer than the compiled file).
cp -r -u --preserve=timestamps "$KIT/resource/." "$RES/" || { echo "ext_resources: FAILED -- copying the kit's resource/"; exit 1; }
cp -r --preserve=timestamps "$ROOT/src/ext/ui/." "$RES/" || { echo "ext_resources: FAILED -- copying src/ext/ui/"; exit 1; }

# The game's packages beside the compiled data (see the header): hard links, else copies.
ls "$PKG"/*.pkg > /dev/null 2>&1 || { echo "ext_resources: FAILED -- no game packages (*.pkg) in $PKG (--pkg <the game's WinData>)"; exit 1; }
for P in "$PKG"/*.pkg; do
  N="$(basename "$P")"
  if [ ! -f "$DAT/$N" ] || [ "$(stat -c %s "$P")" != "$(stat -c %s "$DAT/$N")" ]; then
    rm -f "$DAT/$N"
    ln "$P" "$DAT/$N" 2>/dev/null || cp -f "$P" "$DAT/$N" || { echo "ext_resources: FAILED -- linking $P"; exit 1; }
  fi
done

rel() { realpath -m --relative-to="$KIT/bin" "$1"; }
RRES="$(rel "$RES")"; RDAT="$(rel "$DAT")"
case "$RRES$RDAT" in [A-Za-z]:*|/*) echo "ext_resources: FAILED -- the repo and the kit must be on one drive (paths are relative to the kit's bin/)"; exit 1;; esac

# The typeface (see INPUTS): installed for this user or for the machine.
FACE="FrancophilSans"
face_installed() {
  local K
  for K in 'HKCU\Software\Microsoft\Windows NT\CurrentVersion\Fonts' 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts'; do
    MSYS2_ARG_CONV_EXCL='*' reg.exe query "$K" /v "$FACE (TrueType)" > /dev/null 2>&1 && return 0
  done
  return 1
}
if ! face_installed; then
  echo "ext_resources: FAILED -- the typeface $FACE is not installed (no '$FACE (TrueType)' under the HKCU / HKLM Fonts keys). The kit's compiler rasterises the game's fonts from it and would silently substitute another face. Install FrancophilSans (the kit's README: 'The game uses the font FrancophilSans'), then run this again."
  exit 1
fi

TARGETS=(
  "Dialog/Extensions.rsc"
  "Dialog/Extensions.rsc:buttonExtensions"
  "Dialog/ExtensionsStringTable.rsc"
  "Dialog/ExtensionsSprites.rsc"
  "Dialog/EnhancedOptions.rsc:buttonEnhanced"    # ext.ambient_occlusion: the Options dialog's Enhanced tab,
  "Dialog/EnhancedOptions.rsc:enhancedOptions"   # added at run time by TabElement::AddTab (AOMENU)
  "Font/Font.rsc"
)
# The typeface witness (tools/ext_font_check.py): the kit's own Font/Font.rsc, recompiled on every
# run so the check always reflects the face installed NOW (the compiler would keep an old output).
rm -f "$DAT/Font/Font_resource.crs"
# ext.sharp_ui's font variants, each skipped while its four outputs are up to date. The compiler
# stamps an output with its SOURCE's time, so "up to date" is "the source is not newer".
FONTS=0; FONTS_SKIPPED=0
for SRC in "$ROOT"/src/ext/ui/Font/SharpFont*.rsc; do
  [ -f "$SRC" ] || continue
  N="$(basename "$SRC" .rsc)"
  FONTS=$((FONTS + 1))
  FRESH=1
  for O in "Font/${N}_resource.crs" "Font/${N}_material.crs" "Font/${N}_texture.crs" "Build/${N}Sheet_resource.crs"; do
    [ -s "$DAT/$O" ] && ! [ "$SRC" -nt "$DAT/$O" ] || FRESH=0
  done
  if [ "$FRESH" = 1 ]; then FONTS_SKIPPED=$((FONTS_SKIPPED + 1)); else TARGETS+=("Font/$N.rsc"); fi
done
# The compiler's problem lines. Its progress lines ("Compiling '<file>:<name>'" and
# "  --> Compiled <type> '<file>:<name>'") quote resource names, which may contain these words
# (Dialog/Extensions.rsc:saveFailed), so they are dropped before the search.
problems() {
  grep -v -E "^(Compiling '|  --> Compiled )" "$1" | grep -E 'Warning|Error|Failed|Unable|Assert'
}
FAIL=0
for T in "${TARGETS[@]}"; do
  LOG="$LOGS/$(echo "$T" | tr '/:' '__').log"
  ( cd "$KIT" && MSYS2_ARG_CONV_EXCL='*' timeout 900 ./bin/x64/Tools-x64.exe /build "$T" /pathres "$RRES" /pathdat "$RDAT" /pathpkg "$RDAT" ) > "$LOG" 2>&1
  RC=$?
  BAD="$(problems "$LOG" | wc -l)"
  echo "ext_resources: /build $T -> exit $RC, $(grep -c -- '--> Compiled' "$LOG") compiled, $BAD problem line(s)  ($LOG)"
  if [ "$RC" != 0 ] || [ "$BAD" != 0 ]; then
    FAIL=1
    problems "$LOG" | sort -u | head -10
  fi
done
[ "$FAIL" = 0 ] || { echo "ext_resources: FAILED (see $LOGS)"; exit 1; }
echo "ext_resources: font variants: $FONTS source(s), $((FONTS - FONTS_SKIPPED)) compiled, $FONTS_SKIPPED up to date"

# The typeface proof and the variants' contents (see INPUTS).
if ! python -I "$ROOT/tools/ext_font_check.py" "$DAT" "$RES"; then
  # Variants compiled with the wrong face must not survive as "up to date".
  rm -f "$DAT"/Font/SharpFont*.crs "$DAT"/Build/SharpFont*.crs
  echo "ext_resources: FAILED -- the font check (typeface $FACE); the variant outputs were removed"
  exit 1
fi

# Our outputs: present and non-empty.
mapfile -t OURS < <(cd "$DAT" && ls Dialog/Extensions*.crs Dialog/EnhancedOptions_*.crs Font/SharpFont*.crs Build/SharpFont*Sheet_resource.crs 2>/dev/null)
[ -n "$(cd "$DAT" && ls Dialog/Extensions*.crs 2>/dev/null)" ] || { echo "ext_resources: FAILED -- no Dialog/Extensions*.crs under $DAT"; exit 1; }
for F in "${OURS[@]}"; do
  [ -s "$DAT/$F" ] || { echo "ext_resources: FAILED -- $F is empty"; exit 1; }
done
NDLG="$(cd "$DAT" && ls Dialog/Extensions*.crs | wc -l)"
NFNT="$(cd "$DAT" && ls Font/SharpFont*.crs Build/SharpFont*Sheet_resource.crs 2>/dev/null | wc -l)"
KB="$(cd "$DAT" && du -ck "${OURS[@]}" | tail -1 | cut -f1)"
echo "ext_resources: ${#OURS[@]} file(s), ${KB} KB: $NDLG in Dialog/ (Extensions*.crs), $NFNT font variant file(s) in Font/ + Build/ (SharpFont*)"

if [ -n "$DEPLOY" ]; then
  mkdir -p "$DEPLOY/WinData/Dialog" "$DEPLOY/WinData/Font" "$DEPLOY/WinData/Build" || exit 1
  for F in "${OURS[@]}"; do cp -f "$DAT/$F" "$DEPLOY/WinData/$F" || exit 1; done
  echo "ext_resources: deployed ${#OURS[@]} file(s) loose into $DEPLOY/WinData/{Dialog,Font,Build}/"
fi
