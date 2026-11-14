# shot2png.py - convert a framebuffer dump from the device into a PNG screenshot
#
# The firmware has a hidden serial command:
#     shot
# which prints "[shot] 360 360" followed by 360*360*2 raw bytes (RGB565, little endian).
#
# Usage:
#     python shot2png.py shot.bin screenshot.png
import struct
import sys

from PIL import Image


def main():
    if len(sys.argv) < 2:
        print("usage: python shot2png.py <shot.bin> [out.png]")
        return 1
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "screenshot.png"

    data = open(src, "rb").read()
    if len(data) != 360 * 360 * 2:
        raise SystemExit("unexpected size: %d bytes (want %d)" % (len(data), 360 * 360 * 2))

    img = Image.new("RGB", (360, 360))
    px = img.load()
    for y in range(360):
        row = y * 360
        for x in range(360):
            i = (row + x) * 2
            v = data[i] | (data[i + 1] << 8)
            px[x, y] = (
                ((v >> 11) & 0x1F) * 255 // 31,
                ((v >> 5) & 0x3F) * 255 // 63,
                (v & 0x1F) * 255 // 31,
            )
    img.save(out)
    print("saved", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
