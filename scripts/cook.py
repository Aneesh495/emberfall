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

import gltf_import


NAME = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}")
GUID = re.compile(r"[0-9a-f]{32}")
LIMIT = 2_000_000
EXTENSIONS = {"scene": (".scene",), "prefab": (".prefab",), "mesh": (".gltf", ".glb"), "buffer": (".bin",)}
SOURCE_LIMITS = {"mesh": gltf_import.MAX_DOCUMENT + gltf_import.MAX_BINARY + 28, "buffer": gltf_import.MAX_BINARY}


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
    mesh_buffers = {}
    for row in rows[1:]:
        if not row or row.startswith("#"):
            continue
        fields = row.split()
        if len(fields) == 3 and fields[0] in EXTENSIONS:
            kind, name, guid = fields
            if not NAME.fullmatch(name) or not name.endswith(EXTENSIONS[kind]) or not GUID.fullmatch(guid):
                raise ValueError(f"Invalid catalog asset: {row}")
            if name in assets or any(record["guid"] == guid for record in assets.values()):
                raise ValueError(f"Duplicate catalog asset: {row}")
            path = source / name
            if not path.is_file() or path.is_symlink():
                raise ValueError(f"Catalog input must be a regular file: {name}")
            data = bounded(path, SOURCE_LIMITS.get(kind, LIMIT))
            assets[name] = {"kind": kind, "guid": guid, "sha256": digest(data), "bytes": len(data)}
        elif len(fields) == 7 and fields[0] == "place":
            placements.append(fields[1:])
        elif len(fields) == 3 and fields[0] == "mesh-buffer":
            mesh_buffers.setdefault(fields[1], []).append(fields[2])
        else:
            raise ValueError(f"Invalid catalog record: {row}")
        if len(assets) > 128 or len(placements) > 256:
            raise ValueError("Catalog row limit")
    for level, prefab, *_ in placements:
        if assets.get(level, {}).get("kind") != "scene" or assets.get(prefab, {}).get("kind") != "prefab":
            raise ValueError("Invalid catalog placement dependency")
    for mesh, buffers in mesh_buffers.items():
        if assets.get(mesh, {}).get("kind") != "mesh" or len(buffers) > 16 or len(buffers) != len(set(buffers)):
            raise ValueError(f"Invalid mesh dependency list: {mesh}")
        if any(assets.get(name, {}).get("kind") != "buffer" for name in buffers):
            raise ValueError(f"Unknown mesh buffer dependency: {mesh}")
        if sum(assets[name]["bytes"] for name in buffers) > gltf_import.MAX_BINARY:
            raise ValueError(f"Mesh buffers exceed total byte budget: {mesh}")
    return assets, placements, mesh_buffers


def cook(source, target, tool):
    target.mkdir(parents=True, exist_ok=True)
    assets, placements, mesh_buffers = load_catalog(source)
    tool_hash = digest(bounded(tool, 64_000_000))
    importer_hash = digest(Path(gltf_import.__file__).read_bytes())
    recipe_hash = digest(Path(__file__).read_bytes())
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
            kind = record["kind"]
            if kind not in ("scene", "mesh"):
                continue
            uses = [row[1:] for row in placements if row[0] == name] if kind == "scene" else []
            dependency_buffers = mesh_buffers.get(name, [])
            dep_names = sorted({name, *dependency_buffers, *(item[0] for item in uses)})
            output = name if kind == "scene" else Path(name).stem + ".mesh"
            if output in new_cache:
                raise ValueError(f"Duplicate cooked output: {output}")
            lineage = {"kind": kind, "dependencies": dep_names, "placements": uses, "guid": record["guid"]}
            processor = tool_hash if kind == "scene" else importer_hash
            cache_key = digest(encode([recipe_hash, processor, lineage, [assets[item] for item in dep_names]]))
            path = target / output
            cached = cache.get(output, {})
            if (isinstance(cached, dict) and cached.get("key") == cache_key and path.is_file()
                    and not path.is_symlink() and cached.get("sha256") == digest(bounded(path))):
                output_hash = cached["sha256"]
            else:
                if kind == "scene":
                    command = [str(tool), "compose", str(source / name), str(staged / output)]
                    for prefab, *args in uses:
                        command.extend([str(source / prefab), *args])
                    subprocess.run(command, check=True, timeout=30)
                else:
                    buffers = {item: bounded(source / item, gltf_import.MAX_BINARY) for item in dependency_buffers}
                    model = gltf_import.bake(bounded(source / name, SOURCE_LIMITS["mesh"]), buffers)
                    (staged / output).write_bytes(model)
                output_hash = digest(bounded(staged / output))
                rebuilt.append(output)
            lineage["sha256"] = output_hash
            manifest["outputs"][output] = lineage
            new_cache[output] = {"key": cache_key, "sha256": output_hash}
        for output, record in manifest["outputs"].items():
            if record["kind"] != "scene":
                continue
            path = staged / output if output in rebuilt else target / output
            refs = subprocess.check_output([str(tool), "mesh-references", path], text=True, timeout=15).splitlines()
            missing = [name for name in refs if manifest["outputs"].get(name, {}).get("kind") != "mesh"]
            if missing:
                raise ValueError(f"Scene {output} references uncataloged meshes: {', '.join(sorted(set(missing)))}")
            record["mesh_dependencies"] = sorted(set(refs))
        if load_catalog(source) != (assets, placements, mesh_buffers):
            raise RuntimeError("Catalog inputs changed during cooking")
        for name in rebuilt:
            os.replace(staged / name, target / name)
    for old in cache.keys() - new_cache.keys():
        if NAME.fullmatch(old) and old.endswith((".scene", ".mesh")):
            (target / old).unlink(missing_ok=True)
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
