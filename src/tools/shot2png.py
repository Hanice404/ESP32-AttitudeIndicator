# shot2png.py —— 把设备导出的帧缓冲 dump 还原成 PNG 截图
#
# 固件里有一个隐藏串口命令：
#     shot
# 它会先打印 "[shot] 360 360"，随后原样输出 360*360*2 字节（RGB565，小端）。
# 用串口工具（或 PowerShell 的 SerialPort）把这串数据存成 shot.bin，再运行本脚本：
#
#     python shot2png.py shot.bin screenshot.png
import sys

from PIL import Image


def main():
    if len(sys.argv) < 2:
        print("用法: python shot2png.py <shot.bin> [输出.png]")
        return 1
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "screenshot.png"

    data = open(src, "rb").read()
    if len(data) != 360 * 360 * 2:
        raise SystemExit("大小不对：%d 字节（应为 %d）" % (len(data), 360 * 360 * 2))

    img = Image.new("RGB", (360, 360))
    px = img.load()
    for y in range(360):
        row = y * 360
        for x in range(360):
            i = (row + x) * 2
            v = data[i] | (data[i + 1] << 8)  # 小端 RGB565
            px[x, y] = (
                ((v >> 11) & 0x1F) * 255 // 31,  # R: 5 位
                ((v >> 5) & 0x3F) * 255 // 63,   # G: 6 位
                (v & 0x1F) * 255 // 31,          # B: 5 位
            )
    img.save(out)
    print("已保存:", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
