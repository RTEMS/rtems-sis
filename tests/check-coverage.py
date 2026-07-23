#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SIS - SPARC/RISC-V instruction simulator
#
# SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG
"""Check that every graduated source file is still fully covered.

The target for this tree is 100% line and branch coverage of every simulator
source.  Until that is reached, gating on the project total would let a
finished file regress unnoticed: one file losing thirty branches moves the
total by about a tenth of a percent.

So the gate is per file.  tests/covered.txt lists the files that have reached
100%, a file is added to it by the commit that finishes it, and this script
fails if any listed file is no longer at 100% line and branch, or has dropped
out of the report altogether.  Nothing is ever removed from the list.

Run it from the top of the tree after

    ./waf configure --enable-coverage && ./waf

either against a gcovr JSON summary

    gcovr --json-summary -o summary.json
    tests/check-coverage.py summary.json

or with no argument, in which case it runs gcovr itself using gcovr.cfg.
"""

import argparse
import json
import pathlib
import subprocess
import sys

LIST = pathlib.Path(__file__).with_name('covered.txt')


def graduated(path):
    """The file names listed in covered.txt, comments and blanks dropped."""
    names = []
    for line in path.read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            names.append(line)
    return names


def summary(path):
    if path:
        return json.loads(pathlib.Path(path).read_text())
    out = subprocess.run(['gcovr', '--json-summary'],
                         check=True,
                         stdout=subprocess.PIPE,
                         text=True).stdout
    return json.loads(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('summary',
                        nargs='?',
                        help='gcovr --json-summary output, or run gcovr')
    args = parser.parse_args()

    names = graduated(LIST)
    if not names:
        print('no graduated files listed in %s' % LIST)
        return 0

    reported = {f['filename']: f for f in summary(args.summary)['files']}

    failures = []
    for name in names:
        entry = reported.get(name)
        if entry is None:
            # A file that vanishes from the report must not read as a pass:
            # a renamed or unbuilt source would silently leave the gate.
            failures.append('%s: not in the coverage report' % name)
            continue
        for metric in ('line', 'branch'):
            covered = entry['%s_covered' % metric]
            total = entry['%s_total' % metric]
            if covered != total:
                failures.append('%s: %s %d/%d, %.1f%%' %
                                (name, metric, covered, total,
                                 entry['%s_percent' % metric]))

    for failure in failures:
        print('FAIL %s' % failure, file=sys.stderr)

    if failures:
        print('\n%d of %d graduated files regressed' %
              (len({f.split(':')[0] for f in failures}), len(names)),
              file=sys.stderr)
        return 1

    print('%d graduated files still at 100%% line and branch' % len(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
