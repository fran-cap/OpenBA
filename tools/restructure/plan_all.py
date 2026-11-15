# Phase D.3 composite plan (dry-run validation of the whole move; the steps are applied one
# top-level directory at a time with plan_1_game.py .. plan_4_app.py).
import subprocess

SRC_ROOTS = ["code", "runtime", "standalone", "dx11", "tests"]

OLD_INCS = [".", "code/Game", "runtime/types", "runtime/funcs", "dx11/types", "standalone", "tests/unit"]
NEW_INCS = ["src/game", "src/engine", "src/render/dx11", "src/app", "src/diag", "tests/unit"]

# standalone split: the launcher (src/app) vs the diagnostics and harnesses (src/diag)
APP = {
    "main.cpp", "options.cpp", "options.h", "runtime_init.cpp", "runtime_init.h",
    "video_interface.cpp", "video_interface.h", "boot_window.cpp", "boot_window.h",
    "frame_limiter.cpp", "frame_limiter.h", "engine_loader_gate.cpp", "engine_loader_gate.h",
    "res_load_lock.cpp", "res_load_lock.h", "icon.rc", "log_strip.h",
    "perf_experimental.cpp", "perf_experimental.h",
}

RENAMES = {
    "dx11/src/README.md": "src/render/dx11/SOURCES.md",
    # the four private surfaces whose names collide (case-insensitively) with the public
    # class headers once runtime/types and runtime/src fold into one tree per namespace
    "runtime/src/io/serializer.h": "src/engine/io/serializer_internal.h",
    "runtime/src/system/string.h": "src/engine/system/string_internal.h",
    "runtime/src/system/timer.h": "src/engine/system/timer_internal.h",
    "runtime/src/system/widestring.h": "src/engine/system/widestring_internal.h",
}
for f in subprocess.check_output(["git", "ls-files", "standalone"]).decode().split():
    base = f[len("standalone/"):]
    RENAMES[f] = ("src/app/" if base in APP else "src/diag/") + base
for f in subprocess.check_output(["git", "ls-files", "runtime/src"]).decode().split():
    if f.endswith("_win.cpp"):
        RENAMES[f] = "src/platform/win/" + f.rsplit("/", 1)[1]

MOVES = [
    ("code/Game/", "src/game/"),
    ("runtime/types/", "src/engine/"),
    ("runtime/src/", "src/engine/"),
    ("runtime/funcs/", "src/engine/"),
    ("dx11/src/", "src/render/dx11/"),
    ("dx11/types/", "src/render/dx11/"),
    ("dx11/", "src/render/dx11/"),
]
