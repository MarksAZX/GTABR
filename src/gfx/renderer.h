// Vulkan renderer: shadow pass -> scene pass (sky, world meshes, decals, billboards) -> Dual-Kawase blur
// -> composite + UI pass to the swapchain (or an offscreen target in headless mode).
#pragma once
#include <memory>
#include <vector>

#include "../core/math.h"
#include "vk.h"

namespace gtabr {
namespace gfx {

#pragma pack(push, 1)
struct WorldVertex {
  float p[3];
  int8_t n[4];
  float uv[2];
  uint32_t color;  // rgb tint, a = baked AO
  float layer;     // layer in the material texture array
};
#pragma pack(pop)
static_assert(sizeof(WorldVertex) == 32, "WorldVertex layout");

struct SpriteInst {
  float pos[3];
  float size[2];
  float pivot[2];
  float uv[4];
  uint32_t tint;
  float extra;
};
static_assert(sizeof(SpriteInst) == 52, "SpriteInst layout");

struct DecalInst {
  float pos[3];
  float yaw;
  float half[2];
  float alpha;
  float kind;  // 0 = ellipse, 1 = rounded box
};
static_assert(sizeof(DecalInst) == 32, "DecalInst layout");

enum UiKind : int {
  kUiRect = 0, kUiImage = 1, kUiText = 2, kUiArc = 3, kUiMap = 4, kUiGradient = 5, kUiGlow = 6, kUiHGradient = 7, kUiPortrait = 8
};
struct UiInst {
  float rect[4];
  float uv[4];
  uint32_t color, color2;
  float radius, p0, p1, p2;
  float p3, kind;
};
static_assert(sizeof(UiInst) == 64, "UiInst layout");

struct LightUBO {
  Vec4 posRadius;  // xyz position, w radius
  Vec4 colorInt;   // rgb colour * intensity
  Vec4 dirCone;    // xyz spot direction, w = cos(cone) or -2 for point lights
};
constexpr int kMaxLights = 128;       // dynamic lights per frame (culled into screen tiles)
constexpr int kTilesX = 16, kTilesY = 9, kTileCap = 32;
constexpr int kMaxBones = 64;
// Mirrors shaders/globals.glsl (std140).
struct GlobalsUBO {
  Mat4 viewProj, view, invViewProj, lightViewProj[2];
  Vec4 camPos, camRight, camUp, camFwd;
  Vec4 sunDir, sunColor, ambSky, ambGround, fog, params;
  Vec4 sky0, sky1, cascade, lightInfo;
  Vec4 probeRect;   // x0, z0, 1/width, 1/depth of the ambient probe grid (world metres)
  Vec4 probeInfo;   // x = enabled, y = ground layer height offset, z = rooftop layer height offset, w = unused
  Vec4 lightGrid;   // x = light count, y = tiles per pixel (x), z = tiles per pixel (y), w = tiles in x
  Mat4 prevViewProj;   // previous frame's camera, for motion blur reprojection
  Vec4 post;           // x = motion blur, y = contact shadows, z = sharpening, w = volumetric clouds (0 / 1)
  Vec4 taa;            // x = history weight (0 = off / reset), yz = sub-pixel jitter (uv), w = unused
  Vec4 look;           // x = film grain, y = chromatic aberration, z = vignette scale, w = golden-hour grade amount
};

enum class TexFormat : uint32_t { RGBA8_SRGB = 0, ASTC6x6_SRGB = 1, RGBA8_UNORM = 2, R8_UNORM = 3, ASTC6x6_UNORM = 4, ASTC8x8_SRGB = 5 };
enum class SamplerKind { Repeat, ClampLinear, ClampNearest };

struct TextureData {
  TexFormat format = TexFormat::RGBA8_SRGB;
  uint32_t width = 0, height = 0, layers = 1, mips = 1;
  std::vector<uint8_t> bytes;  // mip-major, then layer
};

struct TexHandle { int id = -1; bool valid() const { return id >= 0; } };
struct MeshHandle { int id = -1; bool valid() const { return id >= 0; } };
struct ModelHandle { int id = -1; bool valid() const { return id >= 0; } };
struct MaterialHandle { int id = -1; bool valid() const { return id >= 0; } };

// Vertex of an imported 3D model (matches gmesh::Vertex, 36 bytes).
#pragma pack(push, 1)
struct ModelVertex {
  float p[3];
  int8_t n[4];
  int8_t t[4];
  float uv[2];
  uint8_t j[4];
  uint8_t w[4];
};
#pragma pack(pop)
static_assert(sizeof(ModelVertex) == 36, "ModelVertex layout");

struct ModelLod { uint32_t firstIndex = 0, indexCount = 0; };

// One draw of an imported model (car body, wheel, character...).
struct ModelDraw {
  ModelHandle model;
  MaterialHandle material;
  int lod = 0;
  Mat4 transform;
  Vec4 tint{1, 1, 1, 0};      // rgb paint colour, a = recolour amount
  Vec4 params{0, 0, 1, 1};    // x emissive, y clear coat, z roughness scale, w unused
  int boneOffset = -1;        // first matrix in FrameData::bones (skinned models)
  bool castShadow = true;
};

struct Batch { TexHandle tex; uint32_t first = 0, count = 0; };

struct FrameData {
  GlobalsUBO globals{};
  std::vector<int> worldMeshes;   // visible meshes for the camera
  std::vector<int> shadowMeshes;  // meshes touching the light frustum
  bool drawShadows = true;
  std::vector<SpriteInst> sprites;
  std::vector<Batch> spriteBatches;
  std::vector<SpriteInst> silhouettes;
  std::vector<Batch> silhouetteBatches;
  std::vector<DecalInst> decals;
  std::vector<UiInst> ui;
  std::vector<Batch> uiBatches;
  std::vector<ModelDraw> models;
  // dialogue portrait: a live 3D character rendered into a small offscreen target with its own camera and lighting
  bool portraitActive = false;
  GlobalsUBO portraitGlobals{};
  Vec4 portraitBg{0.10f, 0.115f, 0.14f, 1.0f};
  std::vector<ModelDraw> portraitModels;   // bone palettes live in 'bones' like every other skinned draw
  std::vector<LightUBO> lights;   // up to kMaxLights dynamic point / spot lights; the renderer culls them into screen tiles
  std::vector<Mat4> bones;        // skinning palettes, kMaxBones matrices per skinned draw
  MaterialHandle worldMaterial;   // albedo array + normal/roughness array
  float blur = 0, fade = 0, vignette = 0.28f, dim = 0;
  // HDR post: exposure, bloom, grading
  float exposure = 1.0f, bloom = 0.06f, bloomThreshold = 1.0f;
  Vec4 lift{0, 0, 0, 1};          // rgb lift, w = saturation
  Vec4 gain{1, 1, 1, 1};          // rgb gain, w = contrast
  int shadowCascades = 2;
  // screen-space AO + volumetric light shafts (both need the scene depth)
  float nearZ = 1.0f, farZ = 520.0f, tanHalfX = 1.0f, tanHalfY = 0.5f;
  float aoStrength = 0.0f, aoRadius = 0.9f;        // 0 = pass skipped
  // screen-space reflections on wet ground
  float wetness = 0.0f;                            // 0 = pass skipped
  Vec3 upView{0, 1, 0};                            // world up expressed in the camera basis (x right, y screen-down, z forward)
  float shaftIntensity = 0.0f;                     // 0 = no light shafts
  Vec2 sunUV{0.5f, 0.0f};
  Vec3 shaftColor{1.0f, 0.85f, 0.6f};
  void clear() {
    lights.clear();
    worldMeshes.clear(); shadowMeshes.clear(); sprites.clear(); spriteBatches.clear(); silhouettes.clear();
    silhouetteBatches.clear(); decals.clear(); ui.clear(); uiBatches.clear(); models.clear(); bones.clear();
  }
};

struct RendererConfig {
  bool headless = false;
  uint32_t headlessWidth = 1280, headlessHeight = 576;
  float renderScale = 1.0f;
  int shadowMapSize = 2048;
  bool validation = false;
};

class Renderer {
 public:
  static constexpr uint32_t kPortraitSize = 384;
  TexHandle portraitTexture() const { return portraitTex_; }   // HDR image of the dialogue portrait (valid after init)
  bool init(const RendererConfig& cfg, const SurfaceFactory& surfaceFactory, const std::vector<const char*>& instExts);
  void shutdown();

