// Gameplay entities: vehicle definitions + simulation state, player and pedestrians.
#pragma once
#include <string>
#include <vector>

#include "../core/util.h"
#include "physics.h"
#include "world.h"

namespace gtabr {

struct VehicleDef {
  const char* model;      // sprite model name
  const char* name;       // display name
  const char* colors[3];  // sprite colour names
  float length, width;
  float wheelbase;
  float maxSpeed;         // m/s
  float accel;            // m/s^2
  float brake;            // m/s^2
  float steerMax;         // radians at standstill
  float grip;             // lateral grip (1/s)
  float fuelCap;          // litres
  float consumption;      // L / 100 km (cruise)
  float mass;
};

constexpr int kVehicleModels = 3;
inline const VehicleDef& vehicleDef(int m) {
  static const VehicleDef defs[kVehicleModels] = {
      {"compacto", "Compacto", {"branco", "vermelho", "prata"}, 3.90f, 1.66f, 2.36f, 31.0f, 4.8f, 10.0f, 0.56f, 7.5f, 45.0f, 7.5f, 950.0f},
      {"sedan", "Sedã", {"grafite", "branco", "azul"}, 4.55f, 1.78f, 2.80f, 40.0f, 5.6f, 11.0f, 0.50f, 8.0f, 55.0f, 9.0f, 1350.0f},
      {"picape", "Picape", {"azul", "branco", "vinho"}, 4.95f, 1.82f, 3.00f, 36.0f, 4.4f, 10.5f, 0.46f, 7.0f, 70.0f, 12.5f, 1850.0f},
  };
  return defs[m < 0 || m >= kVehicleModels ? 0 : m];
}

constexpr float kFuelGameScale = 6.0f;   // faster consumption than reality so refuelling matters in a short session

struct VehicleInput {
  float throttle = 0;   // -1 (brake/reverse) .. 1
  float steer = 0;      // -1 left .. 1 right
  bool handbrake = false;
};

struct Vehicle {
  int id = 0;
  int model = 0;
  int color = 0;
  Vec2 pos;
  float yaw = 0;
  Vec2 vel;
  float yawRate = 0;
  float steerAngle = 0;
  float speed = 0;        // signed forward speed (m/s)
  float fuel = 20;        // litres
  float health = 100;     // percent
  float smokeTimer = 0;
  float engineLoad = 0;
  float visualPitch = 0, visualRoll = 0;
  int occupant = -1;      // -1 none, 0 player
  bool engineOn = false;
  float lastImpact = 0;   // impact intensity for HUD / camera shake
  float outOfFuelTimer = 0;
  float y = 0;
  float wheelSpin = 0;
  float steerInput = 0;   // last steering input (turn signals)
  bool braking = false;   // brake lights
};

// Steps one vehicle. Returns the impact speed of a collision this step (0 if none).
float stepVehicle(Vehicle& v, const VehicleInput& in, float dt, const World& w, const std::vector<Vehicle>& others);

inline phys::OBB vehicleObb(const Vehicle& v) {
  const VehicleDef& d = vehicleDef(v.model);
  return {v.pos, {d.width * 0.5f, d.length * 0.5f}, v.yaw};
}

struct Player {
  Vec2 pos;
  float yaw = 0;
  float targetYaw = 0;
  Vec2 vel;
  float speed = 0;
  float health = 100;
  float stamina = 100;
  float staminaCooldown = 0;
  float runBoost = 0;      // seconds of free running
  bool running = false;
  float animTime = 0;
  int vehicle = -1;        // index of the vehicle being driven
  float transition = 0;    // enter / exit animation 0..1
  int transitionVehicle = -1;
  bool entering = false, exiting = false;
  float hurtTimer = 0;
  float y = 0;
  bool indoors = false;
  int weapon = 0;          // equipped weapon item id
};

enum class NpcState : uint8_t { Idle, Walk, Flee, Talk, Stunned, Work };

struct Npc {
  int id = 0;
  std::string archetype;
  int role = 0;            // 0 pedestrian, 1 frentista, 2 mecanico, 3 vizinho, 4 atendente
  Vec2 pos;
  float yaw = 0;
  float y = 0;
  NpcState state = NpcState::Idle;
  float stateTimer = 0;
  std::vector<Vec2> path;
  size_t pathIdx = 0;
  float speed = 0;
  float animTime = 0;
  float walkSpeed = 1.4f;
  float accumDt = 0;       // for reduced-rate simulation
  int lod = 0;             // 0 near, 1 mid, 2 far
  bool interior = false;
  Vec2 home;
  std::string bubble;      // short speech bubble text
  float bubbleTimer = 0;
  float greetCooldown = 0;
  float lookYaw = 0;
  bool stationary = false;
  float repath = 0;
  float blockedTime = 0;
};

}  // namespace gtabr
