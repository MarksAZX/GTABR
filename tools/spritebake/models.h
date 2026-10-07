#pragma once
#include <string>

#include "raster.h"

namespace bake {

enum Mat : uint16_t {
  M_PAINT = 0, M_GLASS, M_PLASTIC, M_CHROME, M_TIRE, M_LIGHT_F, M_LIGHT_R, M_PLATE, M_SKIN, M_CLOTH, M_HAIR, M_SHOE,
  M_FOLIAGE, M_BARK, M_METAL, M_RUBBER, M_MATTE, M_COUNT
};
std::vector<Material> makeMaterials();

struct VehicleSpec {
  std::string name;   // compacto / sedan / picape
  Vec3 paint;
  std::string colorName;
};
Mesh buildVehicle(const std::string& model, const Vec3& paint);

struct CharSpec {
  std::string name;
  Vec3 skin{0.85f, 0.62f, 0.48f};
  Vec3 hair{0.07f, 0.05f, 0.04f};
  int hairStyle = 0;  // 0 short, 1 long, 2 bun, 3 bald, 4 afro/curly, 5 ponytail
  Vec3 shirt{0.9f, 0.8f, 0.1f};
  Vec3 pants{0.15f, 0.2f, 0.4f};
  Vec3 shoes{0.9f, 0.9f, 0.9f};
  bool longSleeves = false;
  bool shorts = false;
  int hat = 0;  // 0 none, 1 cap, 2 straw hat
  Vec3 hatCol{0.1f, 0.2f, 0.6f};
  bool apron = false;
  Vec3 apronCol{0.75f, 0.1f, 0.1f};
  bool backpack = false;
  bool beard = false;
  bool moustache = false;
  bool bag = false;
  float scale = 1.0f;
  float girth = 1.0f;
  bool skirt = false;
};
enum class Gait { Idle, Walk, Run };
Mesh buildCharacter(const CharSpec& spec, Gait gait, float phase01);

Mesh buildTree(int type, unsigned seed);  // 0 broadleaf, 1 palm, 2 shrub
Mesh buildProp(const std::string& name);

}  // namespace bake
