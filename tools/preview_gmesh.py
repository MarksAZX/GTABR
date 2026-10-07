#!/usr/bin/env python3
"""Quick orthographic previews of a .gmesh (side / top / front) with the albedo texture, for checking orientation & cuts."""
import struct, sys, numpy as np
from PIL import Image, ImageDraw

def load(path):
    b = open(path, 'rb').read()
    hdr_fmt = '<5I3f3f3f' + '48s' * 4 + '12f2f6f6f' + 'f3f'   # + animRootScale, pad
    hs = struct.calcsize(hdr_fmt)
    h = struct.unpack(hdr_fmt, b[:hs])
    magic, ver, flags, bones, lods = h[:5]
    off = hs + bones * (48 + 4 + 64 + 40)
    vc, ic = struct.unpack('<2I', b[off:off + 8]); off += 8
    vdt = np.dtype([('p', '<f4', 3), ('n', 'i1', 4), ('t', 'i1', 4), ('uv', '<f4', 2), ('j', 'u1', 4), ('w', 'u1', 4)])
    v = np.frombuffer(b, vdt, vc, off); off += vc * 36
    idx = np.frombuffer(b, '<u4', ic, off)
    tex = [h[14 + i].split(b'\0')[0].decode() for i in range(4)]
    wheels = np.array(h[18:30]).reshape(4, 3)
    return v, idx.reshape(-1, 3), tex, wheels, flags

def render(v, tri, tex_img, view, size=520):
    P = v['p'].astype(np.float32)
    N = v['n'][:, :3].astype(np.float32) / 127.0
    if view == 'side': ax, ay, depth, light = 2, 1, 0, np.array([0.5, 0.6, -0.6])  # look along -x
    elif view == 'top': ax, ay, depth, light = 0, 2, 1, np.array([0.3, 1, 0.2])
    else: ax, ay, depth, light = 0, 1, 2, np.array([0.3, 0.6, -1.0])            # front (look along +z)
    light = light / np.linalg.norm(light)
    xs, ys = P[:, ax], P[:, ay]
    mn = min(xs.min(), ys.min()); mx = max(xs.max(), ys.max())
    sc = (size - 20) / (mx - mn)
    X = (xs - xs.min()) * sc + 10
    Y = size - 10 - (ys - ys.min()) * sc if view != 'top' else (ys - ys.min()) * sc + 10
    d = P[:, depth] * (1 if view == 'front' else -1)
    order = np.argsort(-d[tri].mean(axis=1)) if view != 'top' else np.argsort(P[:, 1][tri].mean(axis=1))
    img = Image.new('RGB', (size, size), (40, 44, 50))
    dr = ImageDraw.Draw(img)
    tw, th = tex_img.size if tex_img else (1, 1)
    tex = np.asarray(tex_img.convert('RGB')) if tex_img else None
    for t in order:
        a, b2, c = tri[t]
        n = (N[a] + N[b2] + N[c]) / 3
        s = 0.35 + 0.65 * max(0, float(n @ light))
        if tex is not None:
            uv = (v['uv'][a] + v['uv'][b2] + v['uv'][c]) / 3
            col = tex[int(np.clip(uv[1], 0, 0.999) * th), int(np.clip(uv[0], 0, 0.999) * tw)]
        else: col = (200, 200, 200)
        col = tuple(int(min(255, ch * s)) for ch in col)
        dr.polygon([(X[a], Y[a]), (X[b2], Y[b2]), (X[c], Y[c])], fill=col)
    return img

if __name__ == '__main__':
    path, out = sys.argv[1], sys.argv[2]
    v, tri, tex, wheels, flags = load(path)
    import os
    base = os.path.dirname(path)
    timg = Image.open(os.path.join(base, tex[0])) if tex[0] else None
    if timg: timg = timg.resize((512, 512))
    imgs = [render(v, tri, timg, k) for k in ('side', 'top', 'front')]
    sheet = Image.new('RGB', (520 * 3, 520))
    for i, im in enumerate(imgs): sheet.paste(im, (i * 520, 0))
    sheet.save(out)
    print('verts', len(v), 'tris', len(tri), 'wheels', wheels.tolist() if flags & 2 else '-')
