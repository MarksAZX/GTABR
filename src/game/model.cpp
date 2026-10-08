#include "model.h"

#include <algorithm>
#include <cstring>

#include "../core/fileio.h"
#include "../core/log.h"

namespace gtabr {

Mat4 quatMatrix(const Quat& q) {
  Mat4 m;
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y,
        wz = q.w * q.z;
  m.at(0, 0) = 1 - 2 * (yy + zz); m.at(0, 1) = 2 * (xy - wz);     m.at(0, 2) = 2 * (xz + wy);
  m.at(1, 0) = 2 * (xy + wz);     m.at(1, 1) = 1 - 2 * (xx + zz); m.at(1, 2) = 2 * (yz - wx);
  m.at(2, 0) = 2 * (xz - wy);     m.at(2, 1) = 2 * (yz + wx);     m.at(2, 2) = 1 - 2 * (xx + yy);
  return m;
}

Mat4 Xform::matrix() const {
  Mat4 m = quatMatrix(r);
  for (int c = 0; c < 3; ++c) {
    float sc = c == 0 ? s.x : (c == 1 ? s.y : s.z);
    for (int rr = 0; rr < 3; ++rr) m.at(rr, c) *= sc;
  }
  m.at(0, 3) = t.x; m.at(1, 3) = t.y; m.at(2, 3) = t.z;
  return m;
}

Mat4 yawMatrix(Vec3 pos, float yaw) {
  Mat4 m;
  float c = std::cos(yaw), s = std::sin(yaw);
  // columns: X = right, Y = up, Z = -forward
  m.at(0, 0) = c;  m.at(1, 0) = 0; m.at(2, 0) = s;
  m.at(0, 2) = -s; m.at(1, 2) = 0; m.at(2, 2) = c;
  m.at(0, 3) = pos.x; m.at(1, 3) = pos.y; m.at(2, 3) = pos.z;
  return m;
}

Mat4 inverseGeneral(const Mat4& a) {
  const float* m = a.m;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  Mat4 r;
  float id = std::fabs(det) > 1e-30f ? 1.0f / det : 0.0f;
  for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * id;
  return r;
}

int Skeleton::find(const std::string& n) const {
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == n) return (int)i;
  return -1;
}

namespace {
template <class T>
bool readAt(const std::vector<uint8_t>& b, size_t& off, T* dst, size_t count = 1) {
  size_t n = sizeof(T) * count;
  if (off + n > b.size()) return false;
  std::memcpy(dst, b.data() + off, n);
  off += n;
  return true;
}
std::string fixedName(const char* s) { return std::string(s, strnlen(s, gmesh::kNameLen)); }
}  // namespace

// The auto-rig weights of the source characters bleed between limbs (thigh vertices bound to the hand), which tears
// legs apart in motion. The bind pose is a clean A-pose, so weights are rebuilt from the distance to each bone segment.
void rebuildSkinWeights(ModelAsset& m, const std::vector<Mat4>& g) {
  const Skeleton& sk = m.skel;
  size_t nb = sk.names.size();
  std::vector<Vec3> jp(nb);
  for (size_t b = 0; b < nb; ++b) jp[b] = (m.rootFix * g[b]).transformPoint({0, 0, 0});
  std::vector<std::vector<int>> kids(nb);
  for (size_t b = 0; b < nb; ++b)
    if (sk.parent[b] >= 0) kids[sk.parent[b]].push_back((int)b);
  struct Seg { Vec3 a, b; int bone; int side; float radius; };
  std::vector<Seg> segs;
  for (size_t b = 0; b < nb; ++b) {
    const std::string& n = sk.names[b];
    if (n == "head_end" || n == "headfront") continue;
    Vec3 end;
    int child = -1;
    for (int c : kids[b]) {
      const std::string& cn = sk.names[c];
      if (cn == "headfront") continue;
      if (child < 0 || cn.find("Spine") != std::string::npos || cn == "neck") child = c;
    }
    if (n == "Hips") end = (jp[b] + jp[sk.find("Spine02") >= 0 ? sk.find("Spine02") : b]) * 0.5f;
    else if (child >= 0) end = jp[child];
    else {
      Vec3 dir = sk.parent[b] >= 0 ? (jp[b] - jp[sk.parent[b]]) : Vec3{0, 0.1f, 0};
      end = jp[b] + dir * 0.6f;
    }
    int side = n.rfind("Left", 0) == 0 ? -1 : (n.rfind("Right", 0) == 0 ? 1 : 0);
    float radius = n.find("Hand") != std::string::npos ? 0.45f : (n.find("Arm") != std::string::npos ? 0.8f : 1.0f);   // hands only claim what is really near them
    segs.push_back({jp[b], end, (int)b, side, radius});
  }
  // which side of the mesh is "Left": sign of the LeftArm joint
  int la = sk.find("LeftArm");
  float leftSign = la >= 0 && jp[la].x > 0 ? 1.0f : -1.0f;
  for (gfx::ModelVertex& v : m.verts) {
    Vec3 p{v.p[0], v.p[1], v.p[2]};
    float best[4] = {1e9f, 1e9f, 1e9f, 1e9f};
    int bi[4] = {0, 0, 0, 0};
    for (const Seg& sgm : segs) {
      if (sgm.side != 0 && std::fabs(p.x) > 0.035f) {
        bool vertLeft = (p.x * leftSign) > 0;
        if (vertLeft != (sgm.side < 0)) continue;
      }
      Vec3 ab = sgm.b - sgm.a;
      float t = clamp((p - sgm.a).dot(ab) / std::max(1e-6f, ab.dot(ab)), 0.0f, 1.0f);
      float d = (p - (sgm.a + ab * t)).length() / sgm.radius;
      for (int k = 0; k < 4; ++k)
        if (d < best[k]) {
          for (int q = 3; q > k; --q) { best[q] = best[q - 1]; bi[q] = bi[q - 1]; }
          best[k] = d; bi[k] = sgm.bone;
          break;
        }
    }
    float w[3], sum = 0;
    for (int k = 0; k < 3; ++k) {
      float d = best[k] + 0.02f;
      w[k] = best[k] < 1e8f ? 1.0f / (d * d * d) : 0.0f;
      sum += w[k];
    }
    int acc = 0;
    for (int k = 0; k < 4; ++k) { v.j[k] = (uint8_t)bi[k]; v.w[k] = 0; }
    for (int k = 0; k < 3; ++k) {
      float f = sum > 0 ? w[k] / sum : (k == 0 ? 1.0f : 0.0f);
      if (f < 0.04f) f = 0;
      v.w[k] = (uint8_t)std::lround(f * 255.0f);
      acc += v.w[k];
    }
    v.w[0] = (uint8_t)clamp(v.w[0] + (255 - acc), 0, 255);
  }
}

