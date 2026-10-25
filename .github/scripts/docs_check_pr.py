#!/usr/bin/env python3
"""Runs tools/docs_check.py for a pull request and reports the result to GitHub Actions.

Usage: docs_check_pr.py --base <branch> [--tree]
The pull request body comes from the PR_BODY environment variable (never from the command line, so the
workflow never pastes untrusted text into a shell). The template's placeholder `Docs: none, <why>` and
HTML comments are removed first, so an unedited template does not count as a reason.

Fails (exit 1) on a critical finding or on PARITY_MISSING: a surface's code changed with none of its pages
and no `Docs: none, <why>` line in the commits or the body. Other warnings are annotations and a table in
the job summary; they do not block. Stdlib only.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
BLOCKING_WARNINGS = {"PARITY_MISSING"}
COMMENT = re.compile(r"<!--.*?-->", re.S)
PLACEHOLDER = re.compile(r"^\s*Docs:\s*none\s*,?\s*<why>\s*$", re.I | re.M)


def clean_note(body):
    """The pull request body without HTML comments or the template's unedited Docs placeholder."""
    return PLACEHOLDER.sub("", COMMENT.sub("", body or ""))


def escape_data(s):
    return s.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_prop(s):
    return escape_data(s).replace(":", "%3A").replace(",", "%2C")


def blocking(f):
    return f["severity"] == "critical" or (f["severity"] == "warning" and f["code"] in BLOCKING_WARNINGS)


def summary_markdown(report, base, failed):
    s = report["summary"]
    lines = [
        "## Docs parity",
        "",
        f"`docs_check.py` against `{base}`, mode {', '.join(report['mode']) or 'none'}: "
        f"**{s['critical']} critical, {s['warning']} warning, {s['info']} info**"
        + (f", {report['accepted']} accepted" if report.get("accepted") else "") + ".",
        "",
    ]
    codes = {f["code"] for f in report["findings"] if blocking(f)}
    if failed:
        lines += ["**Failed** on the findings marked *blocks* below. See `.agents/contracts/docs.md`.", ""]
    if "PARITY_MISSING" in codes:
        lines += ["A surface's code changed without its wiki pages. Update the pages listed, or add a line "
                  "`Docs: none, <why>` to the pull request description (or a commit message) saying why the docs "
                  "need no change.", ""]
    if report["findings"]:
        lines += ["| Severity | Code | Where | Message |", "|---|---|---|---|"]
        for f in report["findings"]:
            where = f["path"] + (f":{f['line']}" if f["line"] else "")
            msg = f["message"].replace("|", "\\|")
            sev = f["severity"] + (" (blocks)" if blocking(f) else "")
            lines.append(f"| {sev} | `{f['code']}` | `{where}` | {msg} |")
    else:
        lines.append("No findings.")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--base", required=True, help="the pull request's base branch, such as dev-2.0")
    ap.add_argument("--tree", action="store_true", help="also run the tree checks (off until the baseline is clean)")
    args = ap.parse_args(argv)

    base = f"origin/{args.base}..HEAD"
    note = clean_note(os.environ.get("PR_BODY", ""))
    cmd = [sys.executable, "-I", str(ROOT / "tools" / "docs_check.py"), "--root", str(ROOT),
           "--changed", base, "--stale", f"--note={note}", "--json"]
    if not args.tree:
        cmd.append("--no-tree")
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
    try:
        report = json.loads(r.stdout)
    except ValueError:
        print(f"::error::docs_check.py did not report (exit {r.returncode}): {escape_data(r.stderr.strip())}")
        return 1

    failed = any(blocking(f) for f in report["findings"])
    for f in report["findings"]:
        if f["severity"] == "info":
            continue
        level = "error" if blocking(f) else "warning"
        props = f"file={escape_prop(f['path'])}" + (f",line={f['line']}" if f["line"] else "")
        props += f",title={escape_prop(f['code'])}"
        print(f"::{level} {props}::{escape_data(f['message'])}")
    s = report["summary"]
    print(f"docs_check: {s['critical']} critical, {s['warning']} warning, {s['info']} info"
          + (" (blocking findings above)" if failed else ""))

    text = summary_markdown(report, base, failed)
    out = os.environ.get("GITHUB_STEP_SUMMARY")
    if out:
        with open(out, "a", encoding="utf-8") as fh:
            fh.write(text)
    else:
        print(text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
