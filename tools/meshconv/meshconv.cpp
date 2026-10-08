// meshconv: GLB (Tripo / Meshy output) -> .gmesh (normalised, LODs, vehicle metadata) and .ganim (sampled clips).
//
//   meshconv mesh in.glb out_prefix --kind char|car|prop [--height H | --length L] [--yaw DEG]
//   meshconv anim in.glb out.ganim [--yaw DEG] [--scale-from model.gmesh] [--name NAME]
//
// Textures referenced by the material are written next to the output as <out_prefix>_<slot>.<png|jpg>
// (tools/build_assets.py turns them into ASTC/RGBA GTEX files).
#define CGLTF_IMPLEMENTATION
#include "../../third_party/cgltf/cgltf.h"
#include "../../third_party/meshoptimizer/meshoptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/math.h"
#include "gfx/gmesh.h"

using namespace gtabr;
namespace gm = gtabr::gmesh;

namespace {

struct Quat { float x = 0, y = 0, z = 0, w = 1; };
Quat qmul(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Vec3 qrot(Quat q, Vec3 v) {
  Vec3 u{q.x, q.y, q.z};
  Vec3 t = u.cross(v) * 2.0f;
  return v + t * q.w + u.cross(t);
}
Quat qslerp(Quat a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
  Quat r;
  if (d > 0.9995f) r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
  else {
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    r = {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
  }
  float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
  return {r.x / l, r.y / l, r.z / l, r.w / l};
}
Quat yawQuat(float rad) { return {0, std::sin(rad * 0.5f), 0, std::cos(rad * 0.5f)}; }

// Similarity transform used to normalise the asset: p' = R(s * p) + t
struct Correction {
  Quat r;
  float s = 1;
  Vec3 t;
  Vec3 apply(Vec3 p) const { return qrot(r, p * s) + t; }
  Vec3 dir(Vec3 d) const { return qrot(r, d); }
};

struct V {
  Vec3 p, n;
  Vec4 t{1, 0, 0, 1};
  Vec2 uv;
  uint8_t j[4] = {0, 0, 0, 0};
  float w[4] = {1, 0, 0, 0};
};

std::string g_err;

void readFloats(const cgltf_accessor* a, size_t comps, std::vector<float>& out) {
  out.resize(a->count * comps);
  for (size_t i = 0; i < a->count; ++i) cgltf_accessor_read_float(a, i, &out[i * comps], comps);
}

bool writeImage(cgltf_texture* tex, const std::string& outBase, std::string& outName) {
  if (!tex || !tex->image) return false;
  cgltf_image* img = tex->image;
  const char* ext = (img->mime_type && strstr(img->mime_type, "jpeg")) ? ".jpg" : ".png";
  std::string path = outBase + ext;
  if (img->buffer_view) {
    const uint8_t* data = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
    // sniff jpeg magic if mime type missing
    if (!img->mime_type && img->buffer_view->size > 3 && data[0] == 0xFF && data[1] == 0xD8) path = outBase + ".jpg";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(data, 1, img->buffer_view->size, f);
    fclose(f);
  } else return false;
  size_t slash = path.find_last_of('/');
  outName = slash == std::string::npos ? path : path.substr(slash + 1);
  return true;
}

void nodeLocal(const cgltf_node* n, Vec3& t, Quat& r, Vec3& s) {
  t = {0, 0, 0}; r = {}; s = {1, 1, 1};
  if (n->has_translation) t = {n->translation[0], n->translation[1], n->translation[2]};
  if (n->has_rotation) r = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
  if (n->has_scale) s = {n->scale[0], n->scale[1], n->scale[2]};
}

// ------------------------------------------------------------------------------------------------ geometry
struct Geometry {
  std::vector<V> verts;
  std::vector<uint32_t> idx;
  bool skinned = false;
  const cgltf_skin* skin = nullptr;
  const cgltf_material* material = nullptr;
  float meshWorld[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

bool loadGeometry(cgltf_data* d, Geometry& g) {
  for (size_t ni = 0; ni < d->nodes_count; ++ni) {
    cgltf_node* node = &d->nodes[ni];
    if (!node->mesh) continue;
    float world[16];
    cgltf_node_transform_world(node, world);
    bool skinned = node->skin != nullptr;
    if (skinned) { g.skinned = true; g.skin = node->skin; memcpy(g.meshWorld, world, 64); }
    for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
      cgltf_primitive& prim = node->mesh->primitives[pi];
      if (prim.type != cgltf_primitive_type_triangles) continue;
      if (!g.material) g.material = prim.material;
      std::vector<float> P, N, T, UV, W;
      std::vector<float> J;
      for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
        cgltf_attribute& at = prim.attributes[ai];
        switch (at.type) {
          case cgltf_attribute_type_position: readFloats(at.data, 3, P); break;
          case cgltf_attribute_type_normal: readFloats(at.data, 3, N); break;
          case cgltf_attribute_type_tangent: readFloats(at.data, 4, T); break;
          case cgltf_attribute_type_texcoord: if (at.index == 0) readFloats(at.data, 2, UV); break;
          case cgltf_attribute_type_joints: if (at.index == 0) readFloats(at.data, 4, J); break;
          case cgltf_attribute_type_weights: if (at.index == 0) readFloats(at.data, 4, W); break;
          default: break;
        }
      }
      size_t vc = P.size() / 3;
      uint32_t base = (uint32_t)g.verts.size();
      for (size_t i = 0; i < vc; ++i) {
        V v;
        Vec3 p{P[i * 3], P[i * 3 + 1], P[i * 3 + 2]};
        Vec3 n = N.empty() ? Vec3{0, 1, 0} : Vec3{N[i * 3], N[i * 3 + 1], N[i * 3 + 2]};
        if (!skinned) {
          // bake the node transform for static meshes
          Vec3 q{world[0] * p.x + world[4] * p.y + world[8] * p.z + world[12], world[1] * p.x + world[5] * p.y + world[9] * p.z + world[13],
                 world[2] * p.x + world[6] * p.y + world[10] * p.z + world[14]};
          Vec3 nn{world[0] * n.x + world[4] * n.y + world[8] * n.z, world[1] * n.x + world[5] * n.y + world[9] * n.z,
                  world[2] * n.x + world[6] * n.y + world[10] * n.z};
          p = q; n = nn.normalized();
        }
        v.p = p; v.n = n;
        if (!UV.empty()) v.uv = {UV[i * 2], UV[i * 2 + 1]};
        if (!T.empty()) v.t = {T[i * 4], T[i * 4 + 1], T[i * 4 + 2], T[i * 4 + 3]};
        if (!J.empty()) for (int k = 0; k < 4; ++k) v.j[k] = (uint8_t)J[i * 4 + k];
        if (!W.empty()) for (int k = 0; k < 4; ++k) v.w[k] = W[i * 4 + k];
        g.verts.push_back(v);
      }
      if (prim.indices) {
        for (size_t i = 0; i < prim.indices->count; ++i) g.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(prim.indices, i));
      } else {
        for (size_t i = 0; i < vc; ++i) g.idx.push_back(base + (uint32_t)i);
      }
    }
  }
  return !g.verts.empty();
}

void computeTangents(std::vector<V>& vs, const std::vector<uint32_t>& idx) {
  std::vector<Vec3> tan(vs.size()), bit(vs.size());
  for (size_t i = 0; i + 2 < idx.size(); i += 3) {
    V &a = vs[idx[i]], &b = vs[idx[i + 1]], &c = vs[idx[i + 2]];
    Vec3 e1 = b.p - a.p, e2 = c.p - a.p;
    Vec2 d1 = b.uv - a.uv, d2 = c.uv - a.uv;
    float r = d1.x * d2.y - d2.x * d1.y;
    if (std::fabs(r) < 1e-12f) continue;
    r = 1.0f / r;
    Vec3 t = (e1 * d2.y - e2 * d1.y) * r, bt = (e2 * d1.x - e1 * d2.x) * r;
    for (int k = 0; k < 3; ++k) { tan[idx[i + k]] += t; bit[idx[i + k]] += bt; }
  }
  for (size_t i = 0; i < vs.size(); ++i) {
    Vec3 n = vs[i].n;
    Vec3 t = (tan[i] - n * n.dot(tan[i])).normalized();
    if (t.lengthSq() < 0.5f) t = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0}.cross(n).normalized() : Vec3{1, 0, 0};
    float w = n.cross(t).dot(bit[i]) < 0 ? -1.0f : 1.0f;
    vs[i].t = {t.x, t.y, t.z, w};
  }
}

int8_t snorm(float v) { return (int8_t)std::lround(clamp(v, -1.0f, 1.0f) * 127.0f); }

gm::Vertex pack(const V& v, bool skinned) {
  gm::Vertex o{};
  o.p[0] = v.p.x; o.p[1] = v.p.y; o.p[2] = v.p.z;
  o.n[0] = snorm(v.n.x); o.n[1] = snorm(v.n.y); o.n[2] = snorm(v.n.z); o.n[3] = 0;
  o.t[0] = snorm(v.t.x); o.t[1] = snorm(v.t.y); o.t[2] = snorm(v.t.z); o.t[3] = v.t.w < 0 ? -127 : 127;
  o.uv[0] = v.uv.x; o.uv[1] = v.uv.y;
  if (skinned) {
    float sum = v.w[0] + v.w[1] + v.w[2] + v.w[3];
    if (sum <= 0) sum = 1;
    int acc = 0;
    for (int k = 0; k < 4; ++k) {
      o.j[k] = v.j[k];
      o.w[k] = (uint8_t)std::lround(v.w[k] / sum * 255.0f);
      acc += o.w[k];
    }
    o.w[0] = (uint8_t)clamp(o.w[0] + (255 - acc), 0, 255);
  }
  return o;
}

// Ray vs mesh, returns nearest hit distance along dir (or -1)
float raycastMesh(const std::vector<V>& vs, const std::vector<uint32_t>& idx, Vec3 o, Vec3 d) {
  float best = 1e30f;
  for (size_t i = 0; i + 2 < idx.size(); i += 3) {
    Vec3 a = vs[idx[i]].p, b = vs[idx[i + 1]].p, c = vs[idx[i + 2]].p;
    Vec3 e1 = b - a, e2 = c - a, pv = d.cross(e2);
    float det = e1.dot(pv);
    if (std::fabs(det) < 1e-10f) continue;
    float inv = 1.0f / det;
    Vec3 tv = o - a;
    float u = tv.dot(pv) * inv;
    if (u < 0 || u > 1) continue;
    Vec3 qv = tv.cross(e1);
    float v = d.dot(qv) * inv;
    if (v < 0 || u + v > 1) continue;
    float t = e2.dot(qv) * inv;
    if (t > 0 && t < best) best = t;
  }
  return best < 1e29f ? best : -1.0f;
}


// Skinned meshes: bring bind-space vertices into world space (mesh node transform, e.g. a 0.01 armature scale) and
// compensate the inverse bind matrices so skinning stays identical.
void bakeMeshWorld(Geometry& g, std::vector<gm::Bone>* bones) {
  if (!g.skinned) return;
  Mat4 M; memcpy(M.m, g.meshWorld, 64);
  for (auto& v : g.verts) {
    Vec3 p = M.transformPoint(v.p);
    Vec3 n{M.m[0] * v.n.x + M.m[4] * v.n.y + M.m[8] * v.n.z, M.m[1] * v.n.x + M.m[5] * v.n.y + M.m[9] * v.n.z, M.m[2] * v.n.x + M.m[6] * v.n.y + M.m[10] * v.n.z};
    v.p = p; v.n = n.normalized();
  }
  if (!bones) return;
  // inverse of a similarity (uniform scale) matrix
  float sc = std::sqrt(M.m[0] * M.m[0] + M.m[1] * M.m[1] + M.m[2] * M.m[2]);
  Mat4 Mi;
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) Mi.at(r, c) = M.at(c, r) / (sc * sc);
  Vec3 t{M.at(0, 3), M.at(1, 3), M.at(2, 3)};
  for (int r = 0; r < 3; ++r) Mi.at(r, 3) = -(Mi.at(r, 0) * t.x + Mi.at(r, 1) * t.y + Mi.at(r, 2) * t.z);
  for (auto& b : *bones) { Mat4 ib; memcpy(ib.m, b.invBind, 64); Mat4 n = ib * Mi; memcpy(b.invBind, n.m, 64); }
}

