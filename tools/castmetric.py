#!/usr/bin/env python3
"""castmetric.py DIR|FILE... [--by-file] [--tsv] [--detail] [--static] [--all-cstyle] [--floor]

THE B.4 cast metric (docs/history/endgame_b/b4_session/BLOCK0_BRIEF.md, inventory F): ONE
summed count of every spelling of a pointer reinterpretation in C/C++ CODE (comments and
string contents blanked), less the floor (docs/history/endgame_b/B4_RULES.md M2 / M3,
docs/history/endgame_b/b4_session/FLOOR.md):

Columns (occurrences, not lines):
  reinterpret     every `reinterpret_cast`
  cstyle_ptr      C-style cast `(T)e` to a pointer / reference / function-pointer / pointer
                  typedef, or to uintptr_t/intptr_t/size_t/64-bit int FROM a pointer
                  (null literals `(T*)0` / NULL / nullptr are not counted)
  functional      functional cast `uintptr_t(p)`, `PtrTypedef(p)`, `size_t(p)` from a pointer
  static_void     `static_cast<T*>(e)` whose operand is a `void*` (T not void)
  static_uintptr  `static_cast<uintptr_t|intptr_t|...>(e)` that is the base of an address
                  handed to a pointer cast (`(T*)(static_cast<uintptr_t>(x) + 0x10)`)
  static_cross    `static_cast<T*>(static_cast<void*>(e))` (and via `(void*)` /
                  `reinterpret_cast<void*>`): an explicit crossing through void*
  const_cast      every `const_cast`
  pun             memcpy / memmove / bit_cast of a pointer or of a raw address
                  (`&ptrVar`, `sizeof(T*)`, `p + off` operands), unions with a pointer member
  accessor        a call of an OFFSET ACCESSOR helper (a function whose body casts
                  `base + off` of its own parameters, e.g. `At<int>(self, 0x28)`, `P64(n, 0x20)`)
                  with a numeric-literal argument: a raw offset read spelled as a call
  total           the sum of the columns above: THE METRIC
  tier_i          (not in total) a counted cast of one of the three tier-I SHAPES (M3):
                  placement_new  `new (static_cast<void*>(x))`, `::new (...)`, `new ((void*)x)`
                  print_arg      a `void*` cast (any spelling) that is a whole argument of a
                                 printf-family / TRACE-style call (PRINTF_FAMILY below)
                                 (neither when x holds a `+ 0x…` offset: that is a field)
                  alloc_result   `static_cast<T*>(MemAlloc…|Allocate…(…))`, `(T*)MemAlloc…(…)`
  cast_ok         (not in total) a counted cast on a line carrying `// CAST-OK: <category>`
                  (tier R, M2; CATEGORIES below). A marked line whose cast has an operand
                  `+ 0x…` offset, whose operand is `self` / `this`, which holds an accessor call,
                  or whose category is unknown is a BAD MARKER: its casts stay in the total.
  bridge          `// B.3.x bridge` marker lines (comment text; not in total)
  The columns before `total` count what is left AFTER tier_i and cast_ok are taken out, so
  (old metric) == total + tier_i + cast_ok, per file.

Deterministic; stdlib only; no build. Paths are reported relative to the repo root and the
type index (known type names, pointer typedefs, member / return types, accessor helpers) is
always built from the whole tree (src tests), so a number
does not depend on the working directory nor on which DIRs were asked for.
--by-file   one row per file (files with a nonzero column) after the directory table
--tsv       tab-separated output (header row) instead of the aligned table
--detail    one line per counted hit: file:line:col<TAB>column<TAB>snippet
--static    the static_cast classification per directory (arith, hier, void, ...)
--all-cstyle  with --detail, also print the C-style casts that are NOT counted (cstyle_other)
--floor     the floor report (TSV sections, each headed `# section`): cast_ok per category,
            per file and category, per category and column; tier_i per shape; every BAD MARKER
            and STRAY MARKER (a CAST-OK line with no counted cast); offset-accessor definitions
            (ACCESSOR; one whose cast sits on a valid CAST-OK line is ACCESSOR CAST-OK, a
            tier-R helper such as a canary reader, and is not an offset helper)
"""
import bisect
import functools
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))  # tools/ -> the repo root
INDEX_ROOTS = ('src', 'tests')
EXTS = ('.cpp', '.h', '.hpp', '.inc', '.c')
COUNTED = ('reinterpret', 'cstyle_ptr', 'functional', 'static_void', 'static_uintptr',
           'static_cross', 'const_cast', 'pun', 'accessor')
COLS = COUNTED + ('total', 'tier_i', 'cast_ok', 'bridge')

# Tier R (B4_RULES.md M2): the categories a `// CAST-OK: <category>` marker may name.
CATEGORIES = ('alloc_header', 'ptr_int', 'crash_walk', 'win32', 'float_bits', 'string_header',
              'pe_walk', 'byte_io', 'elem_stride', 'gpu_map', 'com_iid', 'crt', 'hash', 'vslot',
              # added at the B.4 close by the audit's rulings (AUDIT.md part A, FLOOR.md section 1)
              'const_api', 'user_data', 'null_receiver', 'static_init',
              # Phase D.4 POSIX: the twin of `win32` for src/platform/posix (an OS object over
              # LockStorage bytes, dlsym's void*, the SDL_Window* behind a native handle)
              'posix')
# Tier I (M3), shape print_arg: the callees whose arguments are printf-style (a format string
# and `...`). Read from the tree: the CRT family, the variadic loggers
# (redzone_alloc.cpp RZOut / RZBoth, crash_handler_win.cpp CrashLogf, boot_state_watchdog.cpp HangLogf,
# System::String::Format / FormatIndexed) and the variadic trace macros (gameupdateloop.cpp
# TRACE, disaster_harness.cpp DLOG, market_overstock_harness.cpp MO_LOG, edittool.cpp
# REPO_DIAG, cemeterycomponent.cpp CEM_TRACE, tornadocomponent.cpp TORNADO_TRACE).
PRINTF_FAMILY = frozenset((
    'printf', 'fprintf', 'sprintf', 'snprintf', '_snprintf', 'sprintf_s', 'snprintf_s',
    'wprintf', 'fwprintf', 'swprintf', '_snwprintf', 'wsprintfA', 'wsprintfW',
    'RZOut', 'RZBoth', 'CrashLogf', 'HangLogf', 'Format', 'FormatIndexed',
    'TRACE', 'DLOG', 'MO_LOG', 'REPO_DIAG', 'CEM_TRACE', 'TORNADO_TRACE'))
_RE_MARKER = re.compile(r'CAST-OK:\s*([A-Za-z0-9_]+)')
_RE_ALLOC_CALL = re.compile(r'^(?:::)?(?:[A-Za-z_]\w*\s*(?:\(\s*\))?\s*(?:::|->|\.)\s*)*'
                            r'((?:Rt)?MemAlloc\w*|Allocate\w*|InternalMemAlloc|TypeChildAlloc|\w*OperatorNew'
                            r'|operator\s+new|malloc|realloc|calloc)\s*\(')
_RE_BAD_OPND = re.compile(r'\+\s*0[xX][0-9a-fA-F]')


def excluded(rel):
    q = '/' + rel.replace('\\', '/')
    if '/.claude/worktrees/' in q or '/tests/unit/build/' in q:
        return True
    # Third-party code carried verbatim (Intel ASSAO, MIT; src/render/dx11/ao/assao/README.md):
    # not ours to retype, so not counted. Our own AO code (ao/*.cpp, ao/assao/assao_gcc_compat.*)
    # still is.
    if '/src/render/dx11/ao/assao/' in q and not q.endswith(('assao_gcc_compat.cpp', 'assao_gcc_compat.h')):
        return True
    parts = q.split('/')[:-1]
    return any(p == 'build' or p.startswith('build-') or p.startswith('build_') for p in parts)


