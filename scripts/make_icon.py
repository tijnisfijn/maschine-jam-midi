# Draws assets/icon.png (needs Pillow): python3 scripts/make_icon.py assets/icon.png
import sys
from PIL import Image, ImageDraw, ImageFilter
out = sys.argv[1]
N = 1024
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
# macOS icon grid: 824 x 824 body, corner radius ~185, centred
m = (N - 824) // 2
body = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(body)
d.rounded_rectangle((m, m, N - m, N - m), radius=185, fill=(22, 22, 26, 255))
shadow = body.filter(ImageFilter.GaussianBlur(18))
img.alpha_composite(Image.eval(shadow, lambda a: a // 3))
img.alpha_composite(body)
d = ImageDraw.Draw(img)
# palette rows (full level): 4 + 4*hue + 2
pal = [(255,0,0),(255,32,0),(255,96,0),(255,224,0),(96,255,0),(72,255,0),(0,255,0),(0,255,48),
       (0,255,226),(0,96,255),(0,0,255),(40,0,255),(104,0,255),(255,0,128),(255,0,64),(255,0,16)]
# 4 x 4 pads, rainbow across, top area
pad, gap = 108, 24
x0 = (N - (4 * pad + 3 * gap)) // 2
y0 = m + 82
for r in range(4):
    for c in range(4):
        col = pal[(r * 4 + c) % 16]
        x, y = x0 + c * (pad + gap), y0 + r * (pad + gap)
        glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
        ImageDraw.Draw(glow).rounded_rectangle((x - 6, y - 6, x + pad + 6, y + pad + 6), 26, fill=col + (150,))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(14)))
        d.rounded_rectangle((x, y, x + pad, y + pad), 22, fill=tuple(int(v * 0.55 + 255 * 0.45) for v in col) + (255,))
# 8 strips below as short bars
sy = y0 + 4 * pad + 3 * gap + 50
sw = (4 * pad + 3 * gap - 7 * 30) // 8
for s in range(8):
    x = x0 + s * (sw + 30)
    h = [80, 52, 96, 66, 88, 44, 74, 58][s]
    col = pal[(s * 2) % 16]
    d.rounded_rectangle((x, sy + 100 - h, x + sw, sy + 100), 8, fill=col + (255,))
img.save(out)
