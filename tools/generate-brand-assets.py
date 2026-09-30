"""Generate and validate DUWN icon assets from the approved clean master.

Development dependencies: Pillow, NumPy, SciPy. Run with Python 3.
"""

from io import BytesIO
from pathlib import Path
import struct

import numpy as np
from PIL import Image
from scipy.ndimage import label


ASSETS = Path(__file__).resolve().parents[1] / "assets"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256, 512)
ICO_SIZES = SIZES[:-1]


def validate_png(path, size):
    with Image.open(path) as image:
        if image.size != (size, size) or image.mode != "RGBA":
            raise ValueError(f"{path.name}: expected {size}x{size} RGBA")
        rgba = image.copy()
    alpha = rgba.getchannel("A")
    if any(alpha.getpixel(corner) for corner in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1))):
        raise ValueError(f"{path.name}: opaque corner")
    if not alpha.getbbox():
        raise ValueError(f"{path.name}: empty icon")
    _, components = label(np.asarray(alpha) > 0, structure=np.ones((3, 3), dtype=bool))
    if components != 1:
        raise ValueError(f"{path.name}: {components - 1} detached pixel groups")
    if any(rgba.getpixel((x, y))[:3] != (0, 0, 0)
           for y in range(size) for x in range(size) if alpha.getpixel((x, y)) == 0):
        raise ValueError(f"{path.name}: colored fully transparent pixel")
    return rgba


def validate_master(master):
    alpha = np.asarray(master.getchannel("A"))
    if np.any(alpha[0]) or np.any(alpha[-1]) or np.any(alpha[:, 0]) or np.any(alpha[:, -1]):
        raise ValueError("master: stray pixel on canvas border")
    _, components = label(alpha > 0, structure=np.ones((3, 3), dtype=bool))
    if components != 1:
        raise ValueError(f"master: {components - 1} detached pixel groups")


def ico_bmp_frame(image):
    size = image.width
    bgra = image.tobytes("raw", "BGRA")
    xor = b"".join(bgra[y * size * 4:(y + 1) * size * 4]
                   for y in range(size - 1, -1, -1))
    stride = ((size + 31) // 32) * 4
    alpha = image.getchannel("A")
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        row = bytearray(stride)
        for x in range(size):
            if alpha.getpixel((x, y)) == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask.extend(row)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32,
                         0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + xor + mask


def write_ico(images, path):
    frames = []
    for size in ICO_SIZES:
        image = images[size]
        if size == 256:
            buffer = BytesIO()
            image.save(buffer, format="PNG")
            frames.append(buffer.getvalue())
        else:
            frames.append(ico_bmp_frame(image))
    offset = 6 + len(frames) * 16
    with path.open("wb") as output:
        output.write(struct.pack("<HHH", 0, 1, len(frames)))
        for size, frame in zip(ICO_SIZES, frames):
            dimension = 0 if size == 256 else size
            output.write(struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                                     1, 32, len(frame), offset))
            offset += len(frame)
        for frame in frames:
            output.write(frame)


def validate_ico(path):
    data = path.read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if (reserved, kind, count) != (0, 1, len(ICO_SIZES)):
        raise ValueError("ICO header or frame count is invalid")
    found = []
    for index in range(count):
        width, height, _, _, planes, depth, length, offset = struct.unpack_from(
            "<BBBBHHII", data, 6 + index * 16)
        size = width or 256
        if size != (height or 256) or planes != 1 or depth != 32 or offset + length > len(data):
            raise ValueError(f"ICO frame {index} is invalid")
        found.append(size)
    if tuple(found) != ICO_SIZES:
        raise ValueError(f"ICO sizes {found} do not match expected {ICO_SIZES}")
    with Image.open(path) as icon:
        for size in ICO_SIZES:
            icon.ico.getimage((size, size)).load()
    print("ICO frames:", ", ".join(map(str, found)))


def main():
    master = validate_png(ASSETS / "logo_master_clean.png", 1024)
    validate_master(master)
    images = {}
    for size in SIZES:
        image = master.resize((size, size), Image.Resampling.LANCZOS)
        # Pillow resamples RGBA with alpha-aware filtering. Keep straight RGBA
        # in PNG and clear invisible RGB to prevent matte leakage in consumers.
        pixels = bytearray(image.tobytes())
        alpha = np.asarray(image.getchannel("A"))
        components, count = label(alpha > 0, structure=np.ones((3, 3), dtype=bool))
        sizes = np.bincount(components.ravel())
        sizes[0] = 0
        detached = components != sizes.argmax()
        for x, y in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1)):
            detached[y, x] = True
        detached_flat = detached.ravel()
        for i in range(0, len(pixels), 4):
            if detached_flat[i // 4] or pixels[i + 3] == 0:
                pixels[i:i + 4] = b"\0\0\0\0"
        image = Image.frombytes("RGBA", image.size, bytes(pixels))
        path = ASSETS / f"logo_{size}x{size}.png"
        image.save(path)
        images[size] = validate_png(path, size)
    ico_path = ASSETS / "app_icon.ico"
    write_ico(images, ico_path)
    validate_ico(ico_path)


if __name__ == "__main__":
    main()
