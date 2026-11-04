#!/usr/bin/env bash
# tools/android.sh -- build, install and drive the Android app (docs/android/README.md).
#
#   tools/android.sh [--avd PROFILE | --serial S] COMMAND ...   # COMMAND against that emulator / device
#   tools/android.sh build [debug|release] [--jobs N] [--ccache] [--pgo generate|use|off] [--flavour perf]
#                          [--emulator]                # apk/debug/app-debug.apk, apk/release/app-release-unsigned.apk
#                                                      # (--emulator: x86_64, under app/build-emulator/)
#   tools/android.sh pgo-merge [DIR]                   # pull the instrumented app's raw profiles (the data
#                                                      # folder's pgo/) into DIR (default perf/pgo/android/raw) and
#                                                      # merge them into perf/pgo/android/openba.profdata
#   tools/android.sh sign [IN.apk OUT.apk]             # zipalign -P 16 + apksigner (default: the release apk ->
#                                                      # apk/release/app-release.apk); the key from OPENBA_KEYSTORE*
#   tools/android.sh install [debug|release|FILE.apk]  # adb install -r -d (downgrade allowed) of that apk
#   tools/android.sh push-data                         # the install's WinData/*.pkg -> the data folder (the apk
#                                                      # carries the FrancophilSans files and copies them there)
#   tools/android.sh run [KEY=VALUE ...]               # force-stop, then launch with BANISHED_* flags
#   tools/android.sh stop                              # force-stop the app
#   tools/android.sh pull DIR                          # the standalone_* logs and the boot state -> DIR
#   tools/android.sh logcat                            # follow the app's logcat lines
#   tools/android.sh boot-state [--wait N]             # = tools/boot_state.sh --android [--wait N]
#   tools/android.sh validation on LAYER.so | off       # the Khronos validation layer in the debug app
#   tools/android.sh debug [LLDB ARGS ...]             # LLDB attached to the running debug app (args = batch)
#   tools/android.sh avd list                          # the emulator profiles, their AVDs and serials
#   tools/android.sh avd create PROFILE [--force]      # make the AVD of a profile (tablet | phone)
#   tools/android.sh avd start PROFILE [--window]      # boot it (headless unless --window), wait for the boot
#   tools/android.sh avd stop [PROFILE]                # shut it down (every profile's without one)
#   tools/android.sh screenshot FILE.png               # adb exec-out screencap (no window needed)
#
# Environment: ANDROID_HOME (default %LOCALAPPDATA%/Android/Sdk), JAVA_HOME (default Android
# Studio's bundled JBR), ADB (default platform-tools/adb of ANDROID_HOME, then C:/code/platform-tools,
# then PATH), ANDROID_SERIAL (adb's own: picks the device when several are attached), BANISHED_DIR
# (the Windows install whose WinData is pushed; never copied anywhere else), BUILD_JOBS / --jobs
# (native compile jobs, default 8 as tools/build.sh). `--ccache` compiles the native part through
# ccache (on PATH; the release workflow's cache), which the Gradle build otherwise leaves off.
# `--pgo generate` builds the release apk instrumented (perf/pgo/android/README.md: run the training
# towns on it, `pgo-merge`, then a plain `build release` reads the merged profile); `--flavour perf`
# builds the release apk as the perf flavour (-O2 -g, no PGO / LTO: the perf baselines' build).
#
# Signing (`sign`; .github/workflows/release.yml runs the same command): OPENBA_KEYSTORE (the
# keystore file, PKCS12 or JKS), OPENBA_KEY_ALIAS, and the two passwords in OPENBA_KEYSTORE_PASSWORD
# and OPENBA_KEY_PASSWORD. apksigner reads the passwords from the environment (`env:`), so they are
# never on a command line. ANDROID_BUILD_TOOLS overrides the build-tools version (default 36.0.0).
# A local check with the debug key:
#   OPENBA_KEYSTORE=~/.android/debug.keystore OPENBA_KEY_ALIAS=androiddebugkey \
#   OPENBA_KEYSTORE_PASSWORD=android OPENBA_KEY_PASSWORD=android tools/android.sh sign
#
# The harness flags (src/app/options.h) reach the game as launch-intent string extras, which the
# activity turns into the process environment before the native library loads, beside a one-use
# token `run` writes into the app's data folder (OpenBAActivity.java):
#   tools/android.sh run BANISHED_AUTOSTART=1 BANISHED_AUTOQUIT=600
# A run always force-stops first: SDL's activity ends the process's game once main returns and
# refuses a second start in the same process.
#
# The emulator (docs/android/EMULATOR.md): `avd create tablet`, `avd start tablet`, then every
# command with `--avd tablet` talks to that emulator (its fixed console port names its adb serial),
# and `install debug` takes the x86_64 apk of `build debug --emulator` when the device is x86_64.
set -euo pipefail
# Git Bash on Windows rewrites arguments that look like POSIX paths ("/sdcard/...") before
# handing them to a Windows program; adb's device paths must pass through untouched.
export MSYS_NO_PATHCONV=1

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"

