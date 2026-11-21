# frames.txt (F title / P 256 hex digits per row) -> one sheet with captions, 4x.
import sys
from PIL import Image, ImageDraw, ImageFont
SC, pad, capH = 4, 24, 44
try:
    cap = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 26)
except OSError:
    cap = ImageFont.load_default()
frames = []
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("F "):
        frames.append([line[2:].strip(), []])
    elif line.startswith("P "):
        frames[-1][1].append([int(ch, 16) * 17 for ch in line[2:].strip()])
W = 256 * SC
sheet = Image.new("RGB", (W + 2 * pad, len(frames) * (64 * SC + capH + pad) + pad), (24, 24, 26))
y = pad
for title, rows in frames:
    img = Image.new("L", (256, 64))
    img.putdata([v for r in rows for v in r])
    ImageDraw.Draw(sheet).text((pad, y + 30), title, font=cap, fill=(220, 220, 225), anchor="ls")
    sheet.paste(img.convert("RGB").resize((W, 64 * SC), Image.NEAREST), (pad, y + capH))
    y += 64 * SC + capH + pad
sheet.save(sys.argv[2])
print(len(frames), "screens ->", sys.argv[2])
