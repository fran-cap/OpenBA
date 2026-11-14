"""Core of pre_bash_guard.sh: decide whether one Bash / PowerShell command string must be refused.

Called by .claude/hooks/pre_bash_guard.sh (the PreToolUse hook in .claude/settings.json); tests in
.claude/hooks/tests/test_hooks.py.

usage: python -I -S pre_bash_guard.py < hook-input.json       exit 2 + one stderr line to refuse, else exit 0
       import pre_bash_guard; pre_bash_guard.check(command, tool, cwd) -> reason or None   (tests)

Rules (each one names what it refuses; everything else passes):
  R1 find-root   `find` whose starting point is a filesystem root, a drive root (/, /c, C:/, C:\\, //, /mnt/c,
                 /cygdrive/c), a top-level system or users dir (/c/Windows, /c/Program Files*, /c/ProgramData,
                 /c/Users), or a home dir (~, $HOME, /c/Users/<name>, /home/<name>), unless -maxdepth <= 2.
                 PowerShell / cmd: Get-ChildItem|gci|ls|dir -Recurse (or dir /s) on the same roots, unless
                 -Depth <= 2. In the PowerShell tool and in cmd, `find` is Windows find.exe (a text search) and
                 is not checked.
  R2 kill-name   OpenBA.exe ended by image name or window title: taskkill /IM|/FI IMAGENAME|WINDOWTITLE (any
                 switch spelling: / // -, any order, any case, wildcards matched), Stop-Process|spps|kill
                 -Name, Get-Process <OpenBA> | Stop-Process (without a Where-Object on Path), wmic ... name ...
                 delete|terminate, Win32_Process ... Terminate, pkill / killall, kill $(pgrep|pidof OpenBA).
                 tools/kill_game.sh <dir> and taskkill /PID stay allowed.
  R3 pacman      pacman -S / --sync / -U (installs); -Ss, -Si, -Sl, -Sg, -Sp and -Q* pass.
  R4 attribution git commit (and gh pr|issue create|edit|comment|review) whose message carries a trailer
                 line "Co-Authored-By: <name> <email>", "Claude-Session: ...", or "Generated with [Claude
                 Code]": in -m / --message, in an -F / --file that exists, or anywhere in the command text
                 (heredoc bodies included).
  R5 content     git add / git commit that would stage retail content (.pkg .pkm .crs .dds .cso, or .rsc
                 outside src/ext/): add pathspecs are listed with `git add --dry-run`, a commit checks
                 `git diff --cached --name-only` (deletions not counted) plus its own pathspecs (option
                 values such as the -m message are not pathspecs).
Shell wrappers are unwrapped (bash|sh -c, cmd /c, powershell|pwsh -Command) and so are timeout, time, nice,
nohup, env, sudo, command, builtin, exec, xargs, stdbuf, winpty.
Not seen: commands inside a script file, variables ($X), aliases, eval of built strings, a message file
written by an earlier tool call and then deleted, `--amend --no-edit` reusing an old message, encoded
PowerShell (-EncodedCommand).
"""
import fnmatch
import json
import os
import posixpath
import re
import shlex
import sys

SEPS = {";", "&&", "||", "|", "&", "|&", ";;", "(", ")", "\n", "{", "}"}
REDIRS = {">", ">>", "<", "<<", "<<<", ">&", "<&", ">|", "&>", "&>>", "2>", "2>>"}
WRAPPERS = {"timeout", "time", "nice", "nohup", "env", "sudo", "command", "builtin", "exec", "xargs", "stdbuf",
            "winpty", "start", "call", "&", "."}
# shell keywords that put a command after them (`if ...; then taskkill ...; fi`)
KEYWORDS = {"if", "then", "else", "elif", "do", "while", "until", "!"}
SHELLS_C = {"bash", "sh", "zsh", "dash"}
PWSH = {"powershell", "pwsh"}
CONTENT_EXT = (".pkg", ".pkm", ".crs", ".dds", ".cso")
GAME = ("openba.exe", "openba")
TRAILER = re.compile(r"(?im)(?:^|\\n|[\"'])\s*(?:co-authored-by:\s*[^\n<]*<[^>\n]*@[^>\n]*>|claude-session:\s*\S+)"
                     r"|generated\s+with\s+\[?claude[ -]code")


