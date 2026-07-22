#!/bin/sh
set -eu

build=$1
output=$2
mkdir -p "$output"
output=$(cd "$output" && pwd)
system=$(uname -s)
arch=$(uname -m)
case "$system:$arch" in
    Darwin:arm64) platform=darwin-arm64 ;;
    Linux:x86_64) platform=linux-x86_64 ;;
    *) printf 'Unsupported package host %s:%s\n' "$system" "$arch" >&2; exit 1 ;;
esac
tmp=$(mktemp -d "$output/.ember-package.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM
game="$tmp/The-Lantern-Reach"
cmake --install "$build" --prefix "$game"
test -x "$game/lantern-reach"
test -f "$game/levels/manifest.json"
test -f "$game/shaders/scene.vert.spv"
if [ "$system" = Darwin ]; then
    loader=$(brew --prefix vulkan-loader)
    molten=$(brew --prefix molten-vk)
    mkdir "$game/lib"
    cp -L "$loader/lib/libvulkan.1.dylib" "$game/lib/"
    cp -L "$molten/lib/libMoltenVK.dylib" "$game/lib/"
    cp "$loader/LICENSE.txt" "$game/licenses/Vulkan-Loader.txt"
    cp "$molten/LICENSE" "$game/licenses/MoltenVK.txt"
    old=$(otool -L "$game/lantern-reach" | awk '/libvulkan[.]1[.]dylib/ {print $1; exit}')
    test -n "$old"
    chmod u+w "$game/lib/libvulkan.1.dylib" "$game/lib/libMoltenVK.dylib"
    install_name_tool -id @loader_path/libvulkan.1.dylib "$game/lib/libvulkan.1.dylib"
    install_name_tool -id @loader_path/libMoltenVK.dylib "$game/lib/libMoltenVK.dylib"
    install_name_tool -change "$old" @loader_path/lib/libvulkan.1.dylib "$game/lantern-reach"
    codesign --force --sign - "$game/lib/libvulkan.1.dylib"
    codesign --force --sign - "$game/lib/libMoltenVK.dylib"
    codesign --force --sign - "$game/lantern-reach"
    python3 - "$molten/etc/vulkan/icd.d/MoltenVK_icd.json" "$game/lib/MoltenVK_icd.json" <<'PY'
import json
from pathlib import Path
import sys

original, target = map(Path, sys.argv[1:])
content = json.loads(original.read_text())
content["ICD"]["library_path"] = "./libMoltenVK.dylib"
target.write_text(json.dumps(content, sort_keys=True) + "\n")
PY
fi
cat > "$game/play" <<'LAUNCHER'
#!/bin/sh
set -eu
game=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$game/lib/MoltenVK_icd.json" ]; then
    export VK_DRIVER_FILES="$game/lib/MoltenVK_icd.json"
    export SDL_VULKAN_LIBRARY="$game/lib/libvulkan.1.dylib"
fi
exec "$game/lantern-reach" "$@"
LAUNCHER
chmod +x "$game/play"
python3 - "$game" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
levels = root / "levels"
manifest = json.loads((levels / "manifest.json").read_text())
assert set(manifest["outputs"]) == {p.name for p in levels.iterdir() if p.suffix in (".scene", ".mesh")}
for name, record in manifest["outputs"].items():
    assert hashlib.sha256((levels / name).read_bytes()).hexdigest() == record["sha256"], name
(root / "SHA256SUMS").write_text("".join(
    f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root)}\n"
    for path in sorted(root.rglob("*")) if path.is_file()))
PY
archive="$output/the-lantern-reach-$platform.tar.gz"
COPYFILE_DISABLE=1 tar -C "$tmp" -czf "$tmp/archive.tar.gz" The-Lantern-Reach
mv "$tmp/archive.tar.gz" "$archive"
printf '%s\n' "$archive"
