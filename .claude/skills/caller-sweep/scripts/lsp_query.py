"""Minimal clangd LSP driver with BOUNDED output, for agents (stdio JSON-RPC, no dependencies).

A first pass only, never a proof of absence (skill caller-sweep: grep + nm decide). Needs a compile DB,
build/cmake-debug/compile_commands.json (written by `tools/build.sh build`), and a .clangd at the checkout
root (copy ../clangd.windows.yaml there on Windows).

usage (paths are relative to --root, default the git checkout of the current directory):
  python -I lsp_query.py [opts] refs     <file> <line> <name> [--decl]
  python -I lsp_query.py [opts] callers  <file> <line> <name> [--depth N]
  python -I lsp_query.py [opts] impl     <file> <line> <name>
  python -I lsp_query.py [opts] subtypes <file> <line> <name> [--depth N]
  python -I lsp_query.py [opts] def      <file> <line> <name>
  python -I lsp_query.py [opts] wsym     <query>
  python -I lsp_query.py [opts] diags    <list.txt>        ("<stratum>\\t<relpath>" lines)
  python -I lsp_query.py [opts] inactive <file>
  python -I lsp_query.py [opts] index                      (open one file, wait for the background index to go idle)
opts:
  --root DIR        workspace root (default: `git rev-parse --show-toplevel` of the current directory)
  --clangd EXE      the clangd executable (default: clangd on PATH)
  --index           start clangd with --background-index and wait until it is idle before querying
  --no-wait         with --index: do not wait for idle (warm-start latency test)
  --until N         with --no-wait: re-ask the query every 0.5 s until it returns >= N results (max 300 s)
  -j N              clangd workers (default 2, so a query does not compete with builds)
  --cap N           max result lines printed (default 60)
  --raw FILE        also write the raw JSON result here
  --log FILE        clangd stderr (default <root>/build/lsp_query/clangd_<mode>.log)
  --mem             (Windows) sample clangd's working set once a second; print peak
  --arg ARG         one extra clangd argument (repeatable)
<line> is 1-based; <name> is an identifier on that line (first occurrence; name@K for the K-th).
Output: one header line with counts and timings, then "path: line,line,..." groups (paths relative to root).
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse

CLANGD_HELP = ("clangd not found: pass --clangd <path to clangd>, or put clangd on PATH. It ships in the LLVM "
               "release archives (https://github.com/llvm/llvm-project/releases, bin/clangd.exe; LLVM 23.1.5 "
               "was checked) and at https://github.com/clangd/clangd/releases.")


def git_toplevel(d):
    try:
        p = subprocess.run(["git", "-C", d, "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    except OSError:
        return None
    top = p.stdout.strip()
    return top if p.returncode == 0 and top else None


def find_clangd(given):
    if given:
        if os.path.isfile(given) or shutil.which(given):
            return given
        raise SystemExit("--clangd %s: no such file. %s" % (given, CLANGD_HELP))
    found = shutil.which("clangd")
    if not found:
        raise SystemExit(CLANGD_HELP)
    return found


def path_to_uri(p):
    p = os.path.abspath(p).replace("\\", "/")
    return "file:///" + urllib.parse.quote(p, safe="/:")


def uri_to_path(u):
    p = urllib.parse.unquote(u)
    if p.startswith("file:///"):
        p = p[8:]
    return p.replace("\\", "/")


class PeakMem:
    """Working-set sampler for one PID via GetProcessMemoryInfo (exact peak from the OS too). Windows only:
    ctypes.wintypes is imported here, not at module level, so the script also loads elsewhere."""

    def __init__(self, pid):
        import ctypes
        import ctypes.wintypes

        class PMC(ctypes.Structure):
            _fields_ = [("cb", ctypes.wintypes.DWORD), ("PageFaultCount", ctypes.wintypes.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]

        self.ctypes = ctypes
        self.PMC = PMC
        self.h = ctypes.windll.kernel32.OpenProcess(0x0400 | 0x0010, False, pid)
        self.samples = []
        self.stop = False
        self.t = threading.Thread(target=self.loop, daemon=True)
        self.t.start()

    def read(self):
        ctypes = self.ctypes
        pmc = self.PMC()
        pmc.cb = ctypes.sizeof(pmc)
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(self.h, ctypes.byref(pmc), pmc.cb)
        if not ok:
            return None
        return pmc.WorkingSetSize, pmc.PeakWorkingSetSize, pmc.PagefileUsage

    def loop(self):
        while not self.stop:
            r = self.read()
            if r:
                self.samples.append((time.time(), r[0]))
            time.sleep(1.0)


class Client:
    def __init__(self, root, clangd, args, log):
        self.root = root
        self.logf = open(log, "w", encoding="utf-8")
        self.proc = subprocess.Popen([clangd] + args, cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.logf)
        self.next_id = 1
        self.pending = {}
        self.lock = threading.Lock()
        self.diags = {}
        self.diag_events = queue.Queue()
        self.progress = queue.Queue()
        self.inactive = {}
        self.rx_bytes = 0
        self.alive = True
        self.reader = threading.Thread(target=self.read_loop, daemon=True)
        self.reader.start()

    # --- transport
    def send(self, msg):
        body = json.dumps(msg).encode("utf-8")
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()

    def read_loop(self):
        f = self.proc.stdout
        while True:
            headers = {}
            while True:
                line = f.readline()
                if not line:
                    self.alive = False
                    return
                line = line.strip()
                if not line:
                    break
                k, _, v = line.decode("ascii").partition(":")
                headers[k.strip().lower()] = v.strip()
            n = int(headers.get("content-length", "0"))
            body = f.read(n)
            self.rx_bytes += n
            msg = json.loads(body)
            self.dispatch(msg, n)

    def dispatch(self, msg, nbytes):
        if "id" in msg and "method" in msg:  # server -> client request
            self.send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
            return
        if "id" in msg:
            with self.lock:
                slot = self.pending.pop(msg["id"], None)
            if slot:
                slot["bytes"] = nbytes
                slot["msg"] = msg
                slot["ev"].set()
            return
        m = msg.get("method")
        p = msg.get("params", {})
        if m == "textDocument/publishDiagnostics":
            self.diags[uri_to_path(p["uri"]).lower()] = p.get("diagnostics", [])
            self.diag_events.put(uri_to_path(p["uri"]).lower())
        elif m == "$/progress":
            self.progress.put((time.time(), p.get("token"), p.get("value", {})))
        elif m == "textDocument/inactiveRegions":
            self.inactive[uri_to_path(p["textDocument"]["uri"]).lower()] = p.get("regions", [])

    def request(self, method, params, timeout=600):
        with self.lock:
            rid = self.next_id
            self.next_id += 1
            slot = {"ev": threading.Event()}
            self.pending[rid] = slot
        t0 = time.perf_counter()
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        if not slot["ev"].wait(timeout):
            raise TimeoutError(method)
        msg = slot["msg"]
        if "error" in msg:
            raise RuntimeError("%s: %s" % (method, msg["error"]))
        return msg.get("result"), time.perf_counter() - t0, slot["bytes"]

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    # --- lifecycle
    def initialize(self):
        caps = {
            "window": {"workDoneProgress": True},
            "textDocument": {
                "publishDiagnostics": {"relatedInformation": False},
                "references": {}, "definition": {}, "implementation": {},
                "callHierarchy": {}, "typeHierarchy": {},
                "inactiveRegionsCapabilities": {"inactiveRegions": True},
            },
            "workspace": {"symbol": {}, "workspaceFolders": True},
        }
        res, dt, _ = self.request("initialize", {
            "processId": os.getpid(), "rootUri": path_to_uri(self.root),
            "workspaceFolders": [{"uri": path_to_uri(self.root), "name": "root"}],
            "capabilities": caps})
        self.notify("initialized", {})
        return dt

    def open(self, rel):
        path = os.path.join(self.root, rel)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        self.notify("textDocument/didOpen", {"textDocument": {
            "uri": path_to_uri(path), "languageId": "cpp", "version": 1, "text": text}})
        return path, text

    def close(self, path):
        self.notify("textDocument/didClose", {"textDocument": {"uri": path_to_uri(path)}})

    def shutdown(self):
        try:
            self.request("shutdown", None, timeout=30)
            self.notify("exit", None)
        except Exception:
            pass
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()  # the script's own child, by handle
        self.logf.close()

    def wait_index_idle(self, quiet=1.0, limit=3600):
        """Wait until the background-index progress has ended and stayed ended for `quiet` seconds."""
        t0 = time.time()
        active = set()
        seen = False
        last_end = None
        last_pct = None
        while time.time() - t0 < limit:
            try:
                ts, tok, val = self.progress.get(timeout=0.5)
                kind = val.get("kind")
                if kind == "begin":
                    active.add(tok)
                    seen = True
                    last_end = None
                elif kind == "report":
                    last_pct = val.get("message") or val.get("percentage")
                elif kind == "end":
                    active.discard(tok)
                    if not active:
                        last_end = ts
            except queue.Empty:
                pass
            if seen and not active and last_end and time.time() - last_end >= quiet:
                return last_end - t0, last_pct
            if not seen and time.time() - t0 > 60:
                return None, "no progress seen in 60 s"
        return None, "limit"


def find_col(text, line1, name):
    k = 1
    if "@" in name:
        name, k = name.rsplit("@", 1)
        k = int(k)
    lines = text.split("\n")
    s = lines[line1 - 1]
    pos = -1
    for _ in range(k):
        pos = s.find(name, pos + 1)
        if pos < 0:
            raise SystemExit("name %r not found %d times on line %d: %s" % (name, k, line1, s.strip()))
    return pos


def rel(root, p):
    r = p.replace("\\", "/")
    rr = root.replace("\\", "/").rstrip("/") + "/"
    if r.lower().startswith(rr.lower()):
        return r[len(rr):]
    return r


def print_locs(root, locs, cap, header):
    """locs: list of (path, line1). Prints grouped by file, capped by line count."""
    groups = {}
    for p, l in locs:
        groups.setdefault(rel(root, p), []).append(l)
    print(header)
    shown = 0
    for p in sorted(groups):
        if shown >= cap:
            print("... %d more files" % (len(groups) - shown))
            break
        ls = sorted(set(groups[p]))
        print("%s: %s" % (p, ",".join(str(x) for x in ls)))
        shown += 1


# options that take a value -> (key, type); flags -> key
VALUE_OPTS = {"--root": ("root", str), "--clangd": ("clangd", str), "--until": ("until", int), "-j": ("jobs", int),
              "--cap": ("cap", int), "--raw": ("raw", str), "--log": ("log", str), "--depth": ("depth", int)}
FLAG_OPTS = {"--index": "use_index", "--no-wait": "no_wait", "--mem": "mem", "--decl": "decl"}


def parse_args(argv):
    o = {"root": None, "clangd": None, "until": None, "jobs": 2, "cap": 60, "raw": None, "log": None, "depth": 1,
         "use_index": False, "no_wait": False, "mem": False, "decl": False, "extra_args": [], "pos": []}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in VALUE_OPTS and i + 1 < len(argv):
            key, conv = VALUE_OPTS[a]
            o[key] = conv(argv[i + 1])
            i += 2
        elif a in FLAG_OPTS:
            o[FLAG_OPTS[a]] = True
            i += 1
        elif a == "--arg" and i + 1 < len(argv):
            o["extra_args"].append(argv[i + 1])
            i += 2
        else:
            o["pos"].append(a)
            i += 1
    return o


# positional words each mode needs, the mode included (index takes an optional file)
MODE_ARGS = {"refs": 4, "callers": 4, "impl": 4, "subtypes": 4, "def": 4, "wsym": 2, "diags": 2, "inactive": 2,
             "index": 1}


def main():
    o = parse_args(sys.argv[1:])
    pos = o["pos"]
    if not pos:
        print(__doc__)
        return 2
    # checked before clangd starts, so a typo is one line, not a traceback after a clangd spawn
    if pos[0] not in MODE_ARGS:
        raise SystemExit("unknown mode %r: one of %s (no arguments prints the usage)" % (pos[0], ", ".join(MODE_ARGS)))
    if len(pos) < MODE_ARGS[pos[0]]:
        raise SystemExit("%s needs %d arguments after the mode, got %d (no arguments prints the usage)" % (
            pos[0], MODE_ARGS[pos[0]] - 1, len(pos) - 1))
    if MODE_ARGS[pos[0]] == 4 and not pos[2].isdigit():
        raise SystemExit("%s: <line> must be a 1-based line number, got %r" % (pos[0], pos[2]))
    root = o["root"] or git_toplevel(os.getcwd())
    if not root:
        raise SystemExit("not inside a git checkout: pass --root <checkout>")
    clangd = find_clangd(o["clangd"])
    use_index = o["use_index"]
    wait = not o["no_wait"]
    until = o["until"]
    cap = o["cap"]
    raw = o["raw"]
    log = o["log"]
    mem = o["mem"]
    depth = o["depth"]
    decl = o["decl"]
    if not os.path.isfile(os.path.join(root, "build", "cmake-debug", "compile_commands.json")):
        print("# warning: no build/cmake-debug/compile_commands.json under %s (run tools/build.sh build first)" % root)
    if not os.path.isfile(os.path.join(root, ".clangd")):
        print("# warning: no .clangd at %s (copy .claude/skills/caller-sweep/clangd.windows.yaml there)" % root)
    mode = pos[0]
    args = ["-j", str(o["jobs"]), "--limit-references=0", "--limit-results=0", "--log=info"]
    args.append("--background-index" if (use_index or mode == "index") else "--background-index=false")
    args += o["extra_args"]
    if log is None:
        # one file per mode, overwritten each run, under the gitignored build/ dir of the queried tree
        log = os.path.join(root, "build", "lsp_query", "clangd_%s.log" % mode)
    os.makedirs(os.path.dirname(os.path.abspath(log)), exist_ok=True)
    t_start = time.time()
    c = Client(root, clangd, args, log)
    pm = PeakMem(c.proc.pid) if mem else None
    print("# clangd pid %d" % c.proc.pid, flush=True)
    t_init = c.initialize()
    out_rc = 0
    try:
        if mode == "diags":
            return run_diags(c, root, pos[1], cap)
        if mode == "wsym":
            if use_index:
                # the CDB (and so the index) loads only when a file is opened
                if not os.path.exists(os.path.join(root, "src/app/main.cpp")):
                    raise SystemExit("wsym --index opens src/app/main.cpp to load the compile DB: not found under %s"
                                     % root)
                c.open("src/app/main.cpp")
                if wait:
                    t_idle, last = c.wait_index_idle()
                    print("# index idle after %.1f s (last progress %s)" % (t_idle or -1, last))
            res, dt, nbytes = c.request("workspace/symbol", {"query": pos[1]})
            res = res or []
            print("workspace/symbol %r: %d results, %.1f ms, raw %d B (~%d tok); spawn-to-answer %.1f s" % (
                pos[1], len(res), dt * 1000, nbytes, nbytes // 4, time.time() - t_start))
            for s in res[:cap]:
                loc = s.get("location", {})
                print("  %s%s kind=%s %s:%s" % (s.get("containerName", "") + "::" if s.get("containerName") else "",
                      s["name"], s.get("kind"), rel(root, uri_to_path(loc.get("uri", ""))),
                      loc.get("range", {}).get("start", {}).get("line", -1) + 1))
            if len(res) > cap:
                print("... %d more" % (len(res) - cap))
            return 0
        if mode == "index":
            c.open(pos[1] if len(pos) > 1 else "src/app/main.cpp")
            t_idle, last = c.wait_index_idle()
            print("# index idle after %.1f s since open (last progress %s); wall since spawn %.1f s" % (
                t_idle or -1, last, time.time() - t_start))
            return 0
        if mode == "inactive":
            path, text = c.open(pos[1])
            t0 = time.time()
            while path.replace("\\", "/").lower() not in c.inactive and time.time() - t0 < 120:
                time.sleep(0.2)
            regs = c.inactive.get(path.replace("\\", "/").lower(), [])
            print("inactive regions in %s: %d" % (pos[1], len(regs)))
            lines = text.split("\n")
            for r in regs[:cap]:
                s = r["start"]["line"]
                e = r["end"]["line"]
                # the directive line just above the region names the condition
                print("  %d-%d  (after: %s)" % (s + 1, e + 1, lines[s - 1].strip()[:90] if s > 0 else ""))
            return 0
        f, line1, name = pos[1], int(pos[2]), pos[3]
        path, text = c.open(f)
        col = find_col(text, line1, name)
        td = {"textDocument": {"uri": path_to_uri(path)}, "position": {"line": line1 - 1, "character": col}}
        if use_index and wait:
            t_idle, last = c.wait_index_idle()
            print("# index idle after %.1f s since open (last progress %s)" % (t_idle or -1, last))

        def ask():
            if mode == "refs":
                p = dict(td)
                p["context"] = {"includeDeclaration": decl}
                res, dt, nb = c.request("textDocument/references", p)
                res = res or []
                return [(uri_to_path(r["uri"]), r["range"]["start"]["line"] + 1) for r in res], res, dt, nb
            if mode in ("impl", "def"):
                meth = "textDocument/implementation" if mode == "impl" else "textDocument/definition"
                res, dt, nb = c.request(meth, td)
                res = res or []
                if isinstance(res, dict):
                    res = [res]
                return [(uri_to_path(r.get("uri") or r.get("targetUri")),
                         (r.get("range") or r.get("targetSelectionRange"))["start"]["line"] + 1) for r in res], res, dt, nb
            if mode == "callers":
                items, dt0, nb0 = c.request("textDocument/prepareCallHierarchy", td)
                items = items or []
                allres = []
                locs = []
                tot_dt = dt0
                tot_nb = nb0
                frontier = [(it, 0) for it in items]
                seen = set()
                tree = []
                while frontier:
                    it, d = frontier.pop(0)
                    key = (it["uri"], it["selectionRange"]["start"]["line"], it["name"])
                    if key in seen:
                        continue
                    seen.add(key)
                    res, dt, nb = c.request("callHierarchy/incomingCalls", {"item": it})
                    tot_dt += dt
                    tot_nb += nb
                    for call in res or []:
                        fr = call["from"]
                        allres.append(call)
                        for rg in call.get("fromRanges", []):
                            locs.append((uri_to_path(fr["uri"]), rg["start"]["line"] + 1))
                        tree.append((d, fr["name"], rel(root, uri_to_path(fr["uri"])),
                                     fr["selectionRange"]["start"]["line"] + 1,
                                     [rg["start"]["line"] + 1 for rg in call.get("fromRanges", [])], it["name"]))
                        if d + 1 < depth:
                            frontier.append((fr, d + 1))
                ask.tree = tree
                return locs, allres, tot_dt, tot_nb
            if mode == "subtypes":
                items, dt0, nb0 = c.request("textDocument/prepareTypeHierarchy", td)
                items = items or []
                tree = []
                locs = []
                tot_dt, tot_nb = dt0, nb0
                frontier = [(it, 0) for it in items]
                seen = set()
                while frontier:
                    it, d = frontier.pop(0)
                    key = (it["uri"], it["selectionRange"]["start"]["line"], it["name"])
                    if key in seen:
                        continue
                    seen.add(key)
                    res, dt, nb = c.request("typeHierarchy/subtypes", {"item": it})
                    tot_dt += dt
                    tot_nb += nb
                    for sub in res or []:
                        locs.append((uri_to_path(sub["uri"]), sub["selectionRange"]["start"]["line"] + 1))
                        tree.append((d, sub["name"], rel(root, uri_to_path(sub["uri"])),
                                     sub["selectionRange"]["start"]["line"] + 1, [], it["name"]))
                        if d + 1 < depth:
                            frontier.append((sub, d + 1))
                ask.tree = tree
                return locs, tree, tot_dt, tot_nb
            raise SystemExit("unknown mode " + mode)

        ask.tree = None
        t_q0 = time.time()
        locs, res, dt, nbytes = ask()
        tries = 1
        if until is not None:
            while len(locs) < until and time.time() - t_q0 < 300:
                time.sleep(0.5)
                locs, res, dt, nbytes = ask()
                tries += 1
        t_answer = time.time() - t_start
        if raw:
            with open(raw, "w", encoding="utf-8") as fo:
                json.dump(res, fo)
        hdr = "%s %s:%d %s -> %d locations in %d files; query %.1f ms; raw %d B (~%d tok); spawn-to-answer %.1f s; init %.2f s%s" % (
            mode, f, line1, name, len(locs), len(set(p for p, _ in locs)), dt * 1000, nbytes, nbytes // 4,
            t_answer, t_init, (" ; %d asks" % tries) if until is not None else "")
        if not use_index:
            # without --index clangd answers from the opened file only (refs then reads "0 locations"
            # for a function called all over the tree); say so on the header line.
            hdr += " ; NO INDEX (--index not given): open file only, not a tree-wide answer"
        if ask.tree is not None:
            print(hdr)
            for d, nm, p, l, rgs, parent in ask.tree[:cap]:
                print("%s%s  %s:%d  <- calls %s at %s" % ("  " * d, nm, p, l, parent, ",".join(map(str, rgs))) if mode == "callers"
                      else "%s%s  %s:%d  (sub of %s)" % ("  " * d, nm, p, l, parent))
            if len(ask.tree) > cap:
                print("... %d more" % (len(ask.tree) - cap))
        else:
            print_locs(root, locs, cap, hdr)
    finally:
        if pm:
            pm.stop = True
            r = pm.read()
            if r:
                print("# clangd working set now %.0f MB, peak %.0f MB (OS PeakWorkingSetSize), %d samples" % (
                    r[0] / 2**20, r[1] / 2**20, len(pm.samples)))
        c.shutdown()
        print("# total wall %.1f s; rx %d B" % (time.time() - t_start, c.rx_bytes))
    return out_rc


def run_diags(c, root, lst, cap):
    import collections
    items = []
    for line in open(lst, encoding="utf-8"):
        line = line.strip()
        if line:
            s, r = line.split("\t")
            items.append((s, r))
    sev_name = {1: "error", 2: "warning", 3: "info", 4: "hint"}
    by = collections.Counter()
    ex = {}
    per_file = {}
    t0 = time.time()
    batch = 8
    for b in range(0, len(items), batch):
        chunk = items[b:b + batch]
        want = {}
        for s, r in chunk:
            path, _ = c.open(r)
            want[path.replace("\\", "/").lower()] = (s, r, path)
        got = set()
        tw = time.time()
        while len(got) < len(want) and time.time() - tw < 300:
            try:
                k = c.diag_events.get(timeout=1.0)
                if k in want:
                    got.add(k)
            except queue.Empty:
                pass
        for k, (s, r, path) in want.items():
            ds = c.diags.get(k)
            if ds is None:
                per_file[r] = None
                continue
            per_file[r] = (s, ds)
            for d in ds:
                sev = sev_name.get(d.get("severity"), "?")
                code = str(d.get("code", "")) or d.get("source", "")
                key = (sev, code)
                by[key] += 1
                ex.setdefault(key, "%s:%d %s" % (r, d["range"]["start"]["line"] + 1, d["message"].split("\n")[0][:150]))
            c.close(path)
    wall = time.time() - t0
    nerr = sum(n for (s, _), n in by.items() if s == "error")
    nwarn = sum(n for (s, _), n in by.items() if s == "warning")
    files_err = [r for r, v in per_file.items() if v and any(d.get("severity") == 1 for d in v[1])]
    files_warn = [r for r, v in per_file.items() if v and any(d.get("severity") == 2 for d in v[1])]
    missing = [r for r, v in per_file.items() if v is None]
    print("diags: %d files, %.1f s; errors %d in %d files; warnings %d in %d files; no publish: %d" % (
        len(items), wall, nerr, len(files_err), nwarn, len(files_warn), len(missing)))
    for (sev, code), n in sorted(by.items(), key=lambda kv: (kv[0][0], -kv[1])):
        print("  %-7s %4d %-36s %s" % (sev, n, code, ex[(sev, code)]))
    print("files with warnings (max %d):" % cap)
    for r in sorted(files_warn)[:cap]:
        s, ds = per_file[r]
        print("  %s %s: %s" % (s, r, ", ".join(sorted(set(str(d.get("code", "")) for d in ds)))))
    if missing:
        print("no publishDiagnostics:", missing[:20])
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