def base(word):
    w = word.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return w[:-4] if w.endswith(".exe") else w


def strip_heredocs(cmd):
    """Remove heredoc bodies and their terminator lines (the raw text keeps them for the trailer scan).
    A `<<TAG` whose terminator never appears (a `<<` inside quotes, say) strips nothing."""
    out = []
    lines = cmd.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        i += 1
        for m in re.finditer(r"<<-?\s*(['\"]?)([A-Za-z_][A-Za-z0-9_]*)\1", line):
            tag = m.group(2)
            j = i
            while j < len(lines) and lines[j].strip() != tag:
                j += 1
            if j < len(lines):
                i = j + 1
    return "\n".join(out)


def tokens(cmd, flavour):
    text = cmd
    if flavour == "bash":
        text = strip_heredocs(text).replace("`", " ; ")
    lex = shlex.shlex(text, posix=True, punctuation_chars="();<>|&\n{}")
    lex.whitespace = " \t\r"
    lex.commenters = ""
    if flavour != "bash":
        lex.escape = ""
    lex.whitespace_split = True  # words split on whitespace and punctuation only: C:\x, $HOME, a-b stay whole
    try:
        return list(lex)
    except ValueError:
        # unbalanced quotes: fall back to a plain split on separators and whitespace
        return [t for t in re.split(r"(\s+|;|&&|\|\||\||\n)", text) if t and not t.isspace()]


def simple_commands(toks):
    cur = []
    skip_next = False
    for t in toks:
        if skip_next:
            skip_next = False
            continue
        punct = bool(t) and set(t) <= set(";&|()\n{}<>")
        if t in SEPS or (punct and any(c in t for c in ";|()\n{}")) or t == "&":
            if cur:
                yield cur
            cur = []
            continue
        if t in REDIRS or punct:
            skip_next = True
            continue
        cur.append(t)
    if cur:
        yield cur


def unwrap(words):
    """Drop env assignments and wrappers in front of the real command word."""
    i = 0
    while i < len(words):
        w = words[i]
        b = base(w)
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", w) or w in KEYWORDS:
            i += 1
            continue
        if b in WRAPPERS or w == "&":
            i += 1
            if b == "timeout" or b == "nice" or b == "stdbuf" or b == "xargs" or b == "env" or b == "sudo":
                while i < len(words) and words[i].startswith("-"):
                    opt = words[i]
                    i += 1
                    if b in ("xargs",) and opt in ("-n", "-I", "-L", "-P", "-d", "-s", "-E", "-a"):
                        i += 1
                    if b == "nice" and opt == "-n":
                        i += 1
                if b == "timeout" and i < len(words) and re.match(r"^\d", words[i]):
                    i += 1
            continue
        break
    return words[i:]


# ---------------------------------------------------------------- R1
ROOTS = [r"/+", r"/+\.", r"/[a-z]/?", r"[a-z]:/?", r"[a-z]:", r"/mnt/[a-z]/?", r"/cygdrive/[a-z]/?",
         r"(/[a-z]|[a-z]:)/(windows|program files( \(x86\))?|programdata|users)/?",
         r"(/[a-z]|[a-z]:)/users/[^/]+/?", r"/home(/[^/]+)?/?", r"/root/?", r"~[^/]*/?", r"\$\{?home\}?/?",
         r"\$env:userprofile/?", r"\$\{?userprofile\}?/?", r"%userprofile%/?"]
ROOT_RX = re.compile(r"^(?:%s)$" % "|".join(ROOTS))


