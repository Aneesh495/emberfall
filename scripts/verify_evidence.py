#!/usr/bin/env python3
"""Independently evaluate work counts, timing equations, event identity and replay."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tarfile


def check(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    check(path.is_file() and not path.is_symlink() and path.stat().st_size <= 40_000_000,
          f"Invalid artifact {path.name}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text())


def percentile(samples, fraction):
    check(samples and all(math.isfinite(value) and value > 0 for value in samples), "Invalid timing sample")
    return sorted(samples)[math.ceil(len(samples) * fraction) - 1]


def records(path):
    with path.open(newline="") as data:
        return list(csv.DictReader(data))


def verify_cpu(bundle):
    raw = bundle / "raw"
    samples = records(raw / "ecs.csv")
    check(len(samples) == 400, "Incomplete ECS tick count")
    summaries = {}
    for workers in (1, 4):
        rows = [row for row in samples if int(row["workers"]) == workers]
        check([int(row["tick"]) for row in rows] == list(range(40, 240)), "ECS timing tick order")
        for row in rows:
            check((int(row["entities"]), int(row["component_reads"]), int(row["component_writes"])) ==
                  (100_000, 400_000, 300_000), "ECS work counters")
            check(1 <= int(row["peak_workers"]) <= workers, "ECS worker bound")
        summaries[f"ecs_{workers}_p95_ms"] = percentile([int(row["step_ns"]) / 1e6 for row in rows], 0.95)
    hashes = re.findall(r"state=(\d+)", (raw / "ecs.err").read_text())
    check(len(hashes) == 2 and hashes[0] == hashes[1], "ECS worker state divergence")
    check("1000000 operations, seed 50419" in (raw / "world.out").read_text(), "ECS oracle did not run")
    check("random_graphs=160 tasks_per_graph=80 peak_workers=4" in (raw / "scheduler.out").read_text(),
          "Missing scheduler DAG or concurrency probe")
    physics = records(raw / "physics.csv")
    check([int(row["tick"]) for row in physics] == list(range(60, 600)), "Physics tick order")
    check(all(int(row["bodies"]) == 1001 and 0 < int(row["contacts"]) <= int(row["candidates"])
              and 0 <= int(row["sleeping"]) <= 1000 for row in physics), "Physics workload incomplete")
    summaries["physics_p95_ms"] = percentile([int(row["step_ns"]) / 1e6 for row in physics], 0.95)

    events = records(raw / "network_events.csv")
    seen = set()
    exits = []
    prior = 0
    for event in events:
        now, epoch, level, signal, subject = (int(event[key]) for key in ("time_ms", "epoch", "level", "signal", "subject"))
        check(now >= prior and epoch == level + 1 and level in range(3) and signal in range(5) and subject > 0,
              "Invalid network event record")
        prior = now
        if signal in (1, 4):
            key = (epoch, signal, subject)
            check(key not in seen, "Duplicate irreversible network event")
            seen.add(key)
        if signal == 4:
            exits.append((level, subject))
    check(exits == [(0, 13), (1, 17), (2, 12)], "Missing network completion events")
    check(sum(signal == 1 for _, signal, _ in seen) == 1, "Mirror event count mismatch")
    return summaries


def verify_render(raw):
    rows = records(raw / "render.csv")
    check([int(row["serial"]) for row in rows] == list(range(19, 159)), "Render sample order")
    timings = []
    host = []
    for row in rows:
        values = tuple(int(row[key]) for key in ("width", "height", "submitted", "culled", "triangles", "casters", "lights", "draw_calls"))
        width, height, submitted, culled, triangles, casters, lights, draws = values
        check((width, height, submitted, culled, triangles, lights, draws) ==
              (1920, 1080, 10500, 0, 2016000, 128, 3) and casters > 0, "Missing render workload")
        bits = int(row["timestamp_bits"])
        period = float(row["period_ns"])
        check(16 <= bits <= 64 and 0 < period < 1000, "Invalid hardware timestamp scale")
        ticks = [int(row[key]) for key in ("start", "shadow", "opaque", "display")]
        mask = (1 << bits) - 1
        segments = [((right-left) & mask)*period/1e6 for left, right in zip(ticks, ticks[1:])]
        check(all(0 < length < 1000 for length in segments), "Missing GPU pass timing")
        total = sum(segments)
        claimed = float(row["reported_gpu_ms"])
        check(math.isclose(total, claimed, rel_tol=1e-6, abs_tol=1e-7), "Reported GPU time disagrees with raw queries")
        timings.append(total)
        host.append(int(row["host_ns"])/1e6)
    check("validation_errors=0" in (raw / "gpu_smoke.out").read_text(), "GPU smoke validation incomplete")
    check("validation_errors=0" in (raw / "render.err").read_text(), "Render stress validation incomplete")
    return {
        "render_gpu_p50_ms": percentile(timings, 0.5), "render_gpu_p99_ms": percentile(timings, 0.99),
        "render_host_p50_ms": percentile(host, 0.5), "render_host_p99_ms": percentile(host, 0.99)
    }


def verify_package(bundle):
    archives = sorted((bundle / "package").glob("*.tar.gz"))
    check(len(archives) == 1, "Package archive missing")
    with tarfile.open(archives[0], "r:gz") as package:
        members = list(package)
        check(all(item.isfile() or item.isdir() for item in members), "Unexpected archive member type")
        check(len({item.name for item in members}) == len(members), "Duplicate archive member")
        files = [item for item in members if item.isfile()]
        check(12 <= len(files) <= 32, "Package file count outside bounds")
        prefix = "The-Lantern-Reach/"
        for item in files:
            check(item.name.startswith(prefix) and ".." not in Path(item.name).parts and
                  0 < item.size <= 15_000_000, "Invalid archive member")
        sums = package.extractfile(prefix + "SHA256SUMS")
        check(sums is not None, "Package internal inventory absent")
        expected = {name: checksum for checksum, name in (
            line.split("  ", 1) for line in sums.read().decode().splitlines()
        )}
        check(set(expected) == {item.name.removeprefix(prefix) for item in files} - {"SHA256SUMS"},
              "Package inventory mismatch")
        for name, checksum in expected.items():
            item = package.extractfile(prefix + name)
            check(item is not None and hashlib.sha256(item.read()).hexdigest() == checksum, "Package checksum mismatch")
        check(not any("editor" in name or name.endswith((".h", ".a")) for name in expected), "Editor leaked into package")
    raw = bundle / "raw"
    check("ui_check=settings,save,keyboard,focus,pause,restart,resize" in (raw / "ui_check.out").read_text() and
          "validation_errors=0" in (raw / "ui_check.out").read_text(), "Native UI diagnostic incomplete")
    check("editor_frames=25 validation_errors=0" in (raw / "editor.out").read_text(), "Editor render diagnostic incomplete")
    for filename in ("frame.ppm", "ui.ppm", "editor.ppm", "ending.ppm"):
        header, shape, rgb = (raw / filename).read_bytes().split(b"\n", 2)
        width, height = map(int, shape.split())
        check(header == b"P6" and 200 <= width <= 4096 and 200 <= height <= 4096 and
              rgb.startswith(b"255\n") and len(rgb) == 4 + width * height * 3, "Incomplete framebuffer readback")
    packaged = (raw / "package_smoke.out").read_text()
    states = re.findall(r"replay_ticks=(\d+) final_hash=(\d+) completed=1", packaged)
    check(len(states) == 3, "Packaged route completion missing")
    return states


def verify(bundle, source, tool):
    metadata = read(bundle / "ACCEPTANCE.json")
    profile = metadata["profile"]
    check(profile in ("cpu-diagnostic", "portability-diagnostic") and metadata["status"] == "recorded" and
          metadata["full_pass"] is False, "Unsupported or incomplete campaign")
    contents = read(bundle / "MANIFEST.json")["sha256"]
    actual = {str(path.relative_to(bundle)) for path in bundle.rglob("*") if path.is_file()}
    check(actual == set(contents) | {"MANIFEST.json"} and len(actual) <= 128, "Artifact coverage mismatch")
    for name, expected in contents.items():
        path = bundle / name
        check(path.resolve().is_relative_to(bundle), "Artifact escaped bundle")
        check(digest(path) == expected, f"Artifact hash mismatch: {name}")

    identity = read(bundle / "SOURCE_INPUTS.json")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    check(commit == identity["commit"] == metadata["commit"], "Trusted source commit mismatch")
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=source).decode().split("\0")
    check(set(identity["sha256"]) == set(filter(None, tracked)), "Source path inventory mismatch")
    for name, expected in identity["sha256"].items():
        check(digest(source / name) == expected, f"Source byte mismatch: {name}")
    check(digest(tool) == metadata["replay_tool_sha256"], "Replay tool binary mismatch")

    commands = [json.loads(line) for line in (bundle / "COMMANDS.jsonl").read_text().splitlines()]
    names = ["doctor", "build", "cook", "ctest", "world", "scheduler", "solo", "network", "ecs", "physics"]
    if profile == "portability-diagnostic":
        names += ["gpu_smoke", "render", "ui_check", "editor", "package", "package_smoke"]
    check([row["name"] for row in commands] == names and all(
        row["exit"] == 0 and 0 <= row["wall_seconds"] < row["timeout_seconds"] for row in commands
    ), "Required command failed or missing")
    summary = verify_cpu(bundle)
    packaged = None
    if profile == "portability-diagnostic":
        summary.update(verify_render(bundle / "raw"))
        packaged = verify_package(bundle)

    expected = ("01-aperture.scene", "02-prism.scene", "03-meridian.scene")
    levels = bundle / "levels"
    catalog = read(levels / "manifest.json")["outputs"]
    scene_names = {name for name in catalog if name.endswith(".scene")}
    check(scene_names == set(expected), "Packaged level coverage mismatch")
    files = {path.name for path in levels.iterdir() if path.suffix in (".scene", ".mesh")}
    check(files == set(catalog), "Cooked asset inventory mismatch")
    for name, record in catalog.items():
        check(name == Path(name).name and name.endswith((".scene", ".mesh")), "Invalid cooked asset name")
        check(digest(levels / name) == record["sha256"], "Cooked asset mismatch")
        if name.endswith(".mesh"):
            check((levels / name).read_bytes().startswith(b"EMBMSH01"), "Invalid baked mesh header")
        for dependency in record.get("mesh_dependencies", []):
            check(dependency in catalog and dependency.endswith(".mesh"), "Unknown cooked mesh dependency")
    replays = {}
    for name in expected:
        check(digest(levels / name) == catalog[name]["sha256"], "Cooked level mismatch")
        tape = bundle / "replays" / (name + ".replay")
        result = subprocess.run([tool, "replay-check", levels, tape], text=True, capture_output=True, timeout=20)
        check(result.returncode == 0, "Bounded replay rejected: " + result.stderr.strip())
        observed = re.fullmatch(r"replay_ticks=(\d+) final_hash=(\d+)\s*", result.stdout)
        check(observed and 200 <= int(observed[1]) <= 600, "Incomplete replay tick range")
        check(f"hash {observed[2]}" in (bundle / "raw" / "solo.out").read_text(), "Replay final state mismatch")
        replays[name] = {"ticks": int(observed[1]), "hash": observed[2]}
    if packaged is not None:
        check(packaged == [(str(row["ticks"]), row["hash"]) for row in replays.values()],
              "Packaged replay states mismatch")
    return {"profile": profile, "summary": summary, "replays": replays}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("tool", type=Path)
    args = parser.parse_args()
    try:
        summary = verify(args.bundle.resolve(), args.source.resolve(), args.tool.resolve())
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as failure:
        print(f"Verification rejected: {failure}", file=sys.stderr)
        return 1
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