# ---------------------------------------------------------------- comment / string blanking
def blank(src):
    """Comments -> spaces, string/char literal CONTENTS -> spaces. Same length, same lines."""
    out = []
    i = 0
    n = len(src)
    ap = out.append
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n:
            d = src[i + 1]
            if d == '/':
                j = src.find('\n', i)
                j = n if j < 0 else j
                # a line comment continued by a trailing backslash is still a comment
                while j < n and j > 0 and src[j - 1] == '\\':
                    k = src.find('\n', j + 1)
                    j = n if k < 0 else k
                seg = src[i:j]
                ap(re.sub(r'[^\n]', ' ', seg))
                i = j
                continue
            if d == '*':
                j = src.find('*/', i + 2)
                j = n if j < 0 else j + 2
                ap(re.sub(r'[^\n]', ' ', src[i:j]))
                i = j
                continue
        if c == '"' or c == "'":
            if c == "'" and i > 0 and (src[i - 1].isalnum() or src[i - 1] == '_') and \
                    i + 1 < n and src[i + 1].isalnum():
                # digit separator 1'000 (or u8'x' prefix: handled as literal below)
                if src[i - 1].isdigit():
                    ap(c)
                    i += 1
                    continue
            if c == '"' and i > 0 and src[i - 1] == 'R':
                m = re.match(r'"([^(\s"]*)\(', src[i:i + 20])
                if m:
                    end = src.find(')' + m.group(1) + '"', i)
                    end = n if end < 0 else end + len(m.group(1)) + 2
                    seg = src[i:end]
                    ap('"' + re.sub(r'[^\n]', ' ', seg[1:-1]) + '"')
                    i = end
                    continue
            j = i + 1
            while j < n:
                e = src[j]
                if e == '\\':
                    j += 2
                    continue
                if e == c or e == '\n':
                    break
                j += 1
            j = min(j, n - 1)
            ap(c + ' ' * (j - i - 1) + (src[j] if src[j] != '\n' else '\n'))
            i = j + 1
            continue
        # fast path: copy up to the next interesting char
        m = _PLAIN.match(src, i)
        if m and m.end() > i:
            ap(m.group(0))
            i = m.end()
        else:
            ap(c)
            i += 1
    return ''.join(out)


_PLAIN = re.compile(r'[^/"\']+')

# ---------------------------------------------------------------- type vocabulary
BUILTIN = {'void', 'bool', 'char', 'short', 'int', 'long', 'float', 'double', 'signed', 'unsigned',
           'wchar_t', 'char8_t', 'char16_t', 'char32_t', '__int8', '__int16', '__int32', '__int64',
           '__int128', 'auto'}
QUAL = {'const', 'volatile', 'struct', 'class', 'union', 'enum', 'typename', '__restrict',
        '__restrict__'}
UINTPTR = {'uintptr_t', 'intptr_t', 'UINT_PTR', 'INT_PTR', 'ULONG_PTR', 'LONG_PTR', 'DWORD_PTR',
           'SIZE_T', 'LPARAM', 'WPARAM', 'LRESULT'}
WIDEINT = {'size_t', 'ptrdiff_t', 'ssize_t', 'uint64_t', 'int64_t', 'DWORD64', 'ULONG64',
           'ULONGLONG', 'LONGLONG', 'UINT64', 'INT64', 'QWORD'}
STDTYPES = {'int8_t', 'int16_t', 'int32_t', 'uint8_t', 'uint16_t', 'uint32_t', 'BYTE', 'WORD',
            'DWORD', 'BOOL', 'UINT', 'ULONG', 'LONG', 'USHORT', 'SHORT', 'INT', 'CHAR', 'WCHAR',
            'FLOAT', 'HRESULT', 'ATOM', 'va_list', 'FILE', 'jmp_buf', 'std', 'string', 'wstring',
            'nullptr_t', 'max_align_t', 'type_info', 'GUID', 'IID', 'RECT', 'POINT', 'MSG',
            'LARGE_INTEGER', 'CONTEXT', 'EXCEPTION_POINTERS', 'EXCEPTION_RECORD', 'MEMORY_BASIC_INFORMATION',
            'CRITICAL_SECTION', 'SYSTEM_INFO', 'OVERLAPPED', 'WIN32_FIND_DATAW', 'WIN32_FIND_DATAA',
            'SRWLOCK', 'CONDITION_VARIABLE', 'M128A', 'XMM_SAVE_AREA32', 'STACKFRAME64',
            'SYMBOL_INFO', 'IMAGEHLP_LINE64', 'IMAGE_DOS_HEADER', 'IMAGE_NT_HEADERS', 'IMAGE_NT_HEADERS64',
            'IMAGE_EXPORT_DIRECTORY', 'IMAGE_SECTION_HEADER', 'IMAGE_IMPORT_DESCRIPTOR',
            'IMAGE_THUNK_DATA64', 'IMAGE_IMPORT_BY_NAME', 'IMAGE_DATA_DIRECTORY', 'MODULEENTRY32',
            'THREADENTRY32', 'PROCESS_MEMORY_COUNTERS', 'BITMAPINFO', 'BITMAPINFOHEADER', 'WNDCLASSEXW',
            'WNDCLASSW', 'PAINTSTRUCT', 'DIMOUSESTATE', 'DIMOUSESTATE2', 'DIDEVICEOBJECTDATA',
            'D3D11_BUFFER_DESC', 'D3D11_TEXTURE2D_DESC', 'D3D11_SUBRESOURCE_DATA', 'D3D11_MAPPED_SUBRESOURCE',
            'DXGI_SWAP_CHAIN_DESC', 'D3D11_VIEWPORT', 'D3D11_INPUT_ELEMENT_DESC', 'LOGFONTW',
            'TEXTMETRICW', 'SIZE', 'FILETIME', 'SYSTEMTIME', 'OSVERSIONINFOW', 'OSVERSIONINFOEXW',
            'STARTUPINFOW', 'PROCESS_INFORMATION', 'SECURITY_ATTRIBUTES', 'VS_FIXEDFILEINFO',
            '__m128', '__m128i', '__m128d', 'IUnknown'} | UINTPTR | WIDEINT
PTRTYPEDEFS = {'LPVOID', 'LPCVOID', 'PVOID', 'HANDLE', 'HWND', 'HMODULE', 'HINSTANCE', 'FARPROC',
               'LPSTR', 'LPCSTR', 'LPWSTR', 'LPCWSTR', 'PBYTE', 'LPBYTE', 'HDC', 'HKEY', 'HMENU',
               'HICON', 'HCURSOR', 'HBRUSH', 'HFONT', 'HBITMAP', 'HGLOBAL', 'HLOCAL', 'HMONITOR',
               'PCONTEXT', 'PEXCEPTION_POINTERS', 'LPTHREAD_START_ROUTINE', 'PDWORD', 'LPDWORD',
               'PULONG', 'PLONG', 'PCHAR', 'PWSTR', 'PCWSTR', 'PSTR', 'PCSTR', 'LPTOP_LEVEL_EXCEPTION_FILTER',
               'PVECTORED_EXCEPTION_HANDLER', 'PIMAGE_DOS_HEADER', 'PIMAGE_NT_HEADERS', 'PIMAGE_NT_HEADERS64',
               'PIMAGE_EXPORT_DIRECTORY', 'PIMAGE_SECTION_HEADER', 'PIMAGE_IMPORT_DESCRIPTOR',
               'PIMAGE_THUNK_DATA', 'PIMAGE_THUNK_DATA64', 'PIMAGE_IMPORT_BY_NAME', 'PSYMBOL_INFO',
               'LPUNKNOWN', 'REFIID', 'LPCRITICAL_SECTION', 'LPOVERLAPPED', 'WNDPROC', 'DLGPROC',
               'LPCTSTR', 'LPTSTR', 'LPMSG', 'LPRECT', 'LPPOINT'}
NONCAST_PREV = {'sizeof', 'alignof', '_Alignof', '__alignof__', 'decltype', 'typeid', 'noexcept',
                'alignas', 'if', 'while', 'for', 'switch', 'catch', '__attribute__', '__declspec',
                'static_assert', 'operator', 'new', 'defined', '__typeof__', 'typeof', '__typeof',
                'offsetof', 'requires', 'asm', '__asm__', '__asm', 'return_type', '__has_include',
                '__builtin_offsetof', 'constexpr', 'explicit'}
CALLCONV = {'__cdecl', '__stdcall', '__fastcall', '__thiscall', '__vectorcall', 'WINAPI', 'CALLBACK',
            'APIENTRY', 'STDMETHODCALLTYPE', '__attribute__'}
EXPR_PREV = {'return', 'case', 'else', 'throw', 'co_return', 'co_yield', 'do', 'and', 'or', 'not',
             'delete', 'not_eq', 'bitand', 'bitor', 'xor', 'compl', 'co_await'}


class Layered:
    """A name set: the header-wide names plus the names of ONE translation unit's own file.
    A typedef / alias / class declared in a .cpp is local to that file (`typedef uint8_t* Elem`
    in one file must not make `Elem(n)` a cast in another)."""
    def __init__(self, glob):
        self.glob = glob
        self.local = set()

    def __contains__(self, x):
        return x in self.glob or x in self.local

    def add(self, x):
        self.local.add(x)


