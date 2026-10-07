#!/usr/bin/env python3
"""Builds all runtime assets (GTEX textures, sprite index, font atlases, icon atlas) from sources.

  assets/source/higgsfield/*.png   AI generated sheets (GPT Image 2.5 via Higgsfield)
  tools/work/sprites/*             output of the spritebake tool (procedural 3D -> multi-direction sprites)
Outputs:
  assets/data/*.gtex               ASTC 6x6 compressed textures (packaged in the APK)
  assets/generated_rgba/data/*.gtex  raw RGBA fallbacks for desktop/headless testing (git-ignored)
"""
import argparse, os, sys, math, re, shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
import gtex

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "assets/source/higgsfield")
OUT = os.path.join(ROOT, "assets/data")
OUT_RGBA = os.path.join(ROOT, "assets/generated_rgba/data")
SPR = os.path.join(ROOT, "tools/work/sprites")
os.makedirs(OUT, exist_ok=True)
os.makedirs(OUT_RGBA, exist_ok=True)

MAT_SIZE = 1024


def load(name):
    p = os.path.join(SRC, name)
    if not os.path.exists(p) and name.endswith(".png"):
        p = os.path.join(SRC, name[:-4] + ".jpg")
    return Image.open(p).convert("RGB") if os.path.exists(p) else None


def cell(img, cols, rows, c, r, inset=10):
    w, h = img.size
    cw, ch = w / cols, h / rows
    return img.crop((int(c * cw + inset), int(r * ch + inset), int((c + 1) * cw - inset), int((r + 1) * ch - inset)))


def to_arr(img, size=MAT_SIZE):
    img = img.resize((size, size), Image.LANCZOS)
    a = np.asarray(img.convert("RGBA")).copy()
    a[..., 3] = 255
    return a


