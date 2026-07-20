"""Bake a bounded glTF 2.0 static scene into the Emberfall mesh format."""

import base64
import binascii
import json
import math
import re
import struct


MAX_DOCUMENT = 2_000_000
MAX_BINARY = 8_000_000
MAX_VERTICES = 16_384
MAX_PRIMITIVES = 32
MAX_NODES = 256
FILE_NAME = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}\.bin")
COMPONENTS = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)


class ImportError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ImportError(message)


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON property {key!r}")
        result[key] = value
    return result


def invalid_constant(value):
    raise ImportError(f"Invalid JSON number {value}")


def obj(value, label):
    require(type(value) is dict, f"{label} must be an object")
    return value


def items(value, label, limit):
    require(type(value) is list and len(value) <= limit, f"{label} exceeds array contract")
    return value


def integer(value, label, maximum):
    require(type(value) is int and 0 <= value <= maximum, f"{label} is outside integer bounds")
    return value


def number(value, label, low, high):
    require(type(value) in (int, float) and low <= value <= high, f"{label} is outside numeric bounds")
    return float(value)


def vector(value, length, label, low=-4096, high=4096):
    require(type(value) is list and len(value) == length, f"{label} must have {length} elements")
    return tuple(number(x, label, low, high) for x in value)


def indexed(values, index, label):
    return values[integer(index, label, len(values)-1)]


def unpack_container(source):
    if not source.startswith(b"glTF"):
        require(len(source) <= MAX_DOCUMENT, "JSON byte budget exceeded")
        return source, None
    require(20 <= len(source) <= MAX_DOCUMENT + MAX_BINARY + 28, "Invalid GLB length")
    _, version, declared = struct.unpack_from("<4sII", source)
    require(version == 2 and declared == len(source), "Invalid GLB version or total length")
    cursor = 12
    chunks = []
    while cursor < len(source):
        require(cursor + 8 <= len(source), "Truncated GLB chunk header")
        size, tag = struct.unpack_from("<I4s", source, cursor)
        cursor += 8
        require(size % 4 == 0 and cursor + size <= len(source), "Invalid GLB chunk length")
        require(len(chunks) < 2, "Unsupported additional GLB chunk")
        chunks.append((tag, memoryview(source)[cursor:cursor+size]))
        cursor += size
    require(chunks[0][0] == b"JSON" and len(chunks[0][1]) <= MAX_DOCUMENT, "Invalid GLB JSON chunk")
    if len(chunks) == 2:
        require(chunks[1][0] == b"BIN\0" and len(chunks[1][1]) <= MAX_BINARY, "Invalid GLB BIN chunk")
    return bytes(chunks[0][1]), chunks[1][1] if len(chunks) == 2 else None


