# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG

"""Run the RTEMS test executables under tests/exe through the simulator.

Each board directory under tests/exe holds the same test suite built for that
board.  A run is a success when the program prints the classic
'*** END OF TEST ...' banner, or a T-test 'Z:' summary line with a zero
failure count.

The known good status of every run lives in tests/exe/expected.txt.  A run is
gated against it:

  PASS      expected PASS and the run passed
  xfail     expected XFAIL and the run failed, as recorded
  REGRESSED expected PASS but the run failed, a new breakage
  XPASS     expected XFAIL but the run passed, promote it to PASS

The command exits non zero when any run is REGRESSED or XPASS.  Performance
numbers are read out with --perf and only reported, never gated, because SIS
timing work is expected to move them.
"""

import argparse
import concurrent.futures
import fnmatch
import os
import re
import subprocess
import sys

# Board directory name -> extra simulator flags.  This is the authoritative
# run command for each board; tests/exe/README.md documents the same mapping.
BOARDS = {
    'erc32': ['-erc32'],
    'gr712rc': ['-leon3'],
    'gr740': ['-gr740'],
    'gr740_smp': ['-gr740', '-m', '4'],
    'griscv': ['-griscv'],
    'griscv_smp': ['-griscv', '-m', '4'],
    'grv32imac': ['-griscv'],
    'leon2': ['-leon2'],
}

# A run cannot take longer than this.  crypt01 needs about two minutes on its
# own; under -j it competes for cores and slows down, so the limit is generous.
# Every other test finishes in seconds and the broken griscv runs crash at once,
# so nothing legitimate approaches it.
RUN_TIMEOUT = 600

# Heavy tests skipped by --fast.  crypt01 is CPU bound at about 108 s and built
# for every board, so it dominates the wall time; everything else runs in
# seconds.
HEAVY = {'crypt01.exe'}

Z_FAIL = re.compile(r':F:(\d+)')


def run_passed(output):
    """True if the simulator output shows a successful test run.

    A classic run passes on its '*** END OF TEST' banner.  A T-test run passes
    when it printed at least one 'Z:' summary line and none of them report a
    non zero failure count.
    """
    if '*** END OF TEST' in output:
        return True
    saw_summary = False
    for line in output.splitlines():
        if line.startswith('Z:'):
            m = Z_FAIL.search(line)
            if m:
                saw_summary = True
                if int(m.group(1)) != 0:
                    return False
    return saw_summary


def parse_expected(path):
    """Read expected.txt into a list of (board_glob, exe_glob, status)."""
    rules = []
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 3 or parts[2].upper() not in ('PASS', 'XFAIL'):
                sys.exit('%s:%d: expected "<board> <exe> <PASS|XFAIL>"'
                         % (path, lineno))
            board, exe, status = parts
            rules.append((board, exe, status.upper()))
    return rules


def expected_status(rules, board, exe):
    """Most specific matching status for a run, PASS when nothing matches."""
    best = ('PASS', -1)
    for b, e, status in rules:
        if not fnmatch.fnmatch(board, b) or not fnmatch.fnmatch(exe, e):
            continue
        # Prefer literal segments over wildcards so a specific line wins.
        score = (0 if b == '*' else 2) + (0 if e == '*' else 1)
        if score >= best[1]:
            best = (status, score)
    return best[0]


def run_one(sis, board, exe_path):
    flags = BOARDS[board]
    cmd = [sis] + flags + ['-r', exe_path]
    try:
        with open(os.devnull) as devnull:
            proc = subprocess.run(cmd, stdin=devnull,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,
                                  timeout=RUN_TIMEOUT)
        output = proc.stdout.decode('latin-1', 'replace')
    except subprocess.TimeoutExpired as e:
        out = e.stdout or b''
        output = out.decode('latin-1', 'replace') + '\n[timeout]'
    return output


def classify(expected, passed):
    if expected == 'PASS':
        return 'PASS' if passed else 'REGRESSED'
    return 'XPASS' if passed else 'xfail'


# Performance extractors.  Each returns an ordered list of (label, value) read
# out of the run output, or None when the test produced no numbers.
def perf_tmonetoone(out):
    labels = ['yield', 'event', 'self-contained binary semaphore',
              'Classic binary semaphore (FIFO)',
              'Classic binary semaphore (priority)']
    vals = []
    lines = out.splitlines()
    for label in labels:
        for i, line in enumerate(lines):
            if line.strip() == label and i + 1 < len(lines):
                m = re.match(r'a (\d+)', lines[i + 1].strip())
                if m:
                    vals.append((label, int(m.group(1))))
                break
    return vals or None


