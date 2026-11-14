"""Test runner for .claude/hooks/pre_bash_guard.sh: synthetic Claude Code hook input on stdin, run in three
forms: `bash <hook>`; "settings", the hook's command line read from .claude/settings.json and run by
`bash -c` with CLAUDE_PROJECT_DIR set (Git Bash on Windows: the sourced branch); and "sh", the same line with
BASH_VERSION unset (the `bash <hook>` branch that `sh -c` takes on Linux / macOS when sh is not bash).

usage: python -I .claude/hooks/tests/test_hooks.py [--root <checkout>] [--bash <bash exe>] [--bench N]

--root defaults to the git checkout this file sits in; it is the cwd of most payloads, and the hook only
reads git there (`git diff --cached`, `git add --dry-run`). --bash defaults to Git for Windows' bash.exe
(found next to git on PATH) on Windows and to bash on PATH elsewhere. Fixtures (three throwaway git repos
and two commit-message files) live in a temporary directory that is removed afterwards.
Prints one line per case and form, then the false positives (a must-pass command refused, or any output
on a pass), the false negatives (a must-block command passed, or a refusal longer than one line) and the
fail-open failures. Exit 1 when any count is not 0.
"""
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

TESTS = os.path.dirname(os.path.abspath(__file__))
HOOKS = os.path.dirname(TESTS).replace("\\", "/")
HOOK = "pre_bash_guard.sh"
PROJECT = os.path.dirname(os.path.dirname(HOOKS))  # the checkout holding .claude/ (CLAUDE_PROJECT_DIR)
FORMS = ("bash", "settings", "sh")


def settings_command():
    """The command line .claude/settings.json wires for this hook."""
    path = PROJECT + "/.claude/settings.json"
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    for entry in d.get("hooks", {}).get("PreToolUse", []):
        for h in entry.get("hooks", []):
            if HOOK in h.get("command", ""):
                return h["command"]
    raise SystemExit("no %s hook in %s" % (HOOK, path))


def arg(name, default=None):
    for i, a in enumerate(sys.argv):
        if a == name and i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