def is_root(path, cwd=None):
    p = path.replace("\\", "/").lower().strip()
    if ROOT_RX.match(p):
        return True
    if "/.." in p or p.startswith(".."):
        # `find ~/..`, `find /c/code/GIT/../..`, `find ../../..` walk a root too. Only a path
        # with a `..` part is resolved, so `find .` from any cwd keeps its old answer.
        p = re.sub(r"^(~|\$\{?home\}?)(?=/|$)", "/home/u", p)
        if not p.startswith("/") and not re.match(r"^[a-z]:", p) and cwd:
            p = cwd.replace("\\", "/").lower().rstrip("/") + "/" + p
        drive = ""
        m = re.match(r"^([a-z]:)(.*)$", p)
        if m:
            drive, p = m.group(1), m.group(2) or "/"
        p = drive + posixpath.normpath(p)
        return bool(ROOT_RX.match(p))
    return False


def r1_find(words, cwd=None):
    args = words[1:]
    i = 0
    while i < len(args) and args[i] in ("-H", "-L", "-P"):
        i += 1
    if i < len(args) and args[i] == "-D":
        i += 2
    if i < len(args) and args[i].startswith("-O"):
        i += 1
    starts = []
    while i < len(args) and not args[i].startswith("-") and args[i] not in ("(", "!", ","):
        starts.append(args[i])
        i += 1
    depth = None
    for j, a in enumerate(args):
        if a == "-maxdepth" and j + 1 < len(args) and args[j + 1].isdigit():
            depth = int(args[j + 1])
    bad = [s for s in starts if is_root(s, cwd)]
    if bad and (depth is None or depth > 2):
        return "find over %s walks a whole drive or home dir: scope it to the worktree (find <worktree>/src ...)" \
               " or add -maxdepth 2 (CLAUDE.md: never an unscoped filesystem search)" % bad[0]
    return None


VALUE_PARAMS = {"-path", "-literalpath", "-lp", "-filter", "-include", "-exclude", "-depth", "-attributes",
                "-erroraction", "-ea", "-ev", "-errorvariable", "-ov", "-outvariable"}


def r1_gci(words, flavour, cwd=None):
    """Get-ChildItem / gci / dir / ls with recursion over a root. bash `ls -R`, cmd `dir /s`."""
    b = base(words[0])
    rest = words[1:]
    low = [a.lower() for a in rest]
    if flavour == "bash":
        if b != "ls" or not any(re.match(r"^-[A-Za-z]*R[A-Za-z]*$", a) for a in rest):
            return None
        paths = [a for a in rest if not a.startswith("-")]
        depth = None
    else:
        cmd_dir = flavour == "cmd" or (b == "dir" and "/s" in low)
        # PowerShell takes any unambiguous prefix, and `gci -r` / `ls -r` resolve to -Recurse
        recurse = any(re.match(r"^-r(e(c(u(r(s(e)?)?)?)?)?)?$", a) for a in low) or (cmd_dir and "/s" in low)
        if not recurse:
            return None
        depth = None
        paths = []
        j = 0
        while j < len(rest):
            a = low[j]
            if a in VALUE_PARAMS and j + 1 < len(rest):
                if a in ("-path", "-literalpath", "-lp"):
                    paths.append(rest[j + 1])
                if a == "-depth" and rest[j + 1].isdigit():
                    depth = int(rest[j + 1])
                j += 2
                continue
            if a.startswith("-") or (cmd_dir and re.match(r"^/[a-z](:.*)?$", a)):
                j += 1
                continue
            paths.append(rest[j])
            j += 1
    bad = [p for p in paths if is_root(p, cwd) or p in ("\\", "/")]
    if bad and (depth is None or depth > 2):
        return "%s recursing over %s walks a whole drive or home dir: scope it to the worktree" % (words[0], bad[0])
    return None


# ---------------------------------------------------------------- R2
def matches_game(pattern):
    p = pattern.strip("\"'").lower()
    return any(fnmatch.fnmatch(g, p) for g in GAME)


