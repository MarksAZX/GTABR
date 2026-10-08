// Gameplay entities: vehicle definitions + simulation state, player and pedestrians.
#pragma once
#include <string>
#include <vector>

#include "../core/util.h"
#include "combat.h"
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

constexpr int kVehicleModels = 4;
constexpr int kPoliceCarModel = 3;
inline const VehicleDef& vehicleDef(int m) {
  static const VehicleDef defs[kVehicleModels] = {
      {"compacto", "Compacto", {"branco", "vermelho", "prata"}, 3.90f, 1.66f, 2.36f, 31.0f, 4.8f, 10.0f, 0.56f, 7.5f, 45.0f, 7.5f, 950.0f},
      {"sedan", "Sedã", {"grafite", "branco", "azul"}, 4.55f, 1.78f, 2.80f, 40.0f, 5.6f, 11.0f, 0.50f, 8.0f, 55.0f, 9.0f, 1350.0f},
      {"picape", "Picape", {"azul", "branco", "vinho"}, 4.95f, 1.82f, 3.00f, 36.0f, 4.4f, 10.5f, 0.46f, 7.0f, 70.0f, 12.5f, 1850.0f},
      {"viatura", "Viatura", {"branco", "branco", "branco"}, 4.45f, 1.76f, 2.70f, 42.0f, 6.2f, 12.0f, 0.52f, 8.6f, 55.0f, 9.0f, 1300.0f},
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
  // police / AI driving
  bool police = false;
  bool siren = false;
  int driver = -1;          // npc index driving it (police), -1 none
  Vec2 aiTarget;
  float aiStuck = 0, aiReverse = 0;
  bool despawn = false;
  bool wrecked = false;
  float steerInput = 0;   // last steering input (turn signals)
  bool braking = false;   // brake lights
  // ambient traffic (game_traffic.cpp): civilian cars cruising the street grid in their lane
  bool traffic = false;
  std::vector<Vec2> route;
  size_t routeIdx = 0;
  Vec2 routeDest;
  bool atRouteEnd = false;   // reached the end of the lane route (police: stop and deploy the crew)
  float cruise = 9.0f, blockedT = 0, honkT = 0;
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
  float air = 0, airV = 0, airT = 0, airDur = 0.836f;   // jump height above the ground, its vertical speed and the time since take-off
  bool airborne = false;
  bool indoors = false;
  bool swimming = false;   // in deep water (sea): swim locomotion, no weapons, no cars
  int weapon = 0;          // equipped WeaponId
  // ---- combat
  bool owned[kWeaponCount] = {true};
  int mag[kWeaponCount] = {};
  int reserve[kWeaponCount] = {};
  float attackT = -1;      // seconds into the current melee attack (-1 = none)
  float attackDur = 0;
  int attackKind = 0;      // 1 punch, 2 kick, 3 weapon swing
  bool attackHit = false;
  int combo = 0;
  float comboWindow = 0;
  // dodge roll / guard (the jump button turns into the defence button while a fight is on)
  float dodgeT = -1;       // seconds into the roll (-1 = none); the first 0.4 s cannot be hit
  Vec2 dodgeDir;
  float guardHold = 0;     // how long the defence button has been held
  bool blocking = false;
  float lunge = 0;         // forward drive while a strike is in progress (m/s)
  float strikePower = 1;   // damage / knock-back multiplier of the current strike
  float fireCooldown = 0;
  float reloadT = -1;      // seconds into a reload (-1 = none)
  float aimHold = 0;       // keeps the weapon raised after a shot
  Vec2 aimDir{0, -1};
  int aimNpc = -1;
  float hitStun = 0;
  Vec2 knockVel;
  bool down = false;       // knocked over (gets up), or dead when health <= 0
  float downT = 0;
  bool dead = false;
  int animReq = -1;        // one-shot animation request consumed by the renderer
  float animReqSpeed = 1;
  bool animReqUpper = false, animReqHold = false;
};

enum class NpcState : uint8_t {
  Idle, Walk, Flee, Talk, Stunned, Work,
  Chat,        // talking with another pedestrian
  Alert,       // heard/saw something, looking at it
  Fight,       // fighting a target (self-defence, provoked, police arrest)
  Cower,       // crouched, too scared to run
  Down,        // knocked out on the floor (gets up later)
  Dead,
  CallPolice,  // witness on the phone
  // police behaviour
  CopPatrol, CopInvestigate, CopChase, CopSearch, CopReturn
};
enum class Mood : uint8_t { Calm, Friendly, Scared, Angry };

struct Npc {
  int id = 0;
  std::string archetype;
  int role = 0;            // 0 pedestrian, 1 frentista, 2 mecanico, 3 vizinho, 4 atendente
  int shop = -1;           // shop this clerk works in (World::shops index)
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
  // ---- personality / perception
  float bravery = 0.3f;     // 0 coward .. 1 fights back
  float temper = 0.2f;      // how easily provoked
  Mood mood = Mood::Calm;
  float fear = 0;           // 0..1 decays over time
  ActorRef threat;          // who scared / attacked us
  Vec2 threatPos;
  float alertT = 0;
  int chatWith = -1;        // npc index while chatting
  float reportT = -1;       // seconds until a witness finishes calling the police
  Vec2 reportPos;
  float reportSeverity = 0;
  int lineIdx = 0;          // dialogue rotation
  float lastEventT = -1;    // newest world event already perceived
  // ---- combat
  float health = 100;
  int weapon = 0;
  int mag = 0;
  float attackCd = 0;
  float attackT = -1, attackDur = 0;
  int attackKind = 0;
  bool attackHit = false;
  ActorRef target;
  float hitStun = 0;
  Vec2 knockVel;
  float downT = 0;          // time left on the floor
  float deadT = 0;          // time since death (corpse cleanup)
  int animReq = -1;
  float animReqSpeed = 1;
  bool animReqUpper = false, animReqHold = false;
  // ---- police
  bool police = false;
  int unit = -1;            // police car index (vehicles_) this officer arrived with
  Vec2 lastKnown;           // last known suspect position
  float lastSeenT = 1e9f;   // seconds since the suspect was last seen
  float searchT = 0;
  Vec2 searchPoint;
  bool despawn = false;     // remove when far from the player
};

}  // namespace gtabr
