#!/usr/bin/env python3
import re
from pathlib import Path
from xml.etree import ElementTree

root = Path(__file__).resolve().parent.parent
files = [root / "README.md", root / "CONTRIBUTING.md", *sorted((root / "docs").rglob("*.md"))]
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
for diagram in sorted((root / "docs" / "diagrams").glob("*.dot")):
    export = diagram.with_suffix(".svg")
    try:
        tree = ElementTree.parse(export)
        if not tree.getroot().tag.endswith("svg") or not tree.findall(".//{*}text"):
            failures.append(f"Empty SVG export for {diagram.name}")
    except (OSError, ElementTree.ParseError) as error:
        failures.append(f"Invalid SVG for {diagram.name}: {error}")
if failures:
    raise SystemExit("\n".join(failures))
print(f"Checked {len(files)} Markdown files")
