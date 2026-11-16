#!/usr/bin/env python3
"""abi_audit.py -- the Phase D.4 ABI audit (plans/ABI.md step C.1). Read-only.

    python tools/abi_audit.py                    # checks (i) and (ii) against tools/abi_audit.baseline
    python tools/abi_audit.py --list PATTERN     # every counted line of one section-(ii) pattern
    python tools/abi_audit.py --list PATTERN:guarded   # its uncounted lines (Windows-only #if branches)
    python tools/abi_audit.py --write-baseline   # rewrite the baseline from the tree (ratchet DOWN only:
                                                 #   a growth must be justified in the commit, not baselined)
    python tools/abi_audit.py --layout [-j N]    # (iii) the tail-padding candidates (needs g++ on PATH)

The tree is built by MinGW-w64 GCC, which already lays classes out by the Itanium C++ ABI, as Linux
x86-64 GCC does. What differs on Linux is the data model (`long` 8 bytes, `wchar_t` 4), the calling
convention keywords, bit-field rules and the C runtime. The 1,180 sizeof / 5,763 offsetof asserts fail
the build when such a type moves a PINNED offset; this script covers what they cannot see.

(i)  PINNED MEMBERS. Every class named in a `static_assert` that uses `sizeof(X)` or `offsetof(X, ...)`
     (plus its base classes, transitively) is a pinned layout. A non-static data member of a pinned class
     typed `long` / `unsigned long` / `long double` / `wchar_t` / `size_t` / `ptrdiff_t` / a Win32
     typedef, or declared as a bit-field, is a finding: such a member keeps its Windows size only by
     accident of the data model (a new member can be absorbed by padding the asserts never see).
     Allowed findings are listed in the baseline (`pinned` rows); any other one fails.
(ii) PATTERN COUNTS. The plans/ABI.md section-A patterns, counted as LINES (comments and string
     contents stripped first) per top-level directory, outside src/platform/win, src/render/dx11,
     src/diag/win, src/platform/darwin and src/diag/darwin (the platform backends, where they
     belong; src/diag/win is the Windows-only diagnostics directory, D4_RULES C10, and the
     darwin twins are its macOS counterpart). A line inside a Windows-only preprocessor branch
     (`#ifdef _WIN32`, `#if defined(_WIN32) && ...`, the `#else` of `#ifndef _WIN32`; likewise
     `_WIN64`, `__MINGW32__`, `__MINGW64__`, `_MSC_VER`) is Windows backend code kept beside its
     POSIX twin: it is not counted, and the number of such lines is printed. An `||` condition and
     the `#else` of an `#elif` chain are never treated as Windows-only. Fails when a (pattern,
     directory) count exceeds the baseline. A count below the baseline is reported so the next
     commit can ratchet it down.
(iii) --layout. Compiles one TU per header that pins a layout with `g++ -fsyntax-only -fdump-lang-class`
     (the INCLUDES path below; no build, no object written) and lists every class of the tree whose
     size exceeds its data size (`base size`, the Itanium dsize) AND that is a base of another class:
     where Itanium may reuse the tail padding and MSVC never does. On MinGW and Linux the rule is the
     same, so this matters for a future MSVC / clang-cl build; the documented ones carry explicit pads.

Exit status: 0 clean, 1 a check failed, 2 usage error.
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'tools', 'abi_audit.baseline')
SCAN_ROOTS = ['src', 'tests']
EXCLUDED = ('src/platform/win/', 'src/render/dx11/', 'src/diag/win/', 'src/platform/darwin/', 'src/diag/darwin/')
EXTS = ('.h', '.hpp', '.cpp', '.inl')

# ---------------------------------------------------------------------------------------------
# source reading
# ---------------------------------------------------------------------------------------------

def strip_source(text):
    """Return (no_comments, blanked): comments replaced by spaces in both (newlines kept);
    in `blanked` the contents of string / char literals are removed as well (the quotes and any
    encoding prefix stay, so `L"x"` reads `L""`)."""
    nc, bl = [], []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            nc.append(' ' * (j - i)); bl.append(' ' * (j - i)); i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            seg = re.sub(r'[^\n]', ' ', text[i:j])
            nc.append(seg); bl.append(seg); i = j
        elif c == 'R' and i + 1 < n and text[i + 1] == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_') or text[i - 1] in 'uU8L'):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if not m:
                nc.append(c); bl.append(c); i += 1; continue
            end = text.find(')' + m.group(1) + '"', i + m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            nc.append(text[i:end]); bl.append('R""' + re.sub(r'[^\n]', '', text[i:end])); i = end
        elif c in '"\'':
            # a digit separator (1'000) is not a char literal
            if c == "'" and i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum() \
                    and re.match(r'.*\b\d[\w\']*$', text[max(0, i - 20):i]):
                nc.append(c); bl.append(c); i += 1; continue
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                elif text[j] == '\n':
                    break
                j += 1
            j = min(j + 1, n)
            nc.append(text[i:j]); bl.append(c + c); i = j
        else:
            nc.append(c); bl.append(c); i += 1
    return ''.join(nc), ''.join(bl)


def iter_sources():
    for top in SCAN_ROOTS:
        for dp, dns, fns in os.walk(os.path.join(ROOT, top)):
            dns.sort()
            for fn in sorted(fns):
                if fn.endswith(EXTS):
                    p = os.path.join(dp, fn)
                    yield os.path.relpath(p, ROOT).replace('\\', '/'), p


_cache = {}
def load(rel, path):
    if rel not in _cache:
        with open(path, encoding='utf-8', errors='replace') as f:
            raw = f.read()
        _cache[rel] = strip_source(raw)
    return _cache[rel]


def bucket(rel):
    parts = rel.split('/')
    return '/'.join(parts[:2]) if parts[0] == 'src' else parts[0]

# ---------------------------------------------------------------------------------------------
# (ii) the section-A patterns
# ---------------------------------------------------------------------------------------------

WIN32_TYPEDEFS = (
    'DWORD', 'WORD', 'BYTE', 'UINT', 'ULONG', 'LONG', 'BOOL', 'HANDLE', 'HWND', 'HMODULE', 'HINSTANCE',
    'HDC', 'HICON', 'HCURSOR', 'HKEY', 'HRESULT', 'LARGE_INTEGER', 'ULARGE_INTEGER', 'LPVOID', 'LPCVOID',
    'LPSTR', 'LPCSTR', 'LPWSTR', 'LPCWSTR', 'WPARAM', 'LPARAM', 'LRESULT', 'MAX_PATH', 'CRITICAL_SECTION',
    'SRWLOCK', 'CONDITION_VARIABLE', 'INVALID_HANDLE_VALUE', 'FILETIME', 'SYSTEMTIME', 'WCHAR', 'TCHAR',
    'DWORD_PTR', 'ULONG_PTR', 'LONG_PTR', 'UINT_PTR', 'INT_PTR', 'SIZE_T', 'MSG', 'RECT', 'POINT',
)
WIN_HEADERS = (
    'windows', 'winbase', 'winuser', 'windef', 'winnt', 'wingdi', 'winreg', 'winerror', 'minwindef',
    'objbase', 'combaseapi', 'shlobj', 'shellapi', 'shlwapi', 'tlhelp32', 'dbghelp', 'psapi', 'mmsystem',
    'mmreg', 'dinput', 'xinput', 'xaudio2', 'd3d11', 'dxgi', 'd3dcompiler', 'process', 'direct', 'io',
    'winsock2', 'ws2tcpip', 'intrin',
)

def _long_lines(code):
    code = re.sub(r'\blong\s+long\b', 'LLONG', code)
    code = re.sub(r'\blong\s+double\b', 'LDOUBLE', code)
    return re.search(r'\blong\b', code) is not None

# name -> (description, test(line_blanked, line_nocomment) -> bool)
PATTERNS = collections.OrderedDict([
    ('long',          ('bare `long` / `unsigned long` (not `long long`): 4 bytes on Win64, 8 on Linux',
                       lambda b, r: _long_lines(b))),
    ('long_double',   ('`long double`', lambda b, r: re.search(r'\blong\s+double\b', b))),
    ('wchar_t',       ('`wchar_t`: 2 bytes UTF-16 on Windows, 4 bytes UTF-32 on Linux',
                       lambda b, r: re.search(r'\bwchar_t\b', b))),
    ('wide_literal',  ('`L"..."` / `L\'x\'` wide literals', lambda b, r: re.search(r'(?<![\w])L["\']', b))),
    ('callconv',      ('`__stdcall` / `__cdecl` / `WINAPI` / `CALLBACK` / `APIENTRY` / `__fastcall`',
                       lambda b, r: re.search(r'\b(__stdcall|__cdecl|__fastcall|__thiscall|WINAPI|CALLBACK|APIENTRY)\b', b))),
    ('ms_abi',        ('`ms_abi` / `sysv_abi` attributes in code', lambda b, r: re.search(r'\b(ms_abi|sysv_abi|__ms_abi__)\b', b))),
    ('declspec',      ('`__declspec(...)`', lambda b, r: re.search(r'\b__declspec\s*\(', b))),
    ('stricmp',       ('`_stricmp` / `_strnicmp` / `_wcsicmp` (no glibc twin)',
                       lambda b, r: re.search(r'\b_(str|strn|wcs|wcsn|mbs)icmp\b|\bstricmp\b', b))),
    ('wide_crt',      ('wide CRT calls (`wcslen`, `wcscmp`, `_wfopen`, ...)',
                       lambda b, r: re.search(r'\b(wcslen|wcscmp|wcsncmp|wcscpy|wcsncpy|wcscat|wcschr|wcsrchr|wcsstr|_wfopen|_wfsopen|swprintf|_snwprintf|wprintf|_wtoi|wcstol|_wsplitpath)\b', b))),
    ('win_include',   ('`#include <windows.h>` and the Win32 / DirectX header family',
                       lambda b, r: re.match(r'\s*#\s*include\s*<(' + '|'.join(WIN_HEADERS) + r')\.h>', r, re.I))),
    ('win32_typedef', ('Win32 typedefs in code (`DWORD`, `HANDLE`, `HWND`, `LONG`, `BOOL`, `HRESULT`, ...)',
                       lambda b, r: re.search(r'\b(' + '|'.join(WIN32_TYPEDEFS) + r')\b', b))),
    ('pragma_pack',   ('`#pragma pack`', lambda b, r: re.match(r'\s*#\s*pragma\s+pack\b', b))),
    ('asm_label',     ('a raw `__asm__("sym")` label not spelled through RT_ASM_LABEL (abi.h)',
                       lambda b, r: re.search(r'\b(__asm__|asm)\s*\(\s*"[A-Za-z_]', r))),
    ('va_list',       ('`va_list` (`char*` on Win64, `__va_list_tag[1]` on SysV)', lambda b, r: re.search(r'\bva_list\b', b))),
])


WIN_MACROS = ('_WIN32', '_WIN64', '__MINGW32__', '__MINGW64__', '_MSC_VER')
_WIN_NAME = r'(?:' + '|'.join(WIN_MACROS) + r')'
_WIN_TERM = r'(?:defined\s*\(\s*' + _WIN_NAME + r'\s*\)|defined\s+' + _WIN_NAME + r'\b|' + _WIN_NAME + r'\b)'
RE_PP = re.compile(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def _cond_is_win(expr):
    """True when `#if expr` can only be taken on Windows: a conjunction (no `||`) one of whose
    terms is a plain, un-negated Windows macro test."""
    if '||' in expr:
        return False
    return any(re.fullmatch(r'\s*\(?\s*' + _WIN_TERM + r'\s*\)?\s*', t) for t in expr.split('&&'))


def _cond_is_notwin(expr):
    """True when `#if expr` is exactly one negated Windows macro test, so its `#else` is Windows-only."""
    return re.fullmatch(r'\s*!\s*\(?\s*' + _WIN_TERM + r'\s*\)?\s*', expr) is not None


def windows_only_lines(nc):
    """The 1-based line numbers of `nc` (comments stripped) inside a Windows-only #if branch."""
    out, stack = set(), []          # one [branch_is_win, else_is_win] per open #if
    lines = nc.split('\n')
    i = 0
    while i < len(lines):
        first, text = i + 1, lines[i]
        while text.rstrip().endswith('\\') and i + 1 < len(lines):   # a continued directive
            i += 1
            text = text.rstrip()[:-1] + ' ' + lines[i]
        m = RE_PP.match(text)
        if m:
            kind, rest = m.group(1), m.group(2).strip()
            if kind == 'ifdef':
                stack.append([rest in WIN_MACROS, False])
            elif kind == 'ifndef':
                stack.append([False, rest in WIN_MACROS])
            elif kind == 'if':
                stack.append([_cond_is_win(rest), _cond_is_notwin(rest)])
            elif kind == 'elif' and stack:
                stack[-1] = [_cond_is_win(rest), False]
            elif kind == 'else' and stack:
                stack[-1] = [stack[-1][1], False]
            elif kind == 'endif' and stack:
                stack.pop()
        if any(e[0] for e in stack):
            out.update(range(first, i + 2))
        i += 1
    return out