def load_buffers(root, embedded, external):
    result = []
    budget = MAX_BINARY
    buffers = items(root.get("buffers"), "buffers", 16)
    for index, raw in enumerate(buffers):
        spec = obj(raw, "buffer")
        size = integer(spec.get("byteLength"), "buffer.byteLength", budget)
        require(size > 0, "Empty glTF buffer")
        uri = spec.get("uri")
        if "uri" not in spec:
            require(index == 0 and embedded is not None and 0 <= len(embedded)-size <= 3,
                    "GLB BIN buffer does not match its declared length")
            data = embedded[:size]
        else:
            require(type(uri) is str, "Buffer URI must be a string")
            prefixes = ("data:application/octet-stream;base64,", "data:application/gltf-buffer;base64,")
            prefix = next((value for value in prefixes if uri.startswith(value)), None)
            if prefix:
                encoded = uri[len(prefix):]
                require(len(encoded) == 4*((size+2)//3), "Buffer base64 length mismatch")
                try:
                    data = base64.b64decode(encoded, validate=True)
                except (ValueError, binascii.Error) as error:
                    raise ImportError("Invalid glTF buffer base64") from error
            else:
                require(FILE_NAME.fullmatch(uri) is not None, f"Unsupported buffer URI {uri!r}")
                require(uri in external, f"Buffer {uri!r} must be declared in the catalog")
                data = external[uri]
            require(len(data) == size, "Buffer byteLength does not match data")
        budget -= size
        result.append(memoryview(data))
    require(embedded is None or buffers and "uri" not in buffers[0], "Unreferenced GLB BIN chunk")
    return result


class Accessors:
    def __init__(self, root, buffers):
        self.views = []
        for raw in items(root.get("bufferViews"), "bufferViews", 512):
            spec = obj(raw, "bufferView")
            data = indexed(buffers, spec.get("buffer"), "bufferView.buffer")
            offset = integer(spec.get("byteOffset", 0), "bufferView.byteOffset", len(data))
            size = integer(spec.get("byteLength"), "bufferView.byteLength", len(data)-offset)
            require(size > 0, "Empty bufferView")
            stride = integer(spec.get("byteStride", 0), "bufferView.byteStride", 252)
            require("byteStride" not in spec or stride >= 4 and stride % 4 == 0, "Invalid byteStride")
            self.views.append((data[offset:offset+size], offset, stride))
        self.specs = items(root.get("accessors"), "accessors", 512)

    def read(self, index, shape, allowed, vertex=False):
        spec = obj(indexed(self.specs, index, "accessor index"), "accessor")
        kind = spec.get("componentType")
        require(type(kind) is int and kind in allowed and spec.get("type") == shape, "Accessor type mismatch")
        require("sparse" not in spec and spec.get("normalized", False) is False,
                "Sparse and normalized accessors are outside the static subset")
        data, view_offset, stride = indexed(self.views, spec.get("bufferView"), "accessor.bufferView")
        require(vertex or stride == 0, "Index accessor cannot use a strided bufferView")
        count = integer(spec.get("count"), "accessor.count", MAX_VERTICES)
        require(count > 0, "Empty accessor")
        offset = integer(spec.get("byteOffset", 0), "accessor.byteOffset", len(data))
        scalar, size = COMPONENTS[kind]
        width = WIDTHS[shape]
        element = size * width
        stride = stride or element
        require(stride >= element and stride % size == 0 and offset % size == 0
                and (view_offset+offset) % size == 0 and (not vertex or offset % 4 == 0 and stride % 4 == 0),
                "Accessor alignment or stride invalid")
        require(element + stride*(count-1) <= len(data)-offset, "Accessor exceeds its bufferView")
        unpack = struct.Struct("<" + scalar*width)
        return [unpack.unpack_from(data, offset + i*stride) for i in range(count)]


def normalize(v, label):
    magnitude = math.hypot(*v)
    require(math.isfinite(magnitude) and magnitude > 1e-8, f"Degenerate {label}")
    return tuple(x/magnitude for x in v)


def multiply(a, b):
    return tuple(sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4))


def local_matrix(node):
    if "matrix" in node:
        require(not any(name in node for name in ("translation", "rotation", "scale")),
                "Node mixes matrix and TRS transforms")
        m = vector(node["matrix"], 16, "node.matrix")
        require((m[3], m[7], m[11], m[15]) == (0, 0, 0, 1), "Node matrix must be affine")
        return m
    tx, ty, tz = vector(node.get("translation", [0, 0, 0]), 3, "node.translation")
    sx, sy, sz = vector(node.get("scale", [1, 1, 1]), 3, "node.scale")
    quaternion = vector(node.get("rotation", [0, 0, 0, 1]), 4, "node.rotation", -1.001, 1.001)
    require(abs(math.hypot(*quaternion)-1.0) <= 0.002, "Node quaternion must have unit length")
    x, y, z, w = normalize(quaternion, "node quaternion")
    return ((1-2*(y*y+z*z))*sx, 2*(x*y+z*w)*sx, 2*(x*z-y*w)*sx, 0,
            2*(x*y-z*w)*sy, (1-2*(x*x+z*z))*sy, 2*(y*z+x*w)*sy, 0,
            2*(x*z+y*w)*sz, 2*(y*z-x*w)*sz, (1-2*(x*x+y*y))*sz, 0, tx, ty, tz, 1)


def cross(a, b):
    return a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]


def transpose_inverse(m):
    cols = m[0:3], m[4:7], m[8:11]
    cofactors = cross(cols[1], cols[2]), cross(cols[2], cols[0]), cross(cols[0], cols[1])
    determinant = sum(x*y for x, y in zip(cols[0], cofactors[0]))
    require(math.isfinite(determinant) and abs(determinant) > 1e-9, "Singular mesh transform")
    return tuple(x/determinant for col in cofactors for x in col), determinant < 0


def material(raw):
    value = obj(raw, "material")
    pbr = obj(value.get("pbrMetallicRoughness", {}), "pbrMetallicRoughness")
    require(value.get("alphaMode", "OPAQUE") == "OPAQUE", "Only OPAQUE materials are supported")
    require(not any(name.endswith("Texture") for name in (*value, *pbr)), "Textured materials are unsupported")
    color = vector(pbr.get("baseColorFactor", [1, 1, 1, 1]), 4, "baseColorFactor", 0, 1)
    metal = number(pbr.get("metallicFactor", 1), "metallicFactor", 0, 1)
    roughness = number(pbr.get("roughnessFactor", 1), "roughnessFactor", 0, 1)
    emissive = vector(value.get("emissiveFactor", [0, 0, 0]), 3, "emissiveFactor", 0, 1)
    return (*color[:3], metal, roughness, *emissive)


