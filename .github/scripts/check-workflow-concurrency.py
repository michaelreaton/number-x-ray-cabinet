#!/usr/bin/env python3
"""Fails when a newer GitHub Actions run could no longer cancel a superseded one.

Every CI workflow shares one concurrency group per pull request or ref with cancel-in-progress, so a
new push cancels the run it supersedes. Release, deploy and publish workflows keep their own group
without cancel-in-progress, so they are never cancelled mid-run. `if: always()` ignores cancellation
and keeps a superseded run alive; CI uses `${{ !cancelled() }}` instead. Reads only the workflow files.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NEVER_CANCEL = re.compile(r"release|deploy|publish|pages", re.I)
CI_KEY = re.compile(r"github\.workflow\s*}}.*github\.event\.pull_request\.number\s*\|\|\s*github\.ref\b")

errors = []
paths = sorted(glob.glob(os.path.join(ROOT, ".github", "workflows", "*.y*ml")))
for path in paths:
    text = open(path, encoding="utf-8").read()
    name = os.path.relpath(path, ROOT).replace(os.sep, "/")
    title = re.search(r"(?m)^name:\s*(.+)$", text)
    release = bool(NEVER_CANCEL.search(os.path.basename(path) + " " + (title.group(1) if title else "")))
    block = re.search(r"(?m)^concurrency:[ \t]*\n((?:[ \t]+.*(?:\n|$))*)", text)
    body = block.group(1) if block else ""
    group = re.search(r"(?m)^\s+group:\s*(.+)$", body)
    cancel = re.search(r"(?m)^\s+cancel-in-progress:\s*(true|false)\s*$", body)
    if release:
        if not group or not cancel or cancel.group(1) != "false":
            errors.append(f"{name}: a release, deploy or publish workflow needs its own concurrency group with 'cancel-in-progress: false' so it is never cancelled mid-run.")
        continue
    if not group or not CI_KEY.search(group.group(1)):
        errors.append(f"{name}: CI workflow needs 'concurrency: group: ${{{{ github.workflow }}}}-${{{{ github.event.pull_request.number || github.ref }}}}'.")
    if not cancel or cancel.group(1) != "true":
        errors.append(f"{name}: CI workflow needs 'cancel-in-progress: true' so a newer push cancels the superseded run.")
    for match in re.finditer(r"(?m)^\s*(?:-\s*)?if:.*\balways\(\)", text):
        line = text.count("\n", 0, match.start()) + 1
        errors.append(f"{name}:{line}: 'always()' ignores cancellation and keeps a superseded run alive; use '${{{{ !cancelled() }}}}'.")

if errors:
    print("Workflow concurrency contract violated:", *errors, sep="\n  ", file=sys.stderr)
    sys.exit(1)
print(f"Workflow concurrency contract holds across {len(paths)} workflow(s): CI cancels superseded runs; release, deploy and publish runs finish.")