def count_patterns():
    counts = collections.Counter()
    hits = collections.defaultdict(list)
    guarded = collections.Counter()
    for rel, path in iter_sources():
        if rel.startswith(EXCLUDED):
            continue
        nc, bl = load(rel, path)
        win = windows_only_lines(nc)
        b_lines, r_lines = bl.split('\n'), nc.split('\n')
        for ln, (b, r) in enumerate(zip(b_lines, r_lines), 1):
            if not b.strip():
                continue
            for name, (_, test) in PATTERNS.items():
                if test(b, r):
                    if ln in win:
                        guarded[bucket(rel)] += 1
                        hits[name + ':guarded'].append(f'{rel}:{ln}: {r.strip()}')
                        continue
                    counts[(name, bucket(rel))] += 1
                    hits[name].append(f'{rel}:{ln}: {r.strip()}')
    return counts, hits, guarded

# ---------------------------------------------------------------------------------------------
# (i) pinned members
# ---------------------------------------------------------------------------------------------

RE_CLASS = re.compile(
    r'(?<![\w])(?<!enum )(?<!enum class )(?<!enum struct )(class|struct|union)\s+'
    r'(?:alignas\s*\([^)]*\)\s*|__attribute__\s*\(\(.*?\)\)\s*|\[\[[^\]]*\]\]\s*)*'
    r'([A-Za-z_]\w*)\s*(?:final\s*)?(?::(?!:)([^;{}]*))?\{')