Correction computeCorrection(const Geometry& g, float yawDeg, float height, float length) {
  Correction C;
  C.r = yawQuat(yawDeg * kDeg2Rad);
  AABB rot;
  for (auto& v : g.verts) rot.expand(qrot(C.r, v.p));
  Vec3 e = rot.mx - rot.mn;
  if (height > 0) C.s = height / std::max(1e-4f, e.y);
  else if (length > 0) C.s = length / std::max(1e-4f, e.z);
  AABB tmp;
  for (auto& v : g.verts) tmp.expand(C.apply(v.p));
  C.t = {-(tmp.mn.x + tmp.mx.x) * 0.5f, -tmp.mn.y, -(tmp.mn.z + tmp.mx.z) * 0.5f};
  return C;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ mesh mode
static int runMesh(int argc, char** argv) {
  std::string in = argv[2], out = argv[3];
  std::string kind = "prop";
  float height = 0, length = 0, yawDeg = 0;
  float wz[2] = {0, 0}, wy = 0, wr = 0, wtrack = 0;
  std::string skinFrom;
  bool wheelsGiven = false;
  for (int i = 4; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--wheels" && i + 1 < argc) {
      wheelsGiven = sscanf(argv[++i], "%f,%f,%f,%f,%f", &wz[0], &wz[1], &wy, &wr, &wtrack) == 5;
      continue;
    }
    if (a == "--kind" && i + 1 < argc) kind = argv[++i];
    else if (a == "--height" && i + 1 < argc) height = (float)atof(argv[++i]);
    else if (a == "--length" && i + 1 < argc) length = (float)atof(argv[++i]);
    else if (a == "--yaw" && i + 1 < argc) yawDeg = (float)atof(argv[++i]);
    else if (a == "--skin-from" && i + 1 < argc) skinFrom = argv[++i];
  }
  cgltf_options opt{};
  cgltf_data* d = nullptr;
  if (cgltf_parse_file(&opt, in.c_str(), &d) != cgltf_result_success || cgltf_load_buffers(&opt, d, in.c_str()) != cgltf_result_success) {
    fprintf(stderr, "cannot read %s\n", in.c_str());
    return 1;
  }
  Geometry g;
  if (!loadGeometry(d, g)) { fprintf(stderr, "no geometry\n"); return 1; }
  bool skinned = g.skinned && kind == "char";
  // (a static character mesh becomes skinned through --skin-from)

  // --- bones (rest pose) for skinned meshes
  std::vector<gm::Bone> bones;
  std::vector<const cgltf_node*> jointNodes;
  if (skinned) {
    const cgltf_skin* sk = g.skin;
    for (size_t i = 0; i < sk->joints_count; ++i) jointNodes.push_back(sk->joints[i]);
    for (size_t i = 0; i < sk->joints_count; ++i) {
      gm::Bone b{};
      snprintf(b.name, gm::kNameLen, "%s", sk->joints[i]->name ? sk->joints[i]->name : ("bone" + std::to_string(i)).c_str());
      b.parent = -1;
      for (size_t k = 0; k < jointNodes.size(); ++k)
        if (jointNodes[k] == sk->joints[i]->parent) b.parent = (int)k;
      if (sk->inverse_bind_matrices) cgltf_accessor_read_float(sk->inverse_bind_matrices, i, b.invBind, 16);
      else { Mat4 id; memcpy(b.invBind, id.m, 64); }
      Vec3 t, s; Quat r;
      nodeLocal(sk->joints[i], t, r, s);
      if (b.parent < 0) {
        // fold any non-joint ancestors into the root's rest transform
        float w[16];
        if (sk->joints[i]->parent) {
          cgltf_node_transform_world(sk->joints[i]->parent, w);
          Vec3 pt{w[12], w[13], w[14]};
          float sx = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
          t = pt + t * sx;   // assumes no rotation on ancestors (true for Meshy/Tripo exports)
          s = s * sx;
        }
      }
      b.t[0] = t.x; b.t[1] = t.y; b.t[2] = t.z; b.r[0] = r.x; b.r[1] = r.y; b.r[2] = r.z; b.r[3] = r.w; b.s[0] = s.x; b.s[1] = s.y; b.s[2] = s.z;
      bones.push_back(b);
    }
  }

  // --- normalisation: rotate by yaw, scale to the target size, centre on XZ, feet on the ground
  bakeMeshWorld(g, skinned ? &bones : nullptr);
  Correction C = computeCorrection(g, yawDeg, height, length);
  for (auto& v : g.verts) { v.p = C.apply(v.p); v.n = C.dir(v.n).normalized(); Vec3 t3 = C.dir({v.t.x, v.t.y, v.t.z}); v.t = {t3.x, t3.y, t3.z, v.t.w}; }
  if (skinned) {
    // vertices moved by C: joint world must move by C as well -> premultiply roots, and invBind' = invBind * C^-1
    Mat4 Cm, Ci;
    {
      // build C as a matrix
      Vec3 ex = qrot(C.r, {C.s, 0, 0}), ey = qrot(C.r, {0, C.s, 0}), ez = qrot(C.r, {0, 0, C.s});
      Cm.at(0, 0) = ex.x; Cm.at(1, 0) = ex.y; Cm.at(2, 0) = ex.z;
      Cm.at(0, 1) = ey.x; Cm.at(1, 1) = ey.y; Cm.at(2, 1) = ey.z;
      Cm.at(0, 2) = ez.x; Cm.at(1, 2) = ez.y; Cm.at(2, 2) = ez.z;
      Cm.at(0, 3) = C.t.x; Cm.at(1, 3) = C.t.y; Cm.at(2, 3) = C.t.z;
      Quat ri{-C.r.x, -C.r.y, -C.r.z, C.r.w};
      float is = 1.0f / C.s;
      Vec3 ix = qrot(ri, {is, 0, 0}), iy = qrot(ri, {0, is, 0}), iz = qrot(ri, {0, 0, is});
      Vec3 it = qrot(ri, C.t * -1.0f) * is;
      Ci.at(0, 0) = ix.x; Ci.at(1, 0) = ix.y; Ci.at(2, 0) = ix.z;
      Ci.at(0, 1) = iy.x; Ci.at(1, 1) = iy.y; Ci.at(2, 1) = iy.z;
      Ci.at(0, 2) = iz.x; Ci.at(1, 2) = iz.y; Ci.at(2, 2) = iz.z;
      Ci.at(0, 3) = it.x; Ci.at(1, 3) = it.y; Ci.at(2, 3) = it.z;
    }
    for (auto& b : bones) {
      Mat4 ib;
      memcpy(ib.m, b.invBind, 64);
      Mat4 n = ib * Ci;
      memcpy(b.invBind, n.m, 64);
      if (b.parent < 0) {
        Vec3 t{b.t[0], b.t[1], b.t[2]};
        Vec3 nt = C.apply(t);
        Quat nr = qmul(C.r, Quat{b.r[0], b.r[1], b.r[2], b.r[3]});
        b.t[0] = nt.x; b.t[1] = nt.y; b.t[2] = nt.z;
        b.r[0] = nr.x; b.r[1] = nr.y; b.r[2] = nr.z; b.r[3] = nr.w;
        b.s[0] *= C.s; b.s[1] *= C.s; b.s[2] *= C.s;
      }
    }
    (void)Cm;
  }

  // --- weight transfer from an already rigged reference character (same skeleton for every NPC)
  float animRootScale = 1.0f;
  if (!skinFrom.empty()) {
    FILE* rf = fopen(skinFrom.c_str(), "rb");
    if (!rf) { fprintf(stderr, "cannot open %s\n", skinFrom.c_str()); return 1; }
    gm::Header rh;
    fread(&rh, sizeof(rh), 1, rf);
    std::vector<gm::Bone> rb(rh.boneCount);
    fread(rb.data(), sizeof(gm::Bone), rb.size(), rf);
    gm::LodHeader rl;
    fread(&rl, sizeof(rl), 1, rf);
    std::vector<gm::Vertex> rv(rl.vertexCount);
    fread(rv.data(), sizeof(gm::Vertex), rv.size(), rf);
    fclose(rf);
    AABB nb;
    for (auto& v : g.verts) nb.expand(v.p);
    Vec3 rmn{rh.bmin[0], rh.bmin[1], rh.bmin[2]}, rmx{rh.bmax[0], rh.bmax[1], rh.bmax[2]};
    float u = (nb.mx.y - nb.mn.y) / std::max(1e-4f, rmx.y - rmn.y);
    animRootScale = u;
    // map each NPC vertex into the reference body's box (per axis) and blend the weights of the 6 nearest vertices
    Vec3 nsz = nb.mx - nb.mn, rsz = rmx - rmn;
    const float cell = 0.04f;
    std::map<long long, std::vector<int>> gridm;
    auto key = [&](int x, int y, int z) { return ((long long)(x + 512) << 40) | ((long long)(y + 512) << 20) | (long long)(z + 512); };
    for (size_t i = 0; i < rv.size(); ++i) {
      Vec3 p{rv[i].p[0], rv[i].p[1], rv[i].p[2]};
      gridm[key((int)std::floor(p.x / cell), (int)std::floor(p.y / cell), (int)std::floor(p.z / cell))].push_back((int)i);
    }
    for (auto& v : g.verts) {
      Vec3 q{rmn.x + (v.p.x - nb.mn.x) / std::max(1e-4f, nsz.x) * rsz.x, rmn.y + (v.p.y - nb.mn.y) / std::max(1e-4f, nsz.y) * rsz.y,
             rmn.z + (v.p.z - nb.mn.z) / std::max(1e-4f, nsz.z) * rsz.z};
      int cx = (int)std::floor(q.x / cell), cy = (int)std::floor(q.y / cell), cz = (int)std::floor(q.z / cell);
      std::vector<std::pair<float, int>> best;
      for (int r = 1; r <= 6 && best.size() < 6; ++r) {
        best.clear();
        for (int dz = -r; dz <= r; ++dz)
          for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
              auto it = gridm.find(key(cx + dx, cy + dy, cz + dz));
              if (it == gridm.end()) continue;
              for (int id : it->second) {
                Vec3 p{rv[id].p[0], rv[id].p[1], rv[id].p[2]};
                best.push_back({(p - q).lengthSq(), id});
              }
            }
      }
      std::sort(best.begin(), best.end());
      if (best.size() > 6) best.resize(6);
      float acc[256] = {};
      for (auto& [dd, id] : best) {
        float wgt = 1.0f / (std::sqrt(dd) + 0.01f);
        for (int k = 0; k < 4; ++k) acc[rv[id].j[k]] += wgt * rv[id].w[k] / 255.0f;
      }
      // keep the 4 strongest joints
      for (int k = 0; k < 4; ++k) {
        int bj = 0; float bw = -1;
        for (int j = 0; j < (int)rb.size(); ++j) if (acc[j] > bw) { bw = acc[j]; bj = j; }
        v.j[k] = (uint8_t)bj; v.w[k] = std::max(0.0f, bw); acc[bj] = -1;
      }
    }
    // reference skeleton scaled to this character's height (uniform scale about the origin)
    bones = rb;
    for (auto& b : bones) {
      Mat4 ib; memcpy(ib.m, b.invBind, 64);
      Mat4 S; S.at(0, 0) = S.at(1, 1) = S.at(2, 2) = 1.0f / u;
      Mat4 n = ib * S;
      memcpy(b.invBind, n.m, 64);
      if (b.parent < 0) { for (int k = 0; k < 3; ++k) { b.t[k] *= u; b.s[k] *= u; } }
    }
    skinned = true;
    printf("weights transferred from %s (scale %.3f)\n", skinFrom.c_str(), u);
  }

  // --- textures
  gm::Header h{};
  h.magic = gm::kMagicMesh;
  h.version = 2;
  h.flags = (skinned ? gm::kSkinned : 0) | (kind == "car" ? gm::kVehicle : 0);
  if (g.material) {
    const cgltf_material* m = g.material;
    std::string name;
    if (m->has_pbr_metallic_roughness && writeImage(m->pbr_metallic_roughness.base_color_texture.texture, out + "_albedo", name))
      snprintf(h.textures[0], gm::kNameLen, "%s", name.c_str());
    if (writeImage(m->normal_texture.texture, out + "_normal", name)) snprintf(h.textures[1], gm::kNameLen, "%s", name.c_str());
    if (m->has_pbr_metallic_roughness && writeImage(m->pbr_metallic_roughness.metallic_roughness_texture.texture, out + "_mr", name))
      snprintf(h.textures[2], gm::kNameLen, "%s", name.c_str());
    if (writeImage(m->occlusion_texture.texture, out + "_ao", name)) {}
    if (writeImage(m->emissive_texture.texture, out + "_emissive", name)) snprintf(h.textures[3], gm::kNameLen, "%s", name.c_str());
  }

  // --- weld
  {
    std::vector<unsigned int> remap(g.verts.size());
    size_t unique = meshopt_generateVertexRemap(remap.data(), g.idx.data(), g.idx.size(), g.verts.data(), g.verts.size(), sizeof(V));
    std::vector<V> nv(unique);
    std::vector<uint32_t> ni(g.idx.size());
    meshopt_remapVertexBuffer(nv.data(), g.verts.data(), g.verts.size(), sizeof(V), remap.data());
    meshopt_remapIndexBuffer(ni.data(), g.idx.data(), g.idx.size(), remap.data());
    g.verts.swap(nv);
    g.idx.swap(ni);
  }
  computeTangents(g.verts, g.idx);

  AABB bb;
  for (auto& v : g.verts) bb.expand(v.p);

  // --- vehicles: locate wheels, cut their geometry (the runtime draws spinning / steering wheels) and find light anchors
  if (kind == "car") {
    float L = bb.mx.z - bb.mn.z, W = bb.mx.x - bb.mn.x, H = bb.mx.y - bb.mn.y;
    // wheel placement comes from tools/car_wheels.py (dark tyre texels); fall back to proportions
    float zf = wheelsGiven ? wz[0] : -L * 0.32f, zr = wheelsGiven ? wz[1] : L * 0.32f;
    float radius = wheelsGiven ? wr : clamp(H * 0.19f, 0.27f, 0.40f);
    float yc = wheelsGiven ? wy : radius;
    float wheelX = wheelsGiven ? wtrack : W * 0.5f - 0.15f;
    for (int k = 0; k < 4; ++k) {
      h.wheel[k][0] = (k & 1) ? wheelX : -wheelX;
      h.wheel[k][1] = yc;
      h.wheel[k][2] = k < 2 ? zf : zr;
    }
    h.wheelRadius = radius;
    h.wheelWidth = 0.21f;
    // cut the baked wheels: triangles inside the tyre cylinder on the outer part of the body
    std::vector<uint32_t> kept;
    size_t removed = 0;
    for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
      Vec3 c = (g.verts[g.idx[i]].p + g.verts[g.idx[i + 1]].p + g.verts[g.idx[i + 2]].p) / 3.0f;
      bool inWheel = false;
      for (int k = 0; k < 4; ++k) {
        float dz = c.z - h.wheel[k][2], dy = c.y - h.wheel[k][1];
        bool sameSide = (c.x > 0) == (h.wheel[k][0] > 0);
        if (sameSide && std::sqrt(dz * dz + dy * dy) < radius * 1.03f && std::fabs(c.x) > wheelX - 0.24f) inWheel = true;
      }
      if (inWheel && wheelsGiven) { ++removed; continue; }
      kept.insert(kept.end(), {g.idx[i], g.idx[i + 1], g.idx[i + 2]});
    }
    g.idx.swap(kept);
    // light anchors: raycast towards the body from the front / back at ~62% height
    for (int s = 0; s < 2; ++s) {
      float x = (s ? 1.0f : -1.0f) * (W * 0.5f - 0.32f);
      float yF = H * 0.55f, yR = H * 0.62f;
      float tf = raycastMesh(g.verts, g.idx, {x, yF, bb.mn.z - 1.0f}, {0, 0, 1});
      float tr = raycastMesh(g.verts, g.idx, {x, yR, bb.mx.z + 1.0f}, {0, 0, -1});
      h.headlight[s][0] = x; h.headlight[s][1] = yF; h.headlight[s][2] = tf > 0 ? bb.mn.z - 1.0f + tf - 0.02f : bb.mn.z;
      h.taillight[s][0] = x; h.taillight[s][1] = yR; h.taillight[s][2] = tr > 0 ? bb.mx.z + 1.0f - tr + 0.02f : bb.mx.z;
    }
    printf("car: L=%.2f W=%.2f H=%.2f wheels z=%.2f/%.2f r=%.2f track %.2f removed %zu tris\n", L, W, H, zf, zr, radius, wheelX, removed);
  }

  // --- LODs
  std::vector<std::vector<V>> lodV;
  std::vector<std::vector<uint32_t>> lodI;
  const float ratios[gm::kMaxLods] = {1.0f, 0.32f, 0.10f};
  float diag = (bb.mx - bb.mn).length();
  for (int l = 0; l < gm::kMaxLods; ++l) {
    std::vector<uint32_t> idx = g.idx;
    if (l > 0) {
      size_t target = (size_t)(g.idx.size() * ratios[l]) / 3 * 3;
      std::vector<uint32_t> out2(g.idx.size());
      float err = 0;
      size_t n = meshopt_simplify(out2.data(), g.idx.data(), g.idx.size(), &g.verts[0].p.x, g.verts.size(), sizeof(V), target, 0.05f, 0, &err);
      if (n < target * 0.5f || n == 0) n = meshopt_simplifySloppy(out2.data(), g.idx.data(), g.idx.size(), &g.verts[0].p.x, g.verts.size(), sizeof(V), target, 0.2f, &err);
      out2.resize(n);
      idx = out2;
    }
    meshopt_optimizeVertexCache(idx.data(), idx.data(), idx.size(), g.verts.size());
    std::vector<V> vv(g.verts.size());
    size_t vcount = meshopt_optimizeVertexFetch(vv.data(), idx.data(), idx.size(), g.verts.data(), g.verts.size(), sizeof(V));
    vv.resize(vcount);
    lodV.push_back(vv);
    lodI.push_back(idx);
  }
  h.boneCount = (uint32_t)bones.size();
  h.animRootScale = animRootScale;
  if (skinned) h.flags |= gm::kSkinned;
  h.lodCount = gm::kMaxLods;
  for (int k = 0; k < 3; ++k) { h.bmin[k] = (&bb.mn.x)[k]; h.bmax[k] = (&bb.mx.x)[k]; }
  h.lodDistance[0] = diag * 4.0f;
  h.lodDistance[1] = diag * 12.0f;
  h.lodDistance[2] = diag * 40.0f;

  std::string path = out + ".gmesh";
  FILE* f = fopen(path.c_str(), "wb");
  fwrite(&h, sizeof(h), 1, f);
  if (!bones.empty()) fwrite(bones.data(), sizeof(gm::Bone), bones.size(), f);
  for (int l = 0; l < gm::kMaxLods; ++l) {
    gm::LodHeader lh{(uint32_t)lodV[l].size(), (uint32_t)lodI[l].size()};
    fwrite(&lh, sizeof(lh), 1, f);
    for (auto& v : lodV[l]) { gm::Vertex pv = pack(v, skinned); fwrite(&pv, sizeof(pv), 1, f); }
    fwrite(lodI[l].data(), 4, lodI[l].size(), f);
  }
  fclose(f);
  printf("%s: %s, %zu bones, LOD tris %zu/%zu/%zu, bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f), tex [%s|%s|%s]\n", path.c_str(), skinned ? "skinned" : "static",
         bones.size(), lodI[0].size() / 3, lodI[1].size() / 3, lodI[2].size() / 3, bb.mn.x, bb.mn.y, bb.mn.z, bb.mx.x, bb.mx.y, bb.mx.z, h.textures[0], h.textures[1],
         h.textures[2]);
  cgltf_free(d);
  return 0;
}

