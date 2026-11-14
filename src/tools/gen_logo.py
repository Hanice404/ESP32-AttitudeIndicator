# gen_logo.py —— 把位图（PNG/JPG/BMP/GIF/WebP）转成 Boot_Logo.h（360x360 RGB565 + RLE）
#
# 用法:
#   python gen_logo.py <源图> <输出 Boot_Logo.h>
#
# 注意：SVG 是矢量图，设备端无法渲染，需要先光栅化。
#       请改用 make-logo.ps1（它会调无头浏览器把 SVG 渲染成位图再转）：
#           .\make-logo.ps1 logo.svg
import os
import sys

from PIL import Image

W = H = 360


def main():
    if len(sys.argv) < 3:
        print("用法: python gen_logo.py <源图.png/.jpg/.bmp> <输出 Boot_Logo.h>")
        print("      SVG 请用: .\\make-logo.ps1 logo.svg")
        return 1

    src, out = sys.argv[1], sys.argv[2]
    if src.lower().endswith(".svg"):
        print("SVG 需要先光栅化，请使用: .\\make-logo.ps1 " + os.path.basename(src))
        return 1

    im = Image.open(src).convert("RGB")
    im.thumbnail((W, H), Image.LANCZOS)

    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(im, ((W - im.width) // 2, (H - im.height) // 2))

    # 去噪 + 量化：近黑归零（合并大片背景），其余按 16 级灰阶量化
    px = canvas.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            if r < 12 and g < 12 and b < 12:
                px[x, y] = (0, 0, 0)
            else:
                q = 16
                px[x, y] = ((r // q) * q, (g // q) * q, (b // q) * q)

    runs = []
    prev = None
    count = 0
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            if v == prev:
                count += 1
            else:
                if prev is not None:
                    runs.append((count, prev))
                prev = v
                count = 1
    runs.append((count, prev))

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("// 开机 Logo 数据（360x360 RGB565，RLE），由 gen_logo.py 生成\n")
        f.write("// 源文件: %s\n" % os.path.basename(src))
        f.write("// 格式：uint32 数组两两成对 (runLength, rgb565)\n")
        f.write("#include <stdint.h>\n\n")
        f.write("namespace bootlogo {\n")
        f.write("constexpr int kW = 360;\n")
        f.write("constexpr int kH = 360;\n")
        f.write("constexpr uint32_t kRleLen = %d;\n" % (len(runs) * 2))
        f.write("const uint32_t kRle[] = {\n")
        line = []
        for c, v in runs:
            line.append("%d,%d" % (c, v))
            if len(line) >= 10:
                f.write("  " + ",".join(line) + ",\n")
                line = []
        if line:
            f.write("  " + ",".join(line) + ",\n")
        f.write("};\n")
        f.write("}  // namespace bootlogo\n")

    print("runs: %d, flash 占用约 %.1f KB" % (len(runs), len(runs) * 4 / 1024))
    print("已生成: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