RE_ASSERT = re.compile(r'\bstatic_assert\s*\(')
RE_SIZEOF = re.compile(r'\bsizeof\s*\(\s*((?:::)?[A-Za-z_][\w:]*)')
RE_OFFSETOF = re.compile(r'\boffsetof\s*\(\s*((?:::)?[A-Za-z_][\w:]*)')
BUILTIN = {'int', 'char', 'short', 'long', 'float', 'double', 'bool', 'void', 'unsigned', 'signed',
           'wchar_t', 'char16_t', 'char32_t', 'size_t', 'ptrdiff_t', 'intptr_t', 'uintptr_t', 'decltype',
           'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
           'std', 'T', 'U'}
BANNED_TYPE = re.compile(r'\b(wchar_t|size_t|ptrdiff_t|' + '|'.join(WIN32_TYPEDEFS) + r')\b')


def match_close(s, i, op='{', cl='}'):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == op:
            depth += 1
        elif s[j] == cl:
            depth -= 1
            if depth == 0:
                return j
    return len(s) - 1


def class_defs():
    """name -> [(rel, line, body, bases)]"""
    defs = collections.defaultdict(list)
    for rel, path in iter_sources():
        _, bl = load(rel, path)
        for m in RE_CLASS.finditer(bl):
            ob = m.end() - 1
            cb = match_close(bl, ob)
            bases = []
            if m.group(3):
                for b in m.group(3).split(','):
                    b = re.sub(r'\b(public|private|protected|virtual)\b', '', b)
                    b = re.sub(r'<.*', '', b).strip()
                    if b:
                        bases.append(b.split('::')[-1])
            defs[m.group(2)].append((rel, bl.count('\n', 0, m.start()) + 1, bl[ob + 1:cb], bases, ob + 1))
    return defs


