// Island generator (included inside class Gen in world.cpp).
//
// The whole map is an island: an irregular coastline with beaches all around, the main town in a coastal corner (its centre is the
// grid core with every special place: gas station, market, workshop, shops, plaza) wrapped by curving ring roads and radials,
// one or two more towns with fully organic streets (rings + radials), country roads winding through forest and fields between
// them, dirt tracks to farmhouses, and the sea around everything. Every road ends up in one polyline graph (World::redges).

// ---------------------------------------------------------------------------------------------------------------- noise
static float hashf(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return (x & 0xFFFFFF) / 16777215.0f;
}

bool island_ = false;

// ---------------------------------------------------------------------------------------------------------------- occupancy
// 2 m grid of what has been claimed: 1 road / sidewalk, 2 lot or building, 3 plaza / reserved
std::vector<uint8_t> occ_;
float occX0_ = 0, occZ0_ = 0;
int occW_ = 0, occH_ = 0;
const float occCell_ = 2.0f;
uint8_t occAt(float x, float z) const {
  int i = (int)std::floor((x - occX0_) / occCell_), j = (int)std::floor((z - occZ0_) / occCell_);
  if (i < 0 || j < 0 || i >= occW_ || j >= occH_) return 255;
  return occ_[(size_t)j * occW_ + i];
}
// marks / tests a convex quad (world space)
template <typename F>
void forQuadCells(const Vec2 q[4], float cell, float x0, float z0, int W, int H, F fn) {
  float mnx = 1e9f, mnz = 1e9f, mxx = -1e9f, mxz = -1e9f;
  for (int i = 0; i < 4; ++i) { mnx = std::min(mnx, q[i].x); mxx = std::max(mxx, q[i].x); mnz = std::min(mnz, q[i].y); mxz = std::max(mxz, q[i].y); }
  int i0 = std::max(0, (int)std::floor((mnx - x0) / cell)), i1 = std::min(W - 1, (int)std::floor((mxx - x0) / cell));
  int j0 = std::max(0, (int)std::floor((mnz - z0) / cell)), j1 = std::min(H - 1, (int)std::floor((mxz - z0) / cell));
  for (int j = j0; j <= j1; ++j)
    for (int i = i0; i <= i1; ++i) {
      Vec2 c{x0 + (i + 0.5f) * cell, z0 + (j + 0.5f) * cell};
      bool in = true;
      for (int e = 0; e < 4 && in; ++e) {
        Vec2 a = q[e], b = q[(e + 1) % 4];
        float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (cr < 0) in = false;
      }
      if (!in) {   // the other winding
        in = true;
        for (int e = 0; e < 4 && in; ++e) {
          Vec2 a = q[e], b = q[(e + 1) % 4];
          float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
          if (cr > 0) in = false;
        }
      }
      if (in) fn(i, j);
    }
}
void occMark(const Vec2 q[4], uint8_t v) {
  forQuadCells(q, occCell_, occX0_, occZ0_, occW_, occH_, [&](int i, int j) { uint8_t& o = occ_[(size_t)j * occW_ + i]; if (o == 0 || v == 1) o = v; });
}
bool occFree(const Vec2 q[4]) {
  bool ok = true;
  forQuadCells(q, occCell_, occX0_, occZ0_, occW_, occH_, [&](int i, int j) { if (occ_[(size_t)j * occW_ + i] != 0) ok = false; });
  return ok;
}
void rasterMark(const Vec2 q[4], uint8_t bit) {
  forQuadCells(q, w_.rasterCell, w_.rasterX0, w_.rasterZ0, w_.rasterW, w_.rasterH, [&](int i, int j) { w_.raster[(size_t)j * w_.rasterW + i] |= bit; });
}

// ---------------------------------------------------------------------------------------------------------------- road graph
int addNode(Vec2 p) {
  for (size_t i = 0; i < w_.rnodes.size(); ++i)
    if ((w_.rnodes[i].p - p).length() < 1.5f) return (int)i;
  w_.rnodes.push_back({p, {}});
  return (int)w_.rnodes.size() - 1;
}
std::vector<uint8_t> edgeGrid_;   // 1 = converted from the grid core (already drawn by streets())
std::vector<int> edgeTown_;       // town index of an edge (-1 country road)
int addEdge(std::vector<Vec2> pts, float hw, uint8_t kind, bool grid = false, int town = -1) {
  if (pts.size() < 2) return -1;
  RoadEdge e;
  e.a = addNode(pts.front());
  e.b = addNode(pts.back());
  if (e.a == e.b) return -1;
  pts.front() = w_.rnodes[e.a].p;
  pts.back() = w_.rnodes[e.b].p;
  e.pts = std::move(pts);
  e.hw = hw;
  e.kind = kind;
  for (size_t i = 0; i + 1 < e.pts.size(); ++i) e.len += (e.pts[i + 1] - e.pts[i]).length();
  w_.redges.push_back(e);
  int id = (int)w_.redges.size() - 1;
  w_.rnodes[e.a].edges.push_back(id);
  w_.rnodes[e.b].edges.push_back(id);
  edgeGrid_.push_back(grid ? 1 : 0);
  edgeTown_.push_back(town);
  return id;
}
// Chaikin smoothing (keeps the end points) then resampling every 'step' metres
static std::vector<Vec2> smoothPath(const std::vector<Vec2>& in, int iters, float step) {
  std::vector<Vec2> p = in;
  for (int it = 0; it < iters && p.size() > 2; ++it) {
    std::vector<Vec2> q;
    q.push_back(p.front());
    for (size_t i = 0; i + 1 < p.size(); ++i) {
      Vec2 a = p[i], b = p[i + 1];
      if (i > 0) q.push_back(a * 0.75f + b * 0.25f);
      if (i + 2 < p.size()) q.push_back(a * 0.25f + b * 0.75f);
    }
    q.push_back(p.back());
    p = q;
  }
  std::vector<Vec2> out;
  out.push_back(p.front());
  float carry = 0;
  for (size_t i = 0; i + 1 < p.size(); ++i) {
    Vec2 a = p[i], b = p[i + 1];
    float L = (b - a).length();
    float t = step - carry;
    while (t < L) { out.push_back(a + (b - a) * (t / L)); t += step; }
    carry = L - (t - step);
  }
  if ((out.back() - p.back()).length() > step * 0.35f) out.push_back(p.back());
  else out.back() = p.back();
  return out;
}

// ---------------------------------------------------------------------------------------------------------------- island shape
float coastR(float theta) const {
  float r = 1.0f;
  for (int k = 0; k < 8; ++k) r += w_.coastA[k] * std::sin((k + 2) * theta + w_.coastP[k]);
  return r;
}