prop() {
    sed -n "s/^$1=//p" "$ANDROID_DIR/gradle.properties" | tr -d '\r' | head -1
}
PKG="$(prop openba.applicationId)"
ACTIVITY="org.openba.game.OpenBAActivity"
DATA="/sdcard/Android/data/$PKG/files"

if [ -z "${ANDROID_HOME:-}" ]; then
    if [ -n "${LOCALAPPDATA:-}" ]; then
        ANDROID_HOME="$(cygpath -m "$LOCALAPPDATA" 2>/dev/null || echo "$LOCALAPPDATA")/Android/Sdk"
    else
        ANDROID_HOME="$HOME/Android/Sdk"
    fi
fi
export ANDROID_HOME
if [ -z "${JAVA_HOME:-}" ] && [ -d "C:/Program Files/Android/Android Studio/jbr" ]; then
    export JAVA_HOME="C:/Program Files/Android/Android Studio/jbr"
fi
if [ -z "${ADB:-}" ]; then
    for cand in "$ANDROID_HOME/platform-tools/adb.exe" "$ANDROID_HOME/platform-tools/adb" \
                "C:/code/platform-tools/adb.exe"; do
        if [ -x "$cand" ]; then
            ADB="$cand"
            break
        fi
    done
    ADB="${ADB:-adb}"
fi

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

# ---- The emulator profiles (docs/android/EMULATOR.md) ----
# One AVD per profile on the SDK's x86_64 API 35 image, each on a fixed console port so its adb
# serial is known without asking adb: emulator-<port>.
AVD_IMAGE="system-images;android-35;google_apis_playstore;x86_64"
AVD_PROFILES="tablet phone"
avd_name() {
    case "$1" in
        tablet) echo "OpenBA_Tablet" ;;
        phone) echo "OpenBA_Phone" ;;
        *) echo "unknown emulator profile '$1' (tablet | phone)" >&2; return 1 ;;
    esac
}
avd_port() {
    case "$1" in
        tablet) echo 5580 ;;
        phone) echo 5582 ;;
        *) echo "unknown emulator profile '$1' (tablet | phone)" >&2; return 1 ;;
    esac
}
# The hardware keys written into the AVD's config.ini over the base device's.
#   tablet: the OnePlus Pad 3 class (issue #96): 3392x2400, 13.2", ~315 ppi -> the 320 dpi bucket;
#           landscape is its natural orientation.
#   phone:  the Galaxy S25 class (the reference device): 1080x2340, 6.2", ~416 ppi -> 420 dpi;
#           portrait natural, so the landscape app runs pre-rotated as on the S25.
# Both: host GPU (gfxstream on the host's Vulkan / GL driver), 6 cores (the emulator's maximum), 6 GB RAM, an 8 GB data partition.
avd_hw() {
    case "$1" in
        tablet)
            echo "hw.lcd.width=3392 hw.lcd.height=2400 hw.lcd.density=320 hw.initialOrientation=landscape"
            ;;
        phone)
            echo "hw.lcd.width=1080 hw.lcd.height=2340 hw.lcd.density=420 hw.initialOrientation=portrait"
            ;;
    esac
    echo "hw.ramSize=6144 vm.heapSize=512 hw.cpu.ncore=6 hw.gpu.enabled=yes hw.gpu.mode=host hw.keyboard=yes"
    echo "disk.dataPartition.size=8G showDeviceFrame=no skin.dynamic=yes"
}
avd_base_device() {
    case "$1" in
        tablet) echo "pixel_tablet" ;;
        phone) echo "medium_phone" ;;
    esac
}
avd_home() {
    echo "${ANDROID_AVD_HOME:-${ANDROID_USER_HOME:-$HOME/.android}/avd}"
}
sdk_tool() {
    for cand in "$@"; do
        if [ -f "$cand" ]; then
            echo "$cand"
            return 0
        fi
    done
    echo "none of: $*" >&2
    return 1
}
emulator_bin() {
    sdk_tool "$ANDROID_HOME/emulator/emulator.exe" "$ANDROID_HOME/emulator/emulator"
}
avdmanager_bin() {
    sdk_tool "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager.bat" "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"
}
# key=value into an AVD config.ini: the line replaced when the key is there, appended when not.
ini_set() {
    local file="$1" key="$2" value="$3"
    if grep -q "^$key[ ]*=" "$file"; then
        sed -i "s|^$key[ ]*=.*|$key=$value|" "$file"
    else
        printf '%s=%s\n' "$key" "$value" >> "$file"
    fi
}
# Is the emulator behind a serial up and answering adb?
serial_online() {
    [ "$("$ADB" -s "$1" get-state 2>/dev/null | tr -d '\r')" = device ]
}

