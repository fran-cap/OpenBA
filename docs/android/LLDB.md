# Native debugging on Android (LLDB) from a Windows host

Target: debuggable debug apk `org.openba.game`, native lib `libmain.so`, arm64 (Galaxy S10 / Android 12,
Galaxy S25 / Android 15+). `tools/android.sh debug` is the current wrapper (platform mode, NDK lldb.exe).

Legend: [V] verified locally on this host, [W] taken from the web, [K] general LLDB/Android knowledge, not
re-verified against a source in this pass (try it, do not trust it blindly).

## Local facts [V]

- NDK 30.0.16248370: host `toolchains/llvm/prebuilt/windows-x86_64/bin/lldb.exe`, `lldb version 21.0.0`
  (Android clang r574158); needs `PYTHONHOME` and `PATH` to include `.../python3` (Python 3.11).
- NDK device server: `.../lib/clang/21/lib/linux/aarch64/lldb-server` (4.2 MB), same LLVM 21 as the host client.
- NDK `ndk-lldb.cmd` / `ndk-gdb.cmd` -> `prebuilt/windows-x86_64/bin/ndk-gdb.cmd` -> `ndkgdb.pyz` (Python zipapp).
  Still present in r30. `--help`: `-p PROJECT`, `--attach [PKG]` (default), `--launch [ACTIVITY]`,
  `--launch-list`, `--lldb`, `--port`, `--force`, `--delay`, `-x EXEC_FILE`, `--nowait`, `-s SERIAL`. Its banner says
  Android Studio is the supported route and the script targets ndk-build / ANT-style projects.
- Android Studio 2024.2 (build AI-242.23726): no `lldb.exe`. The debugger lives under
  `C:/Program Files/Android/Android Studio/plugins/android-ndk/resources/lldb/`:
  `bin/LLDBFrontend.exe` + `bin/liblldb.dll` (JetBrains frontend, not an lldb CLI; liblldb reports
  `19.0.0git`), `android/<abi>/lldb-server` (arm64-v8a copy is 45 MB, unstripped build) and
  `android/start_lldb_server.sh`. So Studio's host client is older (LLVM 19) than the NDK 30 pair (21).
- No LLVM-for-Windows lldb installed. WSL2 has an `Ubuntu` distro (lldb package not checked).
- Not verified (no device): every attach step.

## Recipe 1 (try first): gdb-remote mode, no platform plugin [K]

The host crash is inside `process attach` after a successful platform connect, i.e. in the
remote-android platform attach path (executable resolution / module download). `gdbserver` mode attaches on the
device side and the host only speaks the gdb-remote protocol.

```
PKG=org.openba.game; PID=$(adb shell pidof $PKG)
# device: server attaches to the app itself (run-as keeps the app's uid and SELinux domain)
adb shell "run-as $PKG setsid sh -c 'cd /data/data/$PKG && exec ./lldb-server gdbserver \
  unix-abstract:///$PKG/gdb.sock --attach $PID' </dev/null >/dev/null 2>&1 &"
adb forward tcp:5039 localabstract:/$PKG/gdb.sock   # the URL's path keeps its leading slash in the abstract name
# host (NDK lldb.exe, PYTHONHOME set as in tools/android.sh)
lldb -o "settings set target.exec-search-paths <dir of unstripped libmain.so>" \
     -o "gdb-remote localhost:5039"
```
(lldb-server is already copied to the data dir by `tools/android.sh debug`.) Add
`-o "settings set plugin.process.gdb-remote.packet-timeout 60"` on a slow device. Without the platform, system
libs are read from the target over the wire (slow, but works); a pulled sysroot avoids it: `adb pull
/system/lib64 sysroot/system/lib64` (and `/apex/com.android.runtime/lib64/bionic`), then
`settings set target.sysroot <sysroot>`. Do not combine this with
`platform select remote-android` + `platform connect`: that is the attach path that crashes.

## Recipe 2: capture why the host crashes, then pick a client [K]

