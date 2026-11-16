#!/usr/bin/env python3
"""Audit: component vtable slot-3 (stream Serialize) wrappers that never resolve.

THE DEFECT THIS FINDS (closed 2026-09-04 in ParticleComponent, PC_SerSlot3):

  A component's slot-3 body calls the stream through file-static function
  pointers that a lazy `*_InitFuncs()` / `Resolve()` fills in, and every call is
  null-guarded:

      if (s_fn_Component_Serialize) s_fn_Component_Serialize(self, stream);
      if (s_fn_Serializer_Ctor)     s_fn_Serializer_Ctor(ser, stream, 0);
      ...

  On the SAVE leg the component has been alive for thousands of frames, so some
  other slot (Startup / Tick / Event) has long since called the resolver and the
  pointers are live: the body writes its full record.

  On the LOAD leg `InternalFactory::PersistObject` creates the object and
  dispatches vt[0x18] as the VERY FIRST call into the class. If the slot-3
  wrapper does not call the resolver itself, every pointer is null, every
  guarded call is skipped, and the body consumes ZERO bytes -- while the save
  wrote a full record. The stream is then short by exactly that record and every
  object after it deserializes from the wrong offset.

  Symmetric code over asymmetric state. It is SILENT: no crash at the defect, no
  warning, and our own save/load round trip cannot see it, because... it can:
  the two legs are NOT symmetric here, which is why a matched-pair cursor diff
  (BANISHED_SERIALIZE_TRACE=1 on both legs) is what finds it. Diff the per-
  component cursors; the first component whose LOAD delta is smaller than its
  SAVE delta is the culprit.

FIX: one line at the top of the slot-3 wrapper, matching its sibling slots.

      static void Xxx_SerializeVSlot(void* s, void* stream) {
          XxxComponent_InitFuncs();      // slot 3 is the FIRST call into this
                                         // class on a load; without this every
                                         // s_fn_* is null and the body is a no-op
          XxxComponent_Shutdown(reinterpret_cast<XxxComponent*>(s), stream);
      }

Usage:  tools/ghidra_mcp/.venv/Scripts/python.exe tools/slot3_resolver_audit.py
Exit 0 = clean, 1 = at least one wrapper reaches a guarded body with no resolver.
"""
import re, sys, glob, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = {}
for f in glob.glob(os.path.join(ROOT, 'code', 'Game', '**', '*.cpp'), recursive=True):
    if '.claude' in f:
        continue
    with open(f, encoding='utf8', errors='replace') as fh:
        SRC[f] = fh.read()

RESOLVER = re.compile(r'\b\w*(?:InitFuncs|Resolve\w*|Ensure\w*)\s*\(')
GUARDED  = re.compile(r'if\s*\(\s*!?\s*s_fn_\w+')


def body_of(name):
    """Return (file, body-text) of the first definition of `name`."""
    pat = re.compile(r'\n[A-Za-z_][\w:<>\*\s&]*\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{')
    for f, t in SRC.items():
        m = pat.search(t)
        if not m:
            continue
        i = m.end(); depth = 1; j = i
        while j < len(t) and depth:
            if t[j] == '{':
                depth += 1
            elif t[j] == '}':
                depth -= 1
            j += 1
        return f, t[i:j]
    return None, None


def slot3_targets():
    """(file, line, fn) for every `...[3] = reinterpret_cast<void*>(&fn)`."""
    out = []
    pat = re.compile(r'\[3\]\s*=\s*(?:\s*\n\s*)?reinterpret_cast<void\*>\(\s*&\s*(\w+)\s*\)')
    for f, t in SRC.items():
        for m in pat.finditer(t):
            out.append((f, t[:m.start()].count('\n') + 1, m.group(1)))
    return out


def main():
    bad, clean, manual = [], [], []
    for f, line, fn in slot3_targets():
        wf, wb = body_of(fn)
        if wb is None:
            manual.append((f, line, fn, 'wrapper body not found'))
            continue
        chain = [(fn, wb)]
        # follow a single forwarding call one level down
        for callee in re.findall(r'\b(\w+)\s*\(', wb):
            if callee in ('if', 'for', 'while', 'return', 'sizeof',
                          'reinterpret_cast', 'static_cast'):
                continue
            cf, cb = body_of(callee)
            if cb is not None and callee != fn:
                chain.append((callee, cb))
                break
        text = '\n'.join(b for _, b in chain)
        if not GUARDED.search(text):
            clean.append(fn)                      # no lazily-resolved calls: not exposed
        elif RESOLVER.search(text):
            clean.append(fn)                      # resolves on this path
        else:
            bad.append((os.path.relpath(f, ROOT), line, ' -> '.join(n for n, _ in chain)))

    if bad:
        print('SLOT-3 WRAPPERS THAT REACH A GUARDED BODY WITHOUT RESOLVING (%d):' % len(bad))
        for f, line, ch in sorted(bad):
            print('  %s:%d\n      %s' % (f, line, ch))
    else:
        print('No slot-3 wrapper reaches a guarded body without resolving.')
    print('\nnot exposed / resolves on path: %d' % len(clean))
    if manual:
        print('NEEDS A HUMAN LOOK (%d):' % len(manual))
        for f, line, fn, why in manual:
            print('  %s:%d %s -- %s' % (os.path.relpath(f, ROOT), line, fn, why))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