class Index:
    def __init__(self):
        self.types_g = set(BUILTIN) | set(STDTYPES) | set(PTRTYPEDEFS)
        self.ptr_g = set(PTRTYPEDEFS)
        self.types_f = {}     # rel -> names local to that file
        self.ptr_f = {}
        self.types = Layered(self.types_g)
        self.ptrtypedefs = Layered(self.ptr_g)
        self.members = {}     # name -> set((stars, isvoid))
        self.returns = {}     # function name -> set((stars, isvoid))
        self.accessors = set()
        self.classes = set()
        self.enums = set()
        self.decls = {}       # rel -> file_decls() of the index pass

    def set_file(self, rel):
        self.types.local = self.types_f.get(rel, set())
        self.ptrtypedefs.local = self.ptr_f.get(rel, set())


def is_header(rel):
    return rel.endswith(('.h', '.hpp', '.inc'))


_RE_TAG = re.compile(r'\b(?:class|struct|union|enum(?:\s+class|\s+struct)?)\s+(?:alignas\s*\([^)]*\)\s*)?'
                     r'(?:__attribute__\s*\(\([^)]*\)\)\s*)?([A-Za-z_]\w*)')
_RE_TYPEDEF = re.compile(r'\btypedef\b([^;{}]*?)\b([A-Za-z_]\w*)\s*(\[[^\]]*\]\s*)?;')
_RE_TYPEDEF_FN = re.compile(r'\btypedef\b[^;{}]*?\(\s*(?:__\w+\s+|\w+\s+)?\*+\s*([A-Za-z_]\w*)\s*\)\s*\(')
_RE_USING = re.compile(r'\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]*);')
_RE_TPARAM = re.compile(r'\b(?:typename|class)\s+([A-Za-z_]\w*)\s*(?=[,>=])')

# a declaration: TYPE [*&const]* NAME followed by , ; = ) [ { : (
_RE_DECL = re.compile(
    r'(?<![\w.>])(?P<type>(?:(?:const|volatile|struct|class|unsigned|signed|static|inline|constexpr|'
    r'mutable|register|extern|thread_local)\s+)*(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*'
    r'(?:\s*<[^;{}()<>]*(?:<[^;{}()<>]*>[^;{}()<>]*)*>)?(?:\s+(?:long|int|char|short|double|const))*)'
    r'(?P<decl>(?:\s*(?:\*|&&|&|\bconst\b|\bvolatile\b|__restrict\b))*)\s*(?P<name>[A-Za-z_]\w*)'
    r'\s*(?P<after>\[|[,;=){:(])')
_DECL_SKIP = {'return', 'case', 'else', 'throw', 'delete', 'new', 'goto', 'using', 'namespace',
              'typedef', 'template', 'public', 'private', 'protected', 'operator', 'sizeof',
              'co_return', 'do', 'if', 'while', 'for', 'switch', 'friend', 'virtual', 'override'}


_RE_TARGS = re.compile(r'<.*>', re.S)
_RE_IDENT = re.compile(r'[A-Za-z_]\w*')
_SPEC = QUAL | {'static', 'inline', 'constexpr', 'mutable', 'register', 'extern', 'thread_local'}


@functools.lru_cache(maxsize=None)
def core_of(typetext):
    return tuple(t for t in _RE_IDENT.findall(_RE_TARGS.sub('', typetext)) if t not in _SPEC)


def is_known_type(idx, typetext):
    toks = core_of(typetext)
    if not toks:
        return False
    if all(t in BUILTIN for t in toks):
        return True
    return toks[-1] in idx.types


def kind_of(idx, typetext, decl):
    toks = core_of(typetext)
    stars = decl.count('*')
    isvoid = bool(toks) and toks[-1] == 'void'
    if toks and toks[-1] in idx.ptrtypedefs and toks[-1] not in ('HANDLE',):
        stars += 1
    if toks and toks[-1] in ('LPVOID', 'PVOID', 'LPCVOID'):
        isvoid = True
    if toks and toks[-1] == 'auto' and stars == 0:
        return None
    return (stars, isvoid)


def index_file_types(idx, code, rel):
    if is_header(rel):
        tset, pset = idx.types_g, idx.ptr_g
    else:
        tset = idx.types_f.setdefault(rel, set())
        pset = idx.ptr_f.setdefault(rel, set())
    for m in _RE_TAG.finditer(code):
        tset.add(m.group(1))
        if not m.group(0).startswith('enum'):
            idx.classes.add(m.group(1))
    for m in _RE_TYPEDEF_FN.finditer(code):
        tset.add(m.group(1))
        pset.add(m.group(1))
    for m in _RE_TYPEDEF.finditer(code):
        body, name = m.group(1), m.group(2)
        if '(' in body:
            continue
        tset.add(name)
        if '*' in body:
            pset.add(name)
    for m in _RE_USING.finditer(code):
        name, rhs = m.group(1), m.group(2)
        tset.add(name)
        rs = rhs.strip()
        if rs.endswith('*') or re.search(r'\(\s*\*\s*\)', rs):
            pset.add(name)
    for m in _RE_TPARAM.finditer(code):
        tset.add(m.group(1))


def file_decls(idx, code):
    """[(pos, name, isarray, (stars, isvoid))] in text order; a function declaration is
    (pos, '\\0ret', name, (stars, isvoid)). build_index fills idx.members / returns from it."""
    out = []
    for m in _RE_DECL.finditer(code):
        t = m.group('type')
        name = m.group('name')
        first = t.split()[0] if t.split() else t
        if first in _DECL_SKIP or name in _DECL_SKIP or name in BUILTIN or name in QUAL:
            continue
        if not is_known_type(idx, t):
            continue
        k = kind_of(idx, t, m.group('decl'))
        if k is None:
            continue
        after = m.group('after')
        if after == '(':
            out.append((m.start('name'), '\0ret', name, k))
            continue
        out.append((m.start('name'), name, after == '[', k))
    return out


# ---------------------------------------------------------------- small parsers
def match_angle(code, i):
    """code[i] == '<' ; return index of the matching '>' (or -1)."""
    depth = 0
    par = 0
    n = len(code)
    j = i
    while j < n:
        c = code[j]
        if c == '(':
            par += 1
        elif c == ')':
            par -= 1
            if par < 0:
                return -1
        elif par == 0:
            if c == '<':
                depth += 1
            elif c == '>':
                if j > 0 and code[j - 1] == '-':
                    pass
                else:
                    depth -= 1
                    if depth == 0:
                        return j
            elif c in ';{}':
                return -1
        j += 1
    return -1


def match_paren(code, i, open_='(', close=')'):
    depth = 0
    n = len(code)
    j = i
    while j < n:
        c = code[j]
        if c == open_:
            depth += 1
        elif c == close:
            depth -= 1
            if depth == 0:
                return j
        elif c == ';' and open_ == '(' and depth <= 1 and code.startswith('for', max(0, i - 4)):
            pass
        j += 1
    return -1


_RE_TOK = re.compile(r'::|->\*?|&&|\|\||<<|>>|[<>=!]=|\+\+|--|0[xX][0-9a-fA-F\']+[uUlL]*|'
                     r'\d[\w.\']*|[A-Za-z_]\w*|\S')


def prev_token(code, i):
    """The token ending just before index i (skipping whitespace), and its start."""
    j = i - 1
    while j >= 0 and code[j] in ' \t\r\n\\':
        j -= 1
    if j < 0:
        return '', -1
    c = code[j]
    if c.isalnum() or c == '_':
        k = j
        while k >= 0 and (code[k].isalnum() or code[k] == '_'):
            k -= 1
        return code[k + 1:j + 1], k + 1
    if c == '>' and j > 0 and code[j - 1] == '-':
        return '->', j - 1
    if c == ':' and j > 0 and code[j - 1] == ':':
        return '::', j - 1
    return c, j