```
lldb -o "log enable -f lldb_host.log lldb process platform gdb-remote" ...   # last lines before the AV
```
Windows Error Reporting / a `procdump -ma -e` of lldb.exe gives the faulting frame in liblldb.dll. Known adjacent
reports [W]: LLVM PR 98581 (2024) reverted commits because remote-android attach failed to resolve the executable
(https://github.com/llvm/llvm-project/pull/98581); lldb-dev bug 26289 (host `std::bad_alloc` on attach to a remote
Android target while loading symbols, reproducible on Windows and Linux)
(https://lists.llvm.org/pipermail/lldb-dev/2016-January/009389.html). No report of the exact NDK 30
`0xC0000005` on `process attach` was found. If the log shows the crash while loading modules, try
`settings set plugin.process.gdb-remote.use-libraries-svr4 false` and run without `target.exec-search-paths`
(add the symbols with `target symbols add` after the attach) [K].

## Recipe 3: Android Studio (the supported client) [W]

`Run > Attach Debugger to Android Process` with Debugger = Native (or Dual). Studio pushes `lldb-server` and
`start_lldb_server.sh` into `/data/data/<pkg>/lldb/bin`, runs the script with args
`<lldb dir> unix-abstract <socket dir> <platform socket> <log channels>`, which executes
`$BIN_DIR/lldb-server platform --server --listen unix-abstract://$DOMAINSOCKET_DIR/$PLATFORM_SOCKET --log-file
$LOG_DIR/platform.log` ([V] script text read from the install; [W] flow described at
https://groups.google.com/g/Android-ndk/c/mtobVIVhg2o). Studio then selects the remote-android platform and attaches
(its LLVM 19 client). Symbols: add the unstripped `.so` directory under Run Configuration > Debugger >
Symbol Directories. Studio can debug an apk it did not build ("Profile or Debug APK",
https://developer.android.com/studio/debug/apk-debugger). Its own logs: the `lldb/log/*.log` files in the app dir
(`adb shell run-as org.openba.game ls lldb/log`).

## Attach at startup

Studio waits for a debugger with `am start -D` [W: Stack Overflow flow, kiwix mirror of question 57901590]; the
rest is [K]:
```
adb shell am set-debug-app -w --persistent org.openba.game     # or: am start -D -n org.openba.game/<activity>
adb shell am start -n org.openba.game/<activity>               # process sits at "waiting for debugger"
PID=$(adb shell pidof org.openba.game)
# attach lldb-server (recipe 1), set breakpoints, `continue`; then release the Java VM wait:
adb forward tcp:8700 jdwp:$PID
jdb -connect com.sun.jdi.SocketAttach:hostname=localhost,port=8700   # any command then `quit`/resume
adb shell am clear-debug-app
```
The VM runs only after a JDWP client attaches, so the native attach must come first and `jdb` second. The
native lib (`SDL_main` / `libmain.so` constructors) loads after the VM starts, so break on
`dlopen`/`android_dlopen_ext` or use `breakpoint set -n SDL_main -s libmain.so` once pending breakpoints resolve
(`settings set target.preload-symbols true`, `breakpoint set -P`). `ndkgdb.pyz --launch --lldb -p <project>` does
the launch + wait + attach itself but expects an ndk-build style project tree (`AndroidManifest.xml`, `libs/<abi>`)
and is not a fit for this Gradle layout; try `--attach org.openba.game --verbose` against a running app only.

## Fallbacks without a working attach

- All-thread backtraces, no attach: `adb shell run-as org.openba.game ...` cannot run `debuggerd`; use
  `adb shell debuggerd -b <pid>` (Android 12+, needs shell/root; works for debuggable apps) or `adb shell kill -3 <pid>`
  (ART traces only) [K].
- ANR / crash tombstones: `adb logcat -b crash`, `/data/tombstones` (readable only via `adb bugreport`) [K].
- Profiling: `simpleperf` ships in the NDK (`$NDK/simpleperf/app_profiler.py -p org.openba.game`,
  `report_html.py`) [V present]; Perfetto via `adb shell perfetto` or ui.perfetto.dev [K].
- LLDB from WSL2 Ubuntu (`apt install lldb`): the Windows adb server must be reachable from WSL
  (`adb -a -P 5037 nodaemon server` on Windows, `ADB_SERVER_SOCKET=tcp:<windows-host-ip>:5037` in WSL; or install
  Linux `adb` and `adb kill-server` on the Windows side so one server owns the USB device). Then recipe 1 verbatim
  with the Linux NDK `lldb`; the Linux `lldb` needs the same-major `lldb-server` or a version-tolerant gdb-remote
  session [K].
- Upstream-faithful alternative: the AOSP `gdbclient.py` shows the same flow (adb forward tcp:5039, per-arch
  `lldb-server` from the NDK runtimes dir) (https://android.googlesource.com/platform/development/+/443c89032/scripts/gdbclient.py).

## Symbol setup

- Unstripped library: Gradle keeps it at `app/build/intermediates/cxx/<cfg>/<hash>/obj/arm64-v8a/libmain.so`
  (what `tools/android.sh debug` picks). Options, in order: `settings append target.exec-search-paths <dir>`;
  after the process is attached, `target symbols add <path>/libmain.so`; `image search-paths add
  /data/app/~~<rand>/org.openba.game-<rand>/lib/arm64 <local dir>` (device path from `image list libmain.so`).
  Verify with `image list -b` and `image lookup -n SDL_main`.
- System libs: platform mode downloads them (cached under `~/.lldb/module_cache`); gdb-remote mode needs the pulled
  sysroot above. Without them, frames in libc/libart show addresses only; app frames still resolve.

## Sources

- https://developer.android.com/ndk/guides/ndk-gdb , https://developer.android.com/studio/debug/apk-debugger
- https://opensecura.googlesource.com/3p/openxla/iree/+/1f3295a63837bb58c929cccd18f042708c21f68a/docs/developers/debugging/lldb_on_android.md
  (push lldb-server, `adb forward tcp:5039`, `platform select remote-android`, `platform connect connect://:5039`)
- https://groups.google.com/g/Android-ndk/c/mtobVIVhg2o (Studio's lldb-server invocation, unix-abstract platform)
- https://github.com/llvm/llvm-project/pull/98581 , https://lists.llvm.org/pipermail/lldb-dev/2016-January/009389.html
- https://android.googlesource.com/platform/development/+/443c89032/scripts/gdbclient.py
- https://qdc.qualcomm.com/support/user-guide/lldb-setup-guide (Windows NDK lldb.cmd + lldb-server workflow)
