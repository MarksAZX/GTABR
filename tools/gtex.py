"""GTEX texture container writer + helpers (mips, ASTC encoding, colour bleed).

Layout: 'GTX1', u32 format (0=RGBA8_SRGB,1=ASTC6x6_SRGB,2=RGBA8_UNORM,3=R8_UNORM), u32 w,h,layers,mips,
then data for each mip (largest first), each layer in order.
"""
import struct
import numpy as np

FMT_RGBA8_SRGB, FMT_ASTC6, FMT_RGBA8_UNORM, FMT_R8, FMT_ASTC6_UNORM, FMT_ASTC8 = 0, 1, 2, 3, 4, 5
_ctx_cache = {}


def _astc_ctx(quality, srgb=True, block=6):
    import astc_encoder as a
    key = (quality, srgb, block)
    if key not in _ctx_cache:
        q = {"fast": a.ASTCQualityPreset.FAST, "medium": a.ASTCQualityPreset.MEDIUM, "thorough": a.ASTCQualityPreset.THOROUGH}[quality]
        cfg = a.ASTCConfig(a.ASTCProfile.LDR_SRGB if srgb else a.ASTCProfile.LDR, block, block, 1, q)
        _ctx_cache[key] = a.ASTCContext(cfg, threads=4)
    return _ctx_cache[key]


def astc_compress(rgba_u8, quality="medium", srgb=True, block=6):
    import astc_encoder as a
    h, w = rgba_u8.shape[:2]
    img = a.ASTCImage(a.ASTCType.U8, w, h, 1, np.ascontiguousarray(rgba_u8).tobytes())
    return _astc_ctx(quality, srgb, block).compress(img, a.ASTCSwizzle.from_str("rgba"))


def srgb_to_lin(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(np.maximum(x, 0), 1 / 2.4) - 0.055)


def _gather4(arr):
    h, w = arr.shape[:2]
    nh, nw = max(1, h // 2), max(1, w // 2)
    y0 = np.minimum(np.arange(nh) * 2, h - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    x0 = np.minimum(np.arange(nw) * 2, w - 1)
    x1 = np.minimum(x0 + 1, w - 1)
    return arr[y0][:, x0], arr[y0][:, x1], arr[y1][:, x0], arr[y1][:, x1]


def downsample_color(rgba):
    """2x box downsample of an sRGB RGBA8 image, averaging in linear light (alpha-weighted)."""
    f = rgba.astype(np.float32) / 255.0
    lin = srgb_to_lin(f[..., :3])
    a = f[..., 3:4]
    pm = lin * a
    def box(x):
        p = _gather4(x)
        return (p[0] + p[1] + p[2] + p[3]) * 0.25
    pmb, ab, lb = box(pm), box(a), box(lin)
    rgb = np.where(ab > 1e-5, pmb / np.maximum(ab, 1e-5), lb)
    out = np.concatenate([lin_to_srgb(np.clip(rgb, 0, 1)), ab], axis=-1)
    return (np.clip(out, 0, 1) * 255.0 + 0.5).astype(np.uint8)


def downsample_linear(a):
    """Plain box filter for non-colour data (normal maps, roughness)."""
    p = _gather4(a.astype(np.float32))
    return np.clip((p[0] + p[1] + p[2] + p[3]) * 0.25 + 0.5, 0, 255).astype(np.uint8)


def downsample_gray(a):
    p = _gather4(a.astype(np.float32))
    return ((p[0] + p[1] + p[2] + p[3]) * 0.25 + 0.5).astype(np.uint8)


def bleed_colors(rgba):
    """Extend colours of opaque pixels into transparent ones so filtering never pulls in black fringes."""
    from scipy import ndimage
    a = rgba[..., 3]
    mask = a < 8
    if not mask.any() or mask.all():
        return rgba
    idx = ndimage.distance_transform_edt(mask, return_distances=False, return_indices=True)
    out = rgba.copy()
    out[..., :3] = rgba[idx[0], idx[1], :3]
    out[mask, 3] = a[mask]
    return out


def mip_chain(rgba, kind="color", max_levels=16):
    levels = [rgba]
    while max(levels[-1].shape[0], levels[-1].shape[1]) > 1 and len(levels) < max_levels:
        if kind == "gray":
            levels.append(downsample_gray(levels[-1]))
        elif kind == "linear":
            levels.append(downsample_linear(levels[-1]))
        else:
            levels.append(downsample_color(levels[-1]))
    return levels


def write_gtex(path, fmt, w, h, layers, mip_layers, mip_bytes_fn):
    """mip_layers[m][l] -> numpy array; mip_bytes_fn converts an array to bytes."""
    with open(path, "wb") as f:
        f.write(b"GTX1")
        f.write(struct.pack("<5I", fmt, w, h, layers, len(mip_layers)))
        for m in range(len(mip_layers)):
            for l in range(layers):
                f.write(mip_bytes_fn(mip_layers[m][l]))


def save_texture(out_astc, out_rgba, layers_rgba, kind="color", quality="medium", astc=True, rgba_dir=True, srgb=True, block=6):
    """layers_rgba: list of HxWx4 uint8 arrays (same size). Writes the ASTC and raw RGBA variants."""
    h, w = layers_rgba[0].shape[:2]
    chains = [mip_chain(l, kind) for l in layers_rgba]
    nmips = len(chains[0])
    mips = [[chains[l][m] for l in range(len(layers_rgba))] for m in range(nmips)]
    L = len(layers_rgba)
    if rgba_dir:
        write_gtex(out_rgba, FMT_RGBA8_SRGB if srgb else FMT_RGBA8_UNORM, w, h, L, mips, lambda a: np.ascontiguousarray(a).tobytes())
    if astc:
        fmt = (FMT_ASTC6 if srgb else FMT_ASTC6_UNORM) if block == 6 else FMT_ASTC8
        write_gtex(out_astc, fmt, w, h, L, mips, lambda a: astc_compress(a, quality, srgb, block))
    return nmips


def save_r8(out_path, gray):
    h, w = gray.shape
    chain = mip_chain(gray, "gray")
    write_gtex(out_path, FMT_R8, w, h, 1, [[c] for c in chain], lambda a: np.ascontiguousarray(a).tobytes())
