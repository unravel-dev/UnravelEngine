#!/usr/bin/env python3
"""Judges a project audit report that `unravel-editor --project <folder> --audit <report.json>` wrote.

Exit code 0 when the report has no issue at or above --fail-on that is not ignored and, with --baseline, that the
baseline report did not already have; 1 when it has; 2 when the report is missing, unreadable or says the run failed.

    python .github/scripts/audit_check.py report.json
    python .github/scripts/audit_check.py report.json --baseline main-report.json --fail-on warning
"""

import argparse
import json
import sys

SEVERITY_RANK = {"info": 0, "warning": 1, "error": 2}


def load_report(path):
    """The report's JSON, or None with the reason printed."""
    try:
        with open(path, encoding="utf-8") as file:
            return json.load(file)
    except (OSError, ValueError) as error:
        print(f"The report {path} could not be read: {error}")
        return None


def open_issues(report, least_rank):
    """The issues that count: not ignored, at or above the least severity."""
    return [
        issue
        for issue in report.get("issues", [])
        if not issue.get("ignored", False) and SEVERITY_RANK.get(issue.get("severity"), 0) >= least_rank
    ]


def describe(issue):
    subject = issue.get("subject", {})
    where = subject.get("label") or subject.get("detail") or ""
    key = subject.get("key", "")
    place = f"{key} {where}".strip()
    line = subject.get("line", 0)
    if line:
        place += f" line {line}"
    return f"[{issue.get('severity')}] {issue.get('rule')}: {place}: {issue.get('message')}"


def main():
    parser = argparse.ArgumentParser(description="Judge a project audit report.")
    parser.add_argument("report", help="The report --audit wrote.")
    parser.add_argument("--baseline", help="An earlier report: only issues it lacks count.")
    parser.add_argument("--fail-on", choices=sorted(SEVERITY_RANK, key=SEVERITY_RANK.get), default="error",
                        help="The least severity that fails (default error).")
    args = parser.parse_args()

    report = load_report(args.report)
    if report is None:
        return 2
    if report.get("error"):
        print(f"The audit did not run: {report['error']}")
        return 2
    for note in report.get("notes", []):
        print(f"Note: {note}")

    least_rank = SEVERITY_RANK[args.fail_on]
    issues = open_issues(report, least_rank)
    if args.baseline:
        baseline = load_report(args.baseline)
        if baseline is None:
            return 2
        known = {issue.get("id") for issue in baseline.get("issues", [])}
        issues = [issue for issue in issues if issue.get("id") not in known]

    counts = report.get("counts", [0, 0, 0])
    print(f"{report.get('project', 'The project')}: {counts[0]} error(s), {counts[1]} warning(s), {counts[2]} info.")
    if not issues:
        print("No issue fails the check.")
        return 0
    print(f"{len(issues)} issue(s) fail the check:")
    for issue in issues:
        print("  " + describe(issue))
    return 1


if __name__ == "__main__":
    sys.exit(main())