# The leading device options: --avd PROFILE (that profile's emulator) or --serial S. Both set
# ANDROID_SERIAL, which every adb call here and tools/boot_state.sh then use.
while [ $# -gt 0 ]; do
    case "$1" in
        --avd)
            shift
            export ANDROID_SERIAL="emulator-$(avd_port "${1:?--avd PROFILE}")"
            ;;
        --serial)
            shift
            export ANDROID_SERIAL="${1:?--serial SERIAL}"
            ;;
        *) break ;;
    esac
    shift
done

gradlew() {
    if [ "${OS:-}" = "Windows_NT" ]; then
        (cd "$ANDROID_DIR" && ./gradlew.bat "$@")
    else
        (cd "$ANDROID_DIR" && ./gradlew "$@")
    fi
}

# The apk Gradle writes for a build type: debug is signed with the debug key; release is unsigned
# (`app-release-unsigned.apk`) until `sign` writes `app-release.apk` beside it.
apk_path() {
    local p="$ANDROID_DIR/app/${BUILD_SUBDIR:-build}/outputs/apk/$1/app-$1.apk"
    if [ "${2:-}" = unsigned ]; then p="$ANDROID_DIR/app/${BUILD_SUBDIR:-build}/outputs/apk/$1/app-$1-unsigned.apk"; fi
    if command -v cygpath >/dev/null 2>&1; then p="$(cygpath -m "$p")"; fi
    echo "$p"
}

# A build-tools program: zipalign / apksigner (apksigner.bat on Windows).
build_tool() {
    local bt="$ANDROID_HOME/build-tools/${ANDROID_BUILD_TOOLS:-36.0.0}"
    for cand in "$bt/$1" "$bt/$1.exe" "$bt/$1.bat"; do
        if [ -f "$cand" ]; then
            echo "$cand"
            return 0
        fi
    done
    echo "no $1 in $bt (install build-tools;${ANDROID_BUILD_TOOLS:-36.0.0})" >&2
    return 1
}

cmd="${1:-}"
[ -n "$cmd" ] || usage
shift

case "$cmd" in
build)
    type="debug"
    jobs="${BUILD_JOBS:-8}"
    props=()
    while [ $# -gt 0 ]; do
        case "$1" in
            debug|release) type="$1" ;;
            --jobs) shift; jobs="$1" ;;
            --ccache) props+=("-Popenba.ccache=true") ;;
            --pgo) shift; props+=("-Popenba.pgo=$1") ;;
            --flavour) shift; props+=("-Popenba.flavour=$1") ;;
            --emulator) props+=("-Popenba.emulator=true"); BUILD_SUBDIR=build-emulator ;;
            *) usage ;;
        esac
        shift
    done
    task="assemble$(echo "${type:0:1}" | tr a-z A-Z)${type:1}"
    gradlew --console=plain ":app:$task" "-Popenba.nativeJobs=$jobs" "${props[@]}"
    if [ "$type" = release ]; then
        ls -la "$(apk_path release unsigned)"
    else
        ls -la "$(apk_path "$type")"
    fi
    ;;