bool loadModel(const std::string& path, ModelAsset& m) {
  std::vector<uint8_t> b;
  if (!fileio::readAsset(path, b)) { LOGE("model %s missing", path.c_str()); return false; }
  size_t off = 0;
  gmesh::Header h;
  if (!readAt(b, off, &h) || h.magic != gmesh::kMagicMesh) { LOGE("bad gmesh %s", path.c_str()); return false; }
  m.skinned = (h.flags & gmesh::kSkinned) != 0;
  m.vehicle = (h.flags & gmesh::kVehicle) != 0;
  m.bounds.mn = {h.bmin[0], h.bmin[1], h.bmin[2]};
  m.bounds.mx = {h.bmax[0], h.bmax[1], h.bmax[2]};
  for (int i = 0; i < 3; ++i) m.lodDistance[i] = h.lodDistance[i];
  m.texAlbedo = fixedName(h.textures[0]);
  m.texNormal = fixedName(h.textures[1]);
  m.texOrm = fixedName(h.textures[2]);
  for (int i = 0; i < 4; ++i) m.wheel[i] = {h.wheel[i][0], h.wheel[i][1], h.wheel[i][2]};
  m.wheelRadius = h.wheelRadius;
  m.wheelWidth = h.wheelWidth;
  for (int i = 0; i < 2; ++i) {
    m.headlight[i] = {h.headlight[i][0], h.headlight[i][1], h.headlight[i][2]};
    m.taillight[i] = {h.taillight[i][0], h.taillight[i][1], h.taillight[i][2]};
  }
  m.animRootScale = h.animRootScale > 0 ? h.animRootScale : 1.0f;
  if (h.boneCount > (uint32_t)gfx::kMaxBones) { LOGE("%s: too many bones", path.c_str()); return false; }
  for (uint32_t i = 0; i < h.boneCount; ++i) {
    gmesh::Bone bo;
    if (!readAt(b, off, &bo)) return false;
    m.skel.names.push_back(fixedName(bo.name));
    m.skel.parent.push_back(bo.parent);
    Mat4 ib;
    std::memcpy(ib.m, bo.invBind, 64);
    m.skel.invBind.push_back(ib);
    Xform x;
    x.t = {bo.t[0], bo.t[1], bo.t[2]};
    x.r = {bo.r[0], bo.r[1], bo.r[2], bo.r[3]};
    x.s = {bo.s[0], bo.s[1], bo.s[2]};
    m.skel.rest.push_back(x);
  }
  m.lodCount = (int)std::min<uint32_t>(h.lodCount, gmesh::kMaxLods);
  for (uint32_t l = 0; l < h.lodCount; ++l) {
    gmesh::LodHeader lh;
    if (!readAt(b, off, &lh)) return false;
    uint32_t base = (uint32_t)m.verts.size();
    size_t vstart = m.verts.size();
    m.verts.resize(vstart + lh.vertexCount);
    if (!readAt(b, off, (gfx::ModelVertex*)m.verts.data() + vstart, lh.vertexCount)) return false;
    std::vector<uint32_t> li(lh.indexCount);
    if (!readAt(b, off, li.data(), lh.indexCount)) return false;
    gfx::ModelLod lod{(uint32_t)m.idx.size(), lh.indexCount};
    for (uint32_t i : li) m.idx.push_back(i + base);
    if (l < (uint32_t)gmesh::kMaxLods) {
      m.lods.push_back(lod);
      m.lodTris[l] = lh.indexCount / 3;
    }
  }
  // renormalise skin weights (8-bit quantisation and partial exports leave sums != 1, which pulls vertices to the origin)
  if (m.skinned) {
    int fixedZero = 0;
    for (gfx::ModelVertex& v : m.verts) {
      int sum = v.w[0] + v.w[1] + v.w[2] + v.w[3];
      if (sum <= 0) { v.j[0] = 0; v.w[0] = 255; v.w[1] = v.w[2] = v.w[3] = 0; ++fixedZero; continue; }
      if (sum == 255) continue;
      int acc = 0, best = 0;
      for (int k = 0; k < 4; ++k) {
        v.w[k] = (uint8_t)((v.w[k] * 255 + sum / 2) / sum);
        acc += v.w[k];
        if (v.w[k] > v.w[best]) best = k;
      }
      v.w[best] = (uint8_t)clamp(v.w[best] + (255 - acc), 0, 255);
    }
    if (fixedZero) LOGW("%s: %d unweighted vertices bound to the root", path.c_str(), fixedZero);
  }
  // The exporter's rig space can differ from the mesh space (centimetres, axis swaps). The rest pose is the bind
  // pose, so rootFix = (G_rest * invBind)^-1 restores identity skinning at rest; it is applied above the root.
  if (m.skinned && !m.skel.names.empty()) {
    std::vector<Mat4> g(m.skel.names.size());
    for (size_t b = 0; b < g.size(); ++b) {
      Mat4 l = m.skel.rest[b].matrix();
      g[b] = m.skel.parent[b] >= 0 ? g[m.skel.parent[b]] * l : l;
    }
    m.rootFix = inverseGeneral(g[0] * m.skel.invBind[0]);
    float worst = 0;
    for (size_t b = 0; b < g.size(); ++b) {
      Mat4 p = m.rootFix * g[b] * m.skel.invBind[b];
      for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(p.m[i] - ((i % 5 == 0) ? 1.0f : 0.0f)));
    }
    LOGI("%s: rig fix residual %.4f", path.c_str(), worst);
    rebuildSkinWeights(m, g);
    // A-pose -> relaxed arms for the procedural idle
    const char* arms[2][2] = {{"LeftArm", "LeftHand"}, {"RightArm", "RightHand"}};
    for (int s = 0; s < 2; ++s) {
      int a = m.skel.find(arms[s][0]), h2 = m.skel.find(arms[s][1]);
      if (a < 0 || h2 < 0) continue;
      Vec3 pa = (m.rootFix * g[a]).transformPoint({0, 0, 0}), ph = (m.rootFix * g[h2]).transformPoint({0, 0, 0});
      Vec3 v = ph - pa;
      float phi = std::atan2(v.x, -v.y);
      float target = (pa.x < 0 ? -0.10f : 0.10f);
      m.armDown[s] = (target - phi) * 1.3f;
    }
  }
  m.ok = !m.idx.empty();
  return m.ok;
}

