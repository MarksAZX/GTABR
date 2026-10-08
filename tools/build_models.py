#!/usr/bin/env python3
"""3D model pipeline: Higgsfield GLBs (Tripo H3.1 meshes, Meshy rig + clips) -> runtime .gmesh/.ganim + GTEX textures.

  model -> rig -> materials -> textures -> animations -> LOD -> integration
  1. tools/meshconv converts each GLB (orientation/scale normalisation, skin weights, meshopt LODs, wheel cut, light anchors)
  2. this script turns the extracted PBR textures into GTEX (ASTC 6x6 + RGBA fallback), adding a paint mask to car albedo
Outputs go to assets/data/models/.
"""
import argparse, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import gtex

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "assets/source/models")
WORK = os.path.join(ROOT, "tools/work/models")
OUT = os.path.join(ROOT, "assets/data/models")
OUT_RGBA = os.path.join(ROOT, "assets/generated_rgba/data/models")
MESHCONV = os.path.join(ROOT, "build/meshconv")

CARS = {  # name: (length m, wheels zf,zr,yc,r,track, paint rule)
    "compacto": (3.90, "-1.237,1.191,0.288,0.288,0.778", "white"),
    "sedan": (4.55, "-1.434,1.210,0.321,0.321,0.895", None),
    "picape": (4.95, "-1.684,1.407,0.425,0.425,0.958", "blue"),
    "viatura": (4.45, "-1.394,1.180,0.349,0.349,0.863", None),
}
CHARS = {"protagonista": 1.78, "frentista": 1.74, "atendente": 1.65, "pedestre_mulher": 1.64,
         "mecanico": 1.76, "pedestre_homem": 1.75, "policial": 1.80}
CLIPS = {"idle": "protagonista_idle.glb", "walk": "protagonista_walk.glb", "run": "protagonista_run.glb",
         # action clips (Meshy library on the same rig): runtime ActionId order lives in src/game/model.h
         "punch": "protagonista_punch.glb", "kick": "protagonista_kick.glb", "hit": "protagonista_hit.glb",
         "knockdown": "protagonista_knockdown.glb", "standup": "protagonista_standup.glb", "slash": "protagonista_slash.glb",
         "reload": "protagonista_reload.glb", "chat": "protagonista_chat.glb", "jump": "protagonista_jump.glb",
         "combat": "protagonista_combat.glb", "die": "protagonista_die.glb", "turnl": "protagonista_turnl.glb", "turnr": "protagonista_turnr.glb",
         "hitgun": "protagonista_hitgun.glb", "swing": "protagonista_swing.glb",
         "swim": "protagonista_swim.glb", "swimidle": "protagonista_swimidle.glb"}


def run(args):
    print(" ", " ".join(os.path.relpath(a, ROOT) if a.startswith(ROOT) else a for a in args))
    subprocess.check_call(args)


def convert():
    os.makedirs(WORK, exist_ok=True)
    for n, (L, wheels, _) in CARS.items():
        run([MESHCONV, "mesh", os.path.join(SRC, n + ".glb"), os.path.join(WORK, n), "--kind", "car", "--length", str(L), "--yaw", "90",
             "--wheels", wheels])
    ref = os.path.join(WORK, "protagonista.gmesh")
    run([MESHCONV, "mesh", os.path.join(SRC, "protagonista_idle.glb"), os.path.join(WORK, "protagonista"), "--kind", "char",
         "--height", str(CHARS["protagonista"]), "--yaw", "90"])
    for n, h in CHARS.items():
        if n == "protagonista" or not os.path.exists(os.path.join(SRC, n + ".glb")):
            continue
        run([MESHCONV, "mesh", os.path.join(SRC, n + ".glb"), os.path.join(WORK, n), "--kind", "char", "--height", str(h), "--yaw", "90",
             "--skin-from", ref])
    for clip, f in CLIPS.items():
        run([MESHCONV, "anim", os.path.join(SRC, f), os.path.join(WORK, "anim_" + clip + ".ganim"), "--height", str(CHARS["protagonista"]),
             "--yaw", "90", "--name", clip])


def paint_mask(rgb, rule):
    hsv = np.asarray(Image.fromarray(rgb).convert("HSV")).astype(np.float32) / 255.0
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    if rule == "white":
        m = np.clip((v - 0.70) / 0.12, 0, 1) * np.clip((0.16 - s) / 0.08, 0, 1)
    elif rule == "blue":
        m = np.clip(1 - np.abs(h - 0.62) / 0.07, 0, 1) * np.clip((s - 0.35) / 0.15, 0, 1)
    else:
        return None
    from scipy import ndimage
    return ndimage.gaussian_filter(m, 1.0)


def textures(name, size, rule=None, quality="medium"):
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_RGBA, exist_ok=True)
    a = Image.open(os.path.join(WORK, name + "_albedo.jpg")).convert("RGB").resize((size, size), Image.LANCZOS)
    rgb = np.asarray(a)
    alpha = np.full(rgb.shape[:2], 255, np.uint8)
    m = paint_mask(rgb, rule) if rule else None
    if m is not None:
        alpha = (255 - np.clip(m, 0, 1) * 254).astype(np.uint8)   # 255 = no paint (shader treats a<0.999 as paint)
        print(f"    paint mask covers {float((m > 0.5).mean()) * 100:.1f}% of {name}")
    rgba = np.dstack([rgb, alpha])
    gtex.save_texture(os.path.join(OUT, name + "_a.gtex"), os.path.join(OUT_RGBA, name + "_a.gtex"), [rgba], "color", quality)
    half = max(256, size // 2)
    for suffix, fn in (("_n", "_normal.png"), ("_m", "_mr.jpg")):
        p = os.path.join(WORK, name + fn)
        im = Image.open(p).convert("RGB").resize((half, half), Image.LANCZOS)
        arr = np.dstack([np.asarray(im), np.full((half, half), 255, np.uint8)])
        gtex.save_texture(os.path.join(OUT, name + suffix + ".gtex"), os.path.join(OUT_RGBA, name + suffix + ".gtex"), [arr], "linear",
                          quality, srgb=False)


def package(quality):
    for n, (_, _, rule) in CARS.items():
        print("  textures", n)
        textures(n, 1024, rule, quality)
    for n in CHARS:
        if os.path.exists(os.path.join(WORK, n + ".gmesh")):
            print("  textures", n)
            textures(n, 1024, None, quality)
    for f in os.listdir(WORK):
        if f.endswith(".gmesh") or f.endswith(".ganim"):
            shutil.copy(os.path.join(WORK, f), os.path.join(OUT, f))
    print("  packaged:", sorted(os.listdir(OUT)))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-convert", action="store_true")
    ap.add_argument("--quality", default="medium")
    a = ap.parse_args()
    if not a.skip_convert:
        convert()
    package(a.quality)