def seamless_blend(img):
    """Cross-fade the image with a half-shifted copy so opposite edges match (good for noise-like textures)."""
    a = np.asarray(img.convert("RGB")).astype(np.float32)
    h, w = a.shape[:2]
    shifted = np.roll(np.roll(a, h // 2, axis=0), w // 2, axis=1)
    y = np.abs(np.linspace(-1, 1, h))[:, None]
    x = np.abs(np.linspace(-1, 1, w))[None, :]
    # weight 1 at the centre of the original, 0 at the borders (where the shifted copy is continuous)
    wy = np.clip(1.0 - y, 0, 1) ** 0.9
    wx = np.clip(1.0 - x, 0, 1) ** 0.9
    m = (wy * wx)[..., None]
    m = np.clip(m * 1.35, 0, 1)
    out = a * m + shifted * (1 - m)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def seamless_mirror(img):
    a = np.asarray(img.convert("RGB"))
    top = np.concatenate([a, a[:, ::-1]], axis=1)
    full = np.concatenate([top, top[::-1]], axis=0)
    return Image.fromarray(full)


def fbm_tile(size, seed, octaves=5, persistence=0.55):
    rng = np.random.default_rng(seed)
    acc = np.zeros((size, size), np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        n = 4 * 2 ** o
        base = rng.random((n, n)).astype(np.float32)
        img = Image.fromarray((np.tile(base, (3, 3)) * 255).astype(np.uint8)).resize((size * 3 * n // n, size * 3 * n // n), Image.BICUBIC)
        # take the central tile so the result is periodic
        arr = np.asarray(img).astype(np.float32) / 255.0
        s = arr.shape[0] // 3
        acc += amp * arr[s:2 * s, s:2 * s][:size, :size]
        total += amp
        amp *= persistence
    acc /= total
    return acc


def proc_layer(kind, seed):
    n = fbm_tile(MAT_SIZE, seed)
    if kind == "wall_paint":
        base = 0.86 + (n - 0.5) * 0.14
        yy = np.linspace(0, 1, MAT_SIZE)[:, None]
        grime = np.clip((yy - 0.78) * 2.2, 0, 1) * 0.16
        base = base - grime * (0.5 + n * 0.8)
        rgb = np.stack([base, base * 0.995, base * 0.985], -1)
    elif kind == "concrete":
        base = 0.55 + (n - 0.5) * 0.30
        rgb = np.stack([base, base, base * 1.02], -1)
    elif kind == "wood":
        yy = np.arange(MAT_SIZE)[:, None]
        grain = 0.5 + 0.5 * np.sin(yy * 0.35 + n * 12.0)
        base = 0.34 + grain * 0.12 + (n - 0.5) * 0.08
        rgb = np.stack([base * 1.25, base * 0.95, base * 0.62], -1)
    elif kind == "metal":
        xx = np.arange(MAT_SIZE)[None, :]
        base = 0.45 + 0.08 * np.sin(xx * 0.9 + n * 6) + (n - 0.5) * 0.1
        rgb = np.stack([base, base, base * 1.03], -1)
    elif kind == "wall_dark":
        base = 0.32 + (n - 0.5) * 0.18
        rgb = np.stack([base, base * 0.98, base * 0.95], -1)
    elif kind == "white":
        rgb = np.ones((MAT_SIZE, MAT_SIZE, 3), np.float32)
    else:
        rgb = np.ones((MAT_SIZE, MAT_SIZE, 3), np.float32) * 0.5
    a = np.clip(rgb * 255, 0, 255).astype(np.uint8)
    return np.concatenate([a, np.full((MAT_SIZE, MAT_SIZE, 1), 255, np.uint8)], -1)


MATERIALS = [
    "asphalt", "asphalt_cracked", "sidewalk", "pedra_port", "grass", "dirt", "tile_floor", "garage_floor",
    "house_yellow", "house_blue", "house_brick", "house_modern", "apt_beige", "apt_green", "sobrado_pink", "apt_bands",
    "shop_posto", "shop_mercado", "shop_oficina", "roof_tile", "roof_fiber", "roof_laje", "roof_metal", "wall_paint",
    "concrete", "wall_dark", "white", "shelf", "wood", "metal", "reserved0", "reserved1",
]


def build_materials(quality):
    print("materials ->", len(MATERIALS), "layers")
    ga, gb = load("ground_a.png"), load("ground_b.png")
    fh, fb = load("facades_houses.png"), load("facades_buildings.png")
    roofs = load("roofs.png")
    shelf = load("shelf.png")
    layers = {}
    if ga:
        layers["asphalt"] = to_arr(seamless_blend(cell(ga, 2, 2, 0, 0, 12)))
        layers["asphalt_cracked"] = to_arr(seamless_blend(cell(ga, 2, 2, 1, 0, 12)))
        layers["sidewalk"] = to_arr(seamless_mirror(cell(ga, 2, 2, 0, 1, 12)))
        layers["pedra_port"] = to_arr(seamless_mirror(cell(ga, 2, 2, 1, 1, 12)))
    if gb:
        layers["grass"] = to_arr(seamless_blend(cell(gb, 2, 2, 0, 0, 12)))
        layers["dirt"] = to_arr(seamless_blend(cell(gb, 2, 2, 1, 0, 12)))
        layers["tile_floor"] = to_arr(seamless_mirror(cell(gb, 2, 2, 0, 1, 12)))
        layers["garage_floor"] = to_arr(seamless_blend(cell(gb, 2, 2, 1, 1, 12)))
    if fh:
        for i, n in enumerate(["house_yellow", "house_blue", "house_brick", "house_modern"]):
            layers[n] = to_arr(cell(fh, 2, 2, i % 2, i // 2, 10))
    if fb:
        for i, n in enumerate(["apt_beige", "apt_green", "sobrado_pink", "apt_bands"]):
            layers[n] = to_arr(cell(fb, 2, 2, i % 2, i // 2, 10))
    for n, f in [("shop_posto", "shop_posto.png"), ("shop_mercado", "shop_mercado.png"), ("shop_oficina", "shop_oficina.png")]:
        im = load(f)
        if im:
            layers[n] = to_arr(im)
    if roofs:
        for i, n in enumerate(["roof_tile", "roof_fiber", "roof_laje", "roof_metal"]):
            layers[n] = to_arr(seamless_blend(cell(roofs, 2, 2, i % 2, i // 2, 12)))
    if shelf:
        layers["shelf"] = to_arr(shelf)
    seeds = {"wall_paint": 1, "concrete": 2, "wall_dark": 3, "white": 4, "wood": 5, "metal": 6, "shelf": 7, "reserved0": 8, "reserved1": 9}
    arrs = []
    for i, n in enumerate(MATERIALS):
        if n in layers:
            arrs.append(layers[n])
        else:
            if n not in seeds:
                print("  WARNING: missing source for", n)
            arrs.append(proc_layer(n if n in ("wall_paint", "concrete", "wall_dark", "white", "wood", "metal") else "concrete", seeds.get(n, 10 + i)))
    nm = gtex.save_texture(os.path.join(OUT, "materials.gtex"), os.path.join(OUT_RGBA, "materials.gtex"), arrs, "color", quality)
    print("  materials.gtex mips:", nm)
    # C++ ids header
    with open(os.path.join(ROOT, "src/game/material_ids.h"), "w") as f:
        f.write("// Generated by tools/build_assets.py - material array layer ids.\n#pragma once\nnamespace gtabr {\nnamespace mat {\n")
        for i, n in enumerate(MATERIALS):
            f.write(f"constexpr int {n} = {i};\n")
        f.write(f"constexpr int kCount = {len(MATERIALS)};\n}}  // namespace mat\n}}  // namespace gtabr\n")


def build_sprites(quality):
    meta = open(os.path.join(SPR, "sprites.txt")).read().splitlines()
    pages = [l.split() for l in meta if l.startswith("PAGE")]
    for _, atlas, idx, w, h in pages:
        w, h = int(w), int(h)
        raw = np.fromfile(os.path.join(SPR, f"{atlas}_{idx}.rgba"), np.uint8).reshape(h, w, 4)
        raw = gtex.bleed_colors(raw)
        name = f"{atlas}_{idx}.gtex"
        nm = gtex.save_texture(os.path.join(OUT, name), os.path.join(OUT_RGBA, name), [raw], "color", quality)
        print(f"  {name} {w}x{h} mips={nm}")
    shutil.copy(os.path.join(SPR, "sprites.txt"), os.path.join(OUT, "sprites.txt"))


# ------------------------------------------------------------------------------------------------ font (SDF)
def build_fonts():
    from scipy import ndimage
    fonts = {"font_regular": "/usr/share/fonts/opentype/inter/Inter-Medium.otf", "font_bold": "/usr/share/fonts/opentype/inter/Inter-Bold.otf"}
    chars = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(160, 256)] + list("•–—‘’“”…×€→←↑↓✓")
    SIZE, SPREAD, CELL = 56, 9, 84
    for name, path in fonts.items():
        if not os.path.exists(path):
            print("  font missing:", path)
            continue
        font = ImageFont.truetype(path, SIZE)
        cols = 2048 // CELL
        rows = int(math.ceil(len(chars) / cols))
        H = 1
        while H < rows * CELL:
            H *= 2
        atlas = np.zeros((H, 2048), np.uint8)
        lines = []
        asc, desc = font.getmetrics()
        lines.append(f"FONT {SIZE} {asc} {desc} {SPREAD} {CELL} 2048 {H}")
        for i, ch in enumerate(chars):
            cx, cy = (i % cols) * CELL, (i // cols) * CELL
            big = Image.new("L", (CELL * 4, CELL * 4), 0)
            d = ImageDraw.Draw(big)
            # draw at 4x for a precise distance field, glyph origin at (SPREAD, SPREAD) in cell space
            f4 = ImageFont.truetype(path, SIZE * 4)
            d.text((SPREAD * 4, SPREAD * 4), ch, font=f4, fill=255)
            a = np.asarray(big).astype(np.float32) > 127
            inside = ndimage.distance_transform_edt(a)
            outside = ndimage.distance_transform_edt(~a)
            sd = (inside - outside) / 4.0  # in 1x pixels (positive inside)
            sd = sd.reshape(CELL, 4, CELL, 4).mean(axis=(1, 3))
            val = np.clip(0.5 + sd / (2.0 * SPREAD), 0, 1)
            atlas[cy:cy + CELL, cx:cx + CELL] = (val * 255).astype(np.uint8)
            adv = font.getlength(ch)
            lines.append(f"G {ord(ch)} {cx / 2048:.6f} {cy / H:.6f} {(cx + CELL) / 2048:.6f} {(cy + CELL) / H:.6f} {adv:.3f}")
        gtex.save_r8(os.path.join(OUT, name + ".gtex"), atlas)
        gtex.save_r8(os.path.join(OUT_RGBA, name + ".gtex"), atlas)
        open(os.path.join(OUT, name + ".txt"), "w").write("\n".join(lines) + "\n")
        print(f"  {name}: {len(chars)} glyphs")


# ------------------------------------------------------------------------------------------------ icons
def build_icons(quality):
    S, SS = 128, 4
    names = []
    imgs = []

    def new():
        im = Image.new("RGBA", (S * SS, S * SS), (0, 0, 0, 0))
        return im, ImageDraw.Draw(im)

    def P(x, y):
        return (x * SS, y * SS)

    def fin(name, im):
        names.append(name)
        imgs.append(im.resize((S, S), Image.LANCZOS))

    W = (255, 255, 255, 255)

    def poly(d, pts, fill=W):
        d.polygon([P(*p) for p in pts], fill=fill)

    def line(d, a, b, w, fill=W):
        d.line([P(*a), P(*b)], fill=fill, width=int(w * SS))
        for p in (a, b):
            d.ellipse([P(p[0] - w / 2, p[1] - w / 2), P(p[0] + w / 2, p[1] + w / 2)], fill=fill)

    def circ(d, c, r, fill=W, outline=None, width=0):
        d.ellipse([P(c[0] - r, c[1] - r), P(c[0] + r, c[1] + r)], fill=fill, outline=outline, width=int(width * SS))

    def rrect(d, box, r, fill=W):
        d.rounded_rectangle([P(box[0], box[1]), P(box[2], box[3])], radius=r * SS, fill=fill)

    # run: sprinting figure
    im, d = new(); circ(d, (78, 26), 11); line(d, (74, 42), (58, 70), 12); line(d, (58, 70), (84, 86), 11); line(d, (84, 86), (80, 108), 10)
    line(d, (58, 70), (40, 96), 11); line(d, (40, 96), (18, 98), 9); line(d, (74, 44), (98, 56), 9); line(d, (74, 44), (50, 54), 9); line(d, (50, 54), (34, 44), 8); fin("run", im)
    # hand / interact
    im, d = new(); rrect(d, (38, 54, 92, 106), 14)
    for i, (x, top) in enumerate([(42, 28), (56, 18), (70, 22), (84, 34)]): rrect(d, (x - 6, top, x + 8, 70), 7)
    poly(d, [(38, 70), (20, 56), (28, 46), (44, 62)]); fin("hand", im)
    # car
    im, d = new(); rrect(d, (14, 56, 114, 90), 12); poly(d, [(34, 56), (46, 32), (82, 32), (96, 56)]); circ(d, (36, 92), 12, (255, 255, 255, 255)); circ(d, (92, 92), 12)
    circ(d, (36, 92), 5, (0, 0, 0, 0)); circ(d, (92, 92), 5, (0, 0, 0, 0)); fin("car", im)
    # door / enter
    im, d = new(); rrect(d, (28, 14, 86, 114), 6); circ(d, (72, 66), 5, (0, 0, 0, 0)); poly(d, [(92, 54), (118, 54), (118, 44), (126, 64), (118, 84), (118, 74), (92, 74)]); fin("door", im)
    # camera
    im, d = new(); rrect(d, (12, 38, 116, 100), 12); rrect(d, (42, 26, 78, 42), 5); circ(d, (64, 70), 22, (0, 0, 0, 0)); circ(d, (64, 70), 14); fin("camera", im)
    # pause
    im, d = new(); rrect(d, (32, 24, 54, 104), 6); rrect(d, (74, 24, 96, 104), 6); fin("pause", im)
    # wheel (radial)
    im, d = new()
    for k in range(8):
        a0 = k * math.pi / 4 + 0.08; a1 = a0 + math.pi / 4 - 0.16
        pts = []
        for t in np.linspace(a0, a1, 8): pts.append((64 + math.sin(t) * 54, 64 - math.cos(t) * 54))
        for t in np.linspace(a1, a0, 8): pts.append((64 + math.sin(t) * 26, 64 - math.cos(t) * 26))
        poly(d, pts)
    fin("wheel", im)
    # fist
    im, d = new(); rrect(d, (26, 40, 100, 100), 16)
    for i in range(4): rrect(d, (28 + i * 17, 24, 42 + i * 17, 56), 7)
    rrect(d, (80, 58, 114, 80), 10); fin("fist", im)
    # coin
    im, d = new(); circ(d, (64, 64), 50); circ(d, (64, 64), 38, (0, 0, 0, 0)); rrect(d, (58, 36, 70, 92), 4); rrect(d, (46, 46, 82, 56), 4); rrect(d, (46, 72, 82, 82), 4); fin("coin", im)
    # heart
    im, d = new(); circ(d, (44, 46), 26); circ(d, (84, 46), 26); poly(d, [(20, 58), (108, 58), (64, 108)]); fin("heart", im)
    # bolt
    im, d = new(); poly(d, [(74, 8), (30, 72), (58, 72), (48, 120), (98, 50), (68, 50)]); fin("bolt", im)
    # fuel pump
    im, d = new(); rrect(d, (24, 16, 78, 112), 8); rrect(d, (34, 26, 68, 52), 3, (0, 0, 0, 0)); rrect(d, (14, 106, 88, 116), 4)
    line(d, (78, 40), (96, 40), 7); line(d, (96, 40), (100, 82), 7); line(d, (100, 82), (100, 96), 9); fin("fuel", im)
    # wrench
    im, d = new(); line(d, (34, 94), (80, 48), 14); circ(d, (86, 42), 22); poly(d, [(86, 42), (112, 16), (118, 38), (100, 44)], (0, 0, 0, 0)); fin("wrench", im)
    # bag
    im, d = new(); rrect(d, (22, 44, 106, 114), 10); d.arc([P(40, 10), P(88, 70)], 180, 360, fill=W, width=7 * SS); fin("bag", im)
    # chat bubble
    im, d = new(); rrect(d, (12, 20, 116, 92), 20); poly(d, [(36, 88), (30, 114), (62, 90)]); fin("chat", im)
    # check
    im, d = new(); line(d, (26, 68), (52, 94), 14); line(d, (52, 94), (104, 36), 14); fin("check", im)
    # close
    im, d = new(); line(d, (30, 30), (98, 98), 14); line(d, (98, 30), (30, 98), 14); fin("close", im)
    # gear
    im, d = new(); circ(d, (64, 64), 36)
    for k in range(8):
        a = k * math.pi / 4; cx, cy = 64 + math.sin(a) * 46, 64 - math.cos(a) * 46; circ(d, (cx, cy), 11)
    circ(d, (64, 64), 16, (0, 0, 0, 0)); fin("gear", im)
    # play / save / lock / arrows / plus / minus
    im, d = new(); poly(d, [(40, 22), (100, 64), (40, 106)]); fin("play", im)
    im, d = new(); rrect(d, (20, 20, 108, 108), 10); rrect(d, (38, 20, 90, 52), 3, (0, 0, 0, 0)); rrect(d, (38, 70, 90, 108), 3, (0, 0, 0, 0)); fin("save", im)
    im, d = new(); rrect(d, (28, 56, 100, 112), 10); d.arc([P(40, 16), P(88, 78)], 180, 360, fill=W, width=9 * SS); fin("lock", im)
    im, d = new(); poly(d, [(64, 14), (108, 62), (82, 62), (82, 112), (46, 112), (46, 62), (20, 62)]); fin("arrow", im)
    im, d = new(); rrect(d, (54, 20, 74, 108), 6); rrect(d, (20, 54, 108, 74), 6); fin("plus", im)
    im, d = new(); rrect(d, (20, 54, 108, 74), 6); fin("minus", im)
    im, d = new(); circ(d, (64, 64), 52); circ(d, (64, 64), 38, (0, 0, 0, 0)); circ(d, (64, 64), 14); fin("target", im)
    # dot / ring for minimap player arrow
    im, d = new(); poly(d, [(64, 8), (108, 112), (64, 90), (20, 112)]); fin("navarrow", im)
    im, d = new(); circ(d, (64, 64), 40); fin("dot", im)
    im, d = new(); poly(d, [(64, 124), (22, 54), (30, 30), (64, 12), (98, 30), (106, 54)]); circ(d, (64, 52), 14, (0, 0, 0, 0)); fin("pin", im)
    # store (market), shopping cart
    im, d = new(); poly(d, [(12, 20), (32, 20), (44, 80), (100, 80), (112, 34), (38, 34)]); circ(d, (52, 100), 9); circ(d, (92, 100), 9); fin("cart", im)

    cols = 8
    rows = (len(imgs) + cols - 1) // cols
    atlas = Image.new("RGBA", (cols * S, rows * S), (0, 0, 0, 0))
    lines = []
    for i, im in enumerate(imgs):
        x, y = (i % cols) * S, (i // cols) * S
        atlas.paste(im, (x, y))
        lines.append(f"I {names[i]} {x / atlas.width:.6f} {y / atlas.height:.6f} {(x + S) / atlas.width:.6f} {(y + S) / atlas.height:.6f}")
    H = 1
    while H < atlas.height: H *= 2
    full = Image.new("RGBA", (cols * S, H), (0, 0, 0, 0)); full.paste(atlas, (0, 0))
    lines = [re.sub(r"(I \S+ \S+ )(\S+)( \S+ )(\S+)", lambda m: m.group(1) + f"{float(m.group(2)) * atlas.height / H:.6f}" + m.group(3) + f"{float(m.group(4)) * atlas.height / H:.6f}", l) for l in lines]
    arr = gtex.bleed_colors(np.asarray(full).copy())
    arr[..., :3] = 255
    gtex.save_texture(os.path.join(OUT, "icons.gtex"), os.path.join(OUT_RGBA, "icons.gtex"), [arr], "color", quality)
    open(os.path.join(OUT, "icons.txt"), "w").write("\n".join(lines) + "\n")
    print("  icons:", len(names))


# ------------------------------------------------------------------------------------------------ ui art (products, portraits)
def build_ui_art(quality):
    prod = load("products.png")
    port = load("portraits.png")
    W, H = 2048, 1024
    atlas = Image.new("RGBA", (W, H), (30, 30, 34, 255))
    lines = []
    pn = ["agua", "refri", "salgadinho", "pao", "cafe", "barra", "energetico", "kit"]
    if prod:
        for i in range(8):
            c = cell(prod, 4, 2, i % 4, i // 4, 6)
            s = min(c.size)
            c = c.crop(((c.width - s) // 2, (c.height - s) // 2, (c.width + s) // 2, (c.height + s) // 2)).resize((256, 256), Image.LANCZOS)
            x, y = i * 256, 512
            atlas.paste(c, (x, y))
            lines.append(f"A prod_{pn[i]} {x / W:.6f} {y / H:.6f} {(x + 256) / W:.6f} {(y + 256) / H:.6f}")
    portn = ["frentista", "atendente", "mecanico", "vizinho"]
    if port:
        for i in range(4):
            c = cell(port, 2, 2, i % 2, i // 2, 8).resize((512, 512), Image.LANCZOS)
            x, y = i * 512, 0
            atlas.paste(c, (x, y))
            lines.append(f"A portrait_{portn[i]} {x / W:.6f} {y / H:.6f} {(x + 512) / W:.6f} {(y + 512) / H:.6f}")
    gtex.save_texture(os.path.join(OUT, "ui_art.gtex"), os.path.join(OUT_RGBA, "ui_art.gtex"), [np.asarray(atlas).copy()], "color", quality)
    open(os.path.join(OUT, "ui_art.txt"), "w").write("\n".join(lines) + "\n")
    print("  ui_art:", len(lines), "entries")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--quality", default="medium")
    args = ap.parse_args()
    steps = {"materials": lambda: build_materials(args.quality), "sprites": lambda: build_sprites("fast" if args.quality == "medium" else args.quality),
             "fonts": build_fonts, "icons": lambda: build_icons(args.quality), "art": lambda: build_ui_art(args.quality)}
    for k, fn in steps.items():
        if not args.only or args.only == k:
            print("==", k)
            fn()
