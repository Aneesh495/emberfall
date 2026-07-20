import base64
import copy
import importlib.util
import json
import struct
import sys

spec = importlib.util.spec_from_file_location("gltf_import", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# Interleaved attributes and unsigned-short indices. Reflection must flip winding.
points = [(0, 0, 0), (2, 0, 0), (0, 2, 0)]
binary = b"".join(struct.pack("<6f", *point, 0, 0, 1) for point in points) + struct.pack("<3H", 0, 1, 2)
document = {
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": len(binary), "uri": "geometry.bin"}],
    "bufferViews": [
        {"buffer": 0, "byteLength": 72, "byteStride": 24},
        {"buffer": 0, "byteOffset": 72, "byteLength": 6},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ],
    "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 1, 1],
                                            "metallicFactor": 0.75, "roughnessFactor": 0.5}}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
    "nodes": [{"translation": [5, 2, 0], "children": [1]}, {"mesh": 0, "scale": [-2, 3, 1]}],
    "scenes": [{"nodes": [0]}],
}


def encode(asset):
    return json.dumps(asset).encode()


def check_rejection(asset, expected, payload=None):
    try:
        mod.bake(encode(asset), {"geometry.bin": binary if payload is None else payload})
    except ValueError as error:
        assert expected in str(error), (expected, str(error))
    else:
        raise AssertionError("Malformed glTF was accepted")


output = mod.bake(encode(document), {"geometry.bin": binary})
assert struct.unpack_from("<8sII8f", output) == (b"EMBMSH01", 1, 3, .5, .25, 1, .75, .5, 0, 0, 0)
assert list(struct.iter_unpack("<6f", output[48:])) == [
    (5., 2., 0., 0., 0., 1.), (5., 8., 0., 0., 0., 1.), (1., 2., 0., 0., 0., 1.)]

glb_root = copy.deepcopy(document)
del glb_root["buffers"][0]["uri"]
json_chunk = encode(glb_root)
json_chunk += b" " * (-len(json_chunk) % 4)
bin_chunk = binary + b"\0" * (-len(binary) % 4)
chunks = struct.pack("<I4s", len(json_chunk), b"JSON") + json_chunk
chunks += struct.pack("<I4s", len(bin_chunk), b"BIN\0") + bin_chunk
assert mod.bake(struct.pack("<4sII", b"glTF", 2, len(chunks)+12) + chunks, {}) == output

data_uri = copy.deepcopy(document)
data_uri["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(binary).decode()
assert mod.bake(encode(data_uri), {}) == output

for group, index, field, value, expected in (
    ("accessors", 0, "count", 4, "exceeds its bufferView"),
    ("accessors", 1, "byteOffset", 2, "alignment"),
    ("accessors", 0, "componentType", True, "type mismatch"),
    ("bufferViews", 0, "byteStride", 13, "byteStride"),
    ("bufferViews", 0, "byteLength", 200, "integer bounds"),
    ("buffers", 0, "uri", "../geometry.bin", "Unsupported buffer URI"),
    ("nodes", 1, "children", [0], "cycle"),
    ("nodes", 1, "scale", [0, 1, 1], "Singular"),
    ("materials", 0, "alphaMode", "BLEND", "OPAQUE"),
):
    malformed = copy.deepcopy(document)
    malformed[group][index][field] = value
    check_rejection(malformed, expected)
check_rejection(document, "index outside", binary[:-2] + struct.pack("<H", 3))
check_rejection(document, "outside bounds", struct.pack("<f", float("inf")) + binary[4:])
print("glTF static import: interleaving, GLB, embedded and local data, hierarchy, materials, malformed inputs")