def asserted_names():
    """{(rel, name)}: classes a static_assert pins with sizeof / offsetof."""
    out = set()
    for rel, path in iter_sources():
        nc, _ = load(rel, path)
        for m in RE_ASSERT.finditer(nc):
            op = m.end() - 1
            arg = nc[op:match_close(nc, op, '(', ')') + 1]
            for r in (RE_SIZEOF, RE_OFFSETOF):
                for n in r.findall(arg):
                    n = n.split('::')[-1]
                    if n and n not in BUILTIN:
                        out.add((rel, n))
    return out


def member_statements(body):
    """Top-level statements of a class body. Function bodies are skipped; an ANONYMOUS nested
    struct / union is flattened (its members are this class's members); named nested types are
    separate classes."""
    stmts, buf, i, n = [], [], 0, len(body)
    while i < n:
        c = body[i]
        if c == '{':
            head = ''.join(buf)
            j = match_close(body, i)
            if re.search(r'\b(struct|union)\s*$', head.strip() + ' ') and not re.search(r'\b(struct|union)\s+\w+\s*(:[^:].*)?$', head.strip()):
                stmts.extend(member_statements(body[i + 1:j]))
                buf = []
                k = body.find(';', j)          # the anonymous member's declarator, if any
                i = (k + 1) if k >= 0 else j + 1
                continue
            if '(' in head or re.search(r'\b(struct|class|union|enum)\b', head):
                buf = []                        # a function body or a nested named type
                i = j + 1
                if i < n and body[i] == ';':
                    i += 1
                continue
            buf.append(body[i:j + 1])           # a brace initialiser: `int x{0};`
            i = j + 1
            continue
        if c == ';':
            stmts.append(''.join(buf))
            buf = []
        else:
            buf.append(c)
        i += 1
    return stmts


