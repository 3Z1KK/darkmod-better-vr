"""Builds the VR pouch panel background from the glove's tooled leather (CC-BY-4.0, see models/vr/hands/CREDITS.txt)."""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

def rounded_mask(w, h, r, inset=0, rough=0.0, seed=1):
    m = Image.new('L', (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([inset, inset, w - 1 - inset, h - 1 - inset], radius=r, fill=255)
    if rough > 0:
        rng = np.random.default_rng(seed)
        a = np.asarray(m, np.float32)
        noise = Image.fromarray((rng.random((h // 8, w // 8)) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        a = np.clip(a - (np.asarray(noise, np.float32) - 128) * rough, 0, 255)
        m = Image.fromarray(a.astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    return m

def stitches(draw, box, r, dash=18, gap=13, color=(168, 132, 86), width=3):
    x0, y0, x1, y1 = box
    # walk the rounded rectangle perimeter
    pts = []
    def arc(cx, cy, a0, a1):
        for a in np.linspace(a0, a1, 24):
            pts.append((cx + r * np.cos(a), cy + r * np.sin(a)))
    pts.append((x0 + r, y0)); pts.append((x1 - r, y0)); arc(x1 - r, y0 + r, -np.pi / 2, 0)
    pts.append((x1, y1 - r)); arc(x1 - r, y1 - r, 0, np.pi / 2)
    pts.append((x0 + r, y1)); arc(x0 + r, y1 - r, np.pi / 2, np.pi)
    pts.append((x0, y0 + r)); arc(x0 + r, y0 + r, np.pi, 3 * np.pi / 2)
    pts.append(pts[0])
    # resample evenly
    seg = np.array(pts); d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(seg, axis=0), axis=1))]
    total = d[-1]; s = 0.0
    while s + dash < total:
        a = np.array([np.interp(s, d, seg[:, 0]), np.interp(s, d, seg[:, 1])])
        b = np.array([np.interp(s + dash, d, seg[:, 0]), np.interp(s + dash, d, seg[:, 1])])
        draw.line([tuple(a + 2), tuple(b + 2)], fill=(30, 18, 10, 160), width=width)   # shadow
        draw.line([tuple(a), tuple(b)], fill=color + (255,), width=width)
        s += dash + gap

def main(diffuse, out):
    W, H = 1024, 768
    tex = Image.open(diffuse).convert('RGB')
    tw, th = tex.size
    # the tooled leather panel of the bracer
    crop = tex.crop((int(tw * 0.44), int(th * 0.17), int(tw * 0.83), int(th * 0.47))).resize((W, H), Image.LANCZOS)
    base = np.asarray(crop, np.float32) / 255
    # vignette and a darker recess where the item icon sits
    yy, xx = np.mgrid[0:H, 0:W]
    v = 1 - 0.35 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
    base *= v[..., None]
    recess = np.asarray(rounded_mask(W, H, 60, 0).resize((W, H)), np.float32)
    rm = Image.new('L', (W, H), 0)
    ImageDraw.Draw(rm).rounded_rectangle([290, 175, 734, 600], radius=50, fill=255)
    rm = np.asarray(rm.filter(ImageFilter.GaussianBlur(14)), np.float32) / 255
    base *= (1 - 0.45 * rm)[..., None]
    # top flap: darker band with its own stitching and a button
    flap = np.zeros((H, W), np.float32); flap[:150] = 1
    flap = np.asarray(Image.fromarray((flap * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    base *= (1 - 0.25 * flap)[..., None]
    img = Image.fromarray((np.clip(base, 0, 1) * 255).astype(np.uint8)).convert('RGBA')
    d = ImageDraw.Draw(img, 'RGBA')
    # flap edge shadow
    for i in range(10):
        d.line([(40, 150 + i), (W - 40, 150 + i)], fill=(20, 10, 5, 120 - i * 12))
    stitches(d, (48, 48, W - 48, H - 48), 50)
    stitches(d, (70, 70, W - 70, 128), 20, dash=14, gap=10, width=3)
    # brass button on the flap
    cx, cy, r = W // 2, 150, 26
    d.ellipse([cx - r - 3, cy - r - 3, cx + r + 3, cy + r + 3], fill=(40, 25, 10, 200))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(150, 112, 50, 255))
    d.ellipse([cx - r + 6, cy - r + 6, cx + r - 10, cy + r - 10], fill=(196, 160, 86, 255))
    d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=(110, 80, 35, 255))
    # rough, burnt-looking edge and rounded corners
    rough = np.asarray(rounded_mask(W, H, 70, 4, rough=0.5), np.float32) / 255
    solid = np.asarray(rounded_mask(W, H, 62, 16), np.float32) / 255
    edge = np.maximum(rough, solid)		# only the rim is ragged
    inner = np.asarray(rounded_mask(W, H, 60, 22).filter(ImageFilter.GaussianBlur(10)), np.float32) / 255
    rgb = np.clip(np.asarray(img, np.float32)[..., :3] * 1.25 * (0.55 + 0.45 * inner)[..., None], 0, 255)
    out_img = np.dstack([rgb, edge * 255]).astype(np.uint8)
    Image.fromarray(out_img, 'RGBA').save(out)
    Image.fromarray(out_img, 'RGBA').resize((512, 384)).save(out.replace('.tga', '_preview.png'))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