void islandPlan() {
  // main town grid core at the origin (already planned); the island is placed so that the core sits near one coast
  RectF G = w_.land;
  float gr = std::sqrt(G.w() * G.w() + G.h() * G.h()) * 0.5f;
  w_.island = true;
  w_.noiseSeed = seed_ * 2654435761u + 12345u;
  w_.islandRx = rng_.range(600.0f, 670.0f);
  w_.islandRz = rng_.range(520.0f, 580.0f);
  for (int k = 0; k < 8; ++k) {
    w_.coastA[k] = rng_.range(0.0f, 1.0f) * (k < 2 ? 0.07f : (k < 5 ? 0.035f : 0.016f));
    w_.coastP[k] = rng_.range(0.0f, kTau);
  }
  // the core: ~gr + 130 m from the coast on a random side (room for its ring roads and a beach)
  float ang = rng_.range(0.0f, kTau);
  Vec2 dir{std::cos(ang), std::sin(ang)};
  // islandC such that the origin lies at distance (edge - (gr + 150)) along -dir from the coast
  float edge = std::min(w_.islandRx, w_.islandRz);
  float off = std::max(80.0f, edge - (gr + 150.0f));
  w_.islandC = dir * -off;
  for (int it = 0; it < 30 && w_.landDist(0, 0) < gr + 140.0f; ++it) w_.islandC = w_.islandC * 0.9f;
  // more towns, spread over the rest of the island
  static const char* kTownNames[] = {"Vila Serena", "Porto Azul", "Santa Brisa", "Barra Clara", "Monte Alegre", "Praia Nova", "São Vicente do Sul",
                                     "Recanto Verde", "Lagoa Funda", "Morro do Sol", "Pedra Branca", "Itaúna do Mar"};
  const int nNames = (int)(sizeof(kTownNames) / sizeof(kTownNames[0]));
  int nameBase = (int)(seed_ % (uint32_t)nNames);
  w_.towns.push_back({kTownNames[nameBase], {0, 0}, gr + 105.0f, true});
  w_.cityName = kTownNames[nameBase];
  static const char* kIslands[] = {"Ilha Grande do Sul", "Ilha da Saudade", "Ilha do Farol", "Ilha das Garças", "Ilha Bela Vista", "Ilha do Coral"};
  w_.islandName = kIslands[seed_ % 6u];
  int want = 2;
  for (int attempt = 0; attempt < 400 && (int)w_.towns.size() < want + 1; ++attempt) {
    float a = rng_.range(0.0f, kTau), rr = rng_.range(0.2f, 0.8f);
    Vec2 c = w_.islandC + Vec2{std::cos(a) * w_.islandRx * rr, std::sin(a) * w_.islandRz * rr};
    float R = rng_.range(115.0f, 145.0f) * (attempt > 250 ? 0.8f : 1.0f);
    if (w_.landDist(c.x, c.y) < R + 45.0f) continue;
    bool ok = true;
    for (const Town& t : w_.towns) if ((t.c - c).length() < t.r + R + 110.0f) ok = false;
    if (!ok) continue;
    w_.towns.push_back({kTownNames[(nameBase + (int)w_.towns.size() * 5) % nNames], c, R, false});
  }
  // play area: the island plus a swimmable band of sea
  float mnx = 1e9f, mnz = 1e9f, mxx = -1e9f, mxz = -1e9f;
  for (int i = 0; i < 360; i += 2) {
    float th = i * kTau / 360.0f;
    float r = coastR(th);
    Vec2 p = w_.islandC + Vec2{std::cos(th) * w_.islandRx * r, std::sin(th) * w_.islandRz * r};
    mnx = std::min(mnx, p.x); mxx = std::max(mxx, p.x); mnz = std::min(mnz, p.y); mxz = std::max(mxz, p.y);
  }
  w_.playArea = {mnx - 70.0f, mnz - 70.0f, mxx + 70.0f, mxz + 70.0f};
  w_.half = std::max(std::max(-w_.playArea.x0, w_.playArea.x1), std::max(-w_.playArea.z0, w_.playArea.z1)) + 30.0f;
  w_.beach = {0, 0, 0, 0};
  w_.waterLevel = -0.35f;
  // grids: occupancy (2 m) and the walking-surface raster (0.5 m)
  occX0_ = w_.playArea.x0; occZ0_ = w_.playArea.z0;
  occW_ = (int)std::ceil(w_.playArea.w() / occCell_) + 1;
  occH_ = (int)std::ceil(w_.playArea.h() / occCell_) + 1;
  occ_.assign((size_t)occW_ * occH_, 0);
  w_.rasterX0 = w_.playArea.x0; w_.rasterZ0 = w_.playArea.z0;
  w_.rasterW = (int)std::ceil(w_.playArea.w() / w_.rasterCell) + 1;
  w_.rasterH = (int)std::ceil(w_.playArea.h() / w_.rasterCell) + 1;
  w_.raster.assign((size_t)w_.rasterW * w_.rasterH, 0);
  // the grid core is fully claimed
  Vec2 gq[4] = {{G.x0, G.z0}, {G.x1, G.z0}, {G.x1, G.z1}, {G.x0, G.z1}};
  occMark(gq, 3);
}

// ---------------------------------------------------------------------------------------------------------------- roads
// noisy closed ring around c with mean radius R, sampled at the given angles (nodes) with points in between
std::vector<Vec2> ringPoint(Vec2 c, float R, float th, uint32_t s) const {
  float n = 0.10f * std::sin(th * 3.0f + hashf(s) * 6.28f) + 0.06f * std::sin(th * 5.0f + hashf(s + 7) * 6.28f);
  return {c + Vec2{std::cos(th), std::sin(th)} * (R * (1.0f + n))};
}
Vec2 ringAt(Vec2 c, float R, float th, uint32_t s) const { return ringPoint(c, R, th, s)[0]; }

// rings + radials for an organic town. angles: the radial directions. Returns the outer ring node positions (for highways).
void organicTown(int townIdx, const std::vector<float>& radii, std::vector<float> angles, bool fromCentre, uint32_t s, std::vector<Vec2>* inner = nullptr) {
  const Town& T = w_.towns[townIdx];
  std::sort(angles.begin(), angles.end());
  // ring arcs between consecutive radial angles
  for (size_t ri = 0; ri < radii.size(); ++ri) {
    float R = radii[ri];
    uint32_t rs = s + (uint32_t)ri * 101u;
    for (size_t k = 0; k < angles.size(); ++k) {
      float a0 = angles[k], a1 = k + 1 < angles.size() ? angles[k + 1] : angles[0] + kTau;
      std::vector<Vec2> pts;
      int n = std::max(3, (int)((a1 - a0) * R / 9.0f));
      for (int i = 0; i <= n; ++i) pts.push_back(ringAt(T.c, R, a0 + (a1 - a0) * i / n, rs));
      // skip arcs that would run into the sea
      bool wet = false;
      for (const Vec2& p : pts) if (w_.landDist(p.x, p.y) < 30.0f) wet = true;
      if (wet) continue;
      addEdge(smoothPath(pts, 1, 6.0f), ri == 0 ? 3.6f : 3.4f, 0, false, townIdx);
    }
  }
  // radials: centre (or the inner start points) -> each ring in turn, gently curved
  for (size_t k = 0; k < angles.size(); ++k) {
    float a = angles[k];
    Vec2 prev = fromCentre ? T.c : (inner && k < inner->size() ? (*inner)[k] : ringAt(T.c, radii[0] * 0.6f, a, s));
    for (size_t ri = 0; ri < radii.size(); ++ri) {
      Vec2 next = ringAt(T.c, radii[ri], a, s + (uint32_t)ri * 101u);
      if (w_.landDist(next.x, next.y) < 30.0f) break;
      Vec2 mid = (prev + next) * 0.5f;
      Vec2 d = (next - prev);
      Vec2 nrm{-d.y, d.x};
      nrm = nrm.normalized();
      mid += nrm * (hashf(s + (uint32_t)(k * 13 + ri * 7)) - 0.5f) * d.length() * 0.25f;
      std::vector<Vec2> pts{prev, mid, next};
      addEdge(smoothPath(pts, 2, 6.0f), 3.6f, ri == 0 && fromCentre ? 1 : 0, false, townIdx);
      prev = next;
    }
  }
}