def r2_kill(words, line):
    b = base(words[0])
    low = [w.lower() for w in words]
    msg = "ends OpenBA.exe by image name, which also closes the user's own game in the other game dir: use " \
          "tools/kill_game.sh <game dir>, or taskkill //PID <pid> after checking the process start time"
    if b == "taskkill":
        for j, w in enumerate(low):
            sw = w.lstrip("/-")
            if w != sw and sw == "im" and j + 1 < len(words) and matches_game(words[j + 1]):
                return "taskkill /IM " + msg
            if w != sw and sw == "fi" and j + 1 < len(words):
                flt = words[j + 1].strip("\"'").lower()
                m = re.match(r"^\s*(imagename|windowtitle)\s+eq\s+(.+?)\s*$", flt)
                if m and (matches_game(m.group(2)) or "banished" in m.group(2) or "openba" in m.group(2)):
                    return "taskkill /FI %s %s" % (m.group(1).upper(), msg)
        return None
    if b in ("stop-process", "spps") or (b == "kill" and any(w.startswith("-n") for w in low[1:])):
        for j, w in enumerate(low):
            if re.match(r"^-(n|na|nam|name|processname)$", w) and j + 1 < len(words):
                if any(matches_game(n) for n in words[j + 1].split(",")):
                    return "Stop-Process -Name " + msg
            # PowerShell's `-Name:OpenBA` form
            m = re.match(r"^-(n|na|nam|name|processname):(.+)$", w)
            if m and any(matches_game(n) for n in m.group(2).split(",")):
                return "Stop-Process -Name " + msg
        return None
    if b in ("pkill", "killall"):
        for w in words[1:]:
            if w.startswith("-"):
                continue
            try:
                if re.search(w, "OpenBA.exe", re.I) or matches_game(w):
                    return "%s %s" % (b, msg)
            except re.error:
                if "openba" in w.lower():
                    return "%s %s" % (b, msg)
        return None
    if b == "wmic":
        j = " ".join(low)
        if "process" in j and "openba" in j and re.search(r"\b(delete|terminate)\b", j):
            return "wmic process ... delete|terminate " + msg
    return None


def r2_statement(stmt, flavour):
    """Whole-statement forms, judged only where the statement itself starts with the command (never inside an
    echo / grep / commit message): Get-Process OpenBA | Stop-Process, (Get-Process OpenBA).Kill(),
    Get-CimInstance Win32_Process ... Terminate, kill $(pgrep|pidof OpenBA)."""
    t = stmt.strip().lower()
    if "openba" not in t:
        return None
    msg = "ends OpenBA.exe by name, which also closes the user's own game in the other game dir: use " \
          "tools/kill_game.sh <game dir> (it selects by image path)"
    if flavour != "bash":
        # A Get-Process and a stop verb anywhere in one statement that names OpenBA, unless a Path filter
        # selects the process (tools/kill_game.sh's own form). This covers `Get-Process | Where-Object Name -eq
        # OpenBA | Stop-Process`, `Stop-Process -Id (Get-Process OpenBA).Id` and `... | ForEach-Object
        # { Stop-Process -Id $_.Id }`, not only `Get-Process OpenBA | Stop-Process`.
        if re.search(r"\b(get-process|gps)\b", t) and \
                re.search(r"\b(stop-process|spps)\b|\.kill\(\)|\|\s*kill\b", t):
            if not re.search(r"(where-object|\bwhere\b|\?)\s*[^|]*\.?path\b", t):
                return "Get-Process OpenBA | Stop-Process " + msg
        if re.search(r"\b(start-process|saps)\b", t) and re.search(r"\btaskkill\b", t) and re.search(r"[/-]im\b", t):
            return "Start-Process taskkill /IM " + msg
        if re.match(r"^\(?\s*(get-ciminstance|gcim|get-wmiobject|gwmi)\b", t) and "win32_process" in t and \
                "terminate" in t:
            return "Win32_Process Terminate " + msg
    else:
        if re.match(r"^(sudo\s+)?(kill|taskkill)\b[^;&|\n]*\$\(\s*(pgrep|pidof)\b[^)]*openba", t):
            return "kill $(pgrep OpenBA) " + msg
    # `echo OpenBA.exe | xargs taskkill //F //IM`: the image name arrives on stdin
    if re.search(r"\bxargs\b[^|;&\n]*\btaskkill\b[^|;&\n]*\s[/-]{1,2}im\s*$", t):
        return "xargs taskkill /IM <OpenBA from stdin> " + msg
    return None