sign)
    in="${1:-$(apk_path release unsigned)}"
    out="${2:-$(apk_path release)}"
    [ -f "$in" ] || { echo "no apk at $in (tools/android.sh build release first)" >&2; exit 1; }
    : "${OPENBA_KEYSTORE:?OPENBA_KEYSTORE: the keystore file}"
    : "${OPENBA_KEY_ALIAS:?OPENBA_KEY_ALIAS: the key alias}"
    : "${OPENBA_KEYSTORE_PASSWORD:?OPENBA_KEYSTORE_PASSWORD: the keystore password}"
    : "${OPENBA_KEY_PASSWORD:?OPENBA_KEY_PASSWORD: the key password}"
    [ -f "$OPENBA_KEYSTORE" ] || { echo "no keystore at OPENBA_KEYSTORE" >&2; exit 1; }
    # Git Bash: zipalign and apksigner are Windows programs and take Windows paths (a POSIX
    # "/c/Users/..." keystore path is "file not found" to Java).
    if command -v cygpath >/dev/null 2>&1; then
        in="$(cygpath -m "$in")"
        out="$(cygpath -m "$out")"
        OPENBA_KEYSTORE="$(cygpath -m "$OPENBA_KEYSTORE")"
    fi
    zipalign="$(build_tool zipalign)"
    apksigner="$(build_tool apksigner)"
    aligned="$out.aligned"
    # 16 KB pages for the uncompressed native libraries (-P 16), 4-byte alignment for the rest; then the
    # v2 / v3 signature over the aligned file (minSdk 29: no v1). apksigner reads both passwords from
    # the environment; nothing secret is on a command line or in the output.
    "$zipalign" -f -P 16 4 "$in" "$aligned"
    "$apksigner" sign --ks "$OPENBA_KEYSTORE" --ks-key-alias "$OPENBA_KEY_ALIAS" \
        --ks-pass env:OPENBA_KEYSTORE_PASSWORD --key-pass env:OPENBA_KEY_PASSWORD \
        --out "$out" "$aligned"
    rm -f "$aligned" "$out.idsig"
    "$zipalign" -c -P 16 4 "$out"
    "$apksigner" verify --verbose --print-certs "$out"
    ls -la "$out"
    ;;
install)
    # -d: a dev phone moves between trees whose versionCode differs (build.gradle derives it from the
    # CMake VERSION since P8; older trees had 1). Android takes a lower code only over a debuggable
    # install; over a signed release apk, `adb uninstall` first (that deletes the data folder).
    # An x86_64 device (the emulator) takes the apk of `build ... --emulator`.
    type="${1:-debug}"
    abi="$("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r')"
    if [ "$abi" = x86_64 ]; then
        BUILD_SUBDIR=build-emulator
    fi
    case "$type" in
        *.apk) "$ADB" install -r -d "$type" ;;
        *)
            apk="$(apk_path "$type")"
            [ -f "$apk" ] || { echo "no apk at $apk (device ABI $abi: tools/android.sh build $type${BUILD_SUBDIR:+ --emulator})" >&2; exit 1; }
            "$ADB" install -r -d "$apk"
            ;;
    esac
    ;;
