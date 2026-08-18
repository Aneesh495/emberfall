import base64
import hashlib
import json
from pathlib import Path
import shutil
import shlex
import subprocess
import struct
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
    assert len(cook(source, first)) == 4
    assert cook(source, first) == []
    assert len(cook(source, second)) == 4
    assert (first / "manifest.json").read_bytes() == (second / "manifest.json").read_bytes()

    scene = source / "01-aperture.scene"
    initial = scene.read_text()
    scene.write_text(initial.replace("West iris tracer", "West iris trim"))
    assert cook(source, first) == [scene.name]
    prefab = source / "relay-stanchion.prefab"
    prefab.write_text(prefab.read_text().replace("Copper collar", "Copper trim"))
    assert len(cook(source, first)) == 3
    variant = source / "prism-stanchion.prefab"
    variant_bytes = variant.read_bytes()
    variant.write_bytes(variant_bytes.replace(b'0.18 0.60 0.81', b'0.24 0.51 0.86'))
    assert cook(source, first) == ["02-prism.scene"]
    cooked = (first / "02-prism.scene").read_text()
    target = next(shlex.split(row) for row in cooked.splitlines() if row.startswith("node 102 "))
    assert all(abs(float(actual)-expected) < 1e-6 for actual, expected in zip(target[12:15], (.24, .51, .86)))
    prefab_manifest = json.loads((first / "manifest.json").read_text())
    assert {"relay-stanchion.prefab", "prism-stanchion.prefab", "prism-stanchion-amber.prefab"} <= set(
        prefab_manifest["outputs"]["02-prism.scene"]["dependencies"])
    before = (first / "manifest.json").read_bytes()
    variant.write_bytes(variant_bytes.replace(b'relay-stanchion.prefab', b'prism-stanchion-amber.prefab'))
    cook(source, first, succeeds=False)
    assert (first / "manifest.json").read_bytes() == before
    variant.write_bytes(variant_bytes)
    assert cook(source, first) == ["02-prism.scene"]
    gltf = source / "prism-monitor.gltf"
    model = json.loads(gltf.read_text())
    data = base64.b64decode(model["buffers"][0]["uri"].split(",",1)[1])
    model["buffers"][0]["uri"] = "prism.bin"
    gltf.write_text(json.dumps(model))
    external = source / "prism.bin"
    external.write_bytes(data)
    catalog = source / "ember.catalog"
    with catalog.open("a") as target:
        target.write("buffer prism.bin 41b26dff88ae42ce9624e8a8fba89c35\n"
                     "mesh-buffer prism-monitor.gltf prism.bin\n")
    assert cook(source, first) == ["prism-monitor.mesh"]
    external.write_bytes(struct.pack("<f", 0.45) + data[4:])
    assert cook(source, first) == ["prism-monitor.mesh"]
    before = (first / "manifest.json").read_bytes()
    external.write_bytes(data[:-4])
    cook(source, first, succeeds=False)
    assert (first / "manifest.json").read_bytes() == before
    external.write_bytes(data)
    assert cook(source, first) == ["prism-monitor.mesh"]
    dial = source / "prism-dial.png"
    original_dial = dial.read_bytes()
    dial.write_bytes((source / "prism-metal.png").read_bytes())
    assert cook(source, first) == ["prism-monitor.mesh"]
    old_manifest = (first / "manifest.json").read_bytes()
    dial.write_bytes(original_dial[:-3])
    cook(source, first, succeeds=False)
    assert (first / "manifest.json").read_bytes() == old_manifest
    dial.write_bytes(original_dial)
    assert cook(source, first) == ["prism-monitor.mesh"]
    before = (first / "manifest.json").read_bytes()
    manifest = json.loads(before)
    for name, output in manifest["outputs"].items():
        assert hashlib.sha256((first / name).read_bytes()).hexdigest() == output["sha256"]
    scene.write_text("ember-scene 500\n")
    cook(source, first, succeeds=False)
    assert (first / "manifest.json").read_bytes() == before
