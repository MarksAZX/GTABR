#!/usr/bin/env python3
"""Finds the wheel centres / radius / track of a converted car (.gmesh LOD0 + albedo) from the dark tyre texels.
Prints "zf,zr,yc,r,track" for meshconv --wheels."""
import sys, os, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from preview_gmesh import load

v, tri, tex, wheels, flags = load(sys.argv[1])
img = np.asarray(Image.open(os.path.join(os.path.dirname(sys.argv[1]), tex[0])).convert('RGB')).astype(np.float32) / 255
h, w = img.shape[:2]
P = v['p']
uv = v['uv']
col = img[np.clip((uv[:, 1] * h).astype(int), 0, h - 1), np.clip((uv[:, 0] * w).astype(int), 0, w - 1)]
lum = col @ np.array([0.299, 0.587, 0.114])
H = P[:, 1].max(); W = P[:, 0].max() - P[:, 0].min(); L = P[:, 2].max() - P[:, 2].min()
cand = (lum < 0.16) & (P[:, 1] < 0.42 * H) & (np.abs(P[:, 0]) > 0.22 * W)
res = []
for half in (P[:, 2] < 0, P[:, 2] >= 0):
    q = P[cand & half]
    low = q[q[:, 1] < 0.10 * H]                       # contact patch: only tyres reach the ground
    zc = float(np.median(low[:, 2]))
    near = q[np.abs(q[:, 2] - zc) < 0.12]
    ytop = np.percentile(near[:, 1], 98)               # top of the tyre directly above the contact patch
    res.append((zc, ytop / 2, np.percentile(np.abs(low[:, 0]), 75)))
zf, rf, tf = res[0]; zr, rr, tr = res[1]
r = float(np.clip((rf + rr) / 2, 0.26, 0.42))
print(f"{zf:.3f},{zr:.3f},{r:.3f},{r:.3f},{(tf + tr) / 2:.3f}")