def bake_primitive(raw, matrix, access, materials, budget):
    primitive = obj(raw, "primitive")
    require(type(primitive.get("mode", 4)) is int and primitive.get("mode", 4) == 4
            and "targets" not in primitive, "Only static triangle lists are supported")
    attributes = obj(primitive.get("attributes"), "primitive.attributes")
    require(set(attributes) <= {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"},
            "Unsupported primitive vertex attribute")
    positions = access.read(attributes.get("POSITION"), "VEC3", (5126,), vertex=True)
    normals = access.read(attributes["NORMAL"], "VEC3", (5126,), vertex=True) if "NORMAL" in attributes else None
    require(normals is None or len(normals) == len(positions), "Vertex normal count mismatch")
    if "indices" in primitive:
        indices = [value for (value,) in access.read(primitive["indices"], "SCALAR", (5121, 5123, 5125))]
    else:
        indices = list(range(len(positions)))
    require(len(indices) % 3 == 0 and len(indices) <= budget, "Triangle count or expanded vertex budget exceeded")
    require(max(indices) < len(positions), "Triangle index outside position array")
    inv, reflected = transpose_inverse(matrix)
    palette = indexed(materials, primitive["material"], "primitive.material") if "material" in primitive else material({})
    positions = [tuple(sum(matrix[col*4+row]*p[col] for col in range(3)) + matrix[12+row]
                       for row in range(3)) for p in positions]
    require(all(math.isfinite(x) and abs(x) <= 4096 for p in positions for x in p), "Transformed position outside bounds")
    if normals is not None:
        normals = [normalize(tuple(sum(inv[col*3+row]*n[col] for col in range(3)) for row in range(3)),
                             "vertex normal") for n in normals]
    data = bytearray(struct.pack("<I8f", len(indices), *palette))
    for first in range(0, len(indices), 3):
        a, b, c = indices[first:first+3]
        if reflected:
            b, c = c, b
        if normals is None:
            p, q, r = positions[a], positions[b], positions[c]
            normal = normalize(cross(tuple(q[i]-p[i] for i in range(3)), tuple(r[i]-p[i] for i in range(3))),
                               "triangle normal")
        for i in (a, b, c):
            data.extend(struct.pack("<6f", *positions[i], *(normals[i] if normals is not None else normal)))
    return bytes(data), len(indices)


def bake(source, external):
    document, binary = unpack_container(source)
    try:
        root = obj(json.loads(document.decode("utf-8"), object_pairs_hook=object_pairs,
                              parse_constant=invalid_constant), "glTF root")
    except (UnicodeError, RecursionError) as error:
        raise ImportError("Invalid or deeply nested glTF JSON") from error
    asset = obj(root.get("asset"), "asset")
    require(asset.get("version") == "2.0" and asset.get("minVersion", "2.0") == "2.0",
            "Only glTF 2.0 is supported")
    require(not any(root.get(name) for name in ("extensionsRequired", "animations", "skins")),
            "Required extensions, animations and skins are outside the static subset")
    access = Accessors(root, load_buffers(root, binary, external))
    meshes = items(root.get("meshes"), "meshes", MAX_NODES)
    nodes = items(root.get("nodes"), "nodes", MAX_NODES)
    scenes = items(root.get("scenes"), "scenes", 32)
    scene = obj(indexed(scenes, root.get("scene", 0), "scene"), "scene")
    materials = [material(raw) for raw in items(root.get("materials", []), "materials", 64)]
    budget = MAX_VERTICES
    visited = set()
    baked = []

    def visit(index, parent, depth):
        nonlocal budget
        node = obj(indexed(nodes, index, "node index"), "node")
        require(depth <= 32 and index not in visited, "Scene contains a cycle, repeated node or deep hierarchy")
        visited.add(index)
        require("skin" not in node and "weights" not in node, "Skinned and morphed nodes are unsupported")
        matrix = multiply(parent, local_matrix(node))
        require(all(math.isfinite(value) and abs(value) < 100_000 for value in matrix),
                "Composed node transform exceeds bounds")
        if "mesh" in node:
            mesh = obj(indexed(meshes, node["mesh"], "node.mesh"), "mesh")
            for raw in items(mesh.get("primitives"), "mesh.primitives", MAX_PRIMITIVES):
                require(len(baked) < MAX_PRIMITIVES, "Baked primitive budget exceeded")
                data, count = bake_primitive(raw, matrix, access, materials, budget)
                baked.append(data)
                budget -= count
        for child in items(node.get("children", []), "node.children", MAX_NODES):
            visit(child, matrix, depth+1)

    for index in items(scene.get("nodes"), "scene.nodes", MAX_NODES):
        visit(index, IDENTITY, 1)
    require(baked, "Selected scene contains no triangles")
    return struct.pack("<8sI", b"EMBMSH01", len(baked)) + b"".join(baked)
