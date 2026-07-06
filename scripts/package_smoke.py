#!/usr/bin/env python3
"""Extract one produced package and replay every level through its native frontend."""
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


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
        for i, tape in enumerate(sorted(replays.glob("*.replay"))):
            argv = [installed / "play", "--no-audio", "--size", "680x420", "--profile", Path(folder) / "profile",
                    "--replay", tape, "--replay-fast"]
            if i == 2:
                argv.extend(["--capture", capture])
            subprocess.run(argv, check=True, env=env, timeout=35, cwd=installed)


if __name__ == "__main__":
    main()
