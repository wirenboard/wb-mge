#!/usr/bin/env python3
"""Aggregate Unity unittest stdout logs into a single JUnit XML report.

Walks --logs directory for *.log files written by unittests/build_unittests.mk's
RUN_% recipe, parses Unity result lines ("file:line:test:PASS|FAIL|IGNORE[: msg]"),
emits one <testsuite> per log file. ANSI color escapes from UNITY_OUTPUT_COLOR
are stripped before matching.
"""
import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from xml.dom import minidom

ANSI_RE = re.compile(r'\x1b\[[0-9;]*[a-zA-Z]')
TEST_LINE_RE = re.compile(
    r'^(?P<file>[^:\s]+\.c):(?P<line>\d+):(?P<name>[A-Za-z_]\w*):'
    r'(?P<status>PASS|FAIL|IGNORE)(?::\s*(?P<msg>.*))?$'
)


def parse_log(path):
    cases = []
    with open(path, 'r', errors='replace') as fh:
        for raw in fh:
            clean = ANSI_RE.sub('', raw).rstrip()
            m = TEST_LINE_RE.match(clean)
            if m:
                cases.append((m.group('file'), m.group('line'), m.group('name'),
                              m.group('status'), m.group('msg') or ''))
    return cases


def suite_name_from_path(log_path, logs_root):
    """Build a stable suite name from path, e.g. unittests/serial/build/test_x/test_x.log
    → serial.test_x."""
    rel = os.path.relpath(log_path, logs_root)
    parts = rel.split(os.sep)
    group = parts[0] if parts else 'unknown'
    base = os.path.splitext(os.path.basename(log_path))[0]
    return f'{group}.{base}'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', required=True, help='Path to JUnit XML to write')
    ap.add_argument('--logs', required=True, help='Root dir to search for *.log files')
    args = ap.parse_args()

    logs = sorted(glob.glob(os.path.join(args.logs, '**', '*.log'), recursive=True))
    suites = []
    total = failures = skipped = 0

    for log_path in logs:
        cases = parse_log(log_path)
        if not cases:
            continue
        suite_name = suite_name_from_path(log_path, args.logs)
        suite_el = ET.Element('testsuite', name=suite_name, tests=str(len(cases)))
        s_fail = s_skip = 0
        for src, lineno, name, status, msg in cases:
            tc = ET.SubElement(suite_el, 'testcase',
                               classname=suite_name, name=name,
                               file=src, line=lineno)
            if status == 'FAIL':
                f = ET.SubElement(tc, 'failure', message=msg or 'assertion failed',
                                  type='UnityAssertion')
                f.text = f'{src}:{lineno}: {msg}'
                s_fail += 1
            elif status == 'IGNORE':
                ET.SubElement(tc, 'skipped', message=msg or 'ignored')
                s_skip += 1
        suite_el.set('failures', str(s_fail))
        suite_el.set('errors', '0')
        suite_el.set('skipped', str(s_skip))
        suites.append(suite_el)
        total += len(cases)
        failures += s_fail
        skipped += s_skip

    root = ET.Element('testsuites', name='C unittests',
                      tests=str(total), failures=str(failures),
                      errors='0', skipped=str(skipped))
    root.extend(suites)

    os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
    pretty = minidom.parseString(ET.tostring(root, encoding='utf-8')) \
                    .toprettyxml(indent='  ', encoding='utf-8')
    with open(args.output, 'wb') as fh:
        fh.write(pretty)
    print(f'unity_to_junit: wrote {args.output} '
          f'({total} tests, {failures} failures, {skipped} skipped, '
          f'{len(suites)} suites)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
