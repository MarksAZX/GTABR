#!/usr/bin/env python3
"""Generate desktop RGBA fallbacks from the existing compressed GTEX assets.
Requires: pip install astc-encoder-py. Android continues using original ASTC.
"""
from pathlib import Path
import struct
import astc_encoder as a
root=Path(__file__).resolve().parents[1]
contexts={}
for src in (root/'assets/data').rglob('*.gtex'):
    raw=src.read_bytes();fmt,w,h,layers,mips=struct.unpack_from('<5I',raw,4)
    if fmt not in (1,4,5): continue
    out=root/'assets/generated_rgba/data'/src.relative_to(root/'assets/data')
    if out.exists() and out.stat().st_mtime>=src.stat().st_mtime: continue
    block=8 if fmt==5 else 6
    key=(block,fmt==4)
    if key not in contexts:
        config=a.ASTCConfig(a.ASTCProfile.LDR if fmt==4 else a.ASTCProfile.LDR_SRGB,block,block,1,a.ASTCQualityPreset.FAST)
        contexts[key]=a.ASTCContext(config,threads=4)
    out.parent.mkdir(parents=True,exist_ok=True)
    offset=24
    with out.open('wb') as f:
        f.write(b'GTX1'+struct.pack('<5I',2 if fmt==4 else 0,w,h,layers,mips))
        for mip in range(mips):
            mw,mh=max(1,w>>mip),max(1,h>>mip)
            size=((mw+block-1)//block)*((mh+block-1)//block)*16
            for layer in range(layers):
                image=a.ASTCImage(a.ASTCType.U8,mw,mh,1)
                decoded=contexts[key].decompress(raw[offset:offset+size],image,a.ASTCSwizzle.from_str('rgba'))
                f.write(decoded.data);offset+=size
    print(src.relative_to(root),flush=True)
