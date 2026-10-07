#include "raster.h"

#include <cstring>

namespace bake {

// ------------------------------------------------------------------------------------------------------------ primitives
Mesh makeEllipsoid(Vec3 c, Vec3 r, int seg, int rings, Vec3 col, uint16_t mat) {
  Mesh m;
  for (int i = 0; i <= rings; ++i) {
    float v = (float)i / rings, phi = v * kPi;
    for (int j = 0; j <= seg; ++j) {
      float u = (float)j / seg, th = u * 2 * kPi;
      Vec3 d{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
      Vec3 n = Vec3{d.x / r.x, d.y / r.y, d.z / r.z}.normalized();
      m.v.push_back({c + Vec3{d.x * r.x, d.y * r.y, d.z * r.z}, n, col});
    }
  }
  for (int i = 0; i < rings; ++i)
    for (int j = 0; j < seg; ++j) {
      uint32_t a = i * (seg + 1) + j, b = a + 1, c2 = a + seg + 1, d = c2 + 1;
      m.t.push_back({a, c2, b, mat});
      m.t.push_back({b, c2, d, mat});
    }
  return m;
}

Mesh makeRoundedBox(Vec3 c, Vec3 size, float radius, int seg, Vec3 col, uint16_t mat) {
  Mesh m;
  Vec3 h = size * 0.5f;
  radius = std::min(radius, std::min(h.x, std::min(h.y, h.z)) * 0.999f);
  Vec3 inner{h.x - radius, h.y - radius, h.z - radius};
  // 6 faces, each a (seg+1)^2 grid on the cube surface, projected to a rounded box
  for (int face = 0; face < 6; ++face) {
    int axis = face / 2;
    float sgn = (face % 2) ? -1.0f : 1.0f;
    uint32_t base = (uint32_t)m.v.size();
    for (int i = 0; i <= seg; ++i)
      for (int j = 0; j <= seg; ++j) {
        float u = (float)i / seg * 2 - 1, v = (float)j / seg * 2 - 1;
        // map the grid through a smoothing function to concentrate vertices near the edges
        Vec3 q;
        if (axis == 0) q = {sgn, u, v};
        else if (axis == 1) q = {u, sgn, v};
        else q = {u, v, sgn};
        Vec3 p{q.x * h.x, q.y * h.y, q.z * h.z};
        Vec3 cl{gtabr::clamp(p.x, -inner.x, inner.x), gtabr::clamp(p.y, -inner.y, inner.y), gtabr::clamp(p.z, -inner.z, inner.z)};
        Vec3 d = p - cl;
        Vec3 n = d.length() > 1e-6f ? d.normalized() : Vec3{axis == 0 ? sgn : 0, axis == 1 ? sgn : 0, axis == 2 ? sgn : 0};
        m.v.push_back({c + cl + n * radius, n, col});
      }
    for (int i = 0; i < seg; ++i)
      for (int j = 0; j < seg; ++j) {
        uint32_t a = base + i * (seg + 1) + j, b = a + 1, c2 = a + seg + 1, d = c2 + 1;
        bool flip = ((face % 2) == 0) ^ (axis == 1);
        if (flip) { m.t.push_back({a, b, c2, mat}); m.t.push_back({b, d, c2, mat}); }
        else { m.t.push_back({a, c2, b, mat}); m.t.push_back({b, c2, d, mat}); }
      }
  }
  // winding is fixed after the fact by comparing with the outward normal
  for (auto& t : m.t) {
    Vec3 e1 = m.v[t.b].p - m.v[t.a].p, e2 = m.v[t.c].p - m.v[t.a].p;
    Vec3 fn = e1.cross(e2);
    Vec3 avg = m.v[t.a].n + m.v[t.b].n + m.v[t.c].n;
    if (fn.dot(avg) < 0) std::swap(t.b, t.c);
  }
  return m;
}

Mesh makeCylinder(Vec3 a, Vec3 b, float ra, float rb, int seg, Vec3 col, uint16_t mat, bool caps) {
  Mesh m;
  Vec3 axis = (b - a);
  float len = axis.length();
  Vec3 ax = axis.normalized();
  Vec3 ref = std::fabs(ax.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  Vec3 u = ax.cross(ref).normalized(), w = ax.cross(u).normalized();
  float slope = (ra - rb) / std::max(len, 1e-6f);
  for (int i = 0; i <= 1; ++i)
    for (int j = 0; j <= seg; ++j) {
      float th = (float)j / seg * 2 * kPi;
      Vec3 d = u * std::cos(th) + w * std::sin(th);
      Vec3 n = (d + ax * slope).normalized();
      m.v.push_back({(i ? b : a) + d * (i ? rb : ra), n, col});
    }
  for (int j = 0; j < seg; ++j) {
    uint32_t p0 = j, p1 = j + 1, p2 = seg + 1 + j, p3 = seg + 2 + j;
    m.t.push_back({p0, p2, p1, mat});
    m.t.push_back({p1, p2, p3, mat});
  }
  if (caps) {
    for (int e = 0; e < 2; ++e) {
      Vec3 c = e ? b : a;
      Vec3 n = e ? ax : -ax;
      uint32_t base = (uint32_t)m.v.size();
      m.v.push_back({c, n, col});
      for (int j = 0; j <= seg; ++j) {
        float th = (float)j / seg * 2 * kPi;
        Vec3 d = u * std::cos(th) + w * std::sin(th);
        m.v.push_back({c + d * (e ? rb : ra), n, col});
      }
      for (int j = 0; j < seg; ++j) {
        if (e) m.t.push_back({base, base + 1 + j, base + 2 + j, mat});
        else m.t.push_back({base, base + 2 + j, base + 1 + j, mat});
      }
    }
  }
  for (auto& t : m.t) {  // orient outward
    Vec3 fn = (m.v[t.b].p - m.v[t.a].p).cross(m.v[t.c].p - m.v[t.a].p);
    Vec3 avg = m.v[t.a].n + m.v[t.b].n + m.v[t.c].n;
    if (fn.dot(avg) < 0) std::swap(t.b, t.c);
  }
  return m;
}

Mesh makeCapsule(Vec3 a, Vec3 b, float ra, float rb, int seg, Vec3 col, uint16_t mat) {
  Mesh m = makeCylinder(a, b, ra, rb, seg, col, mat, false);
  m.append(makeEllipsoid(a, {ra, ra, ra}, seg, std::max(4, seg / 2), col, mat));
  m.append(makeEllipsoid(b, {rb, rb, rb}, seg, std::max(4, seg / 2), col, mat));
  return m;
}

// ------------------------------------------------------------------------------------------------------------ rasteriser
namespace {
struct Pixel {
  float z = 1e30f;
  Vec3 pos;
  Vec3 n;
  Vec3 c;
  int16_t mat = -1;
  int16_t mat2 = -1;
  float aux = 0.0f;
};

Vec3 skyEnv(const Vec3& dir, const Lighting& L) {
  float t = gtabr::saturate(dir.y * 1.2f + 0.1f);
  Vec3 horizon = L.skyCol * 1.35f + Vec3{0.25f, 0.22f, 0.18f};
  Vec3 sky = horizon * (1 - t) + Vec3{0.28f, 0.48f, 0.95f} * t;
  Vec3 gnd = L.gndCol * 1.6f;
  float g = gtabr::saturate(-dir.y * 3.0f + 0.5f);
  return sky * (1 - g) + gnd * g;
}
}  // namespace

BakeResult bakeMesh(const Mesh& mesh, const std::vector<Material>& mats, const BakeView& view, float yawRad, const Lighting& L,
                    int maxW, int maxH) {
  const float pitch = view.pitchDeg * gtabr::kDeg2Rad;
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  // camera basis (looking north and down). right = +X, up = (0, cp, -sp), forward = (0, -sp, -cp)
  const Vec3 right{1, 0, 0}, up{0, cp, -sp}, fwd{0, -sp, -cp};
  const float cy = std::cos(yawRad), sy = std::sin(yawRad);
  // rotate model about Y by yaw (clockwise from above, matching the game's yaw convention)
  auto rotY = [&](const Vec3& p) { return Vec3{p.x * cy - p.z * sy, p.y, p.x * sy + p.z * cy}; };

  // Project all vertices to find the extents
  const size_t nv = mesh.v.size();
  std::vector<Vec3> wp(nv), wn(nv);
  float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
  for (size_t i = 0; i < nv; ++i) {
    wp[i] = rotY(mesh.v[i].p);
    wn[i] = rotY(mesh.v[i].n);
    float sx = wp[i].dot(right), syy = wp[i].dot(up);
    minX = std::min(minX, sx); maxX = std::max(maxX, sx);
    minY = std::min(minY, syy); maxY = std::max(maxY, syy);
  }
  const int ss = view.ss;
  const float ppm = view.ppm * ss;
  int pad = 3 * ss;
  int W = (int)std::ceil((maxX - minX) * ppm) + 2 * pad, H = (int)std::ceil((maxY - minY) * ppm) + 2 * pad;
  W = std::min(W, maxW * ss); H = std::min(H, maxH * ss);
  float offX = -minX * ppm + pad, offY = maxY * ppm + pad;  // pixel x = sx*ppm + offX ; pixel y = offY - sy*ppm
  std::vector<Pixel> buf((size_t)W * H);

  std::vector<float> px(nv), py(nv), pz(nv);
  for (size_t i = 0; i < nv; ++i) {
    px[i] = wp[i].dot(right) * ppm + offX;
    py[i] = offY - wp[i].dot(up) * ppm;
    pz[i] = wp[i].dot(fwd);  // larger = farther
  }
  for (const Tri& t : mesh.t) {
    float x0 = px[t.a], y0 = py[t.a], x1 = px[t.b], y1 = py[t.b], x2 = px[t.c], y2 = py[t.c];
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-6f) continue;
    int minx = std::max(0, (int)std::floor(std::min({x0, x1, x2}))), maxx = std::min(W - 1, (int)std::ceil(std::max({x0, x1, x2})));
    int miny = std::max(0, (int)std::floor(std::min({y0, y1, y2}))), maxy = std::min(H - 1, (int)std::ceil(std::max({y0, y1, y2})));
    float inv = 1.0f / area;
    for (int y = miny; y <= maxy; ++y)
      for (int x = minx; x <= maxx; ++x) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) * inv;
        float w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) * inv;
        float w2 = 1.0f - w0 - w1;
        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
        float z = w0 * pz[t.a] + w1 * pz[t.b] + w2 * pz[t.c];
        Pixel& p = buf[(size_t)y * W + x];
        if (z >= p.z) continue;
        p.z = z;
        p.pos = mesh.v[t.a].p * w0 + mesh.v[t.b].p * w1 + mesh.v[t.c].p * w2;  // object space (for AO by height)
        p.n = (wn[t.a] * w0 + wn[t.b] * w1 + wn[t.c] * w2);
        p.c = mesh.v[t.a].c * w0 + mesh.v[t.b].c * w1 + mesh.v[t.c].c * w2;
        p.mat = (int16_t)t.mat;
        p.mat2 = (t.mat2 == 0xFFFF) ? (int16_t)-1 : (int16_t)t.mat2;
        p.aux = mesh.v[t.a].aux * w0 + mesh.v[t.b].aux * w1 + mesh.v[t.c].aux * w2;
      }
  }

  // shade
  const Vec3 V = (fwd * -1.0f);  // direction towards the camera
  Vec3 key = L.keyDir.normalized(), fill = L.fillDir.normalized();
  int outW = W / ss, outH = H / ss;
  Image full;
  full.w = outW; full.h = outH;
  full.rgba.assign((size_t)outW * outH * 4, 0.0f);
  for (int oy = 0; oy < outH; ++oy)
    for (int ox = 0; ox < outW; ++ox) {
      float acc[3] = {0, 0, 0}, accA = 0;
      for (int sy2 = 0; sy2 < ss; ++sy2)
        for (int sx2 = 0; sx2 < ss; ++sx2) {
          const Pixel& p = buf[(size_t)(oy * ss + sy2) * W + (ox * ss + sx2)];
          if (p.mat < 0) continue;
          const Material& m = mats[(p.mat2 >= 0 && p.aux > 0.5f) ? p.mat2 : p.mat];
          Vec3 n = p.n.normalized();
          if (n.dot(V) < 0) n = n * -1.0f;  // two-sided shading for thin geometry
          Vec3 alb = m.useVertexColor ? p.c : m.albedo;
          float ndl = std::max(0.0f, n.dot(key));
          float ndf = std::max(0.0f, n.dot(fill));
          float hemi = n.y * 0.5f + 0.5f;
          Vec3 amb = L.gndCol * (1 - hemi) + L.skyCol * hemi;
          float ao = 1.0f;
          if (m.aoLow < 1.0f) ao = m.aoLow + (1.0f - m.aoLow) * gtabr::saturate((p.pos.y - 0.05f) / 0.55f);
          Vec3 diff = (L.keyCol * (L.keyI * ndl) + L.fillCol * (L.fillI * ndf) + amb * L.ambI);
          Vec3 col{alb.x * diff.x, alb.y * diff.y, alb.z * diff.z};
          col = col * ao;
          // specular + environment reflection
          Vec3 H = (key + V).normalized();
          float sp2 = std::pow(std::max(0.0f, n.dot(H)), m.shin) * m.spec * (ndl > 0 ? 1.0f : 0.0f);
          col += Vec3{1.0f, 0.97f, 0.9f} * sp2;
          if (m.env > 0) {
            Vec3 R = (n * (2.0f * n.dot(V)) - V).normalized();
            float fres = 0.25f + 0.75f * std::pow(1.0f - std::max(0.0f, n.dot(V)), 3.0f);
            Vec3 e = skyEnv(R, L);
            float k = m.env * fres;
            col = col * (1.0f - k * 0.6f) + e * k * ao;
          }
          if (m.emissive > 0) col += alb * m.emissive;
          acc[0] += col.x; acc[1] += col.y; acc[2] += col.z;
          accA += 1.0f;
        }
      float* o = &full.rgba[((size_t)oy * outW + ox) * 4];
      if (accA > 0) {
        o[0] = acc[0] / accA; o[1] = acc[1] / accA; o[2] = acc[2] / accA;
        o[3] = accA / (ss * ss);
      }
    }
  // tight crop
  int x0 = outW, y0 = outH, x1 = -1, y1 = -1;
  for (int y = 0; y < outH; ++y)
    for (int x = 0; x < outW; ++x)
      if (full.rgba[((size_t)y * outW + x) * 4 + 3] > 0.004f) {
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
  BakeResult res;
  if (x1 < 0) { res.img.w = res.img.h = 1; res.img.rgba.assign(4, 0.0f); res.pivotX = res.pivotY = 0; return res; }
  res.img.w = x1 - x0 + 1; res.img.h = y1 - y0 + 1;
  res.img.rgba.resize((size_t)res.img.w * res.img.h * 4);
  for (int y = 0; y < res.img.h; ++y)
    std::memcpy(&res.img.rgba[(size_t)y * res.img.w * 4], &full.rgba[((size_t)(y + y0) * outW + x0) * 4], sizeof(float) * 4 * res.img.w);
  res.pivotX = offX / ss - x0;
  res.pivotY = offY / ss - y0;
  return res;
}

}  // namespace bake
