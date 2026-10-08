#!/usr/bin/env python3
"""Update coastal GTEX layers, preserving every other compressed material byte.

Uses the existing texture atlas/mipmap/ASTC pipeline. Run after generating
assets/source/gpt_image/coastal_atlas.png. Quality here is ASTC encoder quality,
not an image-generation model setting.
"""
import argparse
import struct
from pathlib import Path
import numpy as np
from PIL import Image
import build_assets as assets
import gtex

ROOT = Path(__file__).resolve().parents[1]
NAMES = {30: ("sand", 1, 0), 31: ("water", 0, 0),
         40: ("foam", 0, 1), 41: ("sand_wet", 1, 1)}


def update(path, maps, quality):
    original = path.read_bytes()
    magic, fmt, width, height, layers, levels = struct.unpack_from("<4s5I", original)
    if magic != b"GTX1" or fmt not in (4, 5) or layers not in (40, 42):
        raise ValueError(f"Unexpected material format in {path}")
    block = 8 if fmt == 5 else 6
    chains = {layer: gtex.mip_chain(array, "color" if fmt == 5 else "linear")
              for layer, array in maps.items()}
    if any(array.shape != (height, width, 4) for array in maps.values()):
        raise ValueError("Replacement dimensions do not match GTEX")
    data = bytearray(b"GTX1" + struct.pack("<5I", fmt, width, height, 42, levels))
    offset = 24
    for mip in range(levels):
        mw, mh = max(1, width >> mip), max(1, height >> mip)
        stride = ((mw + block - 1) // block) * ((mh + block - 1) // block) * 16
        for layer in range(42):
            if layer in maps:
                data.extend(gtex.astc_compress(chains[layer][mip], quality, fmt == 5, block))
            else:
                if layer >= layers:
                    raise ValueError("Missing appended material")
                # Existing layers are retained verbatim; recompression is confined to the coast.
                start = offset + layer * stride
                data.extend(original[start:start + stride])
        offset += layers * stride
    if offset != len(original):
        raise ValueError("Truncated or unexpected GTEX payload")
    temp = path.with_suffix(".gtex.tmp")
    temp.write_bytes(data)
    temp.replace(path)
    print(f"Updated {path.name}: {layers} -> 42 layers; non-coastal layers preserved", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--quality", choices=["fast", "medium", "thorough"], default="medium")
    args = parser.parse_args()
    atlas = Image.open(ROOT / "assets/source/gpt_image/coastal_atlas.png").convert("RGB")
    colors, normals = {}, {}
    for layer, (name, col, row) in NAMES.items():
        colors[layer] = assets.to_arr(assets.seamless_blend(assets.cell(atlas, 2, 2, col, row, inset=2)))
        normals[layer] = assets.derive_normal_rough(colors[layer], name)
    update(ROOT / "assets/data/materials.gtex", colors, args.quality)
    update(ROOT / "assets/data/materials_n.gtex", normals, args.quality)
