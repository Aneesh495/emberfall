import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


script, original, tool = map(Path, sys.argv[1:])


def cook(source, target, succeeds=True):
    result = subprocess.run([sys.executable, script, source, target, tool],
                            check=False, capture_output=True, text=True, timeout=35)
    if succeeds and result.returncode == 0:
        return json.loads(result.stdout)["rebuilt"]
    if not succeeds and result.returncode != 0:
        return None
    raise AssertionError((result.stdout, result.stderr, result.returncode))


with tempfile.TemporaryDirectory(prefix="ember-cook-test-") as root:
    source, first, second = (Path(root) / item for item in ("source", "first", "second"))
    shutil.copytree(original, source)
    assert len(cook(source, first)) == 3
    assert cook(source, first) == []
    assert len(cook(source, second)) == 3
    assert (first / "manifest.json").read_bytes() == (second / "manifest.json").read_bytes()

    scene = source / "01-aperture.scene"
    initial = scene.read_text()
    scene.write_text(initial.replace("West iris tracer", "West iris trim"))
    assert cook(source, first) == [scene.name]
    prefab = source / "relay-stanchion.prefab"
    prefab.write_text(prefab.read_text().replace("Copper collar", "Copper trim"))
    assert len(cook(source, first)) == 3
    before = (first / "manifest.json").read_bytes()
    manifest = json.loads(before)
    for name, output in manifest["outputs"].items():
        assert hashlib.sha256((first / name).read_bytes()).hexdigest() == output["sha256"]
    scene.write_text("ember-scene 500\n")
    cook(source, first, succeeds=False)
    assert (first / "manifest.json").read_bytes() == before
