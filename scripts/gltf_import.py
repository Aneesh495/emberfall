"""Bake bounded glTF 2.0 meshes and animation into the Emberfall mesh format."""

import base64
import binascii
import json
import math
import re
import struct

import texture_cook


MAX_DOCUMENT = 2_000_000
MAX_BINARY = 8_000_000
MAX_VERTICES = 16_384
MAX_PRIMITIVES = 32
MAX_NODES = 256
FILE_NAME = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}\.bin")
COMPONENTS = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
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

    def read(self, index, shape, allowed, vertex=False, weights=False):
        spec = obj(indexed(self.specs, index, "accessor index"), "accessor")
        kind = spec.get("componentType")
        require(type(kind) is int and kind in allowed and spec.get("type") == shape, "Accessor type mismatch")
        normalized = weights and kind != 5126
        require("sparse" not in spec and spec.get("normalized", False) is normalized,
                "Sparse or invalid normalized accessor")
        data, view_offset, stride = indexed(self.views, spec.get("bufferView"), "accessor.bufferView")
        require(vertex or stride == 0, "Non-vertex accessor cannot use a strided bufferView")
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
        records = [unpack.unpack_from(data, offset + i*stride) for i in range(count)]
        if normalized:
            maximum = (1 << (8*size))-1
            return [tuple(value/maximum for value in record) for record in records]
        return records


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


