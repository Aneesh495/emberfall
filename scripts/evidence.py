#!/usr/bin/env python3
"""Execute a named campaign and retain unprocessed observations."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root)


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def dump(path, document):
    path.write_text(json.dumps(document, sort_keys=True, indent=2) + "\n")


def source_inventory(root):
    names = git(root, "ls-files", "-z").decode().split("\0")
    return {name: sha(root / name) for name in names if name}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--profile", required=True, choices=["full", "cpu-diagnostic", "portability-diagnostic"])
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_id):
        parser.error("Invalid run ID")
    root = Path(__file__).resolve().parent.parent
    if git(root, "status", "--porcelain", "--untracked-files=no"):
        parser.error("Evidence requires a clean tracked source checkout")
    build = args.build.resolve()
    destination = args.output.resolve() / args.run_id
    destination.mkdir(parents=True, exist_ok=False)
    logs = destination / "raw"
    logs.mkdir()
    commands = []
    commit = git(root, "rev-parse", "HEAD").decode().strip()
    identity = source_inventory(root)
    dump(destination / "SOURCE_INPUTS.json", {"commit": commit, "sha256": identity})
    dump(destination / "ENVIRONMENT.json", {
        "time_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "system": platform.system(), "release": platform.release(), "machine": platform.machine(),
        "python": platform.python_version(), "build": str(build), "root": str(root)
    })
    dump(destination / "DATASETS.json", {
        "workloads": "Synthetic engine stress and original Emberfall scenes",
        "catalog_sha256": identity["assets/levels/ember.catalog"],
        "plan_sha256": identity["docs/EXPERIMENT_PLAN.md"]
    })

    def run(name, command, *, timeout=60, stdout=None):
        started = time.monotonic()
        out = stdout or logs / (name + ".out")
        err = logs / (name + ".err")
        command = [str(item) for item in command]
        with out.open("wb") as output, err.open("wb") as error:
            try:
                environment = os.environ.copy()
                layer_library = environment.get("EMBERFALL_VALIDATION_LIB")
                if layer_library and name in ("gpu_smoke", "render", "ui_check", "editor", "shadow_fixture", "temporal_fixture", "material_fixture", "effects_fixture", "editor_frame", "editor_physics", "editor_assets", "editor_skin"):
                    environment["DYLD_LIBRARY_PATH"] = layer_library
                result = subprocess.run(command, stdout=output, stderr=error, cwd=root, timeout=timeout, env=environment)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = 124
            except OSError as issue:
                error.write(str(issue).encode())
                code = 127
        row = {
            "name": name, "argv": command, "exit": code, "wall_seconds": time.monotonic() - started,
            "stdout": str(out.relative_to(destination)), "stderr": str(err.relative_to(destination)),
            "timeout_seconds": timeout
        }
        commands.append(row)
        with (destination / "COMMANDS.jsonl").open("a") as log:
            log.write(json.dumps(row) + "\n")
        return code == 0

    run("doctor", ["./scripts/doctor.sh"])
    if args.profile == "full":
        gates = {
            "G05": "Mandatory render passes and validation matrix incomplete",
            "G06": "Linux x86-64 hardware reference not pinned",
            "G14": "Mixed eight-hour soak profile not ready",
            "G17": "Clean Linux GPU release profile unavailable",
            "G18": "Full acceptance prerequisites incomplete"
        }
        dump(destination / "REQUIREMENTS.json", {
            gate: {"status": "blocked", "reason": reason} for gate, reason in gates.items()
        })
        status = "blocked"
    else:
        def binary(name):
            return build / name
        built = run("build", ["cmake", "--build", build], timeout=600)
        cooked = built and run("cook", ["cmake", "--build", build, "--target", "emberfall_levels"], timeout=120)
        (destination / "replays").mkdir()
        if cooked:
            shutil.copytree(build / "levels", destination / "levels", ignore=shutil.ignore_patterns(".*"))
            levels = destination / "levels"
            run("ctest", ["ctest", "--test-dir", build, "--output-on-failure"], timeout=120)
            run("world", [binary("world_tests")])
            run("scheduler", [binary("scheduler_tests")])
            run("solo", [binary("playthrough_tests"), levels, destination / "replays"])
            run("network", [binary("network_tests"), levels, logs / "network_events.csv"])
            run("ecs", [binary("runtime_bench"), "ecs"], stdout=logs / "ecs.csv")
            run("physics", [binary("runtime_bench"), "physics"], stdout=logs / "physics.csv")
            if args.profile == "portability-diagnostic":
                run("gpu_smoke", [binary("gpu_smoke"), build / "shaders", logs / "frame.ppm"])
                run("shadow_fixture", [binary("gpu_shadow"), build / "shaders", logs / "shadow-fixture"])
                run("temporal_fixture", [binary("gpu_temporal"), build / "shaders", logs / "temporal-fixture"])
                run("material_fixture", [binary("gpu_material"), build / "shaders", logs / "material-fixture"])
                run("effects_fixture", [binary("gpu_effects"), build / "shaders", logs / "effects-fixture"])
                run("render", [binary("gpu_stress"), build / "shaders"], timeout=120, stdout=logs / "render.csv")
                run("ui_check", [binary("lantern-reach"), "--validate", "--ui-check", "--no-audio",
                                 "--profile", destination / "ui-profile", "--capture", logs / "ui.ppm"])
                run("editor", [binary("emberfall-editor"), root / "assets/levels/02-prism.scene",
                               "--validate", "--frames", "25", "--capture", logs / "editor.ppm"])
                for page in ("frame", "physics", "assets"):
                    arguments = ["--inspect", page]
                    if page == "physics":
                        arguments.append("--play")
                    run("editor_" + page, [binary("emberfall-editor"), root / "assets/levels/02-prism.scene",
                                           "--validate", "--frames", "25", *arguments,
                                           "--capture", logs / ("editor-" + page + ".ppm")])
                run("editor_skin", [binary("emberfall-editor"), root / "assets/levels/03-meridian.scene",
                                    "--validate", "--frames", "40", "--capture", logs / "editor-skin.ppm"])
                package = destination / "package"
                if run("package", ["./scripts/package.sh", build, package], timeout=120):
                    archives = list(package.glob("*.tar.gz"))
                    if len(archives) == 1:
                        run("package_smoke", ["python3", "scripts/package_smoke.py", archives[0],
                                              destination / "replays", logs / "ending.ppm"], timeout=190)
        required_count = 24 if args.profile == "portability-diagnostic" else 10
        status = "recorded" if cooked and len(commands) == required_count and all(
            row["exit"] == 0 for row in commands
        ) else "failed"
        dump(destination / "REQUIREMENTS.json", {
            "diagnostic_only": True, "full_gates": "unverified",
            "workloads": [row["name"] for row in commands],
        })

    dump(destination / "ACCEPTANCE.json", {
        "profile": args.profile, "run_id": args.run_id, "status": status,
        "full_pass": False, "commit": commit,
        "replay_tool_sha256": sha(build / "ember-tool") if (build / "ember-tool").is_file() else None
    })
    dump(destination / "MANIFEST.json", {
        "sha256": {str(path.relative_to(destination)): sha(path) for path in sorted(destination.rglob("*")) if path.is_file()}
    })
    print(f"{destination}: {status}")
    return 0 if status == "recorded" else 1


if __name__ == "__main__":
    raise SystemExit(main())