def parse_typeid(idx, content, assume=False):
    """Return (is_type, core_tokens, declarator) for a parenthesised content, else (False,..).
    assume=True: the text is known to be a type-id (a named cast's template argument)."""
    s = content.strip()
    if not s:
        return False, None, ''
    fnptr = False
    m = re.match(r'^(.*?)\(\s*(?:__\w+\s+|\w+\s+)?(\*+|&)\s*(?:const\s*)?\)\s*\((.*)\)\s*$', s, re.S)
    if m:
        s = m.group(1).strip()
        fnptr = True
        if not s:
            return False, None, ''
    toks = _RE_TOK.findall(s)
    i = 0
    n = len(toks)
    core = []
    while i < n and toks[i] in QUAL:
        i += 1
    # core: builtins or one qualified name with template args
    if i < n and toks[i] in BUILTIN:
        while i < n and (toks[i] in BUILTIN or toks[i] in ('const', 'volatile')):
            if toks[i] in BUILTIN:
                core.append(toks[i])
            i += 1
    else:
        if i < n and toks[i] == '::':
            i += 1
        while i < n and re.match(r'[A-Za-z_]\w*$', toks[i]) and toks[i] not in BUILTIN:
            core.append(toks[i])
            i += 1
            if i < n and toks[i] == '<':
                depth = 0
                while i < n:
                    if toks[i] == '<':
                        depth += 1
                    elif toks[i] == '>':
                        depth -= 1
                    elif toks[i] == '>>':
                        depth -= 2
                    i += 1
                    if depth <= 0:
                        break
                core.append('<>')
            if i < n and toks[i] == '::':
                i += 1
                continue
            break
        while i < n and toks[i] in ('const', 'volatile'):
            i += 1
    if not core:
        return False, None, ''
    decl = []
    while i < n and toks[i] in ('*', '&', '&&', 'const', 'volatile', '__restrict', '__restrict__'):
        decl.append(toks[i])
        i += 1
    if i != n:
        return False, None, ''
    if fnptr:
        decl.append('(*)')
    declp = ''.join(d for d in decl if d in ('*', '&', '&&', '(*)'))
    if all(c in BUILTIN for c in core):
        return True, core, declp
    if declp:
        return True, core, declp
    last = [c for c in core if c != '<>'][-1]
    return (assume or last in idx.types), core, declp


# ---------------------------------------------------------------- operand typing
class Ctx:
    def __init__(self, idx, code, decls):
        self.idx = idx
        self.code = code
        self.decls = decls
        self.byname = {}
        self.rets = {}
        for d in decls:
            if d[1] == '\0ret':
                self.rets.setdefault(d[2], set()).add(d[3])
            else:
                self.byname.setdefault(d[1], []).append(d)

    def lookup(self, name, pos):
        """(stars, isvoid) of the nearest preceding declaration of name, else member map."""
        lst = self.byname.get(name)
        if lst:
            best = None
            for d in lst:
                if d[0] < pos:
                    best = d
                else:
                    break
            if best is None:
                best = lst[0]
            st, iv = best[3]
            return (st + (1 if best[2] else 0), iv)
        return pick(self.idx.members.get(name))

    def ret(self, name):
        return pick(self.rets.get(name) or self.idx.returns.get(name))


SCALAR = BUILTIN | UINTPTR | WIDEINT | {
    'int8_t', 'int16_t', 'int32_t', 'uint8_t', 'uint16_t', 'uint32_t', 'BYTE', 'WORD', 'DWORD', 'BOOL',
    'UINT', 'ULONG', 'LONG', 'USHORT', 'SHORT', 'INT', 'CHAR', 'WCHAR', 'FLOAT', 'UINT8', 'UINT16',
    'UINT32', 'INT8', 'INT16', 'INT32', 'std'}


def pick(s):
    """One (stars, isvoid) from the set of kinds a name was declared with across the tree.
    Ambiguous names: a void* declaration wins (a static_cast<T*> of a typed pointer to the
    same name elsewhere would be a no-op), then 'some pointer', else unknown."""
    if not s:
        return None
    if len(s) == 1:
        return next(iter(s))
    if (1, True) in s:
        return (1, True)
    if all(k[0] >= 1 for k in s):
        return (min(k[0] for k in s), False)
    return None


_NULLS = {'0', 'NULL', 'nullptr', '0u', '0ull', '0ULL', '0x0', '0LL'}


def first_term(expr):
    """Strip outer parens and return the leading term of an additive/ternary expression."""
    e = expr.strip()
    while e.startswith('(') and match_paren(e, 0) == len(e) - 1:
        e = e[1:-1].strip()
    # ternary: take the true branch
    depth = 0
    for k, c in enumerate(e):
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == '?' and depth == 0:
            rest = e[k + 1:]
            d2 = 0
            for kk, cc in enumerate(rest):
                if cc in '([{':
                    d2 += 1
                elif cc in ')]}':
                    d2 -= 1
                elif cc == ':' and d2 == 0 and not rest.startswith('::', kk) and \
                        not (kk > 0 and rest[kk - 1] == ':'):
                    return first_term(rest[:kk])
            return first_term(rest)
    return e


def type_of_expr(ctx, expr, pos):
    """(stars, isvoid) of a unary/postfix expression, or None when unknown.
    Returns 'addr' marker as (1, False) for &x; literal ints as (0, False)."""
    e = first_term(expr)
    if not e:
        return None
    if re.match(r'^(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$', e):
        return (0, False)
    if e in ('nullptr', 'NULL'):
        return None
    if e.startswith('&') and not e.startswith('&&'):
        return (1, False)
    if e == 'this' or e.startswith('this ') or e == '(this)':
        return (1, False)
    if re.match(r'^(::\s*)?new\b', e):
        return (1, False)
    m = re.match(r'^(?:static_cast|reinterpret_cast|const_cast|dynamic_cast)\s*<', e)
    if m:
        j = match_angle(e, e.index('<'))
        if j > 0:
            ok, core, decl = parse_typeid(ctx.idx, e[e.index('<') + 1:j], assume=True)
            if ok:
                return (decl.count('*') + decl.count('(*)'), core[-1] == 'void' and decl == '*')
        return None
    if e.startswith('('):
        j = match_paren(e, 0)
        if j > 0:
            ok, core, decl = parse_typeid(ctx.idx, e[1:j])
            if ok and e[j + 1:].strip():
                return (decl.count('*'), core[-1] == 'void' and decl == '*')
        return None
    stars_adj = 0
    while e.startswith('*'):
        stars_adj -= 1
        e = e[1:].lstrip()
    # postfix chain a->b.c[i](...)
    m = re.match(r'^(?:::)?([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)', e)
    if not m:
        return None
    rest = e[m.end():]
    head = re.sub(r'\s+', '', m.group(1))
    name = head.split('::')[-1]
    cur = None
    if rest.lstrip().startswith('(') or rest.lstrip().startswith('<'):
        cur = ctx.ret(name)
        r = rest.lstrip()
        if r.startswith('<'):
            j = match_angle(r, 0)
            r = r[j + 1:].lstrip() if j > 0 else ''
        if r.startswith('('):
            j = match_paren(r, 0)
            rest = r[j + 1:] if j > 0 else ''
    else:
        cur = ctx.lookup(name, pos) if '::' not in head or head.startswith('this') else \
            pick(ctx.idx.members.get(name))
    while True:
        r = rest.lstrip()
        if not r:
            break
        if r.startswith('['):
            j = match_paren(r, 0, '[', ']')
            if j < 0:
                return None
            if cur is not None:
                cur = (cur[0] - 1, cur[1])
            rest = r[j + 1:]
            continue
        mm = re.match(r'^(?:->|\.)\s*([A-Za-z_]\w*)', r)
        if mm:
            s = ctx.idx.members.get(mm.group(1))
            cur = pick(s)
            rest = r[mm.end():]
            if rest.lstrip().startswith('('):
                cur = ctx.ret(mm.group(1))
                rr = rest.lstrip()
                j = match_paren(rr, 0)
                rest = rr[j + 1:] if j > 0 else ''
            continue
        break
    if cur is None:
        return None
    return (cur[0] + stars_adj, cur[1])


# ---------------------------------------------------------------- the scan
_RE_NAMED = re.compile(r'\b(reinterpret_cast|static_cast|const_cast|dynamic_cast)\s*<')
_RE_CPAREN = re.compile(r'\((?P<c>[\w\s:<>*&,]+?)\)(?=\s*[\w(*&\-+!~:"\'])')
_RE_CPAREN_FN = re.compile(r'\((?P<c>[\w\s:<>*&,]*\(\s*(?:__\w+\s+|\w+\s+)?\*+\s*\)\s*\([^()]*\)\s*)\)'
                           r'(?=\s*[\w(*&\-+!~:"\'])')
_RE_CALLNAME = re.compile(r'(?<![\w.])(?<!->)(?:std\s*::\s*)?([A-Za-z_]\w*)\s*\(')
_RE_MEMFN = re.compile(r'(?<![\w.])(?:std\s*::\s*)?(memcpy|memmove|__builtin_memcpy|__builtin_memmove|'
                       r'CopyMemory|RtlCopyMemory|bit_cast|__builtin_bit_cast)\s*(?:<[^;()]*>)?\s*\(')
_RE_BRIDGE = re.compile(r"B\.3\.[0-9A-Za-z]+(?:'s)? bridge")
_RE_UNION = re.compile(r'\bunion\b[^;{]*\{')