// a winding country road between two points (avoids the sea); returns false when no dry route was found
bool countryRoad(Vec2 a, Vec2 b, float hw, uint8_t kind) {
  float L = (b - a).length();
  Vec2 d = (b - a) * (1.0f / std::max(L, 1.0f));
  Vec2 nrm{-d.y, d.x};
  for (int attempt = 0; attempt < 10; ++attempt) {
    std::vector<Vec2> ctrl{a};
    int n = std::max(2, (int)(L / 70.0f));
    float amp = L * (0.06f + 0.05f * attempt) * (attempt % 2 ? -1.0f : 1.0f);
    for (int i = 1; i < n; ++i) {
      float t = (float)i / n;
      float wob = std::sin(t * kPi) * amp + (rng_.uni() - 0.5f) * 30.0f;
      ctrl.push_back(a + (b - a) * t + nrm * wob);
    }
    ctrl.push_back(b);
    std::vector<Vec2> pts = smoothPath(ctrl, 3, 8.0f);
    bool ok = true;
    for (const Vec2& p : pts) if (w_.landDist(p.x, p.y) < 35.0f) { ok = false; break; }
    if (!ok) continue;
    addEdge(pts, hw, kind);
    return true;
  }
  return false;
}

void islandRoads() {
  // 1) the grid core streets become graph edges (split at every crossing)
  for (const RoadLine& r : w_.roads) {
    std::vector<float> cuts{r.a, r.b};
    for (const RoadLine& o : w_.roads)
      if (o.horizontal != r.horizontal && o.c > r.a + 0.5f && o.c < r.b - 0.5f) cuts.push_back(o.c);
    std::sort(cuts.begin(), cuts.end());
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
      Vec2 p0 = r.horizontal ? Vec2{cuts[i], r.c} : Vec2{r.c, cuts[i]};
      Vec2 p1 = r.horizontal ? Vec2{cuts[i + 1], r.c} : Vec2{r.c, cuts[i + 1]};
      addEdge({p0, p1}, r.hw, r.avenue ? 1 : 0, true, 0);
    }
  }
  // 2) main town: two curving rings around the core, joined to the grid by radials that continue its streets
  const Town& M = w_.towns[0];
  RectF G = w_.land;
  float gr = std::sqrt(G.w() * G.w() + G.h() * G.h()) * 0.5f;
  std::vector<std::pair<float, Vec2>> stubs;   // grid street ends on the core boundary (angle, point)
  for (const RoadEdge& e : w_.redges) {
    for (int endI = 0; endI < 2; ++endI) {
      const RoadNode& nd = w_.rnodes[endI ? e.b : e.a];
      if (nd.edges.size() != 1) continue;
      stubs.push_back({std::atan2(nd.p.y, nd.p.x), nd.p});
    }
  }
  std::sort(stubs.begin(), stubs.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
  std::vector<float> angles;
  std::vector<Vec2> starts;
  for (const auto& st : stubs) {
    bool far = true;
    for (float a : angles) { float da = std::fabs(wrapAngle(a - st.first)); if (da < 0.55f) far = false; }
    if (!far) continue;
    angles.push_back(st.first);
    starts.push_back(st.second);
  }
  {
    // order the start points like the sorted angles
    std::vector<std::pair<float, Vec2>> as;
    for (size_t i = 0; i < angles.size(); ++i) as.push_back({angles[i], starts[i]});
    std::sort(as.begin(), as.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    angles.clear(); starts.clear();
    for (auto& v : as) { angles.push_back(v.first); starts.push_back(v.second); }
  }
  float r1 = gr + rng_.range(28.0f, 38.0f), r2 = r1 + rng_.range(52.0f, 68.0f);
  organicTown(0, {r1, r2}, angles, false, seed_ * 31u + 5u, &starts);
  // 3) the other towns: rings and radials from a central square
  for (size_t ti = 1; ti < w_.towns.size(); ++ti) {
    const Town& T = w_.towns[ti];
    int nr = rng_.irange(5, 7);
    std::vector<float> ang;
    float a0 = rng_.range(0.0f, kTau);
    for (int i = 0; i < nr; ++i) ang.push_back(wrapAngle(a0 + i * kTau / nr + rng_.range(-0.18f, 0.18f)));
    organicTown((int)ti, {T.r * 0.36f, T.r * 0.68f, T.r * 0.98f}, ang, true, seed_ * 97u + (uint32_t)ti * 1013u);
  }
  // 4) country roads: every other town to its nearest neighbour already connected (a tree), plus one extra loop
  auto outerNode = [&](int ti, Vec2 toward) {
    int best = -1;
    float bd = 1e9f;
    for (size_t i = 0; i < w_.rnodes.size(); ++i) {
      Vec2 p = w_.rnodes[i].p;
      float dc = (p - w_.towns[ti].c).length();
      if (dc < w_.towns[ti].r * 0.55f) continue;   // only the outer ring
      float d = (p - toward).length();
      if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
  };
  std::vector<int> connected{0};
  for (size_t ti = 1; ti < w_.towns.size(); ++ti) {
    int tgt = 0;
    float bd = 1e9f;
    for (int c : connected) { float d = (w_.towns[c].c - w_.towns[ti].c).length(); if (d < bd) { bd = d; tgt = c; } }
    int na = outerNode(tgt, w_.towns[ti].c), nb = outerNode((int)ti, w_.towns[tgt].c);
    if (na >= 0 && nb >= 0) countryRoad(w_.rnodes[na].p, w_.rnodes[nb].p, 3.4f, 2);
    connected.push_back((int)ti);
  }
  if (w_.towns.size() >= 3) {
    int na = outerNode(1, w_.towns[2].c), nb = outerNode(2, w_.towns[1].c);
    if (na >= 0 && nb >= 0) countryRoad(w_.rnodes[na].p, w_.rnodes[nb].p, 3.2f, 2);
  }
  // a scenic road from the main town along the island interior toward the far side
  {
    Vec2 far = w_.islandC + (w_.islandC - M.c).normalized() * std::min(w_.islandRx, w_.islandRz) * 0.55f;
    int na = outerNode(0, far);
    if (na >= 0 && w_.landDist(far.x, far.y) > 60.0f) countryRoad(w_.rnodes[na].p, far, 3.2f, 2);
  }
  // 5) dirt tracks off the country roads to farmhouses
  std::vector<int> highways;
  for (size_t i = 0; i < w_.redges.size(); ++i) if (w_.redges[i].kind == 2) highways.push_back((int)i);
  int tracks = 0;
  for (int attempt = 0; attempt < 30 && tracks < 4 && !highways.empty(); ++attempt) {
    const RoadEdge& h = w_.redges[highways[rng_.irange(0, (int)highways.size() - 1)]];
    if (h.pts.size() < 8) continue;
    size_t k = (size_t)rng_.irange(3, (int)h.pts.size() - 4);
    Vec2 p = h.pts[k], t = (h.pts[k + 1] - h.pts[k - 1]).normalized();
    Vec2 n{-t.y, t.x};
    if (rng_.chance(0.5f)) n = n * -1.0f;
    Vec2 end = p + n * rng_.range(70.0f, 120.0f) + t * rng_.range(-30.0f, 30.0f);
    if (w_.landDist(end.x, end.y) < 50.0f) continue;
    bool nearTown = false;
    for (const Town& T : w_.towns) if ((end - T.c).length() < T.r + 40.0f) nearTown = true;
    if (nearTown) continue;
    // split the highway at p so the track joins at a node
    std::vector<Vec2> ctrl{p, p + n * 25.0f, (p + end) * 0.5f + t * rng_.range(-20.0f, 20.0f), end};
    std::vector<Vec2> pts = smoothPath(ctrl, 2, 6.0f);
    bool ok = true;
    for (const Vec2& q : pts) if (w_.landDist(q.x, q.y) < 30.0f) ok = false;
    if (!ok) continue;
    int hid = (int)(&h - &w_.redges[0]);
    splitEdge(hid, k);
    addEdge(pts, 2.4f, 3);
    farmSpots_.push_back({end, std::atan2(-n.x, -n.y)});
    ++tracks;
  }
}
std::vector<std::pair<Vec2, float>> farmSpots_;

// splits edge e at polyline index k (0 < k < n-1) into two edges sharing a new node
void splitEdge(int e, size_t k) {
  RoadEdge old = w_.redges[e];
  if (k == 0 || k + 1 >= old.pts.size()) return;
  std::vector<Vec2> a(old.pts.begin(), old.pts.begin() + (long)k + 1), b(old.pts.begin() + (long)k, old.pts.end());
  // detach the old edge from its nodes and reuse its slot for the first half
  auto detach = [&](int node) { auto& v = w_.rnodes[node].edges; v.erase(std::remove(v.begin(), v.end(), e), v.end()); };
  detach(old.a);
  detach(old.b);
  int mid = addNode(a.back());
  RoadEdge ea = old;
  ea.pts = a; ea.b = mid; ea.len = 0;
  for (size_t i = 0; i + 1 < ea.pts.size(); ++i) ea.len += (ea.pts[i + 1] - ea.pts[i]).length();
  w_.redges[e] = ea;
  w_.rnodes[ea.a].edges.push_back(e);
  w_.rnodes[mid].edges.push_back(e);
  addEdge(b, old.hw, old.kind, edgeGrid_[e] != 0, edgeTown_[e]);
}

// ---------------------------------------------------------------------------------------------------------------- road geometry
// horizontal quad whose winding is fixed so it faces up (back faces are culled)
static void upQuad(MeshBuilder& b, Vec3 A, Vec3 B, Vec3 C, Vec3 D, Vec2 ua, Vec2 ub, Vec2 uc, Vec2 ud, int layer, float ma, float mb, float mc, float md) {
  Vec3 n = (B - A).cross(C - A) + (C - A).cross(D - A);
  if (n.y >= 0) b.quadMud(A, B, C, D, ua, ub, uc, ud, layer, ma, mb, mc, md);
  else b.quadMud(A, D, C, B, ua, ud, uc, ub, layer, ma, md, mc, mb);
}
// offset polyline (miter) at distance off to the left (+) / right (-) of the direction of travel
static std::vector<Vec2> offsetLine(const std::vector<Vec2>& p, float off) {
  std::vector<Vec2> o(p.size());
  for (size_t i = 0; i < p.size(); ++i) {
    Vec2 tin = i > 0 ? (p[i] - p[i - 1]).normalized() : Vec2{0, 0};
    Vec2 tout = i + 1 < p.size() ? (p[i + 1] - p[i]).normalized() : Vec2{0, 0};
    Vec2 t = (tin + tout);
    if (t.lengthSq() < 1e-6f) t = tout.lengthSq() > 0 ? tout : tin;
    t = t.normalized();
    Vec2 n{-t.y, t.x};
    float k = 1.0f;
    if (i > 0 && i + 1 < p.size()) { Vec2 nin{-tin.y, tin.x}; k = 1.0f / std::max(0.5f, n.dot(nin)); }
    o[i] = p[i] + n * (off * k);
  }
  return o;
}

void ribbon(const std::vector<Vec2>& L, const std::vector<Vec2>& R, float y, int layer, float tile, float mudL, float mudC, float mudR, Vec3 tint,
            float vStart = 0) {
  // two quads across (left half / right half) so the mud can be strong at the edges and clean in the middle
  float v = vStart;
  for (size_t i = 0; i + 1 < L.size(); ++i) {
    Vec2 l0 = L[i], l1 = L[i + 1], r0 = R[i], r1 = R[i + 1];
    Vec2 c0 = (l0 + r0) * 0.5f, c1 = (l1 + r1) * 0.5f;
    float seg = (c1 - c0).length();
    float w0 = (l0 - r0).length() / tile;
    Vec2 mid = (c0 + c1) * 0.5f;
    MeshBuilder b = mb(mid.x, mid.y, tint);
    float va = v / tile, vb = (v + seg) / tile;
    // seen from above, counter-clockwise: right0 -> right1 -> centre1 -> centre0 for the right half, etc.
    auto P = [&](Vec2 q) { return Vec3{q.x, y, q.y}; };
    upQuad(b, P(r0), P(c0), P(c1), P(r1), {w0, va}, {w0 * 0.5f, va}, {w0 * 0.5f, vb}, {w0, vb}, layer, mudR, mudC, mudC, mudR);
    upQuad(b, P(c0), P(l0), P(l1), P(c1), {w0 * 0.5f, va}, {0, va}, {0, vb}, {w0 * 0.5f, vb}, layer, mudC, mudL, mudL, mudC);
    v += seg;
  }
}

float nodeRadius(int n) const {
  float r = 0;
  for (int e : w_.rnodes[n].edges) r = std::max(r, w_.redges[e].hw);
  return r + (w_.rnodes[n].edges.size() > 2 ? 2.0f : 0.5f);
}

void islandRoadGeometry() {
  for (size_t ei = 0; ei < w_.redges.size(); ++ei) {
    if (edgeGrid_[ei]) {   // grid core: already built; only mark occupancy / raster
      const RoadEdge& e = w_.redges[ei];
      std::vector<Vec2> Lp = offsetLine(e.pts, e.hw + SW), Rp = offsetLine(e.pts, -(e.hw + SW));
      for (size_t i = 0; i + 1 < e.pts.size(); ++i) { Vec2 q[4] = {Rp[i], Rp[i + 1], Lp[i + 1], Lp[i]}; occMark(q, 1); }
      continue;
    }
    RoadEdge& e = w_.redges[ei];
    bool town = edgeTown_[ei] >= 0;
    // trim the ends inside the junction discs
    std::vector<Vec2> pts = e.pts;
    const float y = e.kind >= 2 ? 0.03f : 0.025f;
    std::vector<Vec2> Lp = offsetLine(pts, e.hw), Rp = offsetLine(pts, -e.hw);
    int layer = e.kind == 3 ? mat::dirt_road : mat::asphalt;
    float mudEdge = e.kind == 3 ? 0.85f : (e.kind == 2 ? 0.55f : 0.15f), mudMid = e.kind == 3 ? 0.35f : 0.0f;
    ribbon(Lp, Rp, y, layer, 6.0f, mudEdge, mudMid, mudEdge, e.kind == 3 ? Vec3{0.95f, 0.9f, 0.85f} : Vec3{0.92f, 0.92f, 0.94f});
    // dirt shoulders that fade into the grass on country roads
    if (!town) {
      std::vector<Vec2> L2 = offsetLine(pts, e.hw + 2.2f), R2 = offsetLine(pts, -(e.hw + 2.2f));
      ribbon(L2, Lp, y - 0.008f, mat::dirt_road, 4.0f, 0.2f, 0.6f, 1.0f, {0.9f, 0.86f, 0.8f});
      ribbon(Rp, R2, y - 0.008f, mat::dirt_road, 4.0f, 1.0f, 0.6f, 0.2f, {0.9f, 0.86f, 0.8f});
    }
    // road surface bit + occupancy
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
      Vec2 q[4] = {Rp[i], Rp[i + 1], Lp[i + 1], Lp[i]};
      rasterMark(q, 2);
      occMark(q, 1);
    }
    // centre line (dashed white in town, yellow on the country roads)
    if (e.kind != 3) {
      float s = 0;
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Vec2 a = pts[i], b = pts[i + 1];
        float L = (b - a).length();
        Vec2 t = (b - a) * (1.0f / std::max(L, 1e-3f)), n{-t.y, t.x};
        for (float u = 0; u < L; u += 1.0f) {
          float g = s + u;
          if (std::fmod(g, 6.0f) > 3.0f) continue;
          if (g < nodeRadius(e.a) + 3.0f || g > e.len - nodeRadius(e.b) - 3.0f) continue;
          Vec2 p0 = a + t * u, p1 = a + t * std::min(L, u + 1.0f);
          float hwm = 0.07f;
          Vec3 c = e.kind == 2 ? Vec3{1.0f, 0.82f, 0.1f} : Vec3{0.95f, 0.95f, 0.92f};
          MeshBuilder b2 = mb(p0.x, p0.y, c);
          Vec3 A{p0.x - n.x * hwm, y + 0.006f, p0.y - n.y * hwm}, B{p1.x - n.x * hwm, y + 0.006f, p1.y - n.y * hwm};
          Vec3 C{p1.x + n.x * hwm, y + 0.006f, p1.y + n.y * hwm}, D{p0.x + n.x * hwm, y + 0.006f, p0.y + n.y * hwm};
          upQuad(b2, A, B, C, D, {0, 0}, {1, 0}, {1, 1}, {0, 1}, mat::white, 0, 0, 0, 0);
        }
        s += L;
      }
    }
    // sidewalks with kerbs in town (cut short near junctions)
    if (town) {
      for (float side : {1.0f, -1.0f}) {
        std::vector<Vec2> in = offsetLine(pts, side * e.hw), out = offsetLine(pts, side * (e.hw + SW));
        float s = 0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
          float L = (pts[i + 1] - pts[i]).length();
          bool skip = s < nodeRadius(e.a) + 1.0f || s + L > e.len - nodeRadius(e.b) - 1.0f;
          s += L;
          if (skip) continue;
          Vec2 a0 = in[i], a1 = in[i + 1], b0 = out[i], b1 = out[i + 1];
          Vec2 mid = (a0 + b1) * 0.5f;
          MeshBuilder b = mb(mid.x, mid.y, {0.8f, 0.8f, 0.8f});
          auto P = [&](Vec2 q, float yy) { return Vec3{q.x, yy, q.y}; };
          // top (order chosen so the normal is +y for both sides)
          upQuad(b, P(a0, kH), P(a1, kH), P(b1, kH), P(b0, kH), {0, 0}, {L / 3, 0}, {L / 3, 1}, {0, 1}, mat::sidewalk, 0, 0, 0, 0);
          // kerb faces (both windings: visible from the road whatever the side)
          b.wall(a1.x, a1.y, a0.x, a0.y, 0.0f, kH, mat::concrete, 0, L / 3, 0, 0.05f, 0.9f, 1.0f);
          b.wall(a0.x, a0.y, a1.x, a1.y, 0.0f, kH, mat::concrete, 0, L / 3, 0, 0.05f, 0.9f, 1.0f);
          Vec2 q[4] = {a0, a1, b1, b0};
          rasterMark(q, 1);
          occMark(q, 1);
          // walkable for the navmesh: small squares along the middle of the sidewalk (an AABB of a rotated strip would spill onto
          // the road and send pedestrians into the traffic)
          Vec2 m0 = (a0 + b0) * 0.5f, m1 = (a1 + b1) * 0.5f;
          int nsq = std::max(1, (int)std::ceil(L / 0.9f));
          for (int k = 0; k <= nsq; ++k) {
            Vec2 c = m0 + (m1 - m0) * ((float)k / nsq);
            w_.walkable.push_back({c.x - 0.75f, c.y - 0.75f, c.x + 0.75f, c.y + 0.75f});
          }
        }
      }
    }
  }
  // junction discs
  for (size_t ni = 0; ni < w_.rnodes.size(); ++ni) {
    const RoadNode& n = w_.rnodes[ni];
    bool grid = true;
    for (int e : n.edges) if (!edgeGrid_[e]) grid = false;
    if (grid || n.edges.size() < 2) continue;
    float r = nodeRadius((int)ni) - 0.3f;
    MeshBuilder b = mb(n.p.x, n.p.y, {0.92f, 0.92f, 0.94f});
    const int segs = 16;
    for (int k = 0; k < segs; ++k) {
      float a0 = k * kTau / segs, a1 = (k + 1) * kTau / segs;
      Vec3 c{n.p.x, 0.032f, n.p.y}, p0{n.p.x + std::cos(a0) * r, 0.032f, n.p.y + std::sin(a0) * r}, p1{n.p.x + std::cos(a1) * r, 0.032f, n.p.y + std::sin(a1) * r};
      upQuad(b, c, p1, p0, c, {0, 0}, {r / 6, 0}, {0, r / 6}, {0, 0}, mat::asphalt, 0, 0.1f, 0.1f, 0);
    }
    Vec2 q[4] = {n.p + Vec2{-r, -r}, n.p + Vec2{r, -r}, n.p + Vec2{r, r}, n.p + Vec2{-r, r}};
    rasterMark(q, 2);
    occMark(q, 1);
  }
}

