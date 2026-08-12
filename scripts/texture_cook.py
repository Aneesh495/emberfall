"""Decode bounded PNG source textures and bake role-aware RGBA mip chains."""

import struct
import zlib


SIZE = 64
MAX_PNG = 65_536
MAX_LAYERS = 16
LEVELS = SIZE.bit_length()
LAYER_BYTES = 4 * sum((SIZE >> level)**2 for level in range(LEVELS))


def check(condition, message):
    if not condition:
        raise ValueError(message)


def paeth(left, above, corner):
    prediction = left + above - corner
    dl, da, dc = (abs(prediction-v) for v in (left, above, corner))
    if dl <= da and dl <= dc:
        return left
    return above if da <= dc else corner


def decode_png(source):
    check(8 < len(source) <= MAX_PNG and source[:8] == b"\x89PNG\r\n\x1a\n", "Invalid PNG header or size")
    offset = 8
    stride = 0
    compressed = bytearray()
    idat_closed = False
    ended = False
    while offset < len(source):
        check(len(source)-offset >= 12, "Truncated PNG chunk")
        size, kind = struct.unpack_from(">I4s", source, offset)
        end = offset + 12 + size
        check(end <= len(source), "Truncated PNG chunk")
        data = source[offset+8:end-4]
        checksum, = struct.unpack_from(">I", source, end-4)
        check(zlib.crc32(source[offset+4:end-4]) == checksum, "Invalid PNG checksum")
        if offset == 8:
            check(kind == b"IHDR" and size == 13, "PNG must start with IHDR")
            width, height, bits, color, compression, filtering, interlace = struct.unpack(">2I5B", data)
            check((width, height, bits, compression, filtering, interlace) == (SIZE, SIZE, 8, 0, 0, 0)
                  and color in (2, 6), "PNG must be 64x64 noninterlaced RGB or RGBA8")
            channels = 3 if color == 2 else 4
            stride = SIZE * channels
        elif kind == b"IDAT":
            check(not idat_closed, "PNG IDAT chunks must be consecutive")
            compressed.extend(data)
        elif kind == b"IEND":
            check(not size and end == len(source), "Invalid PNG terminator")
            ended = True
        else:
            check(kind[:1].islower() and kind not in (b"tRNS", b"acTL", b"fcTL", b"fdAT"),
                  "Unsupported PNG chunk")
            idat_closed |= bool(compressed)
        offset = end

    check(ended and stride and compressed, "PNG is missing pixels or IEND")
    expected = SIZE*(stride+1)
    stream = zlib.decompressobj()
    try:
        filtered = stream.decompress(compressed, expected+1)
    except zlib.error as error:
        raise ValueError("Invalid PNG compressed pixels") from error
    check(stream.eof and not stream.unused_data and len(filtered) == expected, "PNG pixel length mismatch")
    rgba = bytearray()
    prior = bytearray(stride)
    for row in range(SIZE):
        start = row*(stride+1)
        method = filtered[start]
        check(method <= 4, "Invalid PNG row filter")
        scanline = bytearray(filtered[start+1:start+1+stride])
        for i in range(stride):
            left = scanline[i-channels] if i >= channels else 0
            above = prior[i]
            corner = prior[i-channels] if i >= channels else 0
            predictor = (0, left, above, (left+above)//2)[method] if method < 4 else paeth(left, above, corner)
            scanline[i] = (scanline[i]+predictor) & 255
        if channels == 4:
            rgba.extend(scanline)
        else:
            for i in range(0, stride, channels):
                rgba.extend(scanline[i:i+3])
                rgba.append(255)
        prior = scanline
    return rgba


def to_linear(value):
    value /= 255
    return value/12.92 if value <= 0.04045 else ((value+0.055)/1.055)**2.4


def to_srgb(value):
    encoded = value*12.92 if value <= 0.0031308 else 1.055*value**(1/2.4)-0.055
    return round(encoded*255)


def mip_chain(pixels, srgb):
    result = bytearray()
    size = SIZE
    while size:
        result.extend(pixels)
        half = size//2
        output = bytearray(half*half*4)
        for y in range(half):
            for x in range(half):
                start = (y*size+x)*8
                indices = (start, start+4, start+4*size, start+4*size+4)
                for channel in range(4):
                    values = [pixels[i+channel] for i in indices]
                    output[(y*half+x)*4+channel] = (to_srgb(sum(map(to_linear, values))*0.25)
                                                  if srgb and channel < 3 else (sum(values)+2)//4)
        size = half
        pixels = output
    return bytes(result)