def split_args(s):
    out = []
    depth = 0
    cur = []
    for c in s:
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == '<':
            pass
        if c == ',' and depth == 0:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(c)
    out.append(''.join(cur))
    return out


def line_of(code, pos, starts):
    return bisect.bisect_right(starts, pos)


def operand_after(code, j):
    """Text of the unary operand that follows index j (just after a ')'): a rough extent."""
    k = j
    n = len(code)
    while k < n and code[k] in ' \t\r\n':
        k += 1
    start = k
    # unary prefixes
    while k < n and code[k] in '*&-+!~':
        k += 1
        while k < n and code[k] in ' \t':
            k += 1
    if k < n and code[k] == '(':
        e = match_paren(code, k)
        k = e + 1 if e > 0 else k + 1
    else:
        m = re.compile(r'(?:::)?[A-Za-z_0-9]\w*(?:\s*::\s*[A-Za-z_]\w*)*').match(code, k)
        if m:
            k = m.end()
    while k < n:
        m = re.compile(r'\s*(->|\.)\s*[A-Za-z_]\w*').match(code, k)
        if m and not code.startswith('...', m.start(1)):
            k = m.end()
            continue
        m = re.compile(r'\s*[\[(<]').match(code, k)
        if m:
            c = code[m.end() - 1]
            if c == '<':
                e = match_angle(code, m.end() - 1)
                if e > 0 and re.match(r'\s*\(', code[e + 1:e + 40]):
                    k = e + 1
                    continue
                break
            e = match_paren(code, m.end() - 1, c, ']' if c == '[' else ')')
            if e < 0:
                break
            k = e + 1
            continue
        break
    return code[start:k]


# ---------------------------------------------------------------- the floor (M2, M3)
def find_markers(src, code, starts):
    """{line: category} for every `// CAST-OK: <category>` in a LINE COMMENT. The marker is
    read from the raw source (the blanked code has no comments); it counts only where the
    blanked code is blank from its `//` to the end of the line, so a string literal holding
    the text is not a marker."""
    out = {}
    if 'CAST-OK:' not in src:
        return out
    for m in _RE_MARKER.finditer(src):
        ln = bisect.bisect_right(starts, m.start())
        ls = starts[ln - 1]
        le = code.find('\n', ls)
        le = len(code) if le < 0 else le
        sl = src.rfind('//', ls, m.start())
        if sl < 0 or code[sl:le].strip():
            # B.4 close: a line of a multi-line MACRO cannot end in a `//` comment (it would
            # swallow the continuation), so there the marker is a block comment directly
            # before the trailing backslash: `... /* CAST-OK: elem_stride */ \`.
            bl = src.rfind('/*', ls, m.start())
            be = src.find('*/', m.end(), le)
            if bl < 0 or be < 0 or src[be + 2:le].strip() != '\\' or code[bl:le].strip() != '\\':
                continue
        out[ln] = m.group(1)
    return out


def _skip_ws_back(code, i):
    while i >= 0 and code[i] in ' \t\r\n\\':
        i -= 1
    return i


def _skip_ws_fwd(code, i):
    n = len(code)
    while i < n and code[i] in ' \t\r\n\\':
        i += 1
    return i


def enclosing_call(code, pos):
    """Name of the call whose argument list holds pos (the identifier before the unmatched
    '('), or '' when pos is not inside a call's parentheses in this statement."""
    depth = 0
    i = pos - 1
    while i >= 0:
        c = code[i]
        if c in ')]':
            depth += 1
        elif c in '([':
            if depth == 0:
                if c == '[':
                    return ''
                j = _skip_ws_back(code, i - 1)
                k = j
                while k >= 0 and (code[k].isalnum() or code[k] == '_'):
                    k -= 1
                return code[k + 1:j + 1]
            depth -= 1
        elif c in ';{}':
            return ''
        i -= 1
    return ''


def tier_i_shape(code, pos, end, target, spelling, opnd):
    """The tier-I shape of a counted cast at code[pos:end], or None.
    target: 'void' (the cast yields void*), 'ptr' (a non-void object pointer), else None.
    spelling: 'static', 'reinterpret' or 'cstyle'."""
    if end is None or target is None:
        return None
    if target == 'void':
        if _RE_BAD_OPND.search(opnd or ''):
            return None    # `new ((void*)(s + 0x30)) T` / `(void*)(p + 0x68)`: an offset, not x
        b = _skip_ws_back(code, pos - 1)
        a = _skip_ws_fwd(code, end)
        after = code[a] if a < len(code) else ''
        before = code[b] if b >= 0 else ''
        if spelling in ('static', 'cstyle') and before == '(' and after == ')':
            j = _skip_ws_back(code, b - 1)
            if j >= 2 and code[j - 2:j + 1] == 'new' and (j < 3 or not (code[j - 3].isalnum() or
                                                                         code[j - 3] == '_')):
                return 'placement_new'
        if before in ',(' and after in ',)' and enclosing_call(code, pos) in PRINTF_FAMILY:
            return 'print_arg'
        return None
    if target == 'ptr' and spelling in ('static', 'cstyle'):
        o = opnd.strip()
        while o.startswith('(') and match_paren(o, 0) == len(o) - 1:
            o = o[1:-1].strip()
        m = _RE_ALLOC_CALL.match(o)
        if m and match_paren(o, m.end() - 1) == len(o) - 1:
            return 'alloc_result'
    return None


def bad_operand(col, opnd, cat='', line=''):
    """Why a cast on a CAST-OK line is not tier R (M2), or ''.
    B.4 close (AUDIT.md part A): for `elem_stride` ONLY, a `this` / `self` operand is allowed where
    the line views the container's own header as the erased core's header type (`ArrayHdr`)."""
    if col == 'accessor':
        return 'offset accessor call'
    if _RE_BAD_OPND.search(opnd or ''):
        return 'operand + 0x offset'
    o = (opnd or '').strip()
    while o.startswith('(') and match_paren(o, 0) == len(o) - 1:
        o = o[1:-1].strip()
    if o in ('self', 'this'):
        if cat == 'elem_stride' and 'ArrayHdr' in line:
            return ''
        return 'operand is ' + o
    return ''


FLOOR = {}   # rel -> {'ok': {(cat, col): n}, 'tier_i': {shape: n}, 'bad': [...], 'stray': [...]}