def data_member(stmt):
    s = stmt.strip()
    while True:
        m = re.match(r'(public|private|protected)\s*:(?!:)\s*', s)
        if not m:
            break
        s = s[m.end():]
    if not s or s.startswith('#'):
        return None
    if re.match(r'(using|typedef|friend|static|template|static_assert|enum|constexpr|inline|virtual|explicit|operator)\b', s):
        return None
    if re.match(r'(class|struct|union)\s+\w+\s*$', s):          # a forward declaration
        return None
    s = re.sub(r'\s*=.*$', '', s, flags=re.S)                    # initialiser
    s = re.sub(r'\{[^{}]*\}\s*$', '', s)
    if '(' in s:
        if re.search(r'\(\s*\*\s*\w+\s*\)', s):                  # a function-pointer member: pointer-sized
            return None
        return None                                              # a member function or a macro
    if not re.search(r'[A-Za-z_]', s):
        return None
    return ' '.join(s.split())


def resolve(defs, name, rel):
    """The definitions of `name` that `rel` can see: its own, else those in headers (a class
    defined in another .cpp is a different class that happens to share the name)."""
    ds = defs.get(name, [])
    return [d for d in ds if d[0] == rel] or [d for d in ds if not d[0].endswith('.cpp')]


def pinned_findings():
    defs = class_defs()
    asserted = asserted_names()
    todo = []
    for rel, name in sorted(asserted):
        for d in resolve(defs, name, rel):
            todo.append((name, d))
    seen, findings = set(), set()
    while todo:
        name, (rel, line, body, bases, _) = todo.pop()
        key = (rel, line, name)
        if key in seen:
            continue
        seen.add(key)
        for b in bases:
            for d in resolve(defs, b, rel):
                todo.append((b, d))
        for st in member_statements(body):
            decl = data_member(st)
            if not decl:
                continue
            why = []
            t = re.sub(r'\blong\s+long\b', 'LLONG', decl)
            if re.search(r'\blong\s+double\b', t):
                why.append('long double')
            elif re.search(r'\blong\b', t):
                why.append('long')
            m = BANNED_TYPE.search(decl)
            if m:
                why.append(m.group(1))
            if re.search(r'(?<!:):(?!:)\s*[\w()+\-* ]+$', decl):
                why.append('bit-field')
            if why:
                ids = re.findall(r'[A-Za-z_]\w*', re.sub(r'\[.*?\]', '', decl.split(':')[0] if 'bit-field' in why else decl))
                member = ids[-1] if ids else '?'
                findings.add((rel, f'{name}::{member}', '+'.join(sorted(set(why)))))
    return sorted(findings), len(seen)