bool loadClip(const std::string& path, AnimClip& c) {
  std::vector<uint8_t> b;
  if (!fileio::readAsset(path, b)) { LOGE("clip %s missing", path.c_str()); return false; }
  size_t off = 0;
  gmesh::AnimHeader h;
  if (!readAt(b, off, &h) || h.magic != gmesh::kMagicAnim) return false;
  c.name = fixedName(h.name);
  c.frames = (int)h.frameCount;
  c.fps = h.fps > 0 ? h.fps : 30.0f;
  c.duration = std::max(1.0f / c.fps, (c.frames - 1) / c.fps);
  for (uint32_t i = 0; i < h.boneCount; ++i) {
    char nm[gmesh::kNameLen];
    if (!readAt(b, off, nm, gmesh::kNameLen)) return false;
    c.boneNames.push_back(fixedName(nm));
  }
  std::vector<float> data((size_t)h.frameCount * h.boneCount * 10);
  if (!readAt(b, off, data.data(), data.size())) return false;
  c.keys.resize((size_t)h.frameCount * h.boneCount);
  for (size_t i = 0; i < c.keys.size(); ++i) {
    const float* f = &data[i * 10];
    c.keys[i].t = {f[0], f[1], f[2]};
    c.keys[i].r = Quat{f[3], f[4], f[5], f[6]}.normalized();
    c.keys[i].s = {f[7], f[8], f[9]};
  }
  // remove the root's horizontal drift so the clip loops in place (the game moves the character)
  if (c.frames > 1) {
    int nb = (int)h.boneCount;
    for (int bi = 0; bi < nb; ++bi) {
      // the root is the first bone (parents precede children)
      if (bi != 0) break;
      Vec3 d = c.keys[(size_t)(c.frames - 1) * nb + bi].t - c.keys[bi].t;
      for (int f = 0; f < c.frames; ++f) {
        float k = (float)f / (float)(c.frames - 1);
        Xform& x = c.keys[(size_t)f * nb + bi];
        x.t.x -= d.x * k;
        x.t.z -= d.z * k;
      }
    }
  }
  return true;
}

void uploadModel(gfx::Renderer& r, ModelAsset& m, gfx::TexHandle albedo, gfx::TexHandle normal, gfx::TexHandle orm) {
  if (!m.ok) return;
  m.gpu = r.createModel(m.verts.data(), m.verts.size(), m.idx.data(), m.idx.size(), m.lods.data(), (int)m.lods.size(), m.skinned);
  m.material = r.createModelMaterial(albedo, normal, orm);
  m.verts.clear(); m.verts.shrink_to_fit();
  m.idx.clear(); m.idx.shrink_to_fit();
}