# ---------------------------------------------------------------- R3
def r3_pacman(words):
    for w in words[1:]:
        if w == "--sync" or w == "--upgrade":
            return "pacman %s: MSYS2 packages are stale here and the build compiler is the pinned WinLibs GCC " \
                   "(tools/toolchain/install_winlibs.sh); do not install with pacman" % w
        m = re.match(r"^-([A-Za-z]+)$", w)
        if m and ("S" in m.group(1) or "U" in m.group(1)):
            ops = m.group(1)
            if "U" in ops or not re.search(r"[silgp]", ops.replace("S", "")):
                return "pacman %s: MSYS2 packages are stale here and the build compiler is the pinned WinLibs " \
                       "GCC (tools/toolchain/install_winlibs.sh); do not install with pacman" % w
    return None


# ---------------------------------------------------------------- R4 / R5
def git_parts(words):
    """-> (dir from -C or None, subcommand, args after it)."""
    i = 1
    d = None
    while i < len(words):
        w = words[i]
        if w == "-C" and i + 1 < len(words):
            d = words[i + 1]
            i += 2
        elif w in ("-c", "--git-dir", "--work-tree", "--namespace") and i + 1 < len(words):
            i += 2
        elif w.startswith("-"):
            i += 1
        else:
            return d, w, words[i + 1:]
    return d, None, []


def win_path(p, cwd):
    if p is None:
        return cwd
    p = p.replace("\\", "/")
    m = re.match(r"^/([a-zA-Z])(/.*)?$", p)
    if m:
        p = m.group(1).upper() + ":" + (m.group(2) or "/")
    if not os.path.isabs(p) and cwd:
        p = os.path.join(cwd, p)
    return p


def bad_content(paths):
    for p in paths:
        q = p.replace("\\", "/").strip("\"'").lower()
        if q.endswith(CONTENT_EXT):
            return p
        if q.endswith(".rsc") and "src/ext/" not in q and not q.startswith("ext/"):
            return p
    return None


# git commit options whose value is the next word (a message, a file, an author): never a pathspec.
# A short-option cluster ending in one of m F C c t (`-am "msg"`) takes the next word the same way.
COMMIT_VALUE_OPTS = {"-m", "--message", "-F", "--file", "-C", "--reuse-message", "-c", "--reedit-message",
                     "--author", "--date", "-t", "--template", "--cleanup", "--fixup", "--squash", "--trailer",
                     "--pathspec-from-file"}


def commit_pathspecs(args):
    """The pathspecs of `git commit <args>`: `git commit -m "read data0.pkg"` has none."""
    out = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--":
            out += args[i + 1:]
            break
        if a in COMMIT_VALUE_OPTS or re.match(r"^-[A-Za-z]*[mFCct]$", a):
            i += 2
            continue
        if not a.startswith("-"):
            out.append(a)
        i += 1
    return out


def run_git(d, args):
    import subprocess  # lazy: only R5 spawns git, and the import costs ~6 ms of every call
    try:
        p = subprocess.run(["git", "-C", d] + args, capture_output=True, text=True, timeout=10)
        return p.stdout if p.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def r4_messages(sub, args, cwd):
    texts = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-m", "--message", "-F", "--file", "-b", "--body", "-t", "--title", "--body-file") and i + 1 < len(args):
            v = args[i + 1]
            if a in ("-F", "--file", "--body-file") and v != "-":
                try:
                    with open(win_path(v, cwd), encoding="utf-8", errors="replace") as f:
                        texts.append(f.read(65536))
                except OSError:
                    pass
            else:
                texts.append(v)
            i += 2
            continue
        m = re.match(r"^(--message=|--body=|--file=|-m)(.+)$", a, re.S)
        if m:
            if m.group(1) == "--file=":
                try:
                    with open(win_path(m.group(2), cwd), encoding="utf-8", errors="replace") as f:
                        texts.append(f.read(65536))
                except OSError:
                    pass
            else:
                texts.append(m.group(2))
        i += 1
    return texts


