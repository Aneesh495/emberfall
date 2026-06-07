#!/usr/bin/env python3
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
files = [root / "README.md", *sorted((root / "docs").rglob("*.md"))]
failures = []
for path in files:
    body = path.read_text()
    if chr(8212) in body:
        failures.append(f"{path.relative_to(root)} contains an em dash")
    for ref in re.findall(r"\]\(([^)\s]+)\)", body):
        if "://" in ref or ref.startswith("#"):
            continue
        target = path.parent / ref.split("#", 1)[0]
        if not target.exists():
            failures.append(f"{path.relative_to(root)} references missing {ref}")
if failures:
    raise SystemExit("\n".join(failures))
print(f"Checked {len(files)} Markdown files")