def git_toplevel(d):
    try:
        p = subprocess.run(["git", "-C", d, "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    except OSError:
        return None
    return p.stdout.strip() if p.returncode == 0 and p.stdout.strip() else None


def find_bash():
    if os.name != "nt":
        return shutil.which("bash") or "/bin/bash"
    # Git for Windows: <Git>/bin/bash.exe, found by walking up from git.exe (<Git>/cmd or <Git>/mingw64/bin).
    git = shutil.which("git")
    d = os.path.dirname(os.path.abspath(git)) if git else ""
    while d and os.path.dirname(d) != d:
        cand = os.path.join(d, "bin", "bash.exe")
        if os.path.isfile(cand):
            return cand
        d = os.path.dirname(d)
    return r"C:\Program Files\Git\bin\bash.exe"


ROOT = (arg("--root") or git_toplevel(HOOKS) or os.path.dirname(os.path.dirname(HOOKS))).replace("\\", "/")
BASH = arg("--bash") or find_bash()
CMD = settings_command()
TMP = tempfile.mkdtemp(prefix="hook-tests-").replace("\\", "/")
GIT1 = TMP + "/gitrepo_untracked"
GIT2 = TMP + "/gitrepo_staged"
GIT3 = TMP + "/gitrepo_clean"
GIT4 = TMP + "/gitrepo_removal"


def win(p):
    """The form Claude Code sends on Windows: absolute, backslash separators."""
    return p.replace("/", "\\") if os.name == "nt" else p


def bash_payload(cmd, tool="Bash", cwd=ROOT):
    return {"session_id": "test", "transcript_path": "x", "cwd": win(cwd), "permission_mode": "bypassPermissions",
            "hook_event_name": "PreToolUse", "tool_name": tool,
            "tool_input": {"command": cmd, "description": "test"}, "tool_use_id": "toolu_test"}


def run(payload, form, env=None):
    data = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
    if form == "settings":
        argv = [BASH, "-c", CMD]
    elif form == "sh":
        argv = [BASH, "-c", "unset BASH_VERSION; " + CMD]
    else:
        argv = [BASH, HOOKS + "/" + HOOK]
    env = dict(os.environ if env is None else env)
    env["CLAUDE_PROJECT_DIR"] = PROJECT
    t0 = time.perf_counter()
    p = subprocess.run(argv, input=data, capture_output=True, env=env)
    ms = (time.perf_counter() - t0) * 1000.0
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), ms


# ----------------------------------------------------------------------------------------------- fixtures
def fixtures():
    for d in (GIT1, GIT2, GIT3):
        os.makedirs(d + "/src/ext/ui/Dialog")
        subprocess.run(["git", "init", "-q", d], check=True)
        with open(d + "/README", "w") as f:
            f.write("x\n")
    for name in ("data0.pkg", "src/ext/ui/Dialog/New.rsc"):
        with open(GIT1 + "/" + name, "w") as f:
            f.write("x\n")
    with open(GIT2 + "/terrain.crs", "w") as f:
        f.write("x\n")
    subprocess.run(["git", "-C", GIT2, "add", "terrain.crs"], check=True)
    with open(GIT3 + "/src/ext/ui/Dialog/Mine.rsc", "w") as f:
        f.write("x\n")
    # a retail file committed by mistake, then staged for removal (`git rm --cached`)
    subprocess.run(["git", "init", "-q", GIT4], check=True)
    with open(GIT4 + "/data0.pkg", "w") as f:
        f.write("x\n")
    subprocess.run(["git", "-C", GIT4, "add", "data0.pkg"], check=True)
    subprocess.run(["git", "-C", GIT4, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
                    "commit", "-q", "-m", "x"], check=True)
    subprocess.run(["git", "-C", GIT4, "rm", "-q", "--cached", "data0.pkg"], check=True)
    with open(TMP + "/msg_trailer.txt", "w", encoding="utf-8") as f:
        f.write("fix: a thing\n\nCo-Authored-By: Claude <noreply@anthropic.com>\n")
    with open(TMP + "/msg_clean.txt", "w", encoding="utf-8") as f:
        f.write("fix: a thing\n\nThe Co-Authored-By trailer is off in settings.\n")


# ----------------------------------------------------------------------------------------------- cases
def cases():
    B = "Bash"
    P = "PowerShell"
    c = []

    def blk(desc, cmd, tool=B, cwd=ROOT):
        c.append(("block", desc, bash_payload(cmd, tool, cwd)))

    def ok(desc, cmd, tool=B, cwd=ROOT):
        c.append(("pass", desc, bash_payload(cmd, tool, cwd)))

    # R1 find
    blk("find /", "find / -name '*.pdb' 2>/dev/null")
    blk("find /c", "find /c -iname OpenBA.exe")
    blk("find /c/", "find /c/ -name x")
    blk("find C:/", "find C:/ -name x")
    blk("find 'C:\\'", "find 'C:\\' -name x")
    blk("find ~", "find ~ -name '*.json'")
    blk("find $HOME", "find $HOME -type f -name x")
    blk("find /c/Users/<name>", "find /c/Users/someone -name settings.json")
    blk("find /c/Windows", "find /c/Windows -name d3d11.dll")
    blk("find -L /", "find -L / -name x")
    blk("find / -maxdepth 5", "find / -maxdepth 5 -name x")
    blk("after cd &&", "cd /tmp && find / -name x")
    blk("second of ;", "ls; find / -name x")
    blk("bash -c wrapper", "bash -c \"find / -name x\"")
    blk("PowerShell gci -Recurse C:\\", "Get-ChildItem -Recurse C:\\ -Filter *.pdb", P)
    blk("PowerShell gci -Path ~ -Recurse", "gci -Path ~ -Recurse -Include *.log", P)
    ok("find .", "find . -name x")
    ok("find checkout src", "find '%s/src' -name y" % ROOT)
    ok("find build objs", "find build/cmake-debug -name '*.obj' -print0 | xargs -0 -n 200 nm -A -u > x")
    ok("find / -maxdepth 1", "find / -maxdepth 1 -name 'x*'")
    ok("find ~ -maxdepth 2", "find ~ -maxdepth 2 -name .gitconfig")
    ok("commit msg mentions find /", "git commit -m \"fix: find / guard\"")
    ok("grep for find /", "grep -n \"find /\" CLAUDE.md")
    ok("echo find /", "echo find / is banned")
    ok("Windows find.exe in PowerShell", "find /c \"error\" build.log", P)
    ok("gci -Recurse in the checkout", "Get-ChildItem -Recurse '%s\\src' -Filter *.h" % ROOT.replace("/", "\\"), P)
    # R2 kill by name
    blk("taskkill //F //IM", "taskkill //F //IM OpenBA.exe")
    blk("taskkill //IM ... //F", "taskkill //IM OpenBA.exe //F")
    blk("taskkill /im lower", "taskkill /im openba.exe /f")
    blk("taskkill -IM", "taskkill -IM OpenBA.exe -F")
    blk("taskkill quoted", "taskkill /F /IM \"OpenBA.exe\"")
    blk("taskkill.exe full path", "\"C:/Windows/System32/taskkill.exe\" //IM OpenBA.exe //F")
    blk("taskkill wildcard", "taskkill //IM Open* //F")
    blk("taskkill /FI IMAGENAME", "taskkill //F //FI \"IMAGENAME eq OpenBA.exe\"")
    blk("cmd /c taskkill", "cmd /c taskkill /IM OpenBA.exe /F")
    blk("cmd //c quoted", "cmd //c \"taskkill /IM OpenBA.exe /F\"")
    blk("after sleep &&", "sleep 2 && taskkill //IM OpenBA.exe 2>/dev/null || true")
    blk("timeout wrapper", "timeout 5 taskkill //IM OpenBA.exe")
    blk("second line", "echo stopping\ntaskkill //F //IM OpenBA.exe")
    blk("if / then body", "if true; then taskkill //IM OpenBA.exe //F; fi")
    blk("bash -xc wrapper", "bash -xc \"taskkill //IM OpenBA.exe //F\"")
    blk("xargs taskkill /IM from stdin", "echo OpenBA.exe | xargs taskkill //F //IM")
    blk("PowerShell Stop-Process -Name", "Stop-Process -Name OpenBA -Force", P)
    blk("PowerShell Stop-Process -Name:", "Stop-Process -Name:OpenBA", P)
    blk("PowerShell spps -n", "spps -n OpenBA", P)
    blk("PowerShell kill -Name", "kill -Name OpenBA", P)
    blk("Get-Process | Stop-Process", "Get-Process OpenBA | Stop-Process -Force", P)
    blk("Where-Object Name | Stop-Process", "Get-Process | Where-Object Name -eq OpenBA | Stop-Process", P)
    blk("Stop-Process -Id (Get-Process)", "Stop-Process -Id (Get-Process OpenBA).Id", P)
    blk("(Get-Process).Kill()", "(Get-Process -Name OpenBA).Kill()", P)
    blk("powershell -Command from Bash", "powershell -NoProfile -Command \"Stop-Process -Name OpenBA -Force\"")
    blk("wmic delete", "wmic process where \"name='OpenBA.exe'\" delete")
    blk("wmic call terminate", "wmic process where name=\"OpenBA.exe\" call terminate")
    blk("Win32_Process Terminate", "Get-CimInstance Win32_Process -Filter \"Name='OpenBA.exe'\" | "
                                   "Invoke-CimMethod -MethodName Terminate", P)
    blk("taskkill extra /T between", "taskkill //F //T //IM OpenBA.exe")
    blk("taskkill bare, no trailing args", "taskkill //IM OpenBA.exe")
    blk("taskkill by MSYS path", "/c/Windows/System32/taskkill.exe //IM OpenBA.exe //F")
    blk("PowerShell -Force before -Name", "Stop-Process -Force -Name OpenBA", P)
    blk("PowerShell taskkill /F /T /IM", "taskkill /F /T /IM OpenBA.exe", P)
    blk("pkill -f", "pkill -f OpenBA")
    blk("killall", "killall OpenBA.exe")
    blk("kill $(pgrep)", "kill $(pgrep -f OpenBA)")
    ok("taskkill //PID", "taskkill //PID 123 //F")
    ok("taskkill /PID /T", "taskkill /PID 4567 /T /F")
    ok("taskkill other image", "taskkill //IM notepad.exe //F")
    ok("tasklist filter", "tasklist //FI \"IMAGENAME eq OpenBA.exe\"")
    ok("wmic query", "wmic process where \"name='OpenBA.exe'\" get ProcessId,ExecutablePath")
    ok("Get-Process select", "Get-Process OpenBA | Select-Object Id,Path,StartTime", P)
    ok("kill_game.sh", "tools/kill_game.sh 'C:/GOG Games/Banished-B'")
    ok("kill_game.sh body inline", "Get-Process OpenBA -ErrorAction SilentlyContinue | Where-Object "
                                   "{ $_.Path -eq $env:KILL_GAME_EXE } | Stop-Process -Force", P)
    ok("Stop-Process -Id", "Stop-Process -Id 1234 -Force", P)
    ok("grep docs for taskkill", "grep -r \"taskkill /IM\" docs")
    ok("echo taskkill", "echo \"never taskkill //IM OpenBA.exe\"")
    ok("commit msg names taskkill", "git commit -m \"docs: taskkill //IM OpenBA.exe is denied\"")
    ok("bash kill pid", "kill -9 12345")
    blk("heredoc writes a .sh that kills by name", "mkdir -p \"$S\" && cat > \"$S/run.sh\" <<'EOF'\n#!/bin/bash\n"
                                                   "sleep 3\ntaskkill //IM OpenBA.exe //F >/dev/null 2>&1\nEOF\nbash \"$S/run.sh\"")
    blk("bash <<EOF heredoc", "bash <<'EOF'\nfind / -name x\nEOF")
    ok("heredoc writes a .md quoting it", "cat > notes.md <<'EOF'\nnever taskkill //IM OpenBA.exe\nEOF")
    ok("python heredoc editing a script", "python - \"$f\" <<'EOF'\nimport sys\np=sys.argv[1]; s=open(p).read()\n"
                                          "s=s.replace('taskkill //IM OpenBA.exe //F','tools/kill_game.sh \"$G\"')\nEOF")
    # R3 pacman
    blk("pacman -S", "pacman -S mingw-w64-x86_64-gcc")
    blk("pacman -Syu", "pacman -Syu --noconfirm")
    blk("pacman --sync", "pacman --sync --needed make")
    blk("pacman -U", "pacman -U ./x.pkg.tar.zst")
    ok("pacman -Ss", "pacman -Ss gcc")
    ok("pacman -Qi", "pacman -Qi gcc")
    ok("grep for pacman -S", "grep -n \"pacman -S\" CLAUDE.md")
    # R4 attribution
    blk("-m trailer", "git commit -m \"fix: x\" -m \"Co-Authored-By: Claude <noreply@anthropic.com>\"")
    blk("heredoc trailer", "git commit -m \"$(cat <<'EOF'\nfix: x\n\nCo-Authored-By: Claude Opus <noreply@anthropic.com>\n"
                           "EOF\n)\"")
    blk("-F - heredoc Claude-Session", "git commit -F - <<'EOF'\nfix: y\n\nClaude-Session: https://claude.ai/code/x\nEOF")
    blk("Generated with", "git commit -m \"fix: z\n\nGenerated with [Claude Code](https://claude.com/claude-code)\"")
    blk("-F file with trailer", "git commit -F \"%s/msg_trailer.txt\"" % TMP)
    blk("cat > file then -F (file not yet written)", "cat > m.txt <<'EOF'\nfix\n\nCo-Authored-By: A B <a@b.c>\nEOF\n"
                                                    "git commit -F m.txt")
    blk("git -C dir commit", "git -C /c/x commit -m \"a\" -m \"Co-authored-by: X <x@y.z>\"")
    blk("gh pr create body", "gh pr create --title t --body \"Summary\n\nGenerated with [Claude Code](https://x)\"")
    blk("gh -R owner/repo pr create", "gh -R o/r pr create --title t --body \"x\n\nCo-Authored-By: A <a@b.c>\"")
    blk("gh pr merge --body", "gh pr merge 5 --squash --body \"x\n\nCo-Authored-By: A <a@b.c>\"")
    ok("plain commit", "git commit -m \"fix: find / guard\"")
    ok("log | grep attribution", "git log -5 --format=%b | grep -ci \"co-authored\\|generated with\\|claude-s\"")
    ok("prose mention", "git commit -m \"docs: the Co-Authored-By: trailer is off\"")
    ok("-F clean file", "git commit -F \"%s/msg_clean.txt\"" % TMP)
    ok("gh pr view", "gh pr view 301 --json title,body")
    ok("gh pr merge squash", "gh pr merge 301 --squash --delete-branch")
    # R5 retail content staging
    blk("git add data0.pkg", "git add data0.pkg", B, GIT1)
    blk("git add -A with untracked .pkg", "git add -A", B, GIT1)
    blk("cd repo && git add .", "cd \"%s\" && git add ." % GIT1, B, ROOT)
    blk("git -C repo add -A", "git -C \"%s\" add -A" % GIT1, B, ROOT)
    blk("commit with staged .crs", "git commit -m \"x\"", B, GIT2)
    blk("git add .dds by path", "git add src/ext/x/tex.dds")
    ok("git add own .rsc", "git add src/ext/ui/Dialog/Mine.rsc", B, GIT3)
    ok("git add -A, own .rsc only", "git add -A", B, GIT3)
    ok("commit, clean index", "git commit -m \"x\"", B, GIT3)
    ok("commit msg ends in .pkg", "git commit -m \"loader: read the retail data0.pkg\"", B, GIT3)
    ok("commit -am msg ends in .rsc", "git commit -am \"fix: the dialog reads TownHall.rsc\"", B, GIT3)
    ok("commit --author, msg ends in .crs", "git commit --author \"A <a@b.c>\" -m \"docs: terrain.crs\"", B, GIT3)
    ok("commit of a staged removal", "git commit -m \"remove the copy\"", B, GIT4)
    blk("commit -m x -- path.pkg", "git commit -m \"x\" -- data0.pkg", B, GIT3)
    ok("git status", "git status --short")
    return c


def failopen_cases():
    """(desc, stdin bytes, env): each must exit 0 with no stdout."""
    if os.name == "nt":
        git_root = os.path.dirname(os.path.dirname(BASH))
        nopy_path = ";".join([os.path.join(git_root, "usr", "bin"), os.path.join(git_root, "mingw64", "bin"),
                              os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")])
    else:
        nopy_path = TMP + "/empty-path"
        os.makedirs(nopy_path, exist_ok=True)
    nopy = dict(os.environ)
    nopy["PATH"] = nopy_path
    # a python that dies on its own command line with exit 2 (a python 2 rejecting -I, say)
    fake = TMP + "/fake-python"
    os.makedirs(fake, exist_ok=True)
    for name in ("python", "python3"):
        with open(fake + "/" + name, "w", newline="\n") as f:
            f.write("#!/bin/sh\necho 'Unknown option: -I' >&2\nexit 2\n")
        os.chmod(fake + "/" + name, 0o755)
    badpy = dict(os.environ)
    badpy["PATH"] = os.pathsep.join([fake, nopy_path])
    kill = json.dumps(bash_payload("taskkill //IM OpenBA.exe //F"))
    return [
        ("malformed JSON", b"{not json", None),
        ("empty stdin", b"", None),
        ("truncated JSON with a trigger", kill[:-20].encode(), None),
        ("unterminated string with a trigger", b'{"tool_input": {"command": "taskkill //IM OpenBA.exe', None),
        ("command is a list", json.dumps({"tool_name": "Bash", "tool_input": {"command": ["taskkill", "//IM"]}}).encode(),
         None),
        ("tool_input null", json.dumps({"tool_name": "Bash", "tool_input": None, "x": "find"}).encode(), None),
        ("invalid UTF-8 bytes + trigger", b'{"tool_name":"Bash","tool_input":{"command":"find \xff\xfe ."}}', None),
        ("deep nesting", (b"[" * 5000) + b"find" + (b"]" * 5000), None),
        ("no python on PATH: a refusable command passes", kill.encode(), nopy),
        ("python exits 2 on its own: a refusable command passes", kill.encode(), badpy),
    ]


def bench(n):
    reps = [
        ("prefilter miss (git status)", bash_payload("git status --short")),
        ("prefilter hit, pass (find .)", bash_payload("find . -name x")),
        ("block (taskkill //IM)", bash_payload("taskkill //IM OpenBA.exe //F")),
        ("git commit, staged check in --root", bash_payload("git commit -m \"x\"")),
        ("git add -A dry run in --root", bash_payload("git add -A")),
    ]
    print("== latency over %d runs per payload, settings form (bash start + hook)" % n)
    for desc, payload in reps:
        run(payload, "settings")
        ts = sorted(run(payload, "settings")[3] for _ in range(n))
        print("  %-38s p50=%.0f p95=%.0f max=%.0f ms" % (desc, statistics.median(ts),
                                                         ts[min(n - 1, int(round(0.95 * (n - 1))))], ts[-1]))


def main():
    print("root %s; bash %s; hooks %s\nsettings command: %s" % (ROOT, BASH, HOOKS, CMD))
    fixtures()
    fp = fn = fo = 0
    cs = cases()
    for form in FORMS:
        for want, desc, payload in cs:
            rc, out, err, ms = run(payload, form)
            got = "block" if rc == 2 else ("pass" if rc == 0 else "rc%d" % rc)
            lines = len([ln for ln in err.splitlines() if ln.strip()])
            if want == "pass":
                good = got == "pass" and not out.strip() and not err.strip()
                fp += 0 if good else 1
            else:
                good = got == "block" and lines == 1 and len(err) <= 700
                fn += 0 if good else 1
            first = err.strip().splitlines()[0][:100] if err.strip() else ""
            print("%s %-8s %-5s got=%-5s %4.0f ms  %-42s %s" % ("PASS" if good else "FAIL", form, want, got, ms,
                                                               desc[:42], first))
    nblock = sum(1 for x in cs if x[0] == "block")
    print("== %d cases (%d must-block, %d must-pass) x %d forms: false positives %d, false negatives %d" % (
        len(cs), nblock, len(cs) - nblock, len(FORMS), fp, fn))
    fcs = failopen_cases()
    for form in FORMS:
        for desc, data, env in fcs:
            rc, out, err, ms = run(data, form, env)
            good = rc == 0 and not out.strip()
            fo += 0 if good else 1
            print("%s failopen %-8s rc=%d %4.0f ms  %s" % ("PASS" if good else "FAIL", form, rc, ms, desc))
    print("== failopen: %d cases x %d forms, %d failures" % (len(fcs), len(FORMS), fo))
    nbench = int(arg("--bench", "0"))
    if nbench:
        bench(nbench)
    print("TOTAL false positives %d, false negatives %d, fail-open failures %d" % (fp, fn, fo))
    return 1 if fp or fn or fo else 0


if __name__ == "__main__":
    try:
        rc = main()
    finally:
        shutil.rmtree(TMP, ignore_errors=True)
    sys.exit(rc)
