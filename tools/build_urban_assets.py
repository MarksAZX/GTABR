#!/usr/bin/env python3
"""Integrate original GPT Image urban surfaces without recompressing other layers."""
from pathlib import Path
from PIL import Image
import build_assets as assets
from build_coastal_assets import update
ROOT=Path(__file__).resolve().parents[1]
atlas=Image.open(ROOT/'assets/source/gpt_image/urban_atlas.png').convert('RGB')
colors={};normals={}
for layer,name,col,row in [(0,'asphalt',0,0),(1,'asphalt_cracked',0,0),(2,'sidewalk',1,0),(24,'concrete',1,0),(4,'grass',0,1),(35,'foliage',1,1)]:
 colors[layer]=assets.to_arr(assets.seamless_blend(assets.cell(atlas,2,2,col,row,inset=2)))
 if layer==1: colors[layer][:,:,:3]=(colors[layer][:,:,:3]*0.88).astype('uint8')
 normals[layer]=assets.derive_normal_rough(colors[layer],name)
Image.fromarray(colors[35]).resize((64,64),Image.Resampling.LANCZOS).tobytes()
(ROOT/'assets/data/decor_leaf.rgba').write_bytes(Image.fromarray(colors[35]).resize((64,64),Image.Resampling.LANCZOS).tobytes())
update(ROOT/'assets/data/materials.gtex',colors,'medium')
update(ROOT/'assets/data/materials_n.gtex',normals,'medium')
