// Weapons, damage and world events shared by the player, pedestrians and the police.
#pragma once
#include <cstdint>

#include "../core/math.h"

namespace gtabr {

enum class DamageType : uint8_t { Unarmed, Blunt, Blade, Bullet, Crash, RunOver };
enum class WeaponClass : uint8_t { Unarmed, Melee, Pistol, Automatic, Shotgun };

constexpr int kWeaponCount = 9;
enum WeaponId : int {
  kWpnFists = 0, kWpnKnife = 1, kWpnBaton = 2, kWpnCrowbar = 3, kWpnBat = 4,
  kWpnPistol = 5, kWpnRevolver = 6, kWpnSmg = 7, kWpnShotgun = 8
};

struct WeaponDef {
  int id;
  const char* key;        // save key
  const char* name;
  const char* typeLabel;  // shown in the wheel
  WeaponClass cls;
  DamageType dmgType;
  float damage;           // per hit / per pellet
  float range;            // metres (melee reach or effective firearm range)
  float cooldown;         // seconds between attacks / shots
  int magazine;           // 0 = melee
  int reserveMax;
  float reloadTime;
  float spread;           // radians (firearms)
  int pellets;
  float knockback;        // metres/s impulse
  float noise;            // radius (m) in which pedestrians hear it
  float hitTime;          // melee: fraction of the swing where the blow lands
  const char* sound;      // sound id
  const char* icon;
};

inline const WeaponDef& weaponDef(int id) {
  static const WeaponDef k[kWeaponCount] = {
      {0, "punhos", "Punhos", "Corpo a corpo", WeaponClass::Unarmed, DamageType::Unarmed, 9, 1.35f, 0.42f, 0, 0, 0, 0, 1, 2.2f, 7, 0.45f, "punch", "fist"},
      {1, "faca", "Faca de cozinha", "Arma branca", WeaponClass::Melee, DamageType::Blade, 28, 1.45f, 0.48f, 0, 0, 0, 0, 1, 1.2f, 6, 0.42f, "slash", "knife"},
      {2, "bastao", "Cassetete", "Arma branca", WeaponClass::Melee, DamageType::Blunt, 20, 1.7f, 0.55f, 0, 0, 0, 0, 1, 3.2f, 8, 0.45f, "blunt", "baton"},
      {3, "pe_de_cabra", "Pé de cabra", "Arma branca", WeaponClass::Melee, DamageType::Blunt, 30, 1.8f, 0.75f, 0, 0, 0, 0, 1, 3.8f, 9, 0.5f, "metal", "crowbar"},
      {4, "taco", "Taco de beisebol", "Arma branca", WeaponClass::Melee, DamageType::Blunt, 34, 2.0f, 0.85f, 0, 0, 0, 0, 1, 5.0f, 9, 0.5f, "blunt", "bat"},
      {5, "pistola", "Pistola 9mm", "Arma de fogo", WeaponClass::Pistol, DamageType::Bullet, 26, 38, 0.24f, 15, 90, 1.25f, 0.035f, 1, 1.5f, 55, 0, "pistol", "pistol"},
      {6, "revolver", "Revólver .38", "Arma de fogo", WeaponClass::Pistol, DamageType::Bullet, 45, 42, 0.62f, 6, 36, 2.1f, 0.02f, 1, 2.6f, 60, 0, "revolver", "revolver"},
      {7, "submetralhadora", "Submetralhadora", "Arma de fogo", WeaponClass::Automatic, DamageType::Bullet, 15, 30, 0.085f, 30, 150, 1.8f, 0.07f, 1, 1.0f, 65, 0, "smg", "smg"},
      {8, "espingarda", "Espingarda calibre 12", "Arma de fogo", WeaponClass::Shotgun, DamageType::Bullet, 13, 18, 0.95f, 6, 36, 2.6f, 0.14f, 8, 4.5f, 75, 0, "shotgun", "shotgun"},
  };
  return k[id < 0 || id >= kWeaponCount ? 0 : id];
}
inline bool isFirearm(int id) { return weaponDef(id).magazine > 0; }

// Who is hit / who attacked. Npc index space covers pedestrians and police officers alike.
enum class ActorKind : uint8_t { None, Player, Npc, Vehicle, World };
struct ActorRef {
  ActorKind kind = ActorKind::None;
  int index = -1;
  bool operator==(const ActorRef& o) const { return kind == o.kind && index == o.index; }
  bool valid() const { return kind != ActorKind::None; }
};

struct DamageInfo {
  DamageType type = DamageType::Unarmed;
  float amount = 0;
  ActorRef attacker;
  Vec2 dir;           // direction the hit travels (knockback)
  float knockback = 0;
  Vec3 point;
  int weapon = -1;
};

// World events heard / seen by pedestrians and the police (assault, gunshot, crash, ...).
enum class EventKind : uint8_t { Assault, Gunshot, Crash, RunOver, Kill, WeaponDrawn, CopAssault };
struct WorldEvent {
  EventKind kind;
  Vec2 pos;
  float radius;       // perception radius
  ActorRef instigator;
  ActorRef victim;
  float time;         // game time when it happened
  float severity;     // crime weight (feeds the wanted level when reported)
};

}  // namespace gtabr
