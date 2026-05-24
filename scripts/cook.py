#!/usr/bin/env python3
"""Cook a bounded catalog and publish canonical scenes plus their source lineage."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


NAME = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}")
GUID = re.compile(r"[0-9a-f]{32}")
LIMIT = 2_000_000


def bounded(path, limit=LIMIT):
    with path.open("rb") as source:
        content = source.read(limit + 1)
    if len(content) > limit:
        raise ValueError(f"Input too large: {path}")
    return content


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def atomic(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as out:
        temporary = Path(out.name)
        try:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def load_catalog(source):
    rows = bounded(source / "ember.catalog", 32768).decode("ascii").splitlines()
    if not rows or rows[0] != "ember-content 1":
        raise ValueError("Unsupported catalog header")
    assets = {}
    placements = []
    for row in rows[1:]:
        if not row or row.startswith("#"):
            continue
        fields = row.split()
        if len(fields) == 3 and fields[0] in ("scene", "prefab"):
            kind, name, guid = fields
            if not NAME.fullmatch(name) or not name.endswith("." + kind) or not GUID.fullmatch(guid):
                raise ValueError(f"Invalid catalog asset: {row}")
            if name in assets or any(record["guid"] == guid for record in assets.values()):
                raise ValueError(f"Duplicate catalog asset: {row}")
            path = source / name
            if not path.is_file() or path.is_symlink():
                raise ValueError(f"Catalog input must be a regular file: {name}")
            assets[name] = {"kind": kind, "guid": guid, "sha256": digest(bounded(path))}
        elif len(fields) == 7 and fields[0] == "place":
            placements.append(fields[1:])
        else:
            raise ValueError(f"Invalid catalog record: {row}")
        if len(assets) > 128 or len(placements) > 256:
            raise ValueError("Catalog row limit")
    for level, prefab, *_ in placements:
        if assets.get(level, {}).get("kind") != "scene" or assets.get(prefab, {}).get("kind") != "prefab":
            raise ValueError("Invalid catalog placement dependency")
    return assets, placements


def cook(source, target, tool):
    target.mkdir(parents=True, exist_ok=True)
    assets, placements = load_catalog(source)
    tool_hash = digest(bounded(tool, 64_000_000))
    cache_file = target / ".cook-cache.json"
    cache = {}
    if cache_file.is_file():
        try:
            cache = json.loads(bounded(cache_file))
            if not isinstance(cache, dict):
                cache = {}
        except (OSError, ValueError):
            pass

    rebuilt = []
    new_cache = {}
    manifest = {"version": 1, "sources": assets, "outputs": {}}
    with tempfile.TemporaryDirectory(prefix=".cook-", dir=target) as staging:
        staged = Path(staging)
        for name, record in sorted(assets.items()):
            if record["kind"] != "scene":
                continue
            uses = [row[1:] for row in placements if row[0] == name]
            dep_names = sorted({name, *(item[0] for item in uses)})
            lineage = {"dependencies": dep_names, "placements": uses, "guid": record["guid"]}
            cache_key = digest(encode([tool_hash, lineage, [assets[item] for item in dep_names]]))
            path = target / name
            cached = cache.get(name, {})
            if (isinstance(cached, dict) and cached.get("key") == cache_key and path.is_file()
                    and cached.get("sha256") == digest(bounded(path))):
                output_hash = cached["sha256"]
            else:
                command = [str(tool), "compose", str(source / name), str(staged / name)]
                for prefab, *args in uses:
                    command.extend([str(source / prefab), *args])
                subprocess.run(command, check=True, timeout=30)
                output_hash = digest(bounded(staged / name))
                rebuilt.append(name)
            lineage["sha256"] = output_hash
            manifest["outputs"][name] = lineage
            new_cache[name] = {"key": cache_key, "sha256": output_hash}
        if load_catalog(source) != (assets, placements):
            raise RuntimeError("Catalog inputs changed during cooking")
        for name in rebuilt:
            os.replace(staged / name, target / name)
    atomic(target / "manifest.json", encode(manifest))
    atomic(cache_file, encode(new_cache))
    print(json.dumps({"rebuilt": rebuilt, "total": len(new_cache)}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("tool", type=Path)
    args = parser.parse_args()
    cook(args.source.resolve(), args.target.resolve(), args.tool.resolve())


if __name__ == "__main__":
    main()