class Textures:
    def __init__(self, root, access, external):
        self.images = items(root.get("images", []), "images", texture_cook.MAX_LAYERS)
        self.textures = items(root.get("textures", []), "textures", texture_cook.MAX_LAYERS)
        self.samplers = items(root.get("samplers", []), "samplers", texture_cook.MAX_LAYERS)
        self.access = access
        self.external = external
        self.decoded = {}
        self.indices = {}
        self.layers = []

    def source(self, index):
        image = obj(indexed(self.images, index, "texture.source"), "image")
        if "bufferView" in image:
            require("uri" not in image and image.get("mimeType") == "image/png", "Expected a PNG image bufferView")
            data, _, stride = indexed(self.access.views, image["bufferView"], "image.bufferView")
            require(stride == 0, "Image bufferView cannot be strided")
            return data
        uri = image.get("uri")
        require(type(uri) is str, "Image URI must be a string")
        prefix = "data:image/png;base64,"
        if uri.startswith(prefix):
            encoded = uri[len(prefix):]
            require(len(encoded) <= 4*((texture_cook.MAX_PNG+2)//3), "PNG base64 exceeds byte budget")
            try:
                return base64.b64decode(encoded, validate=True)
            except (ValueError, binascii.Error) as error:
                raise ImportError("Invalid PNG base64") from error
        require(re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,79}\.png", uri) is not None
                and uri in self.external, "Local PNG must be declared in the catalog")
        return self.external[uri]

    def bind(self, raw, srgb):
        if raw is None:
            return 0
        info = obj(raw, "textureInfo")
        require(integer(info.get("texCoord", 0), "textureInfo.texCoord", 0) == 0 and not info.get("extensions"),
                "Texture requires unsupported coordinates or extensions")
        texture = obj(indexed(self.textures, info.get("index"), "textureInfo.index"), "texture")
        if "sampler" in texture:
            sampler = obj(indexed(self.samplers, texture["sampler"], "texture.sampler"), "sampler")
            require(all(sampler.get(name, default) == default for name, default in
                        (("magFilter", 9729), ("minFilter", 9987), ("wrapS", 10497), ("wrapT", 10497))),
                    "Texture sampler requires repeat and trilinear filtering")
        image = integer(texture.get("source"), "texture.source", len(self.images)-1)
        key = (image, srgb)
        if key not in self.indices:
            require(len(self.layers) < texture_cook.MAX_LAYERS, "Texture layer budget exceeded")
            if image not in self.decoded:
                self.decoded[image] = texture_cook.decode_png(self.source(image))
            self.layers.append(texture_cook.mip_chain(self.decoded[image], srgb))
            self.indices[key] = len(self.layers)
        return self.indices[key]


def material(raw, textures):
    value = obj(raw, "material")
    pbr = obj(value.get("pbrMetallicRoughness", {}), "pbrMetallicRoughness")
    require(value.get("alphaMode", "OPAQUE") == "OPAQUE", "Only OPAQUE materials are supported")
    require(not any(name.endswith("Texture") and name != "normalTexture" for name in value) and
            not any(name.endswith("Texture") and name not in ("baseColorTexture", "metallicRoughnessTexture")
                    for name in pbr), "Unsupported material texture role")
    base = textures.bind(pbr.get("baseColorTexture"), True)
    metal_rough = textures.bind(pbr.get("metallicRoughnessTexture"), False)
    normal = textures.bind(value.get("normalTexture"), False)
    scale = number(value["normalTexture"].get("scale", 1), "normalTexture.scale", 0, 4) if normal else 1
    color = vector(pbr.get("baseColorFactor", [1, 1, 1, 1]), 4, "baseColorFactor", 0, 1)
    metal = number(pbr.get("metallicFactor", 1), "metallicFactor", 0, 1)
    roughness = number(pbr.get("roughnessFactor", 1), "roughnessFactor", 0, 1)
    emissive = vector(value.get("emissiveFactor", [0, 0, 0]), 3, "emissiveFactor", 0, 1)
    return (*color[:3], metal, roughness, *emissive), (base, metal_rough, normal, scale)


def bake_rig(root, access, nodes, parents):
    skins = items(root.get("skins", []), "skins", 1)
    skin = obj(indexed(skins, 0, "skin"), "skin")
    joints = [integer(node, "joint index", len(nodes)-1) for node in items(skin.get("joints"), "joints", 128)]
    require(joints and len(joints) == len(set(joints)) and all(node in parents for node in joints),
            "Skin joints must be unique nodes in the selected scene")
    inverse = access.read(skin["inverseBindMatrices"], "MAT4", (5126,)) if "inverseBindMatrices" in skin else [IDENTITY]*len(joints)
    require(len(inverse) == len(joints), "Inverse bind count mismatch")
    binds = dict(zip(joints, inverse))
    # Scene traversal is parent-first. glTF vertex indices still refer to the original skin.joints order.
    ordered = [node for node in parents if node in binds]
    indices = {node: i for i, node in enumerate(ordered)}
    depths = {}
    clips = items(root.get("animations", []), "animations", 8)
    data = bytearray(struct.pack("<II", len(joints), len(clips)))
    for index in ordered:
        parent = parents[index]
        require(parent is None or parent in indices, "Joint parent must also belong to the skin")
        depths[index] = 1 + depths.get(parent, 0)
        require(depths[index] <= 16, "Joint hierarchy too deep")
        node = nodes[index]
        require("matrix" not in node and "mesh" not in node, "Joints must use TRS and carry no mesh")
        translation = vector(node.get("translation", [0, 0, 0]), 3, "joint translation")
        rotation = vector(node.get("rotation", [0, 0, 0, 1]), 4, "joint rotation", -1.001, 1.001)
        scale = vector(node.get("scale", [1, 1, 1]), 3, "joint scale", 0.75, 1.25)
        matrix = binds[index]
        axes = matrix[0:3], matrix[4:7], matrix[8:11]
        require(all(abs(v) <= 4096 for v in matrix) and
                (matrix[3], matrix[7], matrix[11], matrix[15]) == (0, 0, 0, 1) and
                all(abs(sum(a*b for a, b in zip(axes[i], axes[j])) - (i == j)) < 0.0019
                    for i in range(3) for j in range(i, 3)), "Inverse bind must be a rigid affine matrix")
        data.extend(struct.pack("<I26f", indices[parent]+1 if parent is not None else 0,
                                *translation, *normalize(rotation, "joint rotation"), *scale, *matrix))
    remaining = 16_384
    paths = {"translation": ("VEC3", -4096, 4096), "rotation": ("VEC4", -1.001, 1.001),
             "scale": ("VEC3", 0.75, 1.25)}
    for raw in clips:
        clip = obj(raw, "animation")
        samplers = items(clip.get("samplers"), "animation samplers", 384)
        channels = {}
        duration = 0
        for raw_channel in items(clip.get("channels"), "animation channels", 384):
            channel = obj(raw_channel, "channel")
            target = obj(channel.get("target"), "channel target")
            node = integer(target.get("node"), "animation target", len(nodes)-1)
            path = target.get("path")
            require(node in indices and type(path) is str and path in paths and (node, path) not in channels,
                    "Invalid or duplicate animation target")
            sampler = obj(indexed(samplers, channel.get("sampler"), "animation sampler"), "sampler")
            mode = sampler.get("interpolation", "LINEAR")
            require(mode in ("LINEAR", "STEP"), "Unsupported animation interpolation")
            times = [t for (t,) in access.read(sampler.get("input"), "SCALAR", (5126,))]
            require(len(times) <= min(4096, remaining) and 0 <= times[0] <= times[-1] <= 3600
                    and all(a < b for a, b in zip(times, times[1:])), "Animation key order or budget")
            shape, low, high = paths[path]
            values = access.read(sampler.get("output"), shape, (5126,))
            require(len(values) == len(times) and all(low <= v <= high for value in values for v in value),
                    "Animation key value count or range")
            if path == "rotation":
                require(all(abs(math.hypot(*q)-1) <= 0.0009 for q in values), "Invalid animation quaternion")
                values = [normalize(q, "animation quaternion") for q in values]
            remaining -= len(times)
            duration = max(duration, times[-1])
            record = struct.Struct("<" + "f"*(1+WIDTHS[shape]))
            channels[node, path] = (struct.pack("<II", len(times), mode == "STEP") +
                                    b"".join(record.pack(t, *value) for t, value in zip(times, values)))
        require(duration > 0, "Animation duration must be positive")
        data.extend(struct.pack("<f", duration))
        for node in ordered:
            for path in paths:
                data.extend(channels.get((node, path), b"\0"*4))
    return bytes(data), [indices[joint] for joint in joints]


def bake_primitive(raw, matrix, access, materials, fallback, budget, textured, joint_remap):
    primitive = obj(raw, "primitive")
    require(type(primitive.get("mode", 4)) is int and primitive.get("mode", 4) == 4
            and "targets" not in primitive, "Only triangle lists without morphs are supported")
    attributes = obj(primitive.get("attributes"), "primitive.attributes")
    skin_attributes = {"JOINTS_0", "WEIGHTS_0"}
    supported = {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"}
    if joint_remap is not None:
        require(skin_attributes | {"NORMAL"} <= attributes.keys(), "Skin requires joint, weight and normal data")
        supported |= skin_attributes
    require(set(attributes) <= supported, "Unsupported primitive vertex attribute")
    positions = access.read(attributes.get("POSITION"), "VEC3", (5126,), vertex=True)
    normals = access.read(attributes["NORMAL"], "VEC3", (5126,), vertex=True) if "NORMAL" in attributes else None
    require(normals is None or len(normals) == len(positions), "Vertex normal count mismatch")
    influences = None
    if joint_remap is not None:
        joints = access.read(attributes["JOINTS_0"], "VEC4", (5121, 5123), vertex=True)
        weights = access.read(attributes["WEIGHTS_0"], "VEC4", (5121, 5123, 5126), vertex=True, weights=True)
        require(len(joints) == len(weights) == len(positions) and
                all(j < len(joint_remap) for vertex in joints for j in vertex), "Skin joint index or count")
        require(all(all(0 <= w <= 1 for w in row) and abs(sum(row)-1) <= 0.0019 for row in weights),
                "Skin weights must sum to one")
        influences = [struct.pack("<4B4f", *(joint_remap[j] for j in vertex), *(w/sum(row) for w in row))
                      for vertex, row in zip(joints, weights)]
    if "indices" in primitive:
        indices = [value for (value,) in access.read(primitive["indices"], "SCALAR", (5121, 5123, 5125))]
    else:
        indices = list(range(len(positions)))
    require(len(indices) % 3 == 0 and len(indices) <= budget, "Triangle count or expanded vertex budget exceeded")
    require(max(indices) < len(positions), "Triangle index outside position array")
    inv, reflected = transpose_inverse(matrix)
    palette, layers = indexed(materials, primitive["material"], "primitive.material") if "material" in primitive else fallback
    uv = access.read(attributes["TEXCOORD_0"], "VEC2", (5126,), vertex=True) if "TEXCOORD_0" in attributes else None
    require(not any(layers[:3]) or uv is not None, "Textured primitive requires TEXCOORD_0")
    if uv is not None:
        require(len(uv) == len(positions) and all(-1024 <= v <= 1024 for pair in uv for v in pair),
                "UV count or range outside bounds")
    tangent = access.read(attributes["TANGENT"], "VEC4", (5126,), vertex=True) if "TANGENT" in attributes else None
    require(not layers[2] or tangent is not None and normals is not None, "Normal map requires tangent and normal data")
    if tangent is not None:
        require(len(tangent) == len(positions) and all(abs(t[3]) == 1 and abs(math.hypot(*t[:3])-1) < 0.002 for t in tangent),
                "Tangent count, length or handedness outside bounds")
        sign = -1 if reflected else 1
        tangent = [(*normalize(tuple(sum(matrix[col*4+row]*t[col] for col in range(3)) for row in range(3)),
                                "vertex tangent"), t[3]*sign) for t in tangent]
    positions = [tuple(sum(matrix[col*4+row]*p[col] for col in range(3)) + matrix[12+row]
                       for row in range(3)) for p in positions]
    require(all(math.isfinite(x) and abs(x) <= 4096 for p in positions for x in p), "Transformed position outside bounds")
    if normals is not None:
        normals = [normalize(tuple(sum(inv[col*3+row]*n[col] for col in range(3)) for row in range(3)),
                             "vertex normal") for n in normals]
    if layers[2]:
        require(all(abs(sum(x*y for x, y in zip(n, t[:3]))) < 0.01 for n, t in zip(normals, tangent)),
                "Normal map tangent is not perpendicular")
    data = bytearray(struct.pack("<I8f", len(indices), *palette))
    if textured:
        data.extend(struct.pack("<3If", *layers))
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
            if textured:
                data.extend(struct.pack("<6f", *(uv[i] if uv is not None else (0, 0)),
                                        *(tangent[i] if tangent is not None else (0, 0, 0, 1))))
            if influences is not None:
                data.extend(influences[i])
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
    require(not root.get("extensionsRequired"), "Required glTF extensions are unsupported")
    access = Accessors(root, load_buffers(root, binary, external))
    meshes = items(root.get("meshes"), "meshes", MAX_NODES)
    nodes = items(root.get("nodes"), "nodes", MAX_NODES)
    scenes = items(root.get("scenes"), "scenes", 32)
    scene = obj(indexed(scenes, root.get("scene", 0), "scene"), "scene")
    textures = Textures(root, access, external)
    materials = [material(raw, textures) for raw in items(root.get("materials", []), "materials", 64)]
    fallback = material({}, textures)
    parents = {}
    models = []

    def visit(index, parent, parent_matrix, depth):
        node = obj(indexed(nodes, index, "node index"), "node")
        require(depth <= 32 and index not in parents, "Scene contains a cycle, repeated node or deep hierarchy")
        parents[index] = parent
        require("weights" not in node and ("skin" not in node or "mesh" in node), "Invalid mesh or morph node")
        matrix = multiply(parent_matrix, local_matrix(node))
        require(all(math.isfinite(value) and abs(value) < 100_000 for value in matrix),
                "Composed node transform exceeds bounds")
        if "mesh" in node:
            models.append((node, parent, matrix))
        for child in items(node.get("children", []), "node.children", MAX_NODES):
            visit(child, index, matrix, depth+1)

    for index in items(scene.get("nodes"), "scene.nodes", MAX_NODES):
        visit(index, None, IDENTITY, 1)
    skinned = any("skin" in node for node, _, _ in models)
    rig, joint_remap = b"", None
    if skinned:
        require(len(models) == 1, "Skin subset supports one mesh node")
        node, parent, matrix = models[0]
        require(type(node["skin"]) is int and node["skin"] == 0 and parent is None and matrix == IDENTITY,
                "Skinned mesh must be an untransformed scene root using skin 0")
        rig, joint_remap = bake_rig(root, access, nodes, parents)
    else:
        require(not root.get("animations") and not root.get("skins"), "Animation requires a selected skin")
    textured = bool(textures.layers) or skinned
    budget = MAX_VERTICES
    baked = []
    for node, _, matrix in models:
        mesh = obj(indexed(meshes, node["mesh"], "node.mesh"), "mesh")
        for raw in items(mesh.get("primitives"), "mesh.primitives", MAX_PRIMITIVES):
            require(len(baked) < MAX_PRIMITIVES, "Baked primitive budget exceeded")
            data, count = bake_primitive(raw, matrix, access, materials, fallback, budget, textured, joint_remap)
            baked.append(data)
            budget -= count
    require(baked, "Selected scene contains no triangles")
    if textured:
        magic = b"EMBMSH03" if skinned else b"EMBMSH02"
        return (struct.pack("<8sII", magic, len(baked), len(textures.layers)) +
                b"".join(textures.layers) + rig + b"".join(baked))
    return struct.pack("<8sI", b"EMBMSH01", len(baked)) + b"".join(baked)
