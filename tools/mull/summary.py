#!/usr/bin/env python3
"""tools/mull/summary.py -- the per-module table of tools/mull/run.sh, read from Mull's SQLite reports.

usage: summary.py [--survivors] <report.sqlite> <path regex> <module> [<census.sqlite>]

One mull-runner run reports every mutant its tests reached, other modules' files included; only the
mutants whose source path matches the regex count towards the module. Killed = the tests failed,
crashed or timed out under the mutant; survived = they passed; not covered = no selected test
reaches the line. mull-runner leaves those out of its run; the census (a --dry-run
--include-not-covered report of the same binary) has every mutant, and the last column counts the
ones the run never reached. The score is killed / (killed + survived) over the covered mutants.
--survivors lists the surviving mutants, one per line.
"""
import os
import re
import sqlite3
import sys

# mull ExecutionStatus values (mull-runner 0.37): the ones that mean a test noticed the mutant.
KILLED = {'Failed', 'Crashed', 'Timedout', 'AbnormalExit'}


def status_name(code):
    names = {0: 'Invalid', 1: 'Failed', 2: 'Passed', 3: 'Timedout', 4: 'Crashed', 5: 'AbnormalExit',
             6: 'DryRun', 7: 'FailFast', 8: 'NotCovered'}
    return names.get(code, str(code))


def main():
    args = sys.argv[1:]
    survivors = False
    if args and args[0] == '--survivors':
        survivors = True
        args = args[1:]
    db, path_re, module = args[0], re.compile(args[1]), args[2]
    census = args[3] if len(args) > 3 else None
    con = sqlite3.connect(db)
    rows = con.execute('select mutator, directory, filename, line_number, column_number, '
                       'execution_status, mutation_replacement from mutant').fetchall()
    total = killed = survived = 0
    seen = set()
    lines = []
    for mutator, directory, filename, line, col, status, repl in rows:
        path = os.path.join(directory or '', filename or '')
        if not path_re.search(path):
            continue
        key = (path, line, col, mutator)
        if key in seen:
            continue
        seen.add(key)
        total += 1
        name = status_name(status)
        if name in KILLED or name == 'FailFast':
            killed += 1
        else:
            survived += 1
            rel = re.sub(r'^.*?(src/)', r'\1', path)
            lines.append(f'{rel}:{line}:{col}: {name}: {mutator} -> {repl}')
    score = 100.0 * killed / total if total else 0.0
    not_covered = '-'
    if census and os.path.exists(census):
        everything = set()
        for mutator, directory, filename, line, col in sqlite3.connect(census).execute(
                'select mutator, directory, filename, line_number, column_number from mutant'):
            path = os.path.join(directory or '', filename or '')
            if path_re.search(path):
                everything.add((path, line, col, mutator))
        not_covered = str(len(everything - seen))
    if survivors:
        print('\n'.join(sorted(lines)))
    else:
        print(f'{module:<12} {total:>8} {killed:>8} {survived:>9} {score:>7.1f}% {not_covered:>12}')


if __name__ == '__main__':
    main()