def check_words(words, flavour, cwd, full, cd_dir):
    """One simple command. Returns a reason or None. cd_dir: last `cd` target seen earlier in the command."""
    words = unwrap(words)
    if not words:
        return None
    b = base(words[0])
    low = [w.lower() for w in words]
    # wrappers that run a command string
    if b in SHELLS_C:
        for j, w in enumerate(words[1:], 1):
            # any short-option cluster holding c (`-xc`, `-ec`, `-euc`), not only -c / -lc / -ic
            if re.match(r"^-[A-Za-z]*c[A-Za-z]*$", w) and j + 1 < len(words):
                return check(words[j + 1], "Bash", cwd, depth_flavour="bash")
        return None
    if b == "cmd":
        for j, w in enumerate(low[1:], 1):
            if w in ("/c", "//c", "/k", "//k", "-c"):
                return check(" ".join(words[j + 1:]), "PowerShell", cwd, depth_flavour="cmd")
        return None
    if b in PWSH:
        for j, w in enumerate(low[1:], 1):
            if w in ("-c", "-command", "-com", "/c", "/command"):
                return check(" ".join(words[j + 1:]), "PowerShell", cwd, depth_flavour="pwsh")
        tail = [w for w in words[1:] if not w.startswith("-")]
        if tail and not low[-1].endswith(".ps1"):
            return check(" ".join(tail), "PowerShell", cwd, depth_flavour="pwsh")
        return None
    if b == "find" and flavour == "bash":
        return r1_find(words, cd_dir or cwd)
    if b in ("get-childitem", "gci", "dir", "ls"):
        r = r1_gci(words, flavour, cd_dir or cwd)
        if r:
            return r
    r = r2_kill(words, full)
    if r:
        return r
    if b == "pacman":
        return r3_pacman(words)
    if _SCRIPT[0]:
        return None  # a script body runs later, in another state: R4 / R5 judge the command that runs it
    if b == "git":
        d, sub, args = git_parts(words)
        gd = win_path(d if d else cd_dir, cwd)
        if sub == "commit":
            for t in r4_messages(sub, args, gd) + [full]:
                if TRAILER.search(t):
                    return "git commit message carries an attribution trailer (Co-Authored-By / Claude-Session / " \
                           "Generated with): commits, PRs and issues carry none; remove the line"
            paths = commit_pathspecs(args)
            # --diff-filter=d: a staged deletion (`git rm --cached data0.pkg`) removes content, it records none
            staged = run_git(gd, ["diff", "--cached", "--name-only", "--diff-filter=d"]).splitlines() if gd else []
            hit = bad_content(staged + paths)
            if hit:
                return "git commit would record retail content (%s): AGENTS.md section 1; unstage it " \
                       "(git restore --staged <path>) and delete the copy" % hit
        if sub == "add":
            paths = [a for a in args if not a.startswith("-") or a in ("-A", "--all", "-u", "--update", ".")]
            hit = bad_content([a for a in args if not a.startswith("-")])
            if not hit and gd and paths:
                listed = run_git(gd, ["add", "--dry-run", "--ignore-missing"] + args)
                names = [m.group(1) for m in re.finditer(r"^add '(.+)'$", listed, re.M)]
                hit = bad_content(names)
            if hit:
                return "git add would stage retail content (%s): AGENTS.md section 1; game data is read from the " \
                       "install at run time, never copied into the tree" % hit
        return None
    if b == "gh":
        # Global -R / --repo is skipped; any pr / issue / api / release call whose text carries a trailer line
        # is refused: `gh -R o/r pr create`, `gh pr merge --body` (the squash commit's body on main) and
        # `gh api ... -f body=` included.
        rest = []
        j = 1
        while j < len(words):
            if low[j] in ("-r", "--repo") and j + 1 < len(words):
                j += 2
                continue
            if low[j].startswith("--repo="):
                j += 1
                continue
            rest = words[j:]
            break
        group = rest[0].lower() if rest else ""
        if group in ("pr", "issue", "api", "release"):
            for t in r4_messages("gh", rest[1:], cwd) + [full]:
                if TRAILER.search(t):
                    return "gh %s body carries an attribution line (Co-Authored-By / Claude-Session / Generated " \
                           "with): remove it" % " ".join(rest[:2])
    return None