push-data)
    src="${BANISHED_DIR:-C:/GOG Games/Banished}/WinData"
    [ -d "$src" ] || { echo "no WinData under $src (set BANISHED_DIR)" >&2; exit 1; }
    # WinData/ must belong to the app: a folder adb makes belongs to the shell user and the app
    # cannot read inside it (src/platform/android/app_android.cpp, EnterRunDirectory). The app
    # makes it at its first start, so start it once when the folder is missing or adb-made.
    owner="$("$ADB" shell stat -c %U "$DATA/WinData" 2>/dev/null | tr -d '\r' || true)"
    if [ "$owner" = "shell" ]; then
        echo "WinData on the device was made by adb (unreadable by the app): removing it"
        "$ADB" shell rm -rf "$DATA/WinData"
        owner=""
    fi
    if [ -z "$owner" ]; then
        echo "starting the app once so that it makes $DATA/WinData"
        "$ADB" shell am start -n "$PKG/$ACTIVITY" >/dev/null
        for _ in $(seq 1 30); do
            [ -n "$("$ADB" shell stat -c %U "$DATA/WinData" 2>/dev/null | tr -d '\r' || true)" ] && break
            sleep 1
        done
        "$ADB" shell am force-stop "$PKG"
    fi
    # The folder's packages and mods: parked mods (_mods_off and every other subfolder) stay home.
    for pkg in "$src"/*.pkg "$src"/*.pkm; do
        [ -f "$pkg" ] || continue
        if command -v cygpath >/dev/null 2>&1; then pkg="$(cygpath -m "$pkg")"; fi
        "$ADB" push "$pkg" "$DATA/WinData/"
    done
    "$ADB" shell ls -l "$DATA/WinData"
    # The typeface ext.sharp_ui rasterises is not pushed: the apk carries it (tools/release/fonts as
    # assets, android/app/build.gradle) and OpenBAActivity copies it into the data folder at start.
    ;;
run)
    "$ADB" shell am force-stop "$PKG"
    extras=()
    for kv in "$@"; do
        case "$kv" in
            BANISHED_*=*) extras+=(-e "${kv%%=*}" "${kv#*=}") ;;
            *) echo "not a BANISHED_* flag: $kv" >&2; exit 2 ;;
        esac
    done
    if [ ${#extras[@]} -gt 0 ]; then
        # OpenBAActivity honours the flags only beside this one-use token: adb can write into the
        # app's data folder, another app on the device cannot. adb must not make the folder (one it
        # makes belongs to the shell user): the app makes it at its first start.
        "$ADB" shell "test -d '$DATA'" || { echo "no $DATA on the device: start the app once" >&2; exit 1; }
        token="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
        "$ADB" shell "echo $token > '$DATA/harness_token'"
        extras+=(-e OPENBA_HARNESS_TOKEN "$token")
    fi
    "$ADB" shell rm -f "$DATA/standalone_boot_state.txt"
    "$ADB" shell am start -W -n "$PKG/$ACTIVITY" "${extras[@]}"
    ;;
stop)
    "$ADB" shell am force-stop "$PKG"
    ;;
pgo-merge)
    # The instrumented app (build release --pgo generate) writes pgo/openba-<hash>.profraw into the
    # data folder at each clean quit (src/platform/android/app_android.cpp). llvm-profdata is the
    # NDK's (the same clang that reads the merged file); the raw files are kept in DIR for a
    # later merge with more runs. Commit perf/pgo/android/openba.profdata afterwards: the release
    # build (CMake BANISHED_PGO=auto) reads it when it exists.
    dir="${1:-$ROOT/perf/pgo/android/raw}"
    mkdir -p "$dir"
    if command -v cygpath >/dev/null 2>&1; then dir="$(cygpath -m "$dir")"; fi
    "$ADB" pull "$DATA/pgo" "$dir" >/dev/null || { echo "no $DATA/pgo on the device (an instrumented run, ended by a quit, writes it)" >&2; exit 1; }
    ndk="$ANDROID_HOME/ndk/$(sed -n "s/^ *ndkVersion = '\(.*\)'/\1/p" "$ANDROID_DIR/app/build.gradle" | head -1)"
    profdata=""
    for host in windows-x86_64 linux-x86_64 darwin-x86_64; do
        for cand in "$ndk/toolchains/llvm/prebuilt/$host/bin/llvm-profdata.exe" "$ndk/toolchains/llvm/prebuilt/$host/bin/llvm-profdata"; do
            if [ -f "$cand" ]; then profdata="$cand"; break 2; fi
        done
    done
    [ -n "$profdata" ] || { echo "no llvm-profdata under $ndk/toolchains/llvm/prebuilt" >&2; exit 1; }
    raws=()
    while IFS= read -r f; do raws+=("$f"); done < <(find "$dir" -name '*.profraw' | sort)
    [ ${#raws[@]} -gt 0 ] || { echo "no .profraw under $dir" >&2; exit 1; }
    out="$ROOT/perf/pgo/android/openba.profdata"
    if command -v cygpath >/dev/null 2>&1; then out="$(cygpath -m "$out")"; fi
    "$profdata" merge -o "$out" "${raws[@]}"
    "$profdata" show "$out" | head -20
    ls -la "$out"
    ;;
pull)
    dir="${1:?pull DIR}"
    mkdir -p "$dir"
    if command -v cygpath >/dev/null 2>&1; then dir="$(cygpath -m "$dir")"; fi
    for f in standalone_debug.log standalone_error.log standalone_crash.log standalone_hang.log \
             standalone_boot_state.txt; do
        "$ADB" pull "$DATA/$f" "$dir/$f" >/dev/null 2>&1 || true
    done
    ls -la "$dir"
    ;;
logcat)
    "$ADB" logcat -v time -s OpenBA:V SDL:V SDL/APP:V DEBUG:V AndroidRuntime:E libc:V vulkan:V
    ;;
boot-state)
    exec "$ROOT/tools/boot_state.sh" --android "$@"
    ;;
validation)
    # The Khronos validation layer in the debuggable (debug) app (docs/android/README.md,
    # "Validation layers"). `on` takes the arm64-v8a libVkLayer_khronos_validation.so of the
    # Vulkan-ValidationLayers release "android-binaries-<ver>.zip"; nothing of it is kept in the
    # tree. The Android loader injects the layers named by the gpu_debug_* settings into the one
    # app named there; its messages are logcat tag VALIDATION and, through the backend's
    # messenger, `[Vulkan] validation` lines in standalone_error.log.
    mode="${1:-}"
    case "$mode" in
    on)
        layer="${2:?validation on LAYER.so}"
        [ -f "$layer" ] || { echo "no layer at $layer" >&2; exit 1; }
        if command -v cygpath >/dev/null 2>&1; then layer="$(cygpath -m "$layer")"; fi
        "$ADB" push "$layer" /data/local/tmp/libVkLayer_khronos_validation.so
        "$ADB" shell run-as "$PKG" cp /data/local/tmp/libVkLayer_khronos_validation.so .
        "$ADB" shell rm -f /data/local/tmp/libVkLayer_khronos_validation.so
        "$ADB" shell settings put global enable_gpu_debug_layers 1
        "$ADB" shell settings put global gpu_debug_app "$PKG"
        "$ADB" shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
        "$ADB" shell settings put global gpu_debug_layer_app "$PKG"
        "$ADB" shell run-as "$PKG" ls -l libVkLayer_khronos_validation.so
        ;;
    off)
        "$ADB" shell settings delete global enable_gpu_debug_layers
        "$ADB" shell settings delete global gpu_debug_app
        "$ADB" shell settings delete global gpu_debug_layers
        "$ADB" shell settings delete global gpu_debug_layer_app
        "$ADB" shell run-as "$PKG" rm -f libVkLayer_khronos_validation.so || true
        ;;
    *)
        usage
        ;;
    esac
    "$ADB" shell settings get global enable_gpu_debug_layers
    ;;
debug)
    # LLDB on the running app (the NDK dropped gdbserver; lldb-server is its replacement). Needs the
    # debuggable (debug) apk: run-as copies the NDK's aarch64 lldb-server into the app's data dir and
    # starts it as a platform server on an abstract socket, which the host lldb's remote-android
    # platform reaches through adb. The unstripped libmain.so of the newest native build is put on
    # the symbol path. Any further arguments go to lldb after the attach (batch mode then), e.g.
    #   tools/android.sh debug -o "thread backtrace all"     # all stacks, then detach
    #   tools/android.sh debug                               # interactive
    ndk="$ANDROID_HOME/ndk/$(sed -n "s/^ *ndkVersion = '\(.*\)'/\1/p" "$ANDROID_DIR/app/build.gradle" | head -1)"
    host=""
    for h in windows-x86_64 linux-x86_64 darwin-x86_64; do
        [ -d "$ndk/toolchains/llvm/prebuilt/$h" ] && { host="$h"; break; }
    done
    [ -n "$host" ] || { echo "no NDK toolchain under $ndk" >&2; exit 1; }
    tc="$ndk/toolchains/llvm/prebuilt/$host"
    server="$(find "$tc/lib/clang" -path '*linux/aarch64/lldb-server' | head -1)"
    lldb="$tc/bin/lldb"
    if [ -f "$lldb.exe" ]; then
        # What the NDK's lldb.cmd does: liblldb.dll loads the bundled Python's DLL.
        lldb="$lldb.exe"
        export PYTHONHOME="$tc/python3"
        export PATH="$(cygpath -u "$tc/python3"):$PATH"
    fi
    [ -f "$server" ] || { echo "no aarch64 lldb-server under $tc/lib/clang" >&2; exit 1; }
    pid="$("$ADB" shell pidof "$PKG" | tr -d '\r')"
    [ -n "$pid" ] || { echo "$PKG is not running (tools/android.sh run first)" >&2; exit 1; }
    if command -v cygpath >/dev/null 2>&1; then server="$(cygpath -m "$server")"; fi
    # A server left by an earlier attach holds its binary busy: stop it before the copy over it.
    "$ADB" shell run-as "$PKG" killall lldb-server >/dev/null 2>&1 || true
    "$ADB" push "$server" /data/local/tmp/lldb-server >/dev/null
    "$ADB" shell run-as "$PKG" cp /data/local/tmp/lldb-server ./lldb-server
    "$ADB" shell run-as "$PKG" chmod 700 ./lldb-server
    sock="/$PKG/lldb-platform.sock"
    # setsid and the redirections outside run-as: with a plain nohup ... & the server kept the remote
    # session open and adb shell never returned.
    "$ADB" shell "run-as $PKG setsid sh -c 'cd /data/data/$PKG && exec ./lldb-server platform --server --listen unix-abstract://$sock' </dev/null >/dev/null 2>&1 &"
    sleep 1
    syms="$(ls -t "$ANDROID_DIR"/app/build/intermediates/cxx/*/*/obj/arm64-v8a/libmain.so 2>/dev/null | head -1)"
    args=(-o "platform select remote-android" -o "platform connect unix-abstract-connect://$sock")
    if [ -n "$syms" ]; then
        if command -v cygpath >/dev/null 2>&1; then syms="$(cygpath -m "$syms")"; fi
        args+=(-o "settings append target.exec-search-paths $(dirname "$syms")")
    fi
    args+=(-o "process attach -p $pid")
    if [ $# -gt 0 ]; then
        "$lldb" -b "${args[@]}" "$@" -o "process detach"
        "$ADB" shell run-as "$PKG" killall lldb-server >/dev/null 2>&1 || true
    else
        "$lldb" "${args[@]}"
    fi
    ;;
screenshot)
    out="${1:?screenshot FILE.png}"
    "$ADB" exec-out screencap -p > "$out"
    [ -s "$out" ] || { echo "screencap wrote nothing" >&2; exit 1; }
    ls -la "$out"
    ;;