def scan(idx, rel, src, want_detail=False, all_cstyle=False, code=None):
    if code is None:
        code = blank(src)
    idx.set_file(rel)
    accessors = idx.accessors | idx.acc_f.get(rel, set())
    counts = dict.fromkeys(COUNTED + ('tier_i', 'cast_ok'), 0)
    sc = {}
    hits = []
    starts = [0]
    for m in re.finditer('\n', code):
        starts.append(m.end())
    decls = idx.decls.get(rel)
    ctx = Ctx(idx, code, decls if decls is not None else file_decls(idx, code))
    markers = find_markers(src, code, starts)
    fl = {'ok': {}, 'lines': {}, 'ok_lines': set(), 'tier_i': {}, 'bad': [], 'stray': []}
    marked = {}   # line -> [(col, pos, opnd)]

    def detail(col, pos):
        if want_detail:
            ln = line_of(code, pos, starts)
            colno = pos - starts[ln - 1] + 1
            snippet = code[pos:pos + 90].split('\n')[0].strip()
            hits.append(f'{rel}:{ln}:{colno}\t{col}\t{snippet}')

    def hit(col, pos, counted=True, end=None, target=None, spelling=None, opnd=''):
        if counted:
            shape = tier_i_shape(code, pos, end, target, spelling, opnd)
            if shape:
                counts['tier_i'] += 1
                fl['tier_i'][shape] = fl['tier_i'].get(shape, 0) + 1
                detail('tier_i:' + shape, pos)
                return
            ln = line_of(code, pos, starts)
            if ln in markers:
                marked.setdefault(ln, []).append((col, pos, opnd))
                return
            counts[col] += 1
        if counted or all_cstyle:
            detail(col, pos)

    # ---- named casts
    named_parens = set()
    ptr_operand_spans = []   # (start, end) of operands of pointer-producing casts
    static_sites = []
    for m in _RE_NAMED.finditer(code):
        kw = m.group(1)
        lt = m.end() - 1
        gt = match_angle(code, lt)
        if gt < 0:
            if kw in ('reinterpret_cast', 'const_cast'):
                hit('reinterpret' if kw == 'reinterpret_cast' else 'const_cast', m.start())
            continue
        k = gt + 1
        while k < len(code) and code[k] in ' \t\r\n':
            k += 1
        op_end = match_paren(code, k) if k < len(code) and code[k] == '(' else -1
        if op_end > 0:
            named_parens.add(k)
        target = code[lt + 1:gt]
        ok, core, decl = parse_typeid(idx, target, assume=True)
        is_ptr = ok and ('*' in decl or '(*)' in decl or (core and core[-1] in idx.ptrtypedefs))
        nopnd = code[k + 1:op_end] if op_end > 0 else ''
        nend = op_end + 1 if op_end > 0 else None
        if kw == 'reinterpret_cast':
            hit('reinterpret', m.start(), end=nend, spelling='reinterpret', opnd=nopnd,
                target='void' if ok and core == ['void'] and decl == '*' else None)
            if is_ptr and op_end > 0:
                ptr_operand_spans.append((k + 1, op_end))
        elif kw == 'const_cast':
            hit('const_cast', m.start(), opnd=nopnd)
        elif kw == 'static_cast' and op_end > 0:
            static_sites.append((m.start(), ok, core, decl, k + 1, op_end))
    # ---- C-style casts
    cstyle = []
    seen = set()
    cast_close = set()
    for rx in (_RE_CPAREN, _RE_CPAREN_FN):
        for m in rx.finditer(code):
            p = m.start()
            if p in named_parens or p in seen:
                continue
            content = m.group('c')
            if content.strip() == 'void':
                continue
            pt, pp = prev_token(code, p)
            if pt and (pt[0].isalnum() or pt[0] == '_'):
                if pt in NONCAST_PREV or (pt not in EXPR_PREV):
                    continue
            elif pt == ')':
                # `(A)(B)x`: allowed only when the group before is itself a cast
                if pp not in cast_close:
                    continue
            elif pt == ']':
                continue
            elif pt == '>':
                # template-id call f<T>(x): find the '<' and see an identifier before it
                depth = 0
                q = pp
                while q >= 0 and code[q] not in ';{}':
                    if code[q] == '>':
                        depth += 1
                    elif code[q] == '<':
                        depth -= 1
                        if depth == 0:
                            break
                    q -= 1
                if q >= 0 and code[q] == '<':
                    t2, _ = prev_token(code, q)
                    if t2 and (t2[0].isalnum() or t2[0] == '_') and t2 not in EXPR_PREV:
                        continue
            elif pt == '::' or pt == '.' or pt == '->' or pt == '~':
                continue
            ok, core, decl = parse_typeid(idx, content)
            if not ok or core[0] in CALLCONV:
                continue    # `R(__cdecl*)()` is a declarator, not a cast
            seen.add(p)
            cast_close.add(m.end() - 1)
            cstyle.append((p, m.end(), core, decl))
    for p, e, core, decl in cstyle:
        opnd = operand_after(code, e)
        oend = e + (len(code[e:e + 200]) - len(code[e:e + 200].lstrip())) + len(opnd)
        last = [c for c in core if c != '<>'][-1]
        if '*' in decl or '&' in decl or '(*)' in decl or last in idx.ptrtypedefs:
            if first_term(opnd) in _NULLS:
                hit('cstyle_null', p, counted=False)
                continue
            tvoid = core == ['void'] and decl == '*'
            if tvoid:
                # `(void*)typedPtr` is the implicit T* -> void* conversion spelled out (a %p
                # argument): not a reinterpretation. From an integer it is one.
                t = type_of_expr(ctx, opnd, p)
                if t is not None and t[0] >= 1:
                    hit('cstyle_tovoid', p, counted=False)
                    continue
            ptarget = 'void' if tvoid else ('ptr' if decl and set(decl) == {'*'} else None)
            hit('cstyle_ptr', p, end=oend, target=ptarget, spelling='cstyle', opnd=opnd)
            ptr_operand_spans.append((e, oend))
        elif last in UINTPTR or last in WIDEINT or core == ['unsigned', 'long', 'long'] or \
                core == ['long', 'long'] or core == ['unsigned', '__int64'] or core == ['__int64']:
            t = type_of_expr(ctx, opnd, p)
            if t is not None and t[0] >= 1:
                hit('cstyle_ptr', p, opnd=opnd)
            elif t is None and last in UINTPTR and first_term(opnd) not in _NULLS and \
                    not re.match(r'^[\d(]', first_term(opnd) or '0'):
                hit('cstyle_ptr', p, opnd=opnd)
            else:
                hit('cstyle_other', p, counted=False)
        else:
            hit('cstyle_other', p, counted=False)
    # ---- functional casts T(expr)
    for m in _RE_CALLNAME.finditer(code):
        name = m.group(1)
        if not (name in UINTPTR or name in WIDEINT or name in idx.ptrtypedefs):
            continue
        if name not in UINTPTR and name not in WIDEINT and name in idx.returns:
            continue    # also declared as a function somewhere: `Raw(p)` is a call
        s = m.start(1)
        pt, _ = prev_token(code, m.start())
        if pt in ('sizeof', 'typedef', 'using', 'struct', 'class', 'enum', 'operator', '(') and \
                pt != '(':
            continue
        if pt and (pt[0].isalnum() or pt[0] == '_') and pt not in EXPR_PREV and pt not in ('const',):
            continue    # `Type name(...)` is a declaration, not a cast
        op = m.end() - 1
        e = match_paren(code, op)
        if e < 0:
            continue
        inner = code[op + 1:e].strip()
        if not inner or re.match(r'^\*+\s*\w*\s*$', inner) or re.match(r'^\*', inner) and '(' in code[e:e + 3]:
            continue
        ok, _, _ = parse_typeid(idx, inner)
        if ok:
            continue    # a function type `T(void*)`
        if ',' in split_args(inner)[0] or len(split_args(inner)) > 1:
            continue
        if name in idx.ptrtypedefs:
            if first_term(inner) in _NULLS:
                continue
            hit('functional', s, opnd=inner)
            ptr_operand_spans.append((op + 1, e))
            continue
        t = type_of_expr(ctx, inner, s)
        if t is not None and t[0] >= 1:
            hit('functional', s, opnd=inner)
        elif t is None and name in UINTPTR and first_term(inner) not in _NULLS and \
                not re.match(r'^[\d(]', first_term(inner)):
            hit('functional', s, opnd=inner)
    # ---- static_cast classification
    ptr_operand_spans.sort()
    for (ms, ok, core, decl, a, b) in static_sites:
        opnd = code[a:b]
        ptd = bool(ok and not decl and core and core[-1] in idx.ptrtypedefs)
        if not ok:
            cls = 'unparsed'
        elif decl == '' and core == ['void']:
            cls = 'discard'
        elif '&' in decl and '*' not in decl:
            cls = 'hier_ref'
        elif '*' in decl or '(*)' in decl or ptd:
            # a pointer typedef target (HWND, HANDLE, a local `typedef uint8_t* Elem`) is a
            # pointer target; the void-ish ones are a T* -> void* conversion
            pointee_void = (core == ['void'] and decl == '*') or \
                (ptd and core[-1] in ('HANDLE', 'LPVOID', 'PVOID', 'LPCVOID'))
            inner = opnd.strip()
            inner_void = re.match(r'^(static_cast|reinterpret_cast)\s*<\s*(const\s+)?(volatile\s+)?void\s*'
                                  r'(const\s*)?\*\s*>|^\(\s*(const\s+)?void\s*(const\s*)?\*\s*\)', inner)
            if pointee_void:
                # a void* conversion: is it the operand of an outer static_cast<T*>?
                cls = 'to_void'
                for (ms2, ok2, core2, decl2, a2, b2) in static_sites:
                    if ms2 != ms and a2 <= ms < b2 and ok2 and '*' in decl2 and \
                            code[a2:b2].strip().startswith(code[ms:ms + 11]) and \
                            code[a2:b2].strip().startswith('static_cast'):
                        cls = 'void_inner'
                        break
            elif inner_void:
                cls = 'cross'
            else:
                pointee_builtin = all(c in SCALAR for c in core) or (decl.count('*') >= 2)
                t = type_of_expr(ctx, opnd, ms)
                if inner in _NULLS:
                    cls = 'null'
                elif re.match(r'^(::\s*)?(\w+\s*::\s*)*operator\s+new\b', inner):
                    cls = 'void'
                elif pointee_builtin:
                    cls = 'void'
                elif t is not None and t[1] and t[0] == 1:
                    cls = 'void'
                elif t is not None and t[0] >= 1:
                    cls = 'hier'
                elif re.match(r'^(::\s*)?new\b', inner) or inner.startswith('this'):
                    cls = 'hier'
                else:
                    cls = 'ptr_unknown'
        else:
            last = [c for c in core if c != '<>'][-1]
            if last in UINTPTR:
                cls = 'uintptr_arith'
                for (sa, sb) in ptr_operand_spans:
                    if sa <= ms < sb:
                        lead = code[sa:ms].strip().lstrip('(').strip()
                        if lead == '':
                            cls = 'uintptr_addr'
                        break
            elif all(c in BUILTIN for c in core) or last in STDTYPES:
                cls = 'arith'
            elif last in idx.enums or last not in idx.classes:
                cls = 'arith'
            else:
                cls = 'conv'
        sc[cls] = sc.get(cls, 0) + 1
        if cls == 'void':
            hit('static_void', ms, end=b + 1, spelling='static', opnd=opnd,
                target='ptr' if ok and decl and set(decl) == {'*'} else None)
        elif cls == 'ptr_unknown':
            hit('static_unknown', ms, counted=False)
        elif cls == 'cross':
            hit('static_cross', ms, opnd=opnd)
        elif cls == 'uintptr_addr':
            hit('static_uintptr', ms, opnd=opnd)
        else:
            hit('static:' + cls, ms, counted=False)
    # ---- memcpy / bit_cast puns
    for m in _RE_MEMFN.finditer(code):
        op = m.end() - 1
        e = match_paren(code, op)
        if e < 0:
            continue
        args = [x.strip() for x in split_args(code[op + 1:e])]
        pun = False
        if 'bit_cast' in m.group(1):
            pun = True
        else:
            for x in args[:2]:
                if re.search(r'(_cast\s*<[^>]*\*\s*>|\(\s*[\w:\s]+\*\s*\))[^,]*\+', x) or \
                        re.search(r'\+\s*(0[xX][0-9a-fA-F]+|\d+|off\w*|offset)\b', x):
                    pun = True
                if x.startswith('&') and not x.startswith('&&'):
                    t = type_of_expr(ctx, x[1:], m.start())
                    if t is not None and t[0] >= 1:
                        pun = True
            if len(args) >= 3 and re.search(r'sizeof\s*\(\s*[^()]*\*\s*\)|sizeof\s*\(\s*u?intptr_t\s*\)',
                                            args[2]):
                pun = True
        if pun:
            hit('pun', m.start(), opnd=code[op + 1:e])
    for m in _RE_UNION.finditer(code):
        e = match_paren(code, m.end() - 1, '{', '}')
        if e > 0 and '*' in code[m.end():e]:
            hit('pun', m.start())
    # ---- offset accessor calls
    if accessors:
        for m in _RE_CALLNAME.finditer(code):
            name = m.group(1)
            if name not in accessors:
                continue
            # skip the definitions themselves
            pt, _ = prev_token(code, m.start(1))
            if pt and (pt[0].isalnum() or pt[0] in '_*&') and pt not in EXPR_PREV:
                continue
            op = m.end() - 1
            e = match_paren(code, op)
            if e < 0:
                continue
            args = [x.strip() for x in split_args(code[op + 1:e])]
            if len(args) >= 2 and any(re.match(r'^-?(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$', x) for x in args[1:]):
                hit('accessor', m.start(1))
    # also template-id accessor calls At<int>(self, 0x28)
    if accessors:
        for m in re.finditer(r'(?<![\w.])(?<!->)([A-Za-z_]\w*)\s*<', code):
            name = m.group(1)
            if name not in accessors:
                continue
            gt = match_angle(code, m.end() - 1)
            if gt < 0:
                continue
            k = gt + 1
            while k < len(code) and code[k] in ' \t':
                k += 1
            if k >= len(code) or code[k] != '(':
                continue
            pt, _ = prev_token(code, m.start(1))
            if pt and (pt[0].isalnum() or pt[0] in '_*&') and pt not in EXPR_PREV:
                continue
            e = match_paren(code, k)
            if e < 0:
                continue
            args = [x.strip() for x in split_args(code[k + 1:e])]
            if len(args) >= 2 and any(re.match(r'^-?(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$', x) for x in args[1:]):
                hit('accessor', m.start(1))
    # ---- CAST-OK lines: tier R, or a BAD MARKER whose casts stay in the total
    _srclines = src.split('\n')
    for ln in sorted(markers):
        cat = markers[ln]
        lst = marked.get(ln)
        if not lst:
            fl['stray'].append((ln, cat))
            continue
        why = '' if cat in CATEGORIES else 'unknown category ' + cat
        for col, pos, opnd in lst:
            why = why or bad_operand(col, opnd, cat, _srclines[ln - 1] if 0 < ln <= len(_srclines) else '')
        for col, pos, opnd in sorted(lst, key=lambda x: x[1]):
            if why:
                counts[col] += 1
                detail(col, pos)
            else:
                counts['cast_ok'] += 1
                fl['ok'][(cat, col)] = fl['ok'].get((cat, col), 0) + 1
                detail('cast_ok:' + cat, pos)
        if why:
            fl['bad'].append((ln, cat, why))
        else:
            fl['lines'][cat] = fl['lines'].get(cat, 0) + 1
            fl['ok_lines'].add(ln)
    if want_detail:
        hits.sort(key=lambda h: (int(h.split('\t')[0].rsplit(':', 2)[1]),
                                 int(h.split('\t')[0].rsplit(':', 2)[2])))
    FLOOR[rel] = fl
    counts['total'] = sum(counts[c] for c in COUNTED)
    counts['bridge'] = sum(1 for ln in src.split('\n') if _RE_BRIDGE.search(ln))
    return counts, sc, hits


