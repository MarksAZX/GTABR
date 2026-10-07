// Weapon meshes: modelled in code from tapered cylinders, bevelled boxes and lathe profiles, textured from a shared
// procedural PBR atlas (steel, gunmetal, polymer, wood, rubber, chrome, red paint, leather). Grip at the origin,
// barrel / blade along -Z, up +Y, metres.
#pragma once
#include <vector>

#include "../gfx/renderer.h"
#include "combat.h"

namespace gtabr {

struct WeaponMeshes {
  gfx::ModelHandle mesh[kWeaponCount];
  gfx::MaterialHandle material;
  Vec3 muzzle[kWeaponCount];   // muzzle point in weapon space (firearms)
  bool ok = false;
};

bool buildWeaponMeshes(gfx::Renderer& r, WeaponMeshes& out);

}  // namespace gtabr
