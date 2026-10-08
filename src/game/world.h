// Procedural city: generated deterministically from a seed (districts, street grid with avenues, blocks, lots,
// houses/buildings/shops, parks, parking, beach + sea on a coast chosen by the seed), plus colliders, interiors,
// navigation input, shops and the minimap raster. The same seed always rebuilds exactly the same city.
#pragma once
#include <cstdint>
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
  float w() const { return x1 - x0; }
  float h() const { return z1 - z0; }
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
  std::string base;  // "tree_arvore0" / "prop_lixeira" (sprite impostor names)
  int dirCount = 4;
  Vec3 pos;
  float yaw = 0;
  float scale = 1;
  int chunk = 0;
  bool mirror = false;
  int species = -1;  // trees: 3D species index (procedural model); -1 = impostor only
  int model = -1;    // props: 3D prop model index; -1 = impostor only
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
  int shop = -1;
};

struct PumpDef { int id; Vec3 pos; float yaw; };
struct ProductPoint { int id; int item; Vec3 pos; int shop = -1; };
struct NpcSpawn { std::string archetype; int role; Vec3 pos; float yaw; bool interior = false; int shop = -1; };

struct InteriorDef {
  RectF bounds;
  float height = 3.2f;
  Vec3 spawn;
  float spawnYaw = 0;
  int shop = -1;
};

// A street centre line (axis aligned). Horizontal lines run along X at z = c, vertical along Z at x = c.
struct RoadLine { bool horizontal; float c; float hw; float a, b; bool avenue; };

enum class District : uint8_t { Centro, Residencial, Comercial, Orla, Parque, Industrial };
enum class ShopKind : uint8_t { Mercado, Conveniencia, Padaria, Ferragens };
struct ShopStock { int kind; int id; int priceCents; };   // kind 0 item, 1 weapon, 2 ammo (id = weapon)
struct ShopDef {
  int id = 0;
  ShopKind kind = ShopKind::Mercado;
  std::string name;
  Vec3 door;           // outside door (street side)
  int interior = -1;
  Vec3 clerk;          // inside
  std::vector<ShopStock> stock;
};

// A textured surface decal baked into the city layout (cracks, oil, manholes, drains, tyre marks, leaves, patches on the
// ground; graffiti, grime streaks and posters on walls). kind matches shaders/decal.frag; vertical decals face 'yaw'.
struct SurfaceDecal { Vec3 pos; float yaw = 0, hx = 1, hz = 1, alpha = 1; int kind = 2; bool vertical = false; };

struct World {
  static constexpr float kChunk = 32.0f;
  static constexpr float kSidewalkH = 0.14f;
  static constexpr float kInteriorX = 1000.0f;   // interiors live far away on +X

  uint32_t seed = 1;
  float half = 80.0f;          // playable land extends over [-half, half] (+ beach/sea on the coast side)
  RectF land;                  // city land (blocks + streets)
  RectF playArea;              // where the player may go (land + beach + swimmable sea)
  int coastSide = -1;          // -1 none, 0 north (-z), 1 east (+x), 2 south (+z), 3 west (-x)
  float shoreline = 0;         // coordinate of the water line along the coast axis
  RectF beach, sea;            // sand strip and water area
  float waterLevel = 0.0f;
  std::vector<RoadLine> roads;
  std::vector<std::pair<RectF, District>> blocks;
  std::string cityName;

  // chunk meshes (CPU) -> GPU handles are created by the game after generation
  struct Chunk { int cx, cz; MeshData mesh; MeshData lod; gfx::MeshHandle handle, lodHandle; AABB bounds; bool interior = false; bool resident = false; };
  std::vector<Chunk> chunks;
  MeshData interiorCeiling;
  gfx::MeshHandle interiorCeilingHandle;
  std::vector<SurfaceDecal> decals;
  std::vector<Vec3> lampLights;    // street lamp heads (night lights)
  std::vector<Vec3> interiorLights;

  std::vector<Collider> colliders;
  std::vector<DecorInstance> decor;
  std::vector<ParkedCarDef> parked;
  std::vector<DoorDef> doors;
  std::vector<PumpDef> pumps;
  std::vector<ProductPoint> products;
  std::vector<NpcSpawn> npcs;
  std::vector<RectF> walkable;    // sidewalks, plazas, crossings, forecourts, beach (for the navmesh)
  std::vector<RectF> lowRects;    // road-level surfaces (height 0)
  std::vector<RectF> mapRoads, mapWalk, mapBuildings, mapGreen, mapPlaza, mapSand, mapWater, mapParking;
  std::vector<RectF> interiorWalkable, interiorBlockers;
  std::vector<InteriorDef> interiors;
  std::vector<ShopDef> shops;
  RectF serviceBay;           // workshop service zone
  Vec3 workshopMechanic, gasAttendant, neighbour;
  Vec3 spawnPlayer; float spawnYaw = 0;
  Vec3 vehicleSpawn[3]; float vehicleYaw[3] = {0, 0, 0};
  Vec3 poiGas, poiMarket, poiWorkshop, poiBeach, poiPlaza;
  Vec3 poiGasDoor, poiMarketDoor;
  // driving aids for scripted tests / AI: points in the gas station lane and the workshop bay
  Vec3 gasLaneEntry, gasLanePump, gasLaneExit, workshopBayEntry, marketParking;
  Vec3 gasApproach, gasLaneTurn, gasExitStreet, workshopApproach;   // street-side points in front of the lot entrances
  std::vector<Vec3> pickupSpots;   // sidewalk / park spots for weapon pickups
  std::vector<Vec3> poiList;       // general destinations for pedestrians

  // spatial hash of colliders
  float gridCell = 8.0f;
  int gridMinX = 0, gridMinZ = 0, gridW = 0, gridH = 0;
  std::vector<std::vector<int>> grid;
  void buildGrid();
  void queryColliders(float x0, float z0, float x1, float z1, std::vector<int>& out) const;

  float heightAt(float x, float z) const;     // ground (or sea floor) height
  float waterDepth(float x, float z) const;   // > 0 where there is water above the ground
  int interiorAt(float x, float z) const;
  bool inInterior(float x, float z) const { return interiorAt(x, z) >= 0; }
  // Road graph helpers (centre lines)
  Vec2 nearestRoadPoint(Vec2 p) const;
  // Waypoints from a to b following the street grid (driving on the right lane).
  std::vector<Vec2> roadRoute(Vec2 a, Vec2 b) const;
};

// Generates the whole city for 'seed' into 'w' (thread-safe: touches only 'w').
void buildWorld(World& w, uint32_t seed);
// Rasterises the map (RGBA8, size x size) covering [-extent, extent] on both axes.
void renderMinimap(const World& w, std::vector<uint8_t>& rgba, int size, float extent);

}  // namespace gtabr
