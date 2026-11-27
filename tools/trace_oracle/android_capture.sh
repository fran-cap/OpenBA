#!/usr/bin/env bash
# Capture one in-game frame of OpenBA on an attached Android device under RenderDoc.
#
#   tools/trace_oracle/android_capture.sh [--serial S] [--wait SECS] --out FILE.rdc [BANISHED_*=V ...]
#
# Example, the stress town at retail's max zoom:
#   tools/trace_oracle/android_capture.sh --wait 150 --out s25.rdc \
#       BANISHED_STRESS=1 BANISHED_STRESS_SPEED=1 BANISHED_STRESS_FRAMES=100000 BANISHED_CAMERA_PIN=town,130
# then the per-pass report, replayed on the device:
#   $PY tools/trace_oracle/gpu_timing.py s25.rdc s25.json --android S
#
# Needs the DEBUG apk (tools/android.sh build debug; install debug): RenderDoc cannot hook a
# release-key build on an unrooted phone. The flags reach the game as intent extras beside a
# one-use harness token, as tools/android.sh run passes them.
#
# Three things this script does that the RenderDoc GUI does not (each one failed on the S25,
# Android 16, RenderDoc 1.44, 2026-11-26):
#   1. Grants the server apk "All files access" (appops MANAGE_EXTERNAL_STORAGE): without it
#      the server opens the Settings page and never listens.
#   2. Copies libVkLayer_GLES_RenderDoc.so into the app's data dir with run-as: the Vulkan
#      loader searched only the game's own folders, not gpu_debug_layer_app's, so the game's
#      vkCreateInstance failed with VK_ERROR_LAYER_NOT_PRESENT and hwui's own Vulkan init
#      aborted ("Assertion failed: err < 0").
#   3. Leaves debug.hwui.renderer alone: forcing hwui to GL (skiagl) makes RenderDoc hook
#      the UI's GLES context, and the capture is the Android UI, not the game.
# Afterwards it removes the copied layer and the gpu_debug_* settings and stops the game.
#
# GPU time on Adreno: per-draw timestamps read ~0 on a tiler (the S25 frame summed to
# 0.14 ms). Use the capture for structure (passes, draw counts, triangles, shaders); for
# GPU load read /sys/class/kgsl/kgsl-3d0/{gpu_busy_percentage,clock_mhz} on a live run.
set -euo pipefail
export MSYS_NO_PATHCONV=1

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG=org.openba.game
RDPKG=org.renderdoc.renderdoccmd.arm64
RDAPK="${RENDERDOC_DIR:-C:/Program Files/RenderDoc}/plugins/android/$RDPKG.apk"
DATA=/sdcard/Android/data/$PKG/files
LAYER=libVkLayer_GLES_RenderDoc.so
PY="${PY:-$ROOT/tools/renderdoc_mcp/.venv/Scripts/python.exe}"

SERIAL="${ANDROID_SERIAL:-}"
WAIT=150
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --serial) SERIAL="$2"; shift 2 ;;
        --wait)   WAIT="$2"; shift 2 ;;
        --out)    OUT="$2"; shift 2 ;;
        BANISHED_*=*) break ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$OUT" ] || { echo "usage: android_capture.sh [--serial S] [--wait SECS] --out FILE.rdc [BANISHED_*=V ...]" >&2; exit 2; }
if [ -z "$SERIAL" ]; then
    SERIAL="$(adb devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')"
fi
[ -n "$SERIAL" ] || { echo "no adb device" >&2; exit 2; }
ADB=(adb -s "$SERIAL")
[ -x "$PY" ] || { echo "no RenderDoc python at $PY (tools/trace_oracle/README.md, The interpreter)" >&2; exit 2; }
[ -f "$RDAPK" ] || { echo "no RenderDoc server apk at $RDAPK" >&2; exit 2; }