_SCRIPT = [False]
SCRIPT_EXT = re.compile(r"\.(sh|bash|ps1|psm1|bat|cmd)$", re.I)


def heredoc_scripts(cmd):
    """Heredoc bodies that become a script: `cat > x.sh <<EOF`, `tee x.ps1 <<EOF`, `bash <<EOF`, `sh -s <<EOF`.
    -> [(flavour, body)]."""
    out = []
    lines = cmd.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        for m in re.finditer(r"<<-?\s*(['\"]?)([A-Za-z_][A-Za-z0-9_]*)\1", line):
            j = i
            while j < len(lines) and lines[j].strip() != m.group(2):
                j += 1
            if j >= len(lines):
                continue
            body = "\n".join(lines[i:j])
            head = line[:m.start()]
            tgt = re.search(r"(?:>|\btee\s+(?:-a\s+)?)\s*[\"']?([^\"'\s;&|]+)[\"']?\s*$", head.strip())
            if tgt and SCRIPT_EXT.search(tgt.group(1)):
                out.append(("pwsh" if re.search(r"\.ps", tgt.group(1), re.I) else "bash", body))
            elif re.search(r"(^|[;&|]\s*)(bash|sh)(\s+-s)?\s*$", head.strip()):
                out.append(("bash", body))
            i = j + 1
    return out


def check_script(text, path):
    """R1-R3 over a script's text (a heredoc that writes a .sh / .ps1 / .bat, or feeds bash)."""
    flavour = "pwsh" if re.search(r"\.ps", path, re.I) else ("cmd" if re.search(r"\.(bat|cmd)$", path, re.I)
                                                             else "bash")
    old = _SCRIPT[0]
    _SCRIPT[0] = True
    try:
        r = check(text, "Bash" if flavour == "bash" else "PowerShell", None, depth_flavour=flavour)
    finally:
        _SCRIPT[0] = old
    return ("the script %s would run this: %s" % (os.path.basename(path), r)) if r else None


def check(command, tool="Bash", cwd=None, depth_flavour=None, _depth=[0]):
    if _depth[0] > 3 or not command:
        return None
    flavour = depth_flavour or ("bash" if tool == "Bash" else "pwsh")
    _depth[0] += 1
    try:
        if flavour == "bash":
            for fl, body in heredoc_scripts(command):
                r = check_script(body, "heredoc." + ("ps1" if fl == "pwsh" else "sh"))
                if r:
                    return r.replace("the script heredoc.sh", "a heredoc script").replace(
                        "the script heredoc.ps1", "a heredoc script")
        toks = tokens(command, flavour)
        cd_dir = None
        for words in simple_commands(toks):
            uw = unwrap(words)
            if uw and base(uw[0]) in ("cd", "pushd", "set-location", "sl") and len(uw) > 1:
                cd_dir = win_path(uw[1], cwd)
                continue
            r = check_words(words, flavour, cwd, command, cd_dir)
            if r:
                return r
        for stmt in re.split(r"(?:;|&&|\|\||\n)", strip_heredocs(command) if flavour == "bash" else command):
            r = r2_statement(stmt, flavour)
            if r:
                return r
        return None
    finally:
        _depth[0] -= 1


def main():
    try:
        data = json.load(sys.stdin)
        tool = data.get("tool_name", "Bash")
        cmd = (data.get("tool_input") or {}).get("command", "")
        cwd = data.get("cwd") or os.getcwd()
    except Exception:
        return 0
    try:
        reason = check(cmd, tool, cwd)
    except Exception:
        return 0
    if reason:
        sys.stderr.write("pre_bash_guard: %s\n" % reason[:600])
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
