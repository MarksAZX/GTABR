// Binary model / animation formats produced by tools/meshconv and loaded by the runtime.
//
// .gmesh  'GMS2' header, bones, LODs (vertices + indices), metadata (wheels / light anchors) and texture names.
// .ganim  'GAN1' sampled local TRS per bone at a fixed rate; bones are matched by name at load time.
#pragma once
#include <cstdint>

namespace gtabr {
namespace gmesh {

constexpr uint32_t kMagicMesh = 0x32534D47;  // "GMS2"
constexpr uint32_t kMagicAnim = 0x314E4147;  // "GAN1"
constexpr int kMaxLods = 3;
constexpr int kNameLen = 48;

enum Flags : uint32_t { kSkinned = 1, kVehicle = 2 };

#pragma pack(push, 1)
struct Vertex {          // 36 bytes
  float p[3];
  int8_t n[4];           // normal (snorm)
  int8_t t[4];           // tangent xyz + handedness w (snorm)
  float uv[2];
  uint8_t j[4];          // joint indices (skinned only)
  uint8_t w[4];          // joint weights (unorm)
};
struct Header {
  uint32_t magic, version, flags;
  uint32_t boneCount, lodCount;
  float bmin[3], bmax[3];
  float lodDistance[kMaxLods];   // switch distances (metres) suggested by the converter
  char textures[4][kNameLen];    // albedo, normal, orm, emissive ("" if none)
  // vehicle metadata (object space, after normalisation): wheels FL FR RL RR and light anchors
  float wheel[4][3];
  float wheelRadius, wheelWidth;
  float headlight[2][3], taillight[2][3];
  float animRootScale;           // multiply sampled root translation/scale of shared clips (skeleton re-used at another height)
  float pad[3];
};
struct Bone {
  char name[kNameLen];
  int32_t parent;
  float invBind[16];             // column major
  float t[3], r[4], s[3];        // rest local transform (r = quaternion xyzw)
};
struct LodHeader {
  uint32_t vertexCount, indexCount;
};
struct AnimHeader {
  uint32_t magic, boneCount, frameCount;
  float fps;
  float rootMotion;              // metres per cycle covered by the clip (for stride matching), 0 if in-place
  char name[kNameLen];
};
// followed by boneCount names (kNameLen each) then frameCount * boneCount * 10 floats (t3 r4 s3)
#pragma pack(pop)
static_assert(sizeof(Vertex) == 36, "gmesh vertex");

}  // namespace gmesh
}  // namespace gtabr
