#!/usr/bin/env python3
"""Mutate structurally valid evidence and invoke the shipped verifier as a subprocess."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def rehash(bundle):
    paths = sorted(path for path in bundle.rglob("*") if path.is_file() and path.name != "MANIFEST.json")
    inventory = {str(path.relative_to(bundle)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    (bundle / "MANIFEST.json").write_text(json.dumps({"sha256": inventory}, sort_keys=True) + "\n")


def change_csv(path, operation):
    with path.open(newline="") as content:
        reader = csv.DictReader(content)
        fields = reader.fieldnames
        rows = list(reader)
    operation(rows)
    with path.open("w", newline="") as content:
        writer = csv.DictWriter(content, fields)
        writer.writeheader()
        writer.writerows(rows)


def altered_replay(bundle):
    tape = bundle / "replays/01-aperture.scene.replay"
    rows = tape.read_text().splitlines()
    columns = rows[41].split()
    columns[-1] = str(int(columns[-1]) ^ 1)
    rows[41] = " ".join(columns)
    tape.write_text("\n".join(rows) + "\n")


def duplicate_network(bundle):
    def edit(rows):
        position = next(i for i, row in enumerate(rows) if row["signal"] == "4")
        rows.insert(position, dict(rows[position]))
    change_csv(bundle / "raw/network_events.csv", edit)


def inaccurate_gpu(bundle):
    change_csv(bundle / "raw/render.csv", lambda rows: rows[0].update(reported_gpu_ms="0.0001"))


def erased_shadow(bundle):
    change_csv(bundle / "raw/render.csv", lambda rows: rows[0].update(shadow=rows[0]["start"]))


def erased_history(bundle):
    fixture = bundle / "raw" / "temporal-fixture"
    shutil.copyfile(fixture / "cold.ppm", fixture / "warm.ppm")


def unchanged_material_reload(bundle):
    fixture = bundle / "raw" / "material-fixture"
    shutil.copyfile(fixture / "normal.ppm", fixture / "reloaded.ppm")


def visible_hidden_effect(bundle):
    fixture = bundle / "raw" / "effects-fixture"
    shutil.copyfile(fixture / "burst.ppm", fixture / "occluded.ppm")


def wrong_source(bundle):
    for name in ("SOURCE_INPUTS.json", "ACCEPTANCE.json"):
        target = bundle / name
        content = json.loads(target.read_text())
        content["commit"] = "1" * 40
        target.write_text(json.dumps(content) + "\n")


def corrupt(bundle):
    with (bundle / "raw/solo.out").open("ab") as output:
        output.write(b"tampered\n")


def main():
    bundle, source, tool, output = map(Path, sys.argv[1:])
    verifier = source / "scripts/verify_evidence.py"
    controls = [
        ("replay_state", altered_replay, True, "Replay divergence at tick 41"),
        ("duplicate_network", duplicate_network, True, "Duplicate irreversible network event"),
        ("source_mismatch", wrong_source, True, "Trusted source commit mismatch"),
        ("corrupt_artifact", corrupt, False, "Artifact hash mismatch"),
        ("missing_artifact", lambda root: (root / "raw/solo.out").unlink(), False, "Artifact coverage mismatch"),
    ]
    if json.loads((bundle / "ACCEPTANCE.json").read_text())["profile"] == "portability-diagnostic":
        controls += [
            ("false_timing", inaccurate_gpu, True, "Reported GPU time disagrees"),
            ("missing_gpu_pass", erased_shadow, True, "Missing GPU pass timing"),
            ("missing_temporal_accumulation", erased_history, True, "Temporal accumulation missing"),
            ("stale_material_reload", unchanged_material_reload, True, "Material reload did not replace"),
            ("effect_ignores_depth", visible_hidden_effect, True, "Opaque geometry failed to occlude effects"),
        ]
    results = []
    for name, mutate, update, expected in controls:
        with tempfile.TemporaryDirectory(prefix=f"ember-negative-{name}-") as temporary:
            altered = Path(temporary) / bundle.name
            shutil.copytree(bundle, altered)
            mutate(altered)
            if update:
                rehash(altered)
            invocation = [sys.executable, str(verifier), str(altered), str(source), str(tool)]
            result = subprocess.run(invocation, capture_output=True, text=True, timeout=30)
            results.append({
                "control": name, "argv": invocation, "rehashed": update, "exit": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr, "expected_reason": expected,
                "rejected_for_expected_reason": result.returncode != 0 and expected in result.stderr
            })
    output.write_text("".join(json.dumps(item, sort_keys=True) + "\n" for item in results))
    return 0 if all(item["rejected_for_expected_reason"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