// ---------------------------------------------------------------------------------------------------------------- lots
// rows of lots along both sides of every organic town street, built with plotRow in a rotated frame
void islandLots() {
  for (size_t ei = 0; ei < w_.redges.size(); ++ei) {
    if (edgeGrid_[ei] || edgeTown_[ei] < 0) continue;
    const RoadEdge& e = w_.redges[ei];
    const Town& T = w_.towns[edgeTown_[ei]];
    // cumulative arc length
    std::vector<float> acc{0};
    for (size_t i = 0; i + 1 < e.pts.size(); ++i) acc.push_back(acc.back() + (e.pts[i + 1] - e.pts[i]).length());
    auto at = [&](float s, Vec2& p, Vec2& t) {
      size_t i = 0;
      while (i + 2 < acc.size() && acc[i + 1] < s) ++i;
      float L = std::max(1e-3f, acc[i + 1] - acc[i]);
      float u = clamp((s - acc[i]) / L, 0.0f, 1.0f);
      p = e.pts[i] + (e.pts[i + 1] - e.pts[i]) * u;
      t = (e.pts[i + 1] - e.pts[i]) * (1.0f / L);
    };
    for (float side : {1.0f, -1.0f}) {
      float s = nodeRadius(e.a) + 3.0f;
      const float sEnd = e.len - nodeRadius(e.b) - 3.0f;
      int guard = 0;
      while (s < sEnd - 8.0f) {
        if (++guard > 2000) { LOGW("lot loop stuck: edge %zu s %.2f sEnd %.2f len %.2f", ei, s, sEnd, e.len); break; }
        float L = std::min(rng_.range(22.0f, 34.0f), sEnd - s);
        Vec2 p0, t0, p1, t1, tc;
        // sharp bends: shorter rows (shrink until the row is straight enough)
        for (int g2 = 0; g2 < 50; ++g2) {
          at(s, p0, t0);
          at(s + L, p1, t1);
          tc = (p1 - p0).normalized();
          if ((t0.dot(tc) >= 0.93f && t1.dot(tc) >= 0.93f) || L <= 10.0f) break;
          L *= 0.7f;
        }
        Vec2 chord = (p1 - p0);
        if (chord.length() < 6.0f) { s += std::max(L, 2.0f); continue; }
        Vec2 n{-tc.y, tc.x};
        n = n * side;
        float off = e.hw + SW + 0.3f;
        // frame: local x along t' (perp(t') = n), local +z = n (away from the road)
        Vec2 tp{n.y, -n.x};
        Vec2 a = p0 + n * off, b = p1 + n * off;
        Vec2 origin = (b - a).dot(tp) > 0 ? a : b;
        float len = (b - a).length();
        // deepest free lot depth
        float depth = 0;
        for (float d = 15.0f; d >= 8.0f; d -= 1.0f) {
          Vec2 q[4] = {origin, origin + tp * len, origin + tp * len + n * d, origin + n * d};
          if (occFree(q)) { depth = d; break; }
        }
        if (depth < 8.0f) { s += L * 0.5f; continue; }
        Vec2 q[4] = {origin, origin + tp * len, origin + tp * len + n * depth, origin + n * depth};
        occMark(q, 2);
        rasterMark(q, 1);
        // yaw for setFrame: local x world dir = (c, -s) = tp  ->  c = tp.x, s = -tp.y
        float yaw = std::atan2(-tp.y, tp.x);
        setFrame({origin.x, 0, origin.y}, yaw, kH);
        // the pad: raised ground with a kerb face so the edge reads at street level
        {
          MeshBuilder pb = mb(len * 0.5f, depth * 0.5f, {0.86f, 0.86f, 0.84f});
          pb.box(AABB({0, 0, 0}, {len, kH, depth}), mat::concrete, (T.core ? mat::concrete : mat::grass), 3.0f);
          mapRect(w_.mapWalk, {0, 0, len, depth}, 1);
        }
        float dc = (origin - T.c).length() / T.r;
        int tall = dc < 0.42f ? 1 : 0;
        bool poorTown = !T.core && (edgeTown_[ei] % 2 == 1);
        periphery_ = poorTown || dc > 0.85f;
        rich_ = !poorTown && !periphery_ && w_.landDist(origin.x, origin.y) < 140.0f && rng_.chance(0.7f);
        plotRow(N, 0.0f, len, 0.0f, depth - 1.0f, tall, depth >= 11.0f);
        periphery_ = rich_ = false;
        clearFrame();
        s += L + rng_.range(0.0f, 2.0f);
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------- terrain
float terrainH(float x, float z) const {
  float d = w_.landDist(x, z);
  if (d >= 14.0f) return 0.0f;
  if (d >= 0.0f) return -0.6f + 0.6f * (d / 14.0f);
  return std::max(-8.0f, -0.6f + d * 0.1f);
}
float beachWidth(float x, float z) const {
  float th = std::atan2((z - w_.islandC.y) / w_.islandRz, (x - w_.islandC.x) / w_.islandRx);
  return 22.0f + 12.0f * std::sin(th * 4.0f + 1.3f) + 6.0f * std::sin(th * 9.0f);
}

void islandGround() {
  const RectF& P = w_.playArea;
  const RectF& G = w_.land;
  const float cell = 4.0f;
  int nx = (int)std::ceil(P.w() / cell), nz = (int)std::ceil(P.h() / cell);
  for (int j = 0; j < nz; ++j)
    for (int i = 0; i < nx; ++i) {
      float x0 = P.x0 + i * cell, z0 = P.z0 + j * cell, x1 = x0 + cell, z1 = z0 + cell;
      float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
      if (G.contains(cx, cz) && G.contains(x0 + 0.1f, z0 + 0.1f) && G.contains(x1 - 0.1f, z1 - 0.1f)) continue;   // grid core draws its own
      float d = w_.landDist(cx, cz);
      if (d < -6.0f) continue;   // open sea: the water surface covers it
      float B = beachWidth(cx, cz);
      int layer;
      Vec3 tint{1, 1, 1};
      if (d < B) { layer = mat::sand; tint = d < 7.0f ? Vec3{0.78f, 0.74f, 0.68f} : Vec3{1.0f, 0.98f, 0.94f}; }
      else {
        layer = mat::grass;
        float f = w_.forestAt(cx, cz);
        const Town* T = w_.townAt({cx, cz});
        if (T) tint = {0.92f, 0.95f, 0.82f};
        else tint = lerp(Vec3{0.98f, 1.04f, 0.78f}, Vec3{0.62f, 0.78f, 0.55f}, f);
      }
      float h00 = terrainH(x0, z0), h10 = terrainH(x1, z0), h11 = terrainH(x1, z1), h01 = terrainH(x0, z1);
      MeshBuilder b = mb(cx, cz, tint);
      // grass meeting the sand gets a muddy fringe (soft transition instead of a hard cell edge)
      float m = (layer == mat::grass && d < B + 6.0f) ? 0.45f : 0.0f;
      upQuad(b, {x0, h01, z1}, {x1, h11, z1}, {x1, h10, z0}, {x0, h00, z0}, {x0 / 4, z1 / 4}, {x1 / 4, z1 / 4}, {x1 / 4, z0 / 4}, {x0 / 4, z0 / 4}, layer,
             m, m, m, m);
      if (layer == mat::sand && d > 2.0f) w_.walkable.push_back({x0, z0, x1, z1});
      if (layer == mat::sand) w_.mapSand.push_back({x0, z0, x1, z1});
    }
  // far LOD of the ground: 16 m cells
  const float lc = 16.0f;
  int lx = (int)std::ceil(P.w() / lc), lz = (int)std::ceil(P.h() / lc);
  for (int j = 0; j < lz; ++j)
    for (int i = 0; i < lx; ++i) {
      float x0 = P.x0 + i * lc, z0 = P.z0 + j * lc, x1 = x0 + lc, z1 = z0 + lc;
      float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
      if (G.contains(cx, cz)) continue;
      float d = w_.landDist(cx, cz);
      if (d < -8.0f) continue;
      bool sand = d < beachWidth(cx, cz);
      Vec3 tint = sand ? Vec3{1.0f, 0.97f, 0.92f} : lerp(Vec3{0.98f, 1.04f, 0.78f}, Vec3{0.62f, 0.78f, 0.55f}, w_.forestAt(cx, cz));
      MeshBuilder l = lodb(cx, cz, tint);
      l.groundRect(x0, z0, x1, z1, std::min(0.0f, terrainH(cx, cz)) - 0.02f, sand ? mat::sand : mat::grass, 8.0f);
    }
}

// the sea: strips from the coastline outward; uv.y = metres from the shore (waves and foam in the shader)
void islandSea() {
  const int nTh = 300;
  const float rows[] = {-6.0f, -2.0f, 0.0f, 3.0f, 7.0f, 12.0f, 19.0f, 28.0f, 40.0f, 58.0f, 85.0f, 125.0f, 180.0f, 260.0f};
  const int nRows = (int)(sizeof(rows) / sizeof(rows[0]));
  std::vector<Vec2> coast(nTh), outward(nTh);
  for (int i = 0; i < nTh; ++i) {
    float th = i * kTau / nTh;
    Vec2 dir{std::cos(th) * w_.islandRx, std::sin(th) * w_.islandRz};
    // bisection on the ray for the water line (where the sand dips under the water level)
    float lo = 0.2f, hi = 2.0f;
    for (int it = 0; it < 30; ++it) {
      float m = (lo + hi) * 0.5f;
      Vec2 p = w_.islandC + dir * m;
      if (terrainH(p.x, p.y) > w_.waterLevel) lo = m; else hi = m;
    }
    coast[i] = w_.islandC + dir * lo;
    Vec2 o = dir.normalized();
    outward[i] = o;
  }
  for (int i = 0; i < nTh; ++i) {
    int i2 = (i + 1) % nTh;
    for (int r = 0; r + 1 < nRows; ++r) {
      float d0 = rows[r], d1 = rows[r + 1];
      Vec2 a = coast[i] + outward[i] * d0, b = coast[i2] + outward[i2] * d0, c = coast[i2] + outward[i2] * d1, e = coast[i] + outward[i] * d1;
      Vec3 A{a.x, w_.waterLevel, a.y}, Bv{b.x, w_.waterLevel, b.y}, C{c.x, w_.waterLevel, c.y}, E{e.x, w_.waterLevel, e.y};
      Vec2 mid = (a + c) * 0.5f;
      MeshBuilder m = mb(mid.x, mid.y, {1, 1, 1});
      float u0 = i * 2.0f, u1 = (i + 1) * 2.0f;
      Vec2 ua{u0, std::max(0.0f, d0)}, ub{u1, std::max(0.0f, d0)}, uc{u1, std::max(0.0f, d1)}, ud{u0, std::max(0.0f, d1)};
      Vec3 nrm = (E - A).cross(Bv - A);
      if (nrm.y > 0) m.quad(A, E, C, Bv, ua, ud, uc, ub, mat::water);
      else m.quad(A, Bv, C, E, ua, ub, uc, ud, mat::water);
    }
  }
  w_.mapWater.push_back(w_.playArea);
}

// ---------------------------------------------------------------------------------------------------------------- vegetation & life
// a tree for the deep woods: the low-poly crown variant goes into both the near and the far mesh (thousands of these)
int cheapTrees_ = 0, fullTrees_ = 0;
void woodsTree(float x, float z, int species) {
  float s = rng_.range(0.85f, 1.25f), yaw = rng_.range(0.0f, kTau);
  Vec3 pos{x, 0.0f, z};
  MeshData scratch;
  MeshBuilder dummy(&scratch);
  MeshBuilder nearB = mb(x, z);
  buildTree3D(dummy, &nearB, rng_, species, pos, yaw, s);
  MeshData scratch2;
  MeshBuilder dummy2(&scratch2);
  MeshBuilder farB = lodb(x, z);
  buildTree3D(dummy2, &farB, rng_, species, pos, yaw, s);
  collider(AABB({x - 0.4f, 0, z - 0.4f}, {x + 0.4f, 3.0f, z + 0.4f}), ColKind::Tree);
  ++cheapTrees_;
}

void islandVegetation() {
  const RectF& P = w_.playArea;
  const float cell = 11.0f;
  for (float z = P.z0; z < P.z1; z += cell)
    for (float x = P.x0; x < P.x1; x += cell) {
      float px = x + rng_.range(0.0f, cell), pz = z + rng_.range(0.0f, cell);
      float d = w_.landDist(px, pz);
      float B = beachWidth(px, pz);
      if (d < B + 4.0f) continue;
      if (occAt(px, pz) != 0 || occAt(px + 2.5f, pz) != 0 || occAt(px - 2.5f, pz) != 0 || occAt(px, pz + 2.5f) != 0 || occAt(px, pz - 2.5f) != 0) continue;
      if (w_.land.inflated(4.0f).contains(px, pz)) continue;
      const Town* T = w_.townAt({px, pz});
      float f = w_.forestAt(px, pz);
      float p = T ? 0.08f : (0.05f + 0.85f * f);
      if (!rng_.chance(p)) continue;
      float r = rng_.uni();
      // far from any road the woods use the cheap crowns; along the roads and in town the full trees
      float roadD = (w_.nearestRoadPointNet({px, pz}) - Vec2{px, pz}).length();
      if (roadD > 28.0f && !T) {
        int sp = r < 0.35f ? 0 : (r < 0.5f ? 1 : (r < 0.6f ? 2 : (r < 0.8f ? 6 : (r < 0.9f ? 7 : 5))));
        woodsTree(px, pz, sp);
        continue;
      }
      ++fullTrees_;
      if (f > 0.5f && !T) {
        if (r < 0.28f) tree(px, pz, 2);
        else if (r < 0.55f) tree(px, pz, 0, 0);
        else if (r < 0.7f) tree(px, pz, 0, rng_.chance(0.5f) ? 1 : 2);
        else if (r < 0.88f) tree(px, pz, 0, 6);
        else tree(px, pz, 0, 7);
      } else {
        if (r < 0.4f) tree(px, pz, 2);
        else tree(px, pz, 0);
      }
    }
  // palms and beach furniture along the coast; parasols and kiosks near towns
  const int nTh = 420;
  float lastKiosk = -1e9f;
  for (int i = 0; i < nTh; ++i) {
    float th = i * kTau / nTh;
    Vec2 dir{std::cos(th) * w_.islandRx, std::sin(th) * w_.islandRz};
    float lo = 0.2f, hi = 2.0f;
    for (int it = 0; it < 24; ++it) { float m = (lo + hi) * 0.5f; Vec2 p = w_.islandC + dir * m; if (w_.landDist(p.x, p.y) > 0) lo = m; else hi = m; }
    Vec2 coast = w_.islandC + dir * lo;
    Vec2 in = (w_.islandC - coast).normalized();
    float B = beachWidth(coast.x, coast.y);
    float arc = th * (w_.islandRx + w_.islandRz) * 0.5f;
    float nearTown = 1e9f;
    for (const Town& T : w_.towns) nearTown = std::min(nearTown, (coast - T.c).length() - T.r);
    float sy = std::atan2(-in.x, -in.y);   // facing the sea
    if (i % 3 == 0) {
      Vec2 p = coast + in * (B - rng_.range(1.5f, 4.0f));
      if (occAt(p.x, p.y) == 0) tree(p.x, p.y, 1, rng_.chance(0.75f) ? 3 : 4);
    }
    if (nearTown < 170.0f && i % 2 == 0) {
      Vec2 p = coast + in * rng_.range(8.0f, B - 6.0f) + Vec2{-in.y, in.x} * rng_.range(-2.0f, 2.0f);
      float y = terrainH(p.x, p.y);
      if (y > -0.2f) {
        decor(DecorKind::Prop, "prop_guardasol", 8, {p.x, y, p.y}, rng_.range(0, kTau), 1.0f, true, 0.12f, 2.4f, -1, 7);
        for (int c = 0; c < rng_.irange(1, 2); ++c) {
          Vec2 q = p + Vec2{-in.y, in.x} * (c * 1.2f - 0.6f) - in * 1.4f;
          decor(DecorKind::Prop, "prop_cadeira", 8, {q.x, terrainH(q.x, q.y), q.y}, sy + rng_.range(-0.4f, 0.4f), 1.0f, false, 0.3f, 0.8f, -1, 8);
        }
        if (rng_.chance(0.5f)) { Vec2 q = p + in * -2.0f; decor(DecorKind::Prop, "prop_toalha", 8, {q.x, terrainH(q.x, q.y), q.y}, rng_.range(0, kTau), 1.0f, false, 0.4f, 0.1f, -1, 24); }
      }
      if (arc - lastKiosk > 75.0f) {
        Vec2 k = coast + in * (B - 5.0f);
        if (occAt(k.x, k.y) == 0) {
          decor(DecorKind::Prop, "prop_quiosque", 8, {k.x, terrainH(k.x, k.y), k.y}, sy, 1.0f, true, 1.6f, 3.0f, -1, 9);
          w_.poiList.push_back({k.x - in.x * 3.0f, 0.0f, k.y - in.y * 3.0f});
          Vec2 lg = coast + in * 9.0f;
          decor(DecorKind::Prop, "prop_salvavidas", 8, {lg.x, terrainH(lg.x, lg.y), lg.y}, sy, 1.0f, true, 1.0f, 3.5f, -1, 15);
          lastKiosk = arc;
          if (w_.poiBeach.x == 0 && w_.poiBeach.z == 0) w_.poiBeach = {coast.x + in.x * 12.0f, 0.0f, coast.y + in.y * 12.0f};
        }
      }
    }
  }
  if (w_.poiBeach.x == 0 && w_.poiBeach.z == 0) {
    Vec2 dir = (w_.towns[0].c - w_.islandC).normalized();
    float lo = 0.2f, hi = 3.0f;
    Vec2 d2{dir.x * w_.islandRx, dir.y * w_.islandRz};
    for (int it = 0; it < 24; ++it) { float m = (lo + hi) * 0.5f; Vec2 p = w_.islandC + d2 * m; if (w_.landDist(p.x, p.y) > 0) lo = m; else hi = m; }
    Vec2 c = w_.islandC + d2 * lo;
    w_.poiBeach = {c.x - dir.x * 12.0f, 0.0f, c.y - dir.y * 12.0f};
  }
  w_.poiList.push_back(w_.poiBeach);
}

// lamps on the organic town sidewalks, farmhouses at the end of the dirt tracks, pedestrians and destinations in every town
void islandLife() {
  static const char* arch[] = {"mulher_rosa", "homem_polo", "jovem_moletom", "mulher_vestido", "corredor", "vizinho"};
  int lampN = 0;
  for (size_t ei = 0; ei < w_.redges.size(); ++ei) {
    if (edgeGrid_[ei] || edgeTown_[ei] < 0) continue;
    const RoadEdge& e = w_.redges[ei];
    float s = 0, next = rng_.range(6.0f, 14.0f);
    for (size_t i = 0; i + 1 < e.pts.size(); ++i) {
      Vec2 a = e.pts[i], b = e.pts[i + 1];
      float L = (b - a).length();
      Vec2 t = (b - a) * (1.0f / std::max(L, 1e-3f)), n{-t.y, t.x};
      while (next < s + L) {
        Vec2 p = a + t * (next - s);
        float side = ((lampN++) % 2) ? 1.0f : -1.0f;
        Vec2 q = p + n * side * (e.hw + 0.45f);
        if (next > nodeRadius(e.a) + 2.0f && next < e.len - nodeRadius(e.b) - 2.0f) {
          Vec2 face = n * -side;
          float yaw = std::atan2(face.x, face.y);
          decor(DecorKind::Prop, "prop_poste", 8, {q.x, kH, q.y}, yaw, 1.0f, true, 0.18f, 7.0f, -1, 2);
          w_.lampLights.push_back({q.x + face.x * 1.7f, 7.0f, q.y + face.y * 1.7f});
          // a street tree on the outer edge of the sidewalk between the lamps
          Vec2 tr = p + n * -side * (e.hw + SW - 0.6f) + t * 6.0f;
          if (rng_.chance(0.45f)) tree(tr.x, tr.y, rng_.chance(0.2f) ? 1 : 0);
          // pedestrians
          if (rng_.chance(0.16f)) {
            Vec2 sp = p + n * side * (e.hw + 1.3f);
            w_.npcs.push_back({arch[rng_.irange(0, 5)], 0, {sp.x, kH, sp.y}, rng_.range(0.0f, kTau)});
          }
        }
        next += rng_.range(22.0f, 30.0f);
      }
      s += L;
    }
  }
  for (const Town& T : w_.towns) if (!T.core) w_.poiList.push_back({T.c.x, kH, T.c.y});
  // farmhouses
  for (const auto& [p, yaw] : farmSpots_) {
    Vec2 fwd{std::sin(yaw), std::cos(yaw)};
    Vec2 o = p + fwd * 6.0f;
    float y2 = std::atan2(fwd.x, fwd.y);
    (void)y2;
    setFrame({o.x - 5.0f, 0, o.y - 4.0f}, 0.0f, kH);
    Facade f{rng_.chance(0.5f) ? mat::house_periferia_a : mat::house_yellow, {0.85f, 0.7f, 0.5f}, 3.6f, false, 4.0f};
    f.roofLayer = rng_.chance(0.5f) ? mat::tin : -1;
    houseBuilding({0, 0, 10, 8}, S, f, false);
    clearFrame();
    Vec2 q[4] = {o + Vec2{-6, -5}, o + Vec2{6, -5}, o + Vec2{6, 5}, o + Vec2{-6, 5}};
    occMark(q, 2);
    for (int k = 0; k < 5; ++k) tree(o.x + rng_.range(-14.0f, 14.0f), o.y + rng_.range(-14.0f, 14.0f), rng_.chance(0.5f) ? 0 : 2);
  }
}

// heightAt for the core's raised blocks is still answered by lowRects; mark the core's block surfaces too so the raster is complete
void islandFinish() {
  // the old grid helpers expect the land rect to be the core only
  for (const RectF& r : w_.lowRects) {
    Vec2 q[4] = {{r.x0, r.z0}, {r.x1, r.z0}, {r.x1, r.z1}, {r.x0, r.z1}};
    rasterMark(q, 2);
  }
}

void runIsland() {
  island_ = true;
  auto t0 = std::chrono::steady_clock::now();
  auto step = [&](const char* name) {
    auto t1 = std::chrono::steady_clock::now();
    LOGI("  island gen: %-12s %6.0f ms", name, std::chrono::duration<double, std::milli>(t1 - t0).count());
    t0 = t1;
  };
  plan(); islandPlan(); step("plan");
  blocks(); streets(); step("core");
  islandRoads(); step("roads");
  islandRoadGeometry(); step("road mesh");
  islandLots(); step("lots");
  furniture(); step("furniture");
  islandGround(); step("ground");
  islandSea(); step("sea");
  if (!getenv("GTABR_NOVEG")) islandVegetation();
  step("vegetation");
  islandLife(); step("life");
  boundary();
  interiors();
  population();
  streetsParked();
  islandFinish(); step("finish");
  for (int k = 0; w_.pickupSpots.size() < 8 && k < 64; ++k) {
    const RectF& B = w_.blocks[rng_.irange(0, (int)w_.blocks.size() - 1)].first;
    w_.pickupSpots.push_back({rng_.range(B.x0 + 6, B.x1 - 6), kH, B.z0 + 1.2f});
  }
  LOGI("island '%s': %zu towns, %zu road nodes, %zu road edges, trees %d full + %d woods", w_.islandName.c_str(), w_.towns.size(), w_.rnodes.size(),
       w_.redges.size(), fullTrees_, cheapTrees_);
}