# ---------------------------------------------------------------- the index pass
_RE_ENUM = re.compile(r'\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)')
_RE_ACCESSOR_DEF = re.compile(
    r'(?<![\w.])([A-Za-z_]\w*)\s*\(\s*((?:const\s+)?[\w:]+\s*[*&]?\s*[A-Za-z_]\w*\s*,\s*(?:const\s+)?'
    r'[\w:]+\s+[A-Za-z_]\w*)\s*\)\s*(?:const\s*)?(?:noexcept\s*)?\{')


def build_index(root, files_code):
    idx = Index()
    idx.enums = set()
    idx.acc_f = {}
    idx.acc_defs = []    # (rel, name, (first line, last line)) of every offset-accessor definition
    for rel, code in files_code:
        index_file_types(idx, code, rel)
        for m in _RE_ENUM.finditer(code):
            idx.enums.add(m.group(1))
    defs = []    # (rel, name, base_param, off_param, body)
    for rel, code in files_code:
        idx.set_file(rel)
        idx.decls[rel] = file_decls(idx, code)
        for d in idx.decls[rel]:
            if d[1] == '\0ret':
                idx.returns.setdefault(d[2], set()).add(d[3])
            else:
                st, iv = d[3]
                idx.members.setdefault(d[1], set()).add((st + (1 if d[2] else 0), iv))
        for m in _RE_ACCESSOR_DEF.finditer(code):
            name = m.group(1)
            if name in NONCAST_PREV or name in EXPR_PREV or name in BUILTIN:
                continue
            if prev_token(code, m.start(1))[0] in ('bool', 'void'):
                continue    # `bool Readable(p, n)`: a check, not a field read
            params = [p.strip() for p in m.group(2).split(',')]
            pn = [re.findall(r'[A-Za-z_]\w*', p)[-1] for p in params]
            e = match_paren(code, m.end() - 1, '{', '}')
            if e < 0 or e - m.end() > 400:
                continue
            defs.append((rel, name, pn[0], pn[1], code[m.end():e],
                         (code.count('\n', 0, m.start()) + 1, code.count('\n', 0, e) + 1)))
    # an accessor casts `base + off` of its own parameters, or forwards both to an accessor
    changed = True
    while changed:
        changed = False
        for rel, name, pb, po, body, span in defs:
            acc = idx.accessors if is_header(rel) else idx.acc_f.setdefault(rel, set())
            if name in acc:
                continue
            if not re.search(r'\b' + re.escape(pb) + r'\b', body):
                continue
            direct = re.search(r'\+\s*' + re.escape(po) + r'\b', body) and \
                re.search(r'reinterpret_cast|static_cast\s*<[^>]*\*|\(\s*[\w:\s]+\*\s*\)', body)
            known = idx.accessors | idx.acc_f.get(rel, set())
            fwd = any(k in body and re.search(r'\b' + re.escape(k) + r'\s*(<[^;]*?>)?\s*\([^;]*\b' + re.escape(po) + r'\b', body)
                      for k in known if k != name)
            if direct or fwd:
                acc.add(name)
                idx.acc_defs.append((rel, name, span))
                changed = True
    return idx