pkginfo="$("${ADB[@]}" shell dumpsys package $PKG)"
grep -q "pkgFlags=.*DEBUGGABLE" <<<"$pkginfo" \
    || { echo "$PKG is not the debug apk (tools/android.sh build debug && install debug)" >&2; exit 2; }
"${ADB[@]}" shell "test -d '$DATA'" || { echo "no $DATA: start the app once (tools/android.sh run)" >&2; exit 1; }

# The server apk at the version of the host's RenderDoc.
"${ADB[@]}" install -r -g "$(cygpath -w "$RDAPK" 2>/dev/null || echo "$RDAPK")" >/dev/null
"${ADB[@]}" shell appops set --uid $RDPKG MANAGE_EXTERNAL_STORAGE allow
"${ADB[@]}" shell appops set $RDPKG MANAGE_EXTERNAL_STORAGE allow

# The layer into the app's data dir, where the loader looks.
tmp="$(mktemp -d)"
cleanup() {
    "${ADB[@]}" shell am force-stop $PKG || true
    "${ADB[@]}" shell am force-stop $RDPKG || true
    "${ADB[@]}" shell run-as $PKG rm -f "/data/user/0/$PKG/$LAYER" || true
    "${ADB[@]}" shell rm -f "/data/local/tmp/$LAYER" || true
    for k in gpu_debug_app gpu_debug_layers gpu_debug_layers_gles gpu_debug_layer_app; do
        "${ADB[@]}" shell settings delete global $k >/dev/null || true
    done
    "${ADB[@]}" shell settings put global enable_gpu_debug_layers 0 || true
    "${ADB[@]}" forward --remove-all || true
    rm -rf "$tmp"
}
trap cleanup EXIT
(cd "$tmp" && unzip -q -o "$RDAPK" "lib/arm64-v8a/$LAYER")
# MSYS_NO_PATHCONV also stops the local path's conversion: hand adb.exe a Windows path.
"${ADB[@]}" push "$(cygpath -w "$tmp/lib/arm64-v8a/$LAYER" 2>/dev/null || echo "$tmp/lib/arm64-v8a/$LAYER")" \
    /data/local/tmp/ >/dev/null
"${ADB[@]}" shell "run-as $PKG cp /data/local/tmp/$LAYER /data/user/0/$PKG/"

# The intent extras and the one-use harness token (tools/android.sh run).
extras=()
for kv in "$@"; do
    case "$kv" in
        BANISHED_*=*) extras+=(-e "${kv%%=*}" "${kv#*=}") ;;
        *) echo "not a BANISHED_* flag: $kv" >&2; exit 2 ;;
    esac
done
if [ ${#extras[@]} -gt 0 ]; then
    token="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
    "${ADB[@]}" shell "echo $token > '$DATA/harness_token'"
    extras+=(-e OPENBA_HARNESS_TOKEN "$token")
fi
"${ADB[@]}" shell am force-stop $PKG
"${ADB[@]}" shell rm -f "$DATA/standalone_boot_state.txt"

export RENDERDOC_MODULE_PATH="${RENDERDOC_MODULE_PATH:-$(cygpath -w "$ROOT/tools/renderdoc_mcp/runtime" 2>/dev/null || echo "$ROOT/tools/renderdoc_mcp/runtime")}"
log="$tmp/capture.log"
"$PY" "$(cygpath -w "$ROOT/tools/trace_oracle/android_capture.py" 2>/dev/null || echo "$ROOT/tools/trace_oracle/android_capture.py")" \
    "$SERIAL" "$WAIT" "${extras[@]}" 2>&1 | tr -d '\000' | grep -av "^ *Fatal" | tee "$log"
remote="$(sed -n 's/^capture: //p' "$log" | tr -d '\r')"
[ -n "$remote" ] || { echo "no capture" >&2; exit 1; }
"${ADB[@]}" pull "$remote" "$OUT"
"${ADB[@]}" shell rm -f "$remote"
echo "saved $OUT"
