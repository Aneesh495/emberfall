#!/usr/bin/env python3
"""Extract one produced package and replay every level through its native frontend."""
import hashlib
import os
from pathlib import Path
import platform
import re
import select
import secrets
import socket
import subprocess
import sys
import tempfile


def coop(installed, profile, capture, env):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = str(reservation.getsockname()[1])
    key = secrets.token_hex(12)
    base = [installed / "play", "--no-audio", "--size", "620x390", "--key", key,
            "--network-monitor"]
    roles = (
        ("host", ["--host", port, "--frames", "240"]),
        ("client", ["--join", "127.0.0.1", port, "--frames", "150", "--capture", capture]),
    )
    observations = {}
    processes = {}
    try:
        for role, options in roles:
            processes[role] = subprocess.Popen([*base, "--profile", profile / role, *options], cwd=installed,
                                               env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if role == "host":
                ready, _, _ = select.select([processes[role].stdout], [], [], 10)
                if not ready or processes[role].stdout.readline().strip() != f"network=host port={port}":
                    raise RuntimeError("Packaged host did not publish its bound port")
        for role, process in processes.items():
            output, error = process.communicate(timeout=48)
            witness = re.search(r"packets_sent=(\d+) packets_received=(\d+) connected=[01] peer_seen=1 "
                                r"accepted_snapshots=(\d+) acknowledged_input=(\d+)", output)
            if process.returncode != 0 or witness is None or min(int(witness[1]), int(witness[2])) < 5:
                raise RuntimeError(f"Packaged co-op {role} failed: {output} {error}")
            observations[role] = tuple(map(int, witness.groups()))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)
    sent, received, snapshots, _ = observations["client"]
    host_sent, host_received, _, ack = observations["host"]
    if snapshots < 5 or ack == 0:
        raise RuntimeError("Packaged co-op did not exchange authoritative frames and player input")
    print(f"coop_client_sent={sent} coop_client_received={received} coop_client_snapshots={snapshots}", flush=True)
    print(f"coop_host_sent={host_sent} coop_host_received={host_received} coop_host_input={ack}", flush=True)


def main():
    archive, replays, capture = (Path(arg).resolve() for arg in sys.argv[1:])
    env = os.environ.copy()
    for name in ("DYLD_LIBRARY_PATH", "VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_LAYER_PATH", "SDL_VULKAN_LIBRARY"):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(prefix="lantern-package-check-") as folder:
        subprocess.run(["tar", "-xzf", archive, "-C", folder], check=True, timeout=25)
        installed = (Path(folder) / "The-Lantern-Reach").resolve()
        inventory = {}
        for line in (installed / "SHA256SUMS").read_text().splitlines():
            digest, relative = line.split("  ", 1)
            path = installed / relative
            if not path.resolve().is_relative_to(installed) or path.is_symlink() or not path.is_file():
                raise ValueError(f"Unsafe package artifact {relative}")
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(f"Package hash mismatch {relative}")
            inventory[relative] = digest
        if any("editor" in name or name.endswith((".h", ".a")) for name in inventory):
            raise ValueError("Unexpected development or editor dependency")
        if platform.system() == "Darwin" and not {"lib/libvulkan.1.dylib", "lib/libMoltenVK.dylib"} <= inventory.keys():
            raise ValueError("Missing bundled Vulkan runtime")
        print(f"package_files={len(inventory)}", flush=True)
        tapes = sorted(replays.glob("*.replay"))
        if [tape.name for tape in tapes] != ["01-aperture.scene.replay", "02-prism.scene.replay",
                                             "03-meridian.scene.replay"]:
            raise RuntimeError("Expected one replay per level")
        for i, tape in enumerate(tapes):
            argv = [installed / "play", "--no-audio", "--size", "680x420", "--profile", Path(folder) / "profile",
                    "--replay", tape, "--replay-fast"]
            if i == 2:
                argv.extend(["--capture", capture])
            run = subprocess.run(argv, env=env, timeout=35, cwd=installed, text=True, capture_output=True)
            print(run.stdout, end="", flush=True)
            print(run.stderr, end="", file=sys.stderr, flush=True)
            if run.returncode or not re.search(r"replay_ticks=\d+ final_hash=\d+ completed=1", run.stdout):
                raise RuntimeError(f"Packaged replay did not complete: {tape.name}")
            if tape.name == "03-meridian.scene.replay" and not re.search(
                r"mesh_triangles=198 texture_layers=0 .* animated_triangles=214", run.stdout
            ):
                raise RuntimeError("Packaged Meridian skeletal asset missing")
        coop(installed, Path(folder) / "coop", capture.with_name("network.ppm"), env)


if __name__ == "__main__":
    main()
