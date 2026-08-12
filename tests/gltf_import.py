import base64
import copy
import importlib.util
import json
import struct
import sys
from pathlib import Path
import zlib

sys.path.insert(0, str(Path(sys.argv[1]).parent))

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

def png_chunk(tag, content):
    return struct.pack(">I4s", len(content), tag) + content + struct.pack(">I", zlib.crc32(tag+content))

def fixture_png(filtered):
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">2I5B", 64, 64, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(filtered)) + png_chunk(b"IEND", b""))

def patterned_rows():
    pixels = [bytes(((x*7+y*19) % 256 for x in range(192))) for y in range(64)]
    raw = bytearray()
    for y, row in enumerate(pixels):
        method = y % 5
        prev = pixels[y-1] if y else bytes(192)
        raw.append(method)
        for x, value in enumerate(row):
            a, b, c = row[x-3] if x >= 3 else 0, prev[x], prev[x-3] if x >= 3 else 0
            target = a+b-c
            predictor = (0, a, b, (a+b)//2, min((a,b,c), key=lambda n: abs(n-target)))[method]
            raw.append((value-predictor) % 256)
    return bytes(raw), b"".join(row[i:i+3]+b"\xff" for row in pixels for i in range(0,192,3))

raw, expected_rgba = patterned_rows()
png = fixture_png(raw)
assert mod.texture_cook.decode_png(png) == expected_rgba
checker = b"".join(

    bytes((255*(i%2),)*3 + (255,)) for i in range(64*64))
assert mod.texture_cook.mip_chain(checker, True)[-4:] == bytes((188,188,188,255))
assert mod.texture_cook.mip_chain(checker, False)[-4:] == bytes((128,128,128,255))

textured = copy.deepcopy(document)
textured["images"] = [{"uri": "albedo.png"}]
textured["textures"] = [{"source": 0}]
textured["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
textured["materials"][0]["normalTexture"] = {"index": 0}
attrs = textured["meshes"][0]["primitives"][0]["attributes"]
attrs.update(TEXCOORD_0=3, TANGENT=4)
geometry = binary+b"\0\0"+struct.pack("<18f",0,0,1,0,0,1, *(1,0,0,1)*3)
textured["buffers"][0]["byteLength"] = len(geometry)
textured["bufferViews"].extend([{"buffer":0,"byteOffset":80,"byteLength":24},
                                {"buffer":0,"byteOffset":104,"byteLength":48}])
textured["accessors"].extend([{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
                              {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"}])
images = {"geometry.bin": geometry, "albedo.png": png}
packed = mod.bake(encode(textured), images)
assert struct.unpack_from("<8sII", packed) == (b"EMBMSH02", 1, 2)
offset = 16 + 2*mod.texture_cook.LAYER_BYTES
assert struct.unpack_from("<3If",packed,offset+36) == (1,0,2,1)
assert struct.unpack_from("<6f",packed,offset+76) == (0,0,-1,0,0,-1)
for bad, message in ((png[:-1], "Truncated PNG"), (png[:-5]+b"E"+png[-4:], "checksum"),
                     (fixture_png(raw + bytes(1_000_000)), "pixel length mismatch")):
    try:
        mod.bake(encode(textured), {**images, "albedo.png": bad})
    except ValueError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError("Malformed PNG accepted")
print("glTF textures: PNG filters, sRGB mips, reflected tangents, external lineage and bounded decoding")