void bindClips(ModelAsset& m, const std::vector<AnimClip>& clips) {
  m.clipMap.clear();
  for (const AnimClip& c : clips) {
    std::vector<int> map(m.skel.names.size(), -1);
    for (size_t b = 0; b < m.skel.names.size(); ++b)
      for (size_t k = 0; k < c.boneNames.size(); ++k)
        if (c.boneNames[k] == m.skel.names[b]) { map[b] = (int)k; break; }
    m.clipMap.push_back(std::move(map));
  }
}

namespace {
void samplePose(const Skeleton& sk, const AnimClip& c, const std::vector<int>& map, float t, float rootScale, std::vector<Xform>& out) {
  size_t nb = sk.names.size();
  out.resize(nb);
  int nbc = (int)c.boneNames.size();
  float ft = std::fmod(std::max(0.0f, t), c.duration) * c.fps;
  int f0 = std::min((int)ft, c.frames - 1);
  int f1 = std::min(f0 + 1, c.frames - 1);
  float u = ft - (float)f0;
  for (size_t b = 0; b < nb; ++b) {
    Xform x = sk.rest[b];
    int k = b < map.size() ? map[b] : -1;
    if (k >= 0) {
      const Xform& a = c.keys[(size_t)f0 * nbc + k];
      const Xform& bb = c.keys[(size_t)f1 * nbc + k];
      x.r = nlerp(a.r, bb.r, u);
      if (sk.parent[b] < 0) {
        // root: animated translation/scale, re-scaled for characters that share the skeleton at another height
        x.t = lerp(a.t, bb.t, u) * rootScale;
        x.s = lerp(a.s, bb.s, u) * rootScale;
      }
    }
    out[b] = x;
  }
}

void globals(const Skeleton& sk, const Mat4& rootFix, const std::vector<Xform>& pose, std::vector<Mat4>& g) {
  g.resize(pose.size());
  for (size_t b = 0; b < pose.size(); ++b) {
    Mat4 l = pose[b].matrix();
    g[b] = sk.parent[b] >= 0 ? g[sk.parent[b]] * l : rootFix * l;
  }
}
}  // namespace

