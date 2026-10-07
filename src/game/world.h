// Static world definition: the Brazilian neighbourhood (streets, lots, houses, shops, props), colliders,
// interiors, navigation input and minimap raster. Everything is generated deterministically in code.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/util.h"
#include "meshbuilder.h"

namespace gtabr {

struct RectF {
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
  bool contains(float x, float z) const { return x >= x0 && x <= x1 && z >= z0 && z <= z1; }
  float cx() const { return (x0 + x1) * 0.5f; }
  float cz() const { return (z0 + z1) * 0.5f; }
  RectF inflated(float r) const { return {x0 - r, z0 - r, x1 + r, z1 + r}; }
};

enum class ColKind : uint8_t { Building, Prop, Tree, Car, Wall, Pole };
struct Collider {
  AABB box;
  ColKind kind = ColKind::Building;
  int owner = -1;  // parked car index etc.
};

enum class DecorKind : uint8_t { Tree, Prop };
struct DecorInstance {
  DecorKind kind;
  std::string base;  // "tree_arvore0" / "prop_lixeira"
  int dirCount = 4;
  Vec3 pos;
  float yaw = 0;
  float scale = 1;
  int chunk = 0;
  bool mirror = false;
};

struct ParkedCarDef {
  int model = 0;  // 0 compacto 1 sedan 2 picape
  int color = 1;  // index into the model's colour table
  Vec3 pos;
  float yaw = 0;
};

struct DoorDef {
  int id = 0;
  Vec3 pos;          // interaction point (outside / inside)
  float radius = 1.8f;
  int targetDoor = -1;   // door to teleport to
  Vec3 arrive;       // where the player appears
  float arriveYaw = 0;
  std::string label;
  bool toInterior = false;
};

struct PumpDef { int id; Vec3 pos; float yaw; };
struct ProductPoint { int id; int item; Vec3 pos; };
struct NpcSpawn { std::string archetype; int role; Vec3 pos; float yaw; bool interior = false; };

struct InteriorDef {
  RectF bounds;
  float height = 3.2f;
  Vec3 spawn;
  float spawnYaw = 0;
};

struct World {
  static constexpr float kChunk = 32.0f;
  static constexpr float kHalf = 80.0f;
  static constexpr float kSidewalkH = 0.14f;

  // chunk meshes (CPU) -> GPU handles are created by the game after generation
  struct Chunk { int cx, cz; MeshData mesh; gfx::MeshHandle handle; AABB bounds; };
  std::vector<Chunk> chunks;
  MeshData marketCeiling;
  gfx::MeshHandle marketCeilingHandle;
  std::vector<Vec3> lampLights;   // street lamp heads (night lights)

  std::vector<Collider> colliders;
  std::vector<DecorInstance> decor;
  std::vector<ParkedCarDef> parked;
  std::vector<DoorDef> doors;
  std::vector<PumpDef> pumps;
  std::vector<ProductPoint> products;
  std::vector<NpcSpawn> npcs;
  std::vector<RectF> walkable;    // sidewalks, plazas, crossings, forecourts (for the navmesh)
  std::vector<RectF> lowRects;    // road-level surfaces (height 0)
  std::vector<RectF> mapRoads, mapWalk, mapBuildings, mapGreen, mapPlaza;
  std::vector<RectF> interiorWalkable, interiorBlockers;
  InteriorDef market;
  RectF serviceBay;           // workshop service zone
  Vec3 workshopMechanic, gasAttendant, marketClerk, neighbour;
  Vec3 spawnPlayer; float spawnYaw = 0;
  Vec3 vehicleSpawn[3]; float vehicleYaw[3] = {0, 0, 0};
  Vec3 poiGas, poiMarket, poiWorkshop;
  Vec3 poiGasDoor, poiMarketDoor;

  // spatial hash of colliders
  float gridCell = 8.0f;
  int gridMinX = 0, gridMinZ = 0, gridW = 0, gridH = 0;
  std::vector<std::vector<int>> grid;
  void buildGrid();
  void queryColliders(float x0, float z0, float x1, float z1, std::vector<int>& out) const;

  float heightAt(float x, float z) const;
  bool inInterior(float x, float z) const { return market.bounds.contains(x, z); }
  void chunkRange();
};

// Generates the whole world into 'w' (thread-safe: touches only 'w').
void buildWorld(World& w);
// Rasterises the minimap (RGBA8, size x size) covering [-extent, extent] on both axes.
void renderMinimap(const World& w, std::vector<uint8_t>& rgba, int size, float extent);

}  // namespace gtabr
