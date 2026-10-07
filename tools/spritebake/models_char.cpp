// Procedural, animated humanoid characters (idle / walk / run) built from primitives.
#include <cmath>

#include "models.h"

namespace bake {
namespace {
Vec3 mulc(Vec3 c, float k) { return {c.x * k, c.y * k, c.z * k}; }

// Limb as a tapered capsule from a to b, transformed by xf.
void limb(Mesh& m, const Xf& xf, Vec3 a, Vec3 b, float ra, float rb, Vec3 col, uint16_t mat) {
  Mesh p = makeCapsule(a, b, ra, rb, 10, col, mat);
  transformMesh(p, xf);
  m.append(p);
}
void part(Mesh& m, const Xf& xf, Mesh p) {
  transformMesh(p, xf);
  m.append(p);
}
}  // namespace

Mesh buildCharacter(const CharSpec& s, Gait gait, float phase01) {
  Mesh m;
  const float S = s.scale;
  const float G = s.girth;
  const float ph = phase01 * 2.0f * kPi;
  float A = 0.0f, K = 0.0f, armA = 0.0f, elbow = 0.22f, lean = 0.0f, bob = 0.0f;
  if (gait == Gait::Walk) { A = 0.46f; K = 0.62f; armA = 0.42f; elbow = 0.28f; bob = 0.014f; }
  if (gait == Gait::Run) { A = 0.85f; K = 1.25f; armA = 0.9f; elbow = 1.25f; lean = 0.2f; bob = 0.03f; }
  const float bobY = (gait == Gait::Idle) ? 0.0f : bob * std::cos(2 * ph) - bob * 0.5f;

  // root transform: scale, lean, bob
  Xf root = Xf::scale({S, S, S});
  root.t = {0, bobY * S, 0};
  Xf leanX = Xf::rotX(lean);  // lean forward (towards -z) rotates around x by +angle at the hip
  Xf upper = root * Xf::translate({0, 0.95f, 0}) * leanX * Xf::translate({0, -0.95f, 0});

  Vec3 skin = s.skin, shirt = s.shirt, pants = s.pants;
  Vec3 pantsLow = s.shorts ? skin : pants;
  uint16_t pantsLowMat = s.shorts ? M_SKIN : M_CLOTH;

  // --- legs (hip at y=0.93)
  for (int side = 0; side < 2; ++side) {
    float sx = side == 0 ? -1.0f : 1.0f;
    float legPh = ph + (side == 0 ? 0.0f : kPi);
    float thigh = (gait == Gait::Idle) ? 0.0f : A * std::sin(legPh);
    float knee = (gait == Gait::Idle) ? 0.0f : K * std::max(0.0f, std::cos(legPh));
    Xf hip = root * Xf::translate({sx * 0.10f * G, 0.93f, 0}) * Xf::rotX(thigh);
    float thighLen = 0.43f, shinLen = 0.41f;
    limb(m, hip, {0, 0, 0}, {0, -thighLen, 0}, 0.098f * G, 0.074f * G, pants, M_CLOTH);
    Xf kneeX = hip * Xf::translate({0, -thighLen, 0}) * Xf::rotX(-knee);
    limb(m, kneeX, {0, 0, 0}, {0, -shinLen + 0.02f, 0}, 0.072f * G, 0.052f * G, pantsLow, pantsLowMat);
    // sock/ankle + shoe
    Xf ankle = kneeX * Xf::translate({0, -shinLen, 0}) * Xf::rotX(0.25f * (thigh - knee) * -0.0f + 0.0f);
    Xf footX = Xf::translate({0, 0, 0});
    // keep the foot roughly level with the ground: compensate the accumulated rotation
    float acc = thigh - knee;
    Xf level = ankle * Xf::rotX(-acc * 0.75f);
    part(m, level, makeRoundedBox({0, -0.035f, -0.045f}, {0.12f, 0.095f, 0.285f}, 0.04f, 4, s.shoes, M_SHOE));
    part(m, level, makeRoundedBox({0, -0.07f, -0.035f}, {0.124f, 0.028f, 0.29f}, 0.012f, 2, mulc(s.shoes, 0.45f), M_RUBBER));
    (void)footX;
  }

  // --- torso
  Vec3 chestC{0, 1.285f, 0}, waistC{0, 1.04f, 0};
  part(m, upper, makeEllipsoid(waistC, {0.185f * G, 0.19f, 0.118f * G}, 14, 10, s.apron ? shirt : pants, M_CLOTH));
  part(m, upper, makeEllipsoid({0, 1.13f, 0}, {0.19f * G, 0.13f, 0.12f * G}, 14, 8, shirt, M_CLOTH));
  part(m, upper, makeEllipsoid(chestC, {0.215f * G, 0.235f, 0.13f * G}, 16, 12, shirt, M_CLOTH));
  part(m, upper, makeEllipsoid({0, 1.43f, 0}, {0.225f * G, 0.085f, 0.115f * G}, 14, 8, shirt, M_CLOTH));  // shoulders
  if (s.skirt) part(m, upper, makeCylinder({0, 1.0f, 0}, {0, 0.62f, 0}, 0.16f * G, 0.24f * G, 16, pants, M_CLOTH));
  // hips: blend the legs into the torso
  part(m, upper, makeEllipsoid({0, 0.96f, 0}, {0.2f * G, 0.1f, 0.125f * G}, 14, 8, s.skirt ? s.shirt : pants, M_CLOTH));
  if (s.apron) {
    part(m, upper, makeRoundedBox({0, 1.02f, -0.095f * G}, {0.27f * G, 0.52f, 0.025f}, 0.01f, 3, s.apronCol, M_CLOTH));
    part(m, upper, makeRoundedBox({0, 1.05f, 0.10f * G}, {0.06f, 0.4f, 0.01f}, 0.004f, 2, s.apronCol, M_CLOTH));
  }
  // neck
  part(m, upper, makeCylinder({0, 1.46f, 0}, {0, 1.54f, 0}, 0.045f, 0.04f, 10, skin, M_SKIN));
  // collar
  part(m, upper, makeCylinder({0, 1.455f, 0}, {0, 1.49f, 0}, 0.062f, 0.058f, 12, shirt, M_CLOTH, false));

  // --- arms (shoulder at y=1.425)
  for (int side = 0; side < 2; ++side) {
    float sx = side == 0 ? -1.0f : 1.0f;
    float legPh = ph + (side == 0 ? 0.0f : kPi);
    float swing = (gait == Gait::Idle) ? 0.0f : -armA * std::sin(legPh);
    float flex = (gait == Gait::Idle) ? 0.18f : elbow + 0.25f * std::max(0.0f, swing);
    if (gait == Gait::Run) flex = elbow + 0.3f * std::max(0.0f, swing);
    Xf sh = upper * Xf::translate({sx * 0.245f * G, 1.425f, 0}) * Xf::rotZ(-sx * 0.07f) * Xf::rotX(swing);
    float uLen = 0.285f, fLen = 0.27f;
    Vec3 sleeveCol = shirt;
    limb(m, sh, {0, 0, 0}, {0, -uLen, 0}, 0.068f * G, 0.054f * G, sleeveCol, M_CLOTH);
    Xf el = sh * Xf::translate({0, -uLen, 0}) * Xf::rotX(flex * -1.0f);
    // forearm: sleeve for long-sleeved shirts, skin otherwise
    limb(m, el, {0, 0, 0}, {0, -fLen, 0}, 0.052f * G, 0.04f * G, s.longSleeves ? shirt : skin, s.longSleeves ? M_CLOTH : M_SKIN);
    part(m, el * Xf::translate({0, -fLen - 0.03f, 0}), makeEllipsoid({0, 0, 0}, {0.038f, 0.055f, 0.03f}, 8, 6, skin, M_SKIN));
    if (s.bag && side == 1) {
      part(m, el * Xf::translate({0.02f, -fLen - 0.09f, 0}), makeRoundedBox({0, -0.08f, 0}, {0.07f, 0.30f, 0.26f}, 0.02f, 3, Vec3{0.92f, 0.92f, 0.88f}, M_MATTE));
    }
  }
  if (s.backpack)
    part(m, upper, makeRoundedBox({0, 1.28f, 0.17f * G}, {0.30f * G, 0.38f, 0.14f}, 0.05f, 4, Vec3{0.12f, 0.12f, 0.14f}, M_CLOTH));

  // --- head
  Xf head = upper * Xf::translate({0, 1.625f, 0.005f});
  part(m, head, makeEllipsoid({0, 0, 0}, {0.094f, 0.122f, 0.108f}, 18, 14, skin, M_SKIN));
  part(m, head, makeEllipsoid({0, -0.045f, -0.03f}, {0.075f, 0.075f, 0.075f}, 12, 8, skin, M_SKIN));  // jaw/chin
  part(m, head, makeEllipsoid({0, -0.005f, -0.1f}, {0.017f, 0.022f, 0.02f}, 8, 6, mulc(skin, 0.94f), M_SKIN));  // nose
  for (float ex : {-0.034f, 0.034f}) {
    part(m, head, makeEllipsoid({ex, 0.012f, -0.088f}, {0.012f, 0.008f, 0.01f}, 6, 5, Vec3{0.03f, 0.02f, 0.02f}, M_RUBBER));
    part(m, head, makeEllipsoid({ex, 0.03f, -0.088f}, {0.02f, 0.005f, 0.01f}, 6, 4, mulc(s.hair, 0.8f), M_HAIR));  // brows
  }
  for (float ex : {-0.088f, 0.088f}) part(m, head, makeEllipsoid({ex, -0.005f, 0.0f}, {0.012f, 0.028f, 0.02f}, 6, 5, mulc(skin, 0.95f), M_SKIN));
  // hair
  switch (s.hairStyle) {
    case 0: part(m, head, makeEllipsoid({0, 0.028f, 0.012f}, {0.094f, 0.1f, 0.106f}, 16, 12, s.hair, M_HAIR)); break;
    case 1:
      part(m, head, makeEllipsoid({0, 0.03f, 0.016f}, {0.097f, 0.1f, 0.108f}, 16, 12, s.hair, M_HAIR));
      part(m, head, makeEllipsoid({0, -0.12f, 0.075f}, {0.088f, 0.17f, 0.05f}, 12, 10, s.hair, M_HAIR));
      break;
    case 2:
      part(m, head, makeEllipsoid({0, 0.03f, 0.014f}, {0.095f, 0.1f, 0.107f}, 16, 12, s.hair, M_HAIR));
      part(m, head, makeEllipsoid({0, 0.1f, 0.06f}, {0.045f, 0.045f, 0.045f}, 10, 8, s.hair, M_HAIR));
      break;
    case 3:
      part(m, head, makeEllipsoid({0, 0.07f, 0.03f}, {0.092f, 0.06f, 0.09f}, 12, 8, mulc(s.hair, 1.0f), M_HAIR));
      break;
    case 4: part(m, head, makeEllipsoid({0, 0.05f, 0.02f}, {0.12f, 0.12f, 0.125f}, 16, 12, s.hair, M_HAIR)); break;
    case 5:
      part(m, head, makeEllipsoid({0, 0.03f, 0.014f}, {0.095f, 0.1f, 0.107f}, 16, 12, s.hair, M_HAIR));
      limb(m, head, {0, 0.02f, 0.1f}, {0, -0.14f, 0.15f}, 0.035f, 0.025f, s.hair, M_HAIR);
      break;
    default: break;
  }
  if (s.beard) {
    part(m, head, makeEllipsoid({0, -0.07f, -0.045f}, {0.07f, 0.06f, 0.06f}, 12, 8, s.hair, M_HAIR));
    part(m, head, makeEllipsoid({0, -0.035f, -0.085f}, {0.04f, 0.012f, 0.02f}, 8, 5, s.hair, M_HAIR));
  } else if (s.moustache) {
    part(m, head, makeEllipsoid({0, -0.035f, -0.088f}, {0.042f, 0.012f, 0.02f}, 8, 5, s.hair, M_HAIR));
  }
  if (s.hat == 1) {  // cap
    part(m, head, makeEllipsoid({0, 0.045f, 0.005f}, {0.1f, 0.085f, 0.108f}, 16, 10, s.hatCol, M_CLOTH));
    part(m, head, makeEllipsoid({0, 0.045f, -0.105f}, {0.075f, 0.012f, 0.06f}, 12, 6, s.hatCol, M_CLOTH));
  } else if (s.hat == 2) {  // straw hat
    Vec3 straw{0.82f, 0.68f, 0.38f};
    part(m, head, makeEllipsoid({0, 0.06f, 0.0f}, {0.095f, 0.075f, 0.1f}, 14, 10, straw, M_MATTE));
    part(m, head, makeEllipsoid({0, 0.05f, 0.0f}, {0.24f, 0.012f, 0.24f}, 20, 6, straw, M_MATTE));
    part(m, head, makeCylinder({0, 0.06f, 0}, {0, 0.075f, 0}, 0.098f, 0.098f, 16, mulc(s.hatCol, 1.0f), M_CLOTH, false));
  }
  return m;
}

}  // namespace bake