// ------------------------------------------------------------------------------------------------ anim mode
static int runAnim(int argc, char** argv) {
  std::string in = argv[2], out = argv[3];
  float yawDeg = 0, scaleTo = 0;
  std::string name = "clip";
  for (int i = 4; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--yaw" && i + 1 < argc) yawDeg = (float)atof(argv[++i]);
    else if (a == "--height" && i + 1 < argc) scaleTo = (float)atof(argv[++i]);
    else if (a == "--name" && i + 1 < argc) name = argv[++i];
  }
  cgltf_options opt{};
  cgltf_data* d = nullptr;
  if (cgltf_parse_file(&opt, in.c_str(), &d) != cgltf_result_success || cgltf_load_buffers(&opt, d, in.c_str()) != cgltf_result_success) return 1;
  if (!d->skins_count || !d->animations_count) { fprintf(stderr, "no skin/animation in %s\n", in.c_str()); return 1; }
  const cgltf_skin* sk = &d->skins[0];
  const cgltf_animation* an = &d->animations[0];
  // same normalisation as the mesh (yaw, scale, centring) so clips line up with the converted model
  Geometry g;
  loadGeometry(d, g);
  bakeMeshWorld(g, nullptr);
  Correction CC = computeCorrection(g, yawDeg, scaleTo, 0);
  float s = CC.s;
  Quat yq = CC.r;
  float duration = 0;
  for (size_t i = 0; i < an->samplers_count; ++i) {
    const cgltf_accessor* t = an->samplers[i].input;
    if (t->has_max) duration = std::max(duration, t->max[0]);
  }
  const float fps = 30.0f;
  int frames = std::max(2, (int)std::ceil(duration * fps) + 1);
  size_t B = sk->joints_count;
  std::vector<float> data((size_t)frames * B * 10);
  auto sample = [&](const cgltf_animation_sampler* smp, float time, float* outv, int comps) {
    const cgltf_accessor* ti = smp->input;
    const cgltf_accessor* vo = smp->output;
    size_t n = ti->count;
    float t0, t1;
    size_t k = 0;
    cgltf_accessor_read_float(ti, 0, &t0, 1);
    if (time <= t0 || n == 1) { cgltf_accessor_read_float(vo, 0, outv, comps); return; }
    for (k = 0; k + 1 < n; ++k) {
      cgltf_accessor_read_float(ti, k + 1, &t1, 1);
      if (t1 >= time) break;
    }
    if (k + 1 >= n) { cgltf_accessor_read_float(vo, n - 1, outv, comps); return; }
    cgltf_accessor_read_float(ti, k, &t0, 1);
    cgltf_accessor_read_float(ti, k + 1, &t1, 1);
    float a[4], b[4];
    cgltf_accessor_read_float(vo, k, a, comps);
    cgltf_accessor_read_float(vo, k + 1, b, comps);
    float u = (time - t0) / std::max(1e-6f, t1 - t0);
    if (comps == 4) {
      Quat q = qslerp({a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}, u);
      outv[0] = q.x; outv[1] = q.y; outv[2] = q.z; outv[3] = q.w;
    } else for (int c = 0; c < comps; ++c) outv[c] = a[c] + (b[c] - a[c]) * u;
  };
  for (int f = 0; f < frames; ++f) {
    float time = std::min(duration, f / fps);
    for (size_t j = 0; j < B; ++j) {
      const cgltf_node* nd = sk->joints[j];
      Vec3 t, sc; Quat r;
      nodeLocal(nd, t, r, sc);
      float tv[3] = {t.x, t.y, t.z}, rv[4] = {r.x, r.y, r.z, r.w}, sv[3] = {sc.x, sc.y, sc.z};
      for (size_t c = 0; c < an->channels_count; ++c) {
        const cgltf_animation_channel& ch = an->channels[c];
        if (ch.target_node != nd) continue;
        if (ch.target_path == cgltf_animation_path_type_translation) sample(ch.sampler, time, tv, 3);
        else if (ch.target_path == cgltf_animation_path_type_rotation) sample(ch.sampler, time, rv, 4);
        else if (ch.target_path == cgltf_animation_path_type_scale) sample(ch.sampler, time, sv, 3);
      }
      bool root = true;
      for (size_t k = 0; k < B; ++k) if (sk->joints[k] == nd->parent) root = false;
      if (root) {
        // fold non-joint ancestor scale + normalisation (yaw + scale) into the root
        float sx = 1;
        Vec3 off{0, 0, 0};
        if (nd->parent) {
          float w[16];
          cgltf_node_transform_world(nd->parent, w);
          sx = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
          off = {w[12], w[13], w[14]};
        }
        Vec3 T = qrot(yq, (off + Vec3{tv[0], tv[1], tv[2]} * sx) * s) + CC.t;
        Quat R = qmul(yq, Quat{rv[0], rv[1], rv[2], rv[3]});
        tv[0] = T.x; tv[1] = T.y; tv[2] = T.z;
        rv[0] = R.x; rv[1] = R.y; rv[2] = R.z; rv[3] = R.w;
        for (int c = 0; c < 3; ++c) sv[c] *= sx * s;
      }
      float* o = &data[((size_t)f * B + j) * 10];
      memcpy(o, tv, 12); memcpy(o + 3, rv, 16); memcpy(o + 7, sv, 12);
    }
  }
  // root motion: horizontal displacement of the root joint across the clip
  float* first = &data[0];
  float* last = &data[((size_t)(frames - 1) * B) * 10];
  float rm = std::sqrt((last[0] - first[0]) * (last[0] - first[0]) + (last[2] - first[2]) * (last[2] - first[2]));
  gm::AnimHeader ah{};
  ah.magic = gm::kMagicAnim;
  ah.boneCount = (uint32_t)B;
  ah.frameCount = (uint32_t)frames;
  ah.fps = fps;
  ah.rootMotion = rm;
  snprintf(ah.name, gm::kNameLen, "%s", name.c_str());
  FILE* f = fopen(out.c_str(), "wb");
  fwrite(&ah, sizeof(ah), 1, f);
  for (size_t j = 0; j < B; ++j) {
    char nm[gm::kNameLen] = {};
    snprintf(nm, gm::kNameLen, "%s", sk->joints[j]->name ? sk->joints[j]->name : "");
    fwrite(nm, 1, gm::kNameLen, f);
  }
  fwrite(data.data(), sizeof(float), data.size(), f);
  fclose(f);
  printf("%s: %zu bones, %d frames (%.2fs), root motion %.2f m\n", out.c_str(), B, frames, duration, rm);
  cgltf_free(d);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: meshconv mesh in.glb out_prefix [...] | meshconv anim in.glb out.ganim [...]\n");
    return 1;
  }
  std::string mode = argv[1];
  if (mode == "mesh") return runMesh(argc, argv);
  if (mode == "anim") return runAnim(argc, argv);
  return 1;
}
