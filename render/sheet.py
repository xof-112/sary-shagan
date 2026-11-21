# Setzt die von test/host --render geschriebenen Bildschirme (PGM, 16 Graustufen) zu einem Bogen zusammen.
import sys, glob, os
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
SC, pad, capH = 4, 24, 44
try:
    cap = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 26)
except OSError:
    cap = ImageFont.load_default()
frames = []
for pgm in sorted(glob.glob(os.path.join(src, "screen_*.pgm"))):
    title = open(pgm[:-4] + ".txt", encoding="utf-8").read().strip()
    if title in ("Maximalwerte", "Minimalwerte"):
        continue
    img = Image.open(pgm).convert("RGB")   # PIL already scales maxval 15 to 0..255
    frames.append((title, img))
W = 256 * SC
sheet = Image.new("RGB", (W + 2 * pad, len(frames) * (64 * SC + capH + pad) + pad), (24, 24, 26))
y = pad
for title, img in frames:
    ImageDraw.Draw(sheet).text((pad, y + 30), title, font=cap, fill=(220, 220, 225), anchor="ls")
    sheet.paste(img.resize((W, 64 * SC), Image.NEAREST), (pad, y + capH))
    y += 64 * SC + capH + pad
sheet.save(out)
print(len(frames), "Bildschirme ->", out)