def walk(root, arg):
    base = arg if os.path.isabs(arg) else (arg if os.path.exists(arg) else os.path.join(root, arg))
    base = os.path.normpath(os.path.abspath(base))
    if os.path.isfile(base):
        paths = [base]
    else:
        paths = []
        for dp, dn, fs in os.walk(base):
            dn.sort()
            for f in fs:
                paths.append(os.path.join(dp, f))
    out = []
    for p in sorted(paths):
        rel = os.path.relpath(p, root).replace('\\', '/')
        if not rel.endswith(EXTS) or excluded(rel):
            continue
        out.append((rel, p))
    return out


_CACHE = {}


def read(p):
    try:
        with open(p, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def load_index():
    files = []
    for r in INDEX_ROOTS:
        if os.path.isdir(os.path.join(ROOT, r)):
            files.extend(walk(ROOT, os.path.join(ROOT, r)))
    fc = []
    for rel, p in files:
        src = read(p)
        code = blank(src)
        _CACHE[rel] = (src, code)
        fc.append((rel, code))
    return build_index(ROOT, fc)


INDEX = [None]


def run(dirs, want_detail=False, all_cstyle=False):
    idx = load_index()
    INDEX[0] = idx
    files = []
    seen = set()
    for d in dirs:
        for rel, p in walk(ROOT, d):
            if rel not in seen:
                seen.add(rel)
                files.append((rel, p))
    files.sort()
    rows = []
    for rel, p in files:
        if rel in _CACHE:
            src, code = _CACHE[rel]
        else:
            src = read(p)
            code = blank(src)
            index_file_types(idx, code, rel + '.local')
            idx.types_f[rel] = idx.types_f.pop(rel + '.local', set())
            idx.ptr_f[rel] = idx.ptr_f.pop(rel + '.local', set())
        counts, sc, hits = scan(idx, rel, src, want_detail, all_cstyle, code)
        rows.append((rel, counts, sc, hits))
    return rows


def main(argv):
    flags = {a for a in argv if a.startswith('--')}
    dirs = [a for a in argv if not a.startswith('--')]
    if not dirs:
        print(__doc__)
        return 2
    rows = run(dirs, '--detail' in flags, '--all-cstyle' in flags)
    tsv = '--tsv' in flags
    if '--detail' in flags:
        for rel, c, sc, hits in rows:
            for h in hits:
                print(h)
        return 0
    bydir = {}
    for rel, c, sc, hits in rows:
        d = rel.rsplit('/', 1)[0] if '/' in rel else '.'
        agg = bydir.setdefault(d, [0, dict.fromkeys(COLS, 0), {}])
        agg[0] += 1
        for k in COLS:
            agg[1][k] += c[k]
        for k, v in sc.items():
            agg[2][k] = agg[2].get(k, 0) + v
    if '--static' in flags:
        classes = ('arith', 'conv', 'hier', 'hier_ref', 'to_void', 'void_inner', 'void', 'cross',
                   'ptr_unknown', 'null', 'uintptr_arith', 'uintptr_addr', 'discard', 'unparsed')
        hdr = ['dir', 'static_cast'] + list(classes)
        print('\t'.join(hdr) if tsv else f'{"dir":<40}' + ' '.join(f'{h[:13]:>13}' for h in hdr[1:]))
        tot = dict.fromkeys(classes, 0)
        for d in sorted(bydir):
            s = bydir[d][2]
            vals = [s.get(k, 0) for k in classes]
            for k in classes:
                tot[k] += s.get(k, 0)
            line = [d, str(sum(vals))] + [str(v) for v in vals]
            print('\t'.join(line) if tsv else f'{d:<40}' + ' '.join(f'{v:>13}' for v in line[1:]))
        vals = [tot[k] for k in classes]
        line = ['TOTAL', str(sum(vals))] + [str(v) for v in vals]
        print('\t'.join(line) if tsv else f'{"TOTAL":<40}' + ' '.join(f'{v:>13}' for v in line[1:]))
        return 0
    hdr = ['dir', 'files'] + list(COLS)
    w = max([len(d) for d in bydir] + [5])
    if tsv:
        print('\t'.join(hdr))
    else:
        print(f'{"dir":<{w}} {"files":>5} ' + ' '.join(f'{c[:14]:>14}' for c in COLS))
    tot = dict.fromkeys(COLS, 0)
    nf = 0
    for d in sorted(bydir):
        n, c, _ = bydir[d]
        nf += n
        for k in COLS:
            tot[k] += c[k]
        if tsv:
            print('\t'.join([d, str(n)] + [str(c[k]) for k in COLS]))
        else:
            print(f'{d:<{w}} {n:>5} ' + ' '.join(f'{c[k]:>14}' for k in COLS))
    if tsv:
        print('\t'.join(['TOTAL', str(nf)] + [str(tot[k]) for k in COLS]))
    else:
        print(f'{"TOTAL":<{w}} {nf:>5} ' + ' '.join(f'{tot[k]:>14}' for k in COLS))
    if '--by-file' in flags:
        print()
        if tsv:
            print('\t'.join(['file'] + list(COLS)))
        for rel, c, sc, hits in rows:
            if any(c[k] for k in COLS):
                if tsv:
                    print('\t'.join([rel] + [str(c[k]) for k in COLS]))
                else:
                    print(f'{rel} ' + ' '.join(f'{k}={c[k]}' for k in COLS if c[k]))
    if '--floor' in flags:
        print_floor([r[0] for r in rows])
    print(f'# castmetric total: {tot["total"]}')
    return 0


def print_floor(rels):
    """The floor report over the scanned files (TSV sections)."""
    percat, lines, bycol, byfile, shapes, bad, stray = {}, {}, {}, [], {}, [], []
    for rel in rels:
        fl = FLOOR.get(rel)
        if not fl:
            continue
        fc = {}
        for (cat, col), n in fl['ok'].items():
            percat[cat] = percat.get(cat, 0) + n
            bycol[(cat, col)] = bycol.get((cat, col), 0) + n
            fc[cat] = fc.get(cat, 0) + n
        for cat, n in fl['lines'].items():
            lines[cat] = lines.get(cat, 0) + n
        for cat in sorted(fc):
            byfile.append((rel, cat, fc[cat]))
        for s, n in fl['tier_i'].items():
            shapes[s] = shapes.get(s, 0) + n
        bad.extend((rel, ln, cat, why) for ln, cat, why in fl['bad'])
        stray.extend((rel, ln, cat) for ln, cat in fl['stray'])
    # An accessor whose cast sits on a valid CAST-OK line is a tier-R helper (a boundary-tag
    # or canary reader), not an offset helper: it is listed apart and not counted.
    acc, acc_ok = set(), set()
    for r, n, (a, b) in (INDEX[0].acc_defs if INDEX[0] else []):
        if r not in FLOOR:
            continue
        ok_lines = FLOOR[r]['ok_lines']
        (acc_ok if any(a <= x <= b for x in ok_lines) else acc).add((r, n))
    acc, acc_ok = sorted(acc), sorted(acc_ok)
    print()
    print('# floor: cast_ok per category')
    print('category\tcasts\tlines')
    for cat in sorted(set(percat) | set(lines), key=lambda c: (CATEGORIES.index(c) if c in CATEGORIES else 99, c)):
        print(f'{cat}\t{percat.get(cat, 0)}\t{lines.get(cat, 0)}')
    print(f'TOTAL\t{sum(percat.values())}\t{sum(lines.values())}')
    print('# floor: cast_ok per file and category')
    print('file\tcategory\tcasts')
    for rel, cat, n in byfile:
        print(f'{rel}\t{cat}\t{n}')
    print('# floor: cast_ok per category and column')
    print('category\tcolumn\tcasts')
    for (cat, col) in sorted(bycol):
        print(f'{cat}\t{col}\t{bycol[(cat, col)]}')
    print('# floor: tier_i per shape')
    print('shape\tcasts')
    for s in ('placement_new', 'print_arg', 'alloc_result'):
        print(f'{s}\t{shapes.get(s, 0)}')
    print('# floor: BAD MARKER (the line\'s casts stay in the total)')
    for rel, ln, cat, why in bad:
        print(f'BAD MARKER\t{rel}:{ln}\t{cat}\t{why}')
    print('# floor: STRAY MARKER (a CAST-OK line with no counted cast)')
    for rel, ln, cat in stray:
        print(f'STRAY MARKER\t{rel}:{ln}\t{cat}')
    print('# floor: offset-accessor definitions')
    for rel, name in acc:
        print(f'ACCESSOR\t{rel}\t{name}')
    for rel, name in acc_ok:
        print(f'ACCESSOR CAST-OK\t{rel}\t{name}')
    print(f'# floor summary: cast_ok={sum(percat.values())} tier_i={sum(shapes.values())} '
          f'bad_markers={len(bad)} stray_markers={len(stray)} accessor_defs={len(acc)}')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