  // (Re)creates the swapchain; call when the window is created or resized.
  bool createSwapchain(uint32_t w, uint32_t h);
  void destroySwapchain();
  // Android: the native window was recreated; build a new surface (the device is kept).
  bool resetSurface(const SurfaceFactory& factory);
  bool hasSwapchain() const { return swapchain_ != VK_NULL_HANDLE || cfg_.headless; }

  TexHandle createTexture(const TextureData& td, SamplerKind sampler);
  // Ambient visibility probes (2D array, 4 layers); pass an invalid handle to switch them off.
  void setProbeGrid(TexHandle t);
  TexHandle createTextureRGBA(uint32_t w, uint32_t h, const uint8_t* rgba, bool srgb, bool mips, SamplerKind sampler);
  MeshHandle createMesh(const WorldVertex* v, size_t nv, const uint32_t* idx, size_t ni);
  void destroyMesh(MeshHandle h);
  // Hands the mesh's GPU memory back once the frames that may still reference it have finished (never stalls the GPU).
  void retireMesh(MeshHandle h);
  void destroyTexture(TexHandle h);
  ModelHandle createModel(const ModelVertex* v, size_t nv, const uint32_t* idx, size_t ni, const ModelLod* lods, int lodCount,
                          bool skinned);
  // World material: two texture arrays. Model material: albedo, normal, ORM 2D textures (invalid -> neutral defaults).
  MaterialHandle createWorldMaterial(TexHandle albedoArray, TexHandle normalArray);
  MaterialHandle createModelMaterial(TexHandle albedo, TexHandle normal, TexHandle orm);
  void setShadowMapSize(int size);
  bool hdr() const { return hdrFormat_ != VK_FORMAT_R8G8B8A8_UNORM; }