# ---------------------------------------------------------------------------------------------
# baseline
# ---------------------------------------------------------------------------------------------

def read_baseline():
    counts, pinned = {}, set()
    if not os.path.exists(BASELINE):
        return counts, pinned
    for ln in open(BASELINE, encoding='utf-8'):
        ln = ln.rstrip('\r\n')
        if not ln or ln.startswith('#'):
            continue
        f = ln.split('\t')
        if f[0] == 'count' and len(f) == 4:
            counts[(f[1], f[2])] = int(f[3])
        elif f[0] == 'pinned' and len(f) == 4:
            pinned.add((f[1], f[2], f[3]))
    return counts, pinned


def write_baseline(counts, pinned):
    with open(BASELINE, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# tools/abi_audit.baseline -- written by `python tools/abi_audit.py --write-baseline`.\n')
        f.write('# The check fails when the tree exceeds a row. Lower a row when the tree falls below it;\n')
        f.write('# a row that rises needs the reason in the commit that raises it (plans/ABI.md C.1).\n')
        f.write('# pinned <file> <Class::member> <reason>: an allowed pinned-layout member (section (i)).\n')
        f.write('# count <pattern> <directory> <lines>: section (ii), outside src/platform/win, src/render/dx11, src/diag/win\n')
        f.write('#   and Windows-only #if branches (#ifdef _WIN32, __MINGW32__, ...; see the docstring).\n')
        for p in sorted(pinned):
            f.write('pinned\t' + '\t'.join(p) + '\n')
        for (pat, d), c in sorted(counts.items(), key=lambda kv: (list(PATTERNS).index(kv[0][0]), kv[0][1])):
            f.write(f'count\t{pat}\t{d}\t{c}\n')

# ---------------------------------------------------------------------------------------------
# (iii) --layout
# ---------------------------------------------------------------------------------------------

INCLUDES = ['-I.', '-Isrc/game', '-Isrc/engine', '-Isrc/render/common', '-Isrc/render/dx11', '-Isrc/app', '-Isrc/diag']
FLAGS = ['-std=c++17', '-fexceptions', '-fno-strict-aliasing', '-fno-lifetime-dse', '-w',
         '-fsyntax-only', '-fdump-lang-class']
RE_DUMP_CLASS = re.compile(r'^Class ([^\n]+)\n[ \t]+size=(\d+) align=\d+\n[ \t]+base size=(\d+) base align=\d+\n(.*?)(?=\n\n|\Z)', re.M | re.S)
RE_HIER = re.compile(r'^\s*(\S.*?) \(0x0x[0-9a-f]+\) (\d+)')


def layout(jobs):
    gxx = shutil.which('g++')
    if not gxx:
        print('--layout: g++ is not on PATH (msys2 ucrt64)'); return 2
    headers = []
    for rel, path in iter_sources():
        if not rel.endswith('.h') or not rel.startswith(('src/engine/', 'src/game/', 'src/render/')) or rel.startswith(EXCLUDED):
            continue
        nc, _ = load(rel, path)
        if re.search(r'static_assert\s*\(\s*(sizeof|offsetof)', nc):
            headers.append(rel)
    tmp = tempfile.mkdtemp(prefix='abi_layout_')
    def one(k_rel):
        k, rel = k_rel
        d = os.path.join(tmp, str(k)); os.makedirs(d)
        tu = os.path.join(d, 't.cpp')
        with open(tu, 'w') as f:
            f.write(f'#include "{rel}"\n')
        p = subprocess.run([gxx] + FLAGS + INCLUDES + ['-dumpdir', d + os.sep, tu], cwd=ROOT,
                           capture_output=True, text=True)
        dump = os.path.join(d, 't.cpp.001l.class')
        txt = open(dump, encoding='utf-8', errors='replace').read() if os.path.exists(dump) else ''
        return rel, p.returncode, txt
    classes, bases_of, failed = {}, collections.defaultdict(set), []
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            for rel, rc, txt in ex.map(one, enumerate(headers)):
                if rc != 0:
                    failed.append(rel)
                for m in RE_DUMP_CLASS.finditer(txt):
                    name = m.group(1)
                    classes[name] = (int(m.group(2)), int(m.group(3)))
                    for hl in m.group(4).split('\n')[1:]:
                        h = RE_HIER.match(hl)
                        if h and h.group(1) != name:
                            bases_of[name].add(h.group(1))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    def ours(n):
        return not n.startswith(('std::', '__gnu_cxx::', '__', 'Collection::detail::'))
    rows = []
    for derived, bs in bases_of.items():
        if not ours(derived):
            continue
        for b in bs:
            if b in classes and ours(b):
                size, dsize = classes[b]
                if size > dsize > 0:
                    rows.append((b, size, dsize, derived, classes.get(derived, (0, 0))[0]))
    print(f'--layout: {len(headers)} pinning headers compiled ({len(failed)} did not compile standalone), '
          f'{len(classes)} classes dumped')
    for rel in failed:
        print(f'  did not compile alone: {rel}')
    print(f'tail-padding candidates (base size > dsize, used as a base): {len(rows)}')
    for b, size, dsize, derived, dsz in sorted(rows):
        print(f'  {b}: size {size:#x} dsize {dsize:#x}  <- base of {derived} (size {dsz:#x})')
    used = {r[0] for r in rows}
    loose = sorted(n for n, (size, dsize) in classes.items() if ours(n) and size > dsize > 0 and n not in used)
    print(f'classes with tail padding that nothing derives from (a candidate the day one does): {len(loose)}')
    for n in loose:
        print(f'  {n}: size {classes[n][0]:#x} dsize {classes[n][1]:#x}')
    return 0

# ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--write-baseline', action='store_true')
    ap.add_argument('--list', metavar='PATTERN')
    ap.add_argument('--layout', action='store_true')
    ap.add_argument('-j', type=int, default=2)
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.layout:
        return layout(a.j)
    counts, hits, guarded = count_patterns()
    if a.list:
        base, _, suffix = a.list.partition(':')
        if base not in PATTERNS or suffix not in ('', 'guarded'):
            print('patterns: ' + ', '.join(PATTERNS) + '  (PATTERN:guarded lists its Windows-only, uncounted lines)')
            return 2
        print('\n'.join(hits[a.list])); return 0
    findings, nclasses = pinned_findings()
    if a.write_baseline:
        write_baseline(counts, set(findings))
        print(f'wrote {os.path.relpath(BASELINE, ROOT)}'); return 0
    bcounts, bpinned = read_baseline()
    fail = False
    print(f'(i) pinned layouts scanned: {nclasses} class definitions')
    for f in findings:
        tag = 'baseline' if f in bpinned else 'NEW'
        fail |= tag == 'NEW'
        print(f'  {tag:8s} {f[0]}: {f[1]} ({f[2]})')
    for f in sorted(bpinned - set(findings)):
        print(f'  GONE     {f[0]}: {f[1]} ({f[2]}) -- drop the baseline row')
    print('(ii) pattern lines outside src/platform/win, src/render/dx11 and src/diag/win (tree / baseline):')
    for pat in PATTERNS:
        dirs = sorted({d for (p, d) in list(counts) + list(bcounts) if p == pat})
        for d in dirs:
            c, b = counts.get((pat, d), 0), bcounts.get((pat, d), 0)
            mark = 'GROWTH' if c > b else ('below' if c < b else '')
            fail |= c > b
            if c or b:
                print(f'  {pat:14s} {d:16s} {c:5d} / {b:5d} {mark}')
    if guarded:
        print('  not counted, inside a Windows-only #if branch (--list PATTERN:guarded): ' +
              ', '.join(f'{d} {c}' for d, c in sorted(guarded.items())))
    print('FAIL' if fail else 'OK')
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