def perf_counts(out, pattern):
    vals = [(m.group(1), int(m.group(2)))
            for m in re.finditer(pattern, out)]
    return vals or None


def perf_smpatomic01(out):
    # The test reports several phases; the first per CPU count block is a
    # stable enough fingerprint.
    vals = perf_counts(out, r'processor (\d) count (\d+)')
    return vals[:4] if vals else None


def perf_smpmrsp01(out):
    return perf_counts(out, r'migrations\[(\d)\] = (\d+)')


def perf_smplock01(out):
    vals = [('sum%d' % i, int(m.group(1)))
            for i, m in enumerate(
                re.finditer(r'"sum-of-local-counter": (\d+)', out))]
    return vals[:4] if vals else None


PERF = {
    ('gr740', 'tmonetoone.exe'): perf_tmonetoone,
    ('gr740_smp', 'smpatomic01.exe'): perf_smpatomic01,
    ('gr740_smp', 'smpmrsp01.exe'): perf_smpmrsp01,
    ('gr740_smp', 'smplock01.exe'): perf_smplock01,
}


def load_perf_baseline(path):
    base = {}
    if not os.path.exists(path):
        return base
    with open(path) as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            # A label may contain spaces, so peel the value off the end.
            rest, value = line.rsplit(None, 1)
            board, exe, label = rest.split(None, 2)
            base[(board, exe, label)] = int(value)
    return base


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sis', required=True, help='path to the sis binary')
    ap.add_argument('--exedir', required=True, help='tests/exe directory')
    ap.add_argument('--expected', required=True, help='expected.txt path')
    ap.add_argument('--perf-baseline', help='perf-baseline.txt path')
    ap.add_argument('--jobs', type=int, default=1, help='parallel runs')
    ap.add_argument('--perf', action='store_true',
                    help='report performance numbers, never gates')
    ap.add_argument('--fast', action='store_true',
                    help='skip the heavy compute tests (crypt01)')
    args = ap.parse_args()

    if not os.path.exists(args.sis):
        sys.exit('%s not found, run ./waf first' % args.sis)

    rules = parse_expected(args.expected)

    jobs = []
    skipped = 0
    for board in sorted(BOARDS):
        d = os.path.join(args.exedir, board)
        if not os.path.isdir(d):
            continue
        for exe in sorted(os.listdir(d)):
            if not exe.endswith('.exe'):
                continue
            if args.fast and exe in HEAVY:
                skipped += 1
                continue
            jobs.append((board, exe, os.path.join(d, exe)))

    outputs = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, args.sis, b, p): (b, e)
                for b, e, p in jobs}
        for fut in concurrent.futures.as_completed(futs):
            outputs[futs[fut]] = fut.result()

    tally = {'PASS': 0, 'xfail': 0, 'REGRESSED': 0, 'XPASS': 0}
    bad = []
    for board, exe, _ in jobs:
        out = outputs[(board, exe)]
        expected = expected_status(rules, board, exe)
        result = classify(expected, run_passed(out))
        tally[result] += 1
        if result in ('REGRESSED', 'XPASS'):
            bad.append((board, exe, result))

    summary = ('test-run: %d PASS  %d xfail  %d REGRESSED  %d XPASS'
               % (tally['PASS'], tally['xfail'], tally['REGRESSED'],
                  tally['XPASS']))
    if skipped:
        summary += '  %d skipped (--fast)' % skipped
    print(summary)
    for board, exe, result in bad:
        print('  %-9s %-9s %s' % (result, board, exe))

    rc = 1 if bad else 0

    # The perf report never changes the gate result, so any failure in it stays
    # a warning and rc is returned untouched.
    if args.perf:
        try:
            report_perf(outputs, args.perf_baseline)
        except Exception as e:
            print('warning: perf report failed: %s' % e)

    return rc


def report_perf(outputs, baseline_path):
    base = load_perf_baseline(baseline_path) if baseline_path else {}
    print('\nperformance fingerprint (report only):')
    for (board, exe), extract in sorted(PERF.items()):
        vals = extract(outputs.get((board, exe), ''))
        if not vals:
            continue
        print('  %s %s' % (board, exe))
        for label, value in vals:
            b = base.get((board, exe, label))
            if b is None:
                print('    %-38s %10d' % (label, value))
            else:
                print('    %-38s %10d  base %10d  %+d'
                      % (label, value, b, value - b))


if __name__ == '__main__':
    sys.exit(main())