  void setRenderScale(float s);
  float renderScale() const { return cfg_.renderScale; }
  void setShadowsEnabled(bool e) { shadowsEnabled_ = e; }

  // Renders one frame. Returns false if the frame was skipped (swapchain out of date etc).
  bool renderFrame(const FrameData& fd);
  // Headless/test: reads back the last presented frame as RGBA8 (width*height*4). Must be called after renderFrame.
  bool readback(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
  void requestReadback() { wantReadback_ = true; }

  uint32_t outputWidth() const { return outW_; }
  uint32_t outputHeight() const { return outH_; }
  const DeviceCaps& caps() const { return ctx_.caps; }
  VkCtx& ctx() { return ctx_; }
  float lastGpuFrameMs() const { return 0; }

 private:
  struct MeshRes { Buffer vb, ib; uint32_t indexCount = 0; bool alive = false; };
  struct ModelRes { Buffer vb, ib; ModelLod lods[3]; int lodCount = 0; bool skinned = false; };
  struct TexRes { Image img; VkSampler sampler = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; bool alive = false; };
  struct FrameRes {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    Buffer arena;
    Buffer lightBuf;   // lights + per-tile light lists (storage buffer, binding 3 of globalsB)
    VkDescriptorSet globalsA = VK_NULL_HANDLE, globalsB = VK_NULL_HANDLE, bones = VK_NULL_HANDLE;
    VkDeviceSize arenaOffset = 0;
  };
  struct BlurLevel { Image img; VkFramebuffer fb = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; uint32_t w = 0, h = 0; };

  bool createPasses();
  bool createLayoutsAndPipelines();
  VkPipeline buildPipeline(const char* vs, const char* fs, VkRenderPass rp, VkPipelineLayout layout, int vertexKind,
                           bool depthTest, bool depthWrite, VkCompareOp depthOp, bool blend, VkCullModeFlags cull,
                           bool depthBias = false, VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  void createRenderTargets();
  void destroyRenderTargets();
  void createOffscreenTargets();
  void destroyOffscreenTargets();
  bool acquire(uint32_t& imageIndex, FrameRes& fr);
  void* arenaAlloc(FrameRes& fr, size_t size, VkDeviceSize* outOffset);
  VkDescriptorSet allocTexSet(VkImageView view, VkSampler samp);
  VkSampler getSampler(SamplerKind k);
  void drawModels(VkCommandBuffer cb, FrameRes& fr, const FrameData& fd, VkDeviceSize boneBase, bool shadow, int cascade,
                  const std::vector<ModelDraw>* list = nullptr);
  void createPortraitTarget();
  void destroyPortraitTarget();
  void writeGlobals(VkCommandBuffer cb, FrameRes& fr, const GlobalsUBO& g);
  void drawBatches(VkCommandBuffer cb, FrameRes& fr, const std::vector<Batch>& batches, const void* data, size_t stride,
                   VkPipeline pipe, VkPipelineLayout layout, uint32_t vertsPerInst, int setIndex);

  RendererConfig cfg_;
  VkCtx ctx_;
  bool shadowsEnabled_ = true;
  bool wantReadback_ = false;

  // swapchain / output
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat outFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
  bool outIsSrgb_ = false;
  uint32_t outW_ = 0, outH_ = 0;
  std::vector<VkImage> outImages_;
  std::vector<VkImageView> outViews_;
  std::vector<VkFramebuffer> outFbs_;
  Image headlessImages_[2];
  std::vector<VkSemaphore> renderDone_;
  Buffer readbackBuf_;
  uint32_t lastImage_ = 0;

  // passes
  VkRenderPass shadowPass_ = VK_NULL_HANDLE, scenePass_ = VK_NULL_HANDLE, blurPass_ = VK_NULL_HANDLE,
               compositePass_ = VK_NULL_HANDLE, aoPass_ = VK_NULL_HANDLE;
  VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT, shadowFormat_ = VK_FORMAT_D32_SFLOAT;

  // targets
  Image shadowMap_, sceneColor_, sceneDepth_;
  VkImageView shadowLayerViews_[2] = {};
  VkFramebuffer shadowFbs_[2] = {};
  VkFramebuffer sceneFb_ = VK_NULL_HANDLE;
  VkFormat hdrFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
  uint32_t sceneW_ = 0, sceneH_ = 0;
  std::vector<BlurLevel> blurDown_, blurUp_;
  // half-resolution AO / sky-mask targets (A = raw, B = blurred, sampled by the composite)
  Image aoA_, aoB_;
  VkFramebuffer aoFbA_ = VK_NULL_HANDLE, aoFbB_ = VK_NULL_HANDLE;
  VkDescriptorSet aoDepthSet_ = VK_NULL_HANDLE, aoBlurSet_ = VK_NULL_HANDLE;
  uint32_t aoW_ = 0, aoH_ = 0;
  bool aoSupported_ = false;
  TexHandle probeTex_;
  void bindProbeSets();
  VkSampler shadowSampler_ = VK_NULL_HANDLE;
  VkDescriptorSet sceneSet_ = VK_NULL_HANDLE;  // composite set (scene + blurred)
  VkDescriptorSet sceneSampleSet_ = VK_NULL_HANDLE;  // scene only (blur chain input)
  VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;

  // descriptors / pipelines
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout layoutGlobalsA_ = VK_NULL_HANDLE, layoutGlobalsB_ = VK_NULL_HANDLE, layoutEmpty_ = VK_NULL_HANDLE,
                        layoutTex_ = VK_NULL_HANDLE, layoutTex2_ = VK_NULL_HANDLE, layoutTex3_ = VK_NULL_HANDLE, layoutTex4_ = VK_NULL_HANDLE,
                        layoutBones_ = VK_NULL_HANDLE;
  VkPipelineLayout plWorld_ = VK_NULL_HANDLE, plSprite_ = VK_NULL_HANDLE, plShadow_ = VK_NULL_HANDLE, plMesh_ = VK_NULL_HANDLE,
                   plUi_ = VK_NULL_HANDLE, plBlur_ = VK_NULL_HANDLE, plComposite_ = VK_NULL_HANDLE, plAo_ = VK_NULL_HANDLE;
  VkPipeline pipeWorld_ = VK_NULL_HANDLE, pipeShadow_ = VK_NULL_HANDLE, pipeSprite_ = VK_NULL_HANDLE,
             pipeSilhouette_ = VK_NULL_HANDLE, pipeDecal_ = VK_NULL_HANDLE, pipeSky_ = VK_NULL_HANDLE,
             pipeUi_ = VK_NULL_HANDLE, pipeBlurDown_ = VK_NULL_HANDLE, pipeBlurUp_ = VK_NULL_HANDLE,
             pipeComposite_ = VK_NULL_HANDLE, pipeAo_ = VK_NULL_HANDLE, pipeAoBlur_ = VK_NULL_HANDLE, pipeMesh_ = VK_NULL_HANDLE, pipeMeshSkinned_ = VK_NULL_HANDLE,
             pipeShadowMesh_ = VK_NULL_HANDLE, pipeShadowSkinned_ = VK_NULL_HANDLE;
  VkSampler samplers_[3] = {};
  std::vector<VkShaderModule> shaderModules_;

  static constexpr int kFrames = 2;
  FrameRes frames_[kFrames];
  uint32_t frameIndex_ = 0;
  uint64_t frameCounter_ = 0;
  struct Retired { Buffer vb, ib; uint64_t frame; };
  std::vector<Retired> retired_;

  std::vector<MeshRes> meshes_;
  std::vector<TexRes> textures_;
  Image portraitColor_, portraitDepth_;
  VkFramebuffer portraitFb_ = VK_NULL_HANDLE;
  TexHandle portraitTex_;
  Image taaHist_[2];
  VkFramebuffer taaFb_[2] = {};
  VkDescriptorSet taaSet_[2] = {}, taaSceneSet_[2] = {}, taaCompositeSet_[2] = {};
  VkPipeline pipeTaa_ = VK_NULL_HANDLE;
  bool taaHistValid_ = false;
  std::vector<ModelRes> models_;
  std::vector<VkDescriptorSet> materials_;
  TexHandle dummyTex_, flatNormalTex_, defaultOrmTex_, dummyArray_, flatNormalArray_;

 public:
  // Used by the game to bind mesh draws; exposed for the scene code.
  size_t meshCount() const { return meshes_.size(); }
};

bool findEmbeddedShader(const char* name, const uint32_t** code, size_t* sizeBytes);

}  // namespace gfx
}  // namespace gtabr