float estimateGroundSpeed(const Skeleton& sk, const Mat4& rootFix, const AnimClip& c, const std::vector<int>& map) {
  int feet[2] = {sk.find("LeftFoot"), sk.find("RightFoot")};
  if (feet[0] < 0 || feet[1] < 0 || c.frames < 4) return 0;
  std::vector<Xform> pose;
  std::vector<Mat4> g;
  std::vector<Vec3> pos[2];
  for (int f = 0; f < c.frames; ++f) {
    samplePose(sk, c, map, f / c.fps, 1.0f, pose);
    globals(sk, rootFix, pose, g);
    for (int k = 0; k < 2; ++k) pos[k].push_back(g[feet[k]].transformPoint({0, 0, 0}));
  }
  std::vector<float> speeds;
  for (int k = 0; k < 2; ++k) {
    float ymin = 1e9f;
    for (auto& p : pos[k]) ymin = std::min(ymin, p.y);
    for (int f = 1; f + 1 < c.frames; ++f) {
      if (pos[k][f].y > ymin + 0.035f) continue;   // only planted frames
      float dz = (pos[k][f + 1].z - pos[k][f - 1].z) * 0.5f * c.fps;   // planted foot slides backward (+Z) at ground speed
      if (dz > 0.05f) speeds.push_back(dz);
    }
  }
  if (speeds.size() < 3) return 0;
  std::sort(speeds.begin(), speeds.end());
  float v = speeds[speeds.size() / 2];
  return v > 0.5f && v < 8.0f ? v : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
void Animator::update(CharAnim& a, const ModelAsset& m, float speed, float dt, bool doEval) {
  if (!clips_ || clips_->size() < kClipCount || !m.skinned) return;
  const AnimClip& walk = (*clips_)[kClipWalk];
  const AnimClip& run = (*clips_)[kClipRun];
  float vWalk = walk.groundSpeed > 0.3f ? walk.groundSpeed : 1.4f;
  float vRun = run.groundSpeed > 1.0f ? run.groundSpeed : 3.6f;
  vWalk *= m.animRootScale;
  vRun *= m.animRootScale;
  // 1D blend tree on the real ground speed
  float tw[kClipCount] = {0, 0, 0, 0, 0};
  bool canSwim = clips_->size() > kClipSwimIdle && (*clips_)[kClipSwim].frames > 1 && (*clips_)[kClipSwimIdle].frames > 1;
  if (a.swimming && canSwim) {
    // in deep water: tread water when still, swim stroke when moving
    float k = clamp(speed / 1.2f, 0.0f, 1.0f);
    tw[kClipSwimIdle] = 1 - k; tw[kClipSwim] = k;
  } else if (speed < 0.08f) tw[kClipIdle] = 1;
  else if (speed < vWalk) { float k = clamp((speed - 0.08f) / std::max(0.1f, vWalk * 0.55f), 0.0f, 1.0f); tw[kClipIdle] = 1 - k; tw[kClipWalk] = k; }
  else { float k = clamp((speed - vWalk) / std::max(0.1f, vRun - vWalk), 0.0f, 1.0f); tw[kClipWalk] = 1 - k; tw[kClipRun] = k; }
  float blendRate = expDecay(9.0f, dt);
  float sum = 0;
  for (int i = 0; i < kClipCount; ++i) { a.w[i] += (tw[i] - a.w[i]) * blendRate; sum += a.w[i]; }
  for (float& w : a.w) w /= std::max(1e-4f, sum);
  // stride matching: playback rate = actual speed / clip ground speed, so feet do not slide
  float rateWalk = speed > 0.08f ? clamp(speed / vWalk, 0.55f, 1.8f) : 1.0f;
  float rateRun = speed > 0.08f ? clamp(speed / vRun, 0.6f, 1.6f) : 1.0f;
  // walk and run share a normalised phase so the crossfade keeps the feet in step
  float phase = a.t[kClipWalk] / walk.duration;
  float dPhaseWalk = dt * rateWalk / walk.duration, dPhaseRun = dt * rateRun / run.duration;
  float wsum = a.w[kClipWalk] + a.w[kClipRun];
  float dPhase = wsum > 1e-3f ? (dPhaseWalk * a.w[kClipWalk] + dPhaseRun * a.w[kClipRun]) / wsum : dPhaseWalk;
  phase = std::fmod(phase + dPhase, 1.0f);
  a.t[kClipWalk] = phase * walk.duration;
  a.t[kClipRun] = std::fmod(phase * 2.0f, 1.0f) * run.duration;   // the walk clip holds two full gait cycles
  a.t[kClipIdle] = std::fmod(a.t[kClipIdle] + dt * a.rateScale, (*clips_)[kClipIdle].duration);
  if (canSwim) {
    a.t[kClipSwim] = std::fmod(a.t[kClipSwim] + dt * clamp(0.6f + speed / 2.0f, 0.6f, 1.5f), (*clips_)[kClipSwim].duration);
    a.t[kClipSwimIdle] = std::fmod(a.t[kClipSwimIdle] + dt, (*clips_)[kClipSwimIdle].duration);
  }
  // procedural layers ease toward their targets
  float lr = expDecay(7.0f, dt);
  a.talk += (a.talkTarget - a.talk) * lr;
  a.reach += (a.reachTarget - a.reach) * lr;
  a.refuel += (a.refuelTarget - a.refuel) * lr;
  a.crouch += (a.crouchTarget - a.crouch) * lr;
  a.wave += (a.waveTarget - a.wave) * lr;
  a.lean += (a.leanTarget - a.lean) * expDecay(5.0f, dt);
  a.headYaw += (a.headYawTarget - a.headYaw) * expDecay(5.0f, dt);
  a.gestureClock += dt;
  // action slot
  if (a.action >= 0) {
    float dur = actionDuration(a.action);
    a.actT += dt * a.actSpeed;
    if (a.actT >= dur) {
      if (a.actHold) a.actT = dur;
      else a.actFading = true;
    }
    float target = a.actFading ? 0.0f : 1.0f;
    a.actW += (target - a.actW) * expDecay(a.actFading ? 9.0f : 14.0f, dt);
    if (a.actFading && a.actW < 0.01f) { a.action = -1; a.actW = 0; }
  }
  if (a.prevAction >= 0) {
    a.prevT = std::min(a.prevT + dt * a.actSpeed, actionDuration(a.prevAction));
    a.prevW *= 1.0f - expDecay(10.0f, dt);
    if (a.prevW < 0.01f) a.prevAction = -1;
  }
  a.aim += (a.aimTarget - a.aim) * expDecay(12.0f, dt);
  a.recoil *= 1.0f - expDecay(14.0f, dt);
  if (doEval) evaluate(a, m);
}

bool Animator::hasAction(int action) const {
  int i = kClipCount + action;
  return clips_ && action >= 0 && i < (int)clips_->size() && (*clips_)[i].frames > 1;
}
float Animator::actionDuration(int action) const {
  return hasAction(action) ? (*clips_)[kClipCount + action].duration : 0.6f;
}
void Animator::play(CharAnim& a, int action, float speed, bool upperBody, bool hold) const {
  if (a.action >= 0 && a.actW > 0.05f) {
    a.prevAction = a.action; a.prevT = a.actT; a.prevW = a.actW; a.prevUpper = a.actUpper;
  }
  a.action = action;
  a.actT = 0;
  a.actSpeed = speed;
  a.actUpper = upperBody;
  a.actHold = hold;
  a.actFading = false;
  a.actW = a.prevAction >= 0 ? 0.0f : a.actW;
}

void Animator::evaluate(CharAnim& a, const ModelAsset& m) {
  const Skeleton& sk = m.skel;
  size_t nb = sk.names.size();
  pose_.assign(nb, Xform{});
  bool first = true;
  float acc = 0;
  for (int i = 0; i < kClipCount; ++i) {
    if (a.w[i] < 0.01f || i >= (int)m.clipMap.size()) continue;
    if (i == kClipIdle) tmp_ = sk.rest;   // procedural idle (the stock idle clip steps in place)
    else samplePose(sk, (*clips_)[i], m.clipMap[i], a.t[i], m.animRootScale, tmp_);
    float k = a.w[i] / (acc + a.w[i]);
    acc += a.w[i];
    for (size_t b = 0; b < nb; ++b) {
      if (first) { pose_[b] = tmp_[b]; continue; }
      pose_[b].t = lerp(pose_[b].t, tmp_[b].t, k);
      pose_[b].r = nlerp(pose_[b].r, tmp_[b].r, k);
      pose_[b].s = lerp(pose_[b].s, tmp_[b].s, k);
    }
    first = false;
  }
  if (first) pose_ = sk.rest;
  // action clips over locomotion (upper-body mask keeps the legs walking)
  auto upperBone = [&](size_t b) {
    const std::string& n = sk.names[b];
    return n.find("Leg") == std::string::npos && n.find("Foot") == std::string::npos && n.find("Toe") == std::string::npos &&
           n != "Hips";
  };
  auto applyAction = [&](int act, float t, float w, bool upper) {
    if (act < 0 || w < 0.01f || !hasAction(act)) return;
    int ci = kClipCount + act;
    if (ci >= (int)m.clipMap.size()) return;
    const AnimClip& c = (*clips_)[ci];
    samplePose(sk, c, m.clipMap[ci], std::min(t, c.duration - 1e-3f), m.animRootScale, tmp_);
    for (size_t b = 0; b < nb; ++b) {
      if (upper && !upperBone(b)) continue;
      float k = w;
      pose_[b].r = nlerp(pose_[b].r, tmp_[b].r, k);
      if (!upper && sk.parent[b] < 0) pose_[b].t = lerp(pose_[b].t, tmp_[b].t, k);
    }
  };
  applyAction(a.prevAction, a.prevT, a.prevW, a.prevUpper);
  applyAction(a.action, a.actT, a.actW, a.actUpper);

  // model-space procedural rotations about each joint (independent of the rig's local axes)
  std::vector<Quat> extra(nb, Quat{});
  std::vector<bool> has(nb, false);
  auto add = [&](const char* bone, Vec3 axis, float ang) {
    int b = sk.find(bone);
    if (b < 0 || std::fabs(ang) < 1e-4f) return;
    extra[b] = Quat::axisAngle(axis, ang) * extra[b];
    has[b] = true;
  };
  const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
  float gc = a.gestureClock;
  // idle layer: relaxed arms, breathing, slow weight shift
  float wi = a.w[kClipIdle];
  if (wi > 0.01f) {
    float br = std::sin(gc * 1.7f * a.rateScale);
    float sway = std::sin(gc * 0.45f * a.rateScale + 1.3f);
    add("LeftArm", Z, wi * (m.armDown[0] + 0.03f * br));
    add("RightArm", Z, wi * (m.armDown[1] - 0.03f * br));
    add("LeftForeArm", X, wi * 0.18f);
    add("RightForeArm", X, wi * 0.18f);
    add("Spine01", X, wi * 0.015f * br);
    add("Hips", Z, wi * 0.025f * sway);
    add("Spine02", Z, wi * -0.03f * sway);
    add("Head", Y, wi * 0.08f * std::sin(gc * 0.23f + 2.0f));
  }
  // talking: small head nods and an open-hand forearm gesture
  if (a.talk > 0.01f) {
    add("Head", X, a.talk * 0.06f * std::sin(gc * 3.1f));
    add("RightArm", X, a.talk * (0.35f + 0.12f * std::sin(gc * 2.3f)));
    add("RightForeArm", X, a.talk * (0.55f + 0.2f * std::sin(gc * 3.7f + 1.0f)));
    add("LeftForeArm", X, a.talk * 0.18f * (0.5f + 0.5f * std::sin(gc * 1.7f)));
  }
  // reach / interact / pay at the counter: right arm forward
  if (a.reach > 0.01f) {
    add("RightArm", X, a.reach * 1.05f);
    add("RightForeArm", X, a.reach * 0.35f);
    add("Spine01", X, a.reach * 0.10f);
  }
  // refuel: hold the nozzle out at hip height, slight lean
  if (a.refuel > 0.01f) {
    add("RightArm", X, a.refuel * 0.85f);
    add("RightArm", Z, a.refuel * -0.15f);
    add("RightForeArm", X, a.refuel * 0.75f);
    add("Spine01", Z, a.refuel * 0.05f);
  }
  // crouch to get into / out of a car
  if (a.crouch > 0.01f) {
    add("Spine01", X, a.crouch * 0.45f);
    add("Head", X, a.crouch * -0.2f);
    add("LeftUpLeg", X, a.crouch * 0.5f);
    add("RightUpLeg", X, a.crouch * 0.5f);
    add("LeftLeg", X, a.crouch * -0.8f);
    add("RightLeg", X, a.crouch * -0.8f);
  }
  if (a.wave > 0.01f) {
    add("RightArm", Z, a.wave * 1.9f);
    add("RightForeArm", Z, a.wave * (0.5f + 0.3f * std::sin(gc * 9.0f)));
  }
  if (std::fabs(a.lean) > 0.002f) add("Spine", Z, -a.lean);
  if (std::fabs(a.headYaw) > 0.002f) { add("Head", Y, a.headYaw * 0.7f); add("neck", Y, a.headYaw * 0.3f); }

  global_.resize(nb);
  for (size_t b = 0; b < nb; ++b) {
    Mat4 l = pose_[b].matrix();
    global_[b] = sk.parent[b] >= 0 ? global_[sk.parent[b]] * l : m.rootFix * l;
    if (has[b]) {
      Vec3 p = global_[b].transformPoint({0, 0, 0});
      global_[b] = Mat4::translation(p) * quatMatrix(extra[b]) * Mat4::translation(-p) * global_[b];
    }
  }
  // aiming: rotate the arm chain so arm and forearm point along the aim direction (second pass)
  if (a.aim > 0.01f) {
    auto P = [&](const char* n) { int b = sk.find(n); return b >= 0 ? global_[b].transformPoint({0, 0, 0}) : Vec3{}; };
    Vec3 aimDir = Vec3{0, std::sin(a.aimPitch), -std::cos(a.aimPitch)}.normalized();
    auto rotBetween = [](Vec3 from, Vec3 to, float w) {
      from = from.normalized(); to = to.normalized();
      Vec3 ax = from.cross(to);
      float s2 = ax.length(), c2 = from.dot(to);
      if (s2 < 1e-5f) return Quat{};
      return Quat::axisAngle(ax / s2, std::atan2(s2, c2) * w);
    };
    Vec3 rArm = P("RightArm"), rFore = P("RightForeArm"), rHand = P("RightHand");
    float lift = a.recoil * 0.35f;
    Vec3 dirR = (aimDir + Vec3{0, lift, 0}).normalized();
    Quat qa = rotBetween(rHand - rArm, dirR, a.aim);
    int ba = sk.find("RightArm"), bf = sk.find("RightForeArm");
    if (ba >= 0) { extra[ba] = qa * extra[ba]; has[ba] = true; }
    // straighten the elbow: forearm follows the upper arm direction
    if (bf >= 0) {
      Vec3 upperDir = rFore - rArm, foreDir = rHand - rFore;
      Quat qf = rotBetween(foreDir, upperDir, a.aim * 0.85f);
      extra[bf] = qf * extra[bf]; has[bf] = true;
    }
    if (a.twoHanded) {
      Vec3 lArm = P("LeftArm"), lHand = P("LeftHand"), lFore = P("LeftForeArm");
      // support hand reaches to a point under the weapon, ahead of the right hand
      Vec3 target = rArm + dirR * 0.62f + Vec3{-0.02f, -0.05f, 0};
      Quat ql = rotBetween(lHand - lArm, target - lArm, a.aim);
      int bl = sk.find("LeftArm"), blf = sk.find("LeftForeArm");
      if (bl >= 0) { extra[bl] = ql * extra[bl]; has[bl] = true; }
      if (blf >= 0) { Quat q2 = rotBetween(lHand - lFore, lFore - lArm, a.aim * 0.5f); extra[blf] = q2 * extra[blf]; has[blf] = true; }
    }
    int bs = sk.find("Spine01");
    if (bs >= 0 && a.recoil > 0.01f) { extra[bs] = Quat::axisAngle({1, 0, 0}, a.recoil * 0.08f) * extra[bs]; has[bs] = true; }
    for (size_t b = 0; b < nb; ++b) {
      Mat4 l = pose_[b].matrix();
      global_[b] = sk.parent[b] >= 0 ? global_[sk.parent[b]] * l : m.rootFix * l;
      if (has[b]) {
        Vec3 p = global_[b].transformPoint({0, 0, 0});
        global_[b] = Mat4::translation(p) * quatMatrix(extra[b]) * Mat4::translation(-p) * global_[b];
      }
    }
  }
  {
    int bh = sk.find("RightHand"), bf = sk.find("RightForeArm");
    a.handValid = bh >= 0 && bf >= 0;
    if (a.handValid) {
      Vec3 hp = global_[bh].transformPoint({0, 0, 0}), fp = global_[bf].transformPoint({0, 0, 0});
      a.handPos = hp;
      a.handDir = (hp - fp).normalized();
      // the palm side: the hand bone's local axis that is most perpendicular to the forearm
      Vec3 ax = Vec3{global_[bh].m[0], global_[bh].m[1], global_[bh].m[2]}.normalized();
      a.handSide = (ax - a.handDir * ax.dot(a.handDir)).normalized();
    }
  }
  for (size_t b = 0; b < nb && b < (size_t)gfx::kMaxBones; ++b) a.palette[b] = global_[b] * sk.invBind[b];
  a.valid = true;
}

// ---------------------------------------------------------------------------------------------------------------------
namespace {
void packVertex(gfx::ModelVertex& o, Vec3 p, Vec3 n, Vec3 t, float u, float v) {
  o.p[0] = p.x; o.p[1] = p.y; o.p[2] = p.z;
  auto sn = [](float x) { return (int8_t)std::lround(clamp(x, -1.0f, 1.0f) * 127.0f); };
  o.n[0] = sn(n.x); o.n[1] = sn(n.y); o.n[2] = sn(n.z); o.n[3] = 0;
  o.t[0] = sn(t.x); o.t[1] = sn(t.y); o.t[2] = sn(t.z); o.t[3] = 127;
  o.uv[0] = u; o.uv[1] = v;
  std::memset(o.j, 0, 4);
  o.w[0] = 255; o.w[1] = o.w[2] = o.w[3] = 0;
}
}  // namespace

void buildWheelMesh(std::vector<gfx::ModelVertex>& v, std::vector<uint32_t>& idx) {
  // profile in (x across the tyre, r radius): tread, rounded sidewalls, then a dished rim face on the outer (+X) side
  const int seg = 28;
  struct P { float x, r, u; Vec3 n; };
  const P prof[] = {
      {-0.50f, 0.66f, 0.02f, {-1, 0, 0}}, {-0.50f, 0.86f, 0.10f, {-0.8f, 0.6f, 0}}, {-0.40f, 1.0f, 0.18f, {0, 1, 0}},
      {0.40f, 1.0f, 0.38f, {0, 1, 0}},     {0.50f, 0.86f, 0.44f, {0.8f, 0.6f, 0}},  {0.50f, 0.66f, 0.48f, {1, 0, 0}},
  };
  const int np = sizeof(prof) / sizeof(prof[0]);
  for (int i = 0; i <= seg; ++i) {
    float a = (float)i / seg * kTau, c = std::cos(a), s = std::sin(a);
    for (int k = 0; k < np; ++k) {
      Vec3 p{prof[k].x, prof[k].r * c, prof[k].r * s};
      Vec3 n = Vec3{prof[k].n.x, prof[k].n.y * c, prof[k].n.y * s}.normalized();
      gfx::ModelVertex o;
      packVertex(o, p, n, {0, -s, c}, prof[k].u, (float)i / seg * 3.0f);
      v.push_back(o);
    }
  }
  for (int i = 0; i < seg; ++i)
    for (int k = 0; k + 1 < np; ++k) {
      uint32_t a0 = i * np + k, a1 = a0 + 1, b0 = (i + 1) * np + k, b1 = b0 + 1;
      idx.insert(idx.end(), {a0, b0, a1, a1, b0, b1});
    }
  // rim discs on both faces (spokes painted in the texture)
  for (int side = 0; side < 2; ++side) {
    float x = side ? 0.42f : -0.42f, nx = side ? 1.0f : -1.0f;
    uint32_t centre = (uint32_t)v.size();
    gfx::ModelVertex o;
    packVertex(o, {x - nx * 0.10f, 0, 0}, {nx, 0, 0}, {0, 0, 1}, 0.75f, 0.5f);
    v.push_back(o);
    for (int i = 0; i <= seg; ++i) {
      float a = (float)i / seg * kTau, c = std::cos(a), s = std::sin(a);
      packVertex(o, {x, 0.68f * c, 0.68f * s}, {nx, 0, 0}, {0, 0, 1}, 0.75f + 0.24f * c, 0.5f + 0.48f * s);
      v.push_back(o);
    }
    for (int i = 0; i < seg; ++i) {
      uint32_t a = centre + 1 + i, b = a + 1;
      if (side) idx.insert(idx.end(), {centre, a, b});
      else idx.insert(idx.end(), {centre, b, a});
    }
  }
}

void buildWheelTextures(std::vector<uint8_t>& albedo, std::vector<uint8_t>& orm, uint32_t& w, uint32_t& h) {
  w = 256; h = 128;
  albedo.assign((size_t)w * h * 4, 255);
  orm.assign((size_t)w * h * 4, 255);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      size_t i = ((size_t)y * w + x) * 4;
      float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
      float r, g, b, rough, metal;
      if (u < 0.5f) {
        // rubber with a tread pattern across the band
        float tread = (u > 0.18f && u < 0.38f) ? (std::fmod(v * 24.0f + (u > 0.28f ? 0.5f : 0.0f), 1.0f) < 0.35f ? 0.55f : 1.0f) : 1.0f;
        float c = 0.045f * tread;
        r = g = b = c;
        rough = 0.9f; metal = 0;
      } else {
        // alloy rim: 5 spokes, dark gaps, lug centre
        float dx = (u - 0.75f) / 0.24f, dy = (v - 0.5f) / 0.48f;
        float rr = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx);
        float spoke = std::cos(ang * 5.0f);
        bool gap = rr > 0.25f && rr < 0.86f && spoke < 0.25f;
        bool lip = rr > 0.86f;
        float c = gap ? 0.03f : (lip ? 0.62f : 0.55f + 0.1f * spoke);
        if (rr < 0.12f) c = 0.35f;
        r = g = b = c;
        rough = gap ? 0.8f : 0.32f;
        metal = gap ? 0.0f : 1.0f;
      }
      auto srgb = [](float l) { return (uint8_t)std::lround(clamp(std::pow(l, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f); };
      albedo[i] = srgb(r); albedo[i + 1] = srgb(g); albedo[i + 2] = srgb(b); albedo[i + 3] = 255;
      orm[i] = 255; orm[i + 1] = (uint8_t)(rough * 255); orm[i + 2] = (uint8_t)(metal * 255); orm[i + 3] = 255;
    }
}

}  // namespace gtabr