avd)
    sub="${1:-}"
    [ $# -gt 0 ] && shift
    case "$sub" in
    list)
        for profile in $AVD_PROFILES; do
            name="$(avd_name "$profile")"
            serial="emulator-$(avd_port "$profile")"
            made="no AVD"
            [ -f "$(avd_home)/$name.avd/config.ini" ] && made="AVD made"
            state="stopped"
            serial_online "$serial" && state="running"
            printf '%-7s %-14s %-9s %-15s %s\n' "$profile" "$name" "$made" "$serial" "$state"
        done
        ;;
    create)
        profile="${1:?avd create PROFILE (tablet | phone)}"
        name="$(avd_name "$profile")"
        force=""
        [ "${2:-}" = "--force" ] && force="--force"
        dir="$(avd_home)/$name.avd"
        if [ -f "$dir/config.ini" ] && [ -z "$force" ]; then
            echo "$name exists ($dir); --force makes it again (its data is wiped)" >&2
            exit 1
        fi
        # avdmanager asks whether to make a custom hardware profile: the answer is no, the
        # profile's keys are written into config.ini below.
        echo no | "$(avdmanager_bin)" create avd -n "$name" -k "$AVD_IMAGE" -d "$(avd_base_device "$profile")" $force
        for kv in $(avd_hw "$profile"); do
            ini_set "$dir/config.ini" "${kv%%=*}" "${kv#*=}"
        done
        # No skin: the screen is the lcd size above (a skin would fix the base device's).
        ini_set "$dir/config.ini" skin.name "$(sed -n 's/^hw.lcd.width=//p' "$dir/config.ini")x$(sed -n 's/^hw.lcd.height=//p' "$dir/config.ini")"
        ini_set "$dir/config.ini" skin.path _no_skin
        # avdmanager also writes every hardware default, among them placeholders Android Studio
        # leaves out (a "<temp>" data-partition path, zero partition sizes): drop those.
        sed -i '/=<temp>$/d; /^disk\.systemPartition\.size=0$/d; /^disk\.vendorPartition\.size=0$/d' "$dir/config.ini"
        grep -E '^(hw\.(lcd|ramSize|cpu|gpu|keyboard|initialOrientation)|disk|skin|image)' "$dir/config.ini"
        ;;
    start)
        profile="${1:?avd start PROFILE (tablet | phone)}"
        name="$(avd_name "$profile")"
        port="$(avd_port "$profile")"
        serial="emulator-$port"
        [ -f "$(avd_home)/$name.avd/config.ini" ] || { echo "no AVD $name (tools/android.sh avd create $profile)" >&2; exit 1; }
        if serial_online "$serial"; then
            echo "$name is already running as $serial"
            exit 0
        fi
        # Headless by default (-no-window; screenshots with `screenshot`): a script never opens a
        # visible window unless asked (--window). -gpu host: the host's GPU through gfxstream (the
        # software renderers have no Vulkan). -no-snapshot: a cold boot every time, no saved state.
        args=(-avd "$name" -port "$port" -gpu host -no-audio -no-boot-anim -no-snapshot)
        [ "${2:-}" = "--window" ] || args+=(-no-window)
        log="${TMPDIR:-/tmp}/openba-emulator-$profile.log"
        nohup "$(emulator_bin)" "${args[@]}" > "$log" 2>&1 < /dev/null &
        emu_pid=$!
        echo "starting $name on $serial (pid $emu_pid, log $log)"
        booted=""
        for _ in $(seq 1 300); do
            if ! kill -0 "$emu_pid" 2>/dev/null; then
                echo "the emulator exited during boot:" >&2
                tail -20 "$log" >&2
                exit 1
            fi
            if [ "$("$ADB" -s "$serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then
                booted=yes
                break
            fi
            sleep 1
        done
        [ -n "$booted" ] || { echo "$serial did not finish booting in 300 s (log $log)" >&2; exit 1; }
        # A headless screen still sleeps: an app behind a sleeping screen is in the background
        # (the Vulkan backend drops its swap chain), so keep it awake and unlocked.
        "$ADB" -s "$serial" shell svc power stayon true
        "$ADB" -s "$serial" shell input keyevent KEYCODE_WAKEUP
        "$ADB" -s "$serial" shell wm dismiss-keyguard || true
        # The system's one-time "Viewing full screen" notice takes the focus from a fresh install's
        # first fullscreen start, which pauses the game (GameAutoPause): mark it seen.
        "$ADB" -s "$serial" shell settings put secure immersive_mode_confirmations confirmed
        echo "$name booted: $serial ($("$ADB" -s "$serial" shell wm size | tr -d '\r' | tail -1), $("$ADB" -s "$serial" shell wm density | tr -d '\r' | tail -1))"
        ;;
    stop)
        profiles="${1:-$AVD_PROFILES}"
        for profile in $profiles; do
            serial="emulator-$(avd_port "$profile")"
            if serial_online "$serial"; then
                "$ADB" -s "$serial" emu kill
                for _ in $(seq 1 30); do
                    serial_online "$serial" || break
                    sleep 1
                done
                echo "$serial stopped"
            fi
        done
        ;;
    *)
        usage
        ;;
    esac
    ;;
*)
    usage
    ;;
esac
