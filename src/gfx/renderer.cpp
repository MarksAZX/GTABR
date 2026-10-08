#include "renderer.h"

#include <algorithm>
#include <cstring>

namespace gtabr {
namespace gfx {

namespace {
constexpr VkDeviceSize kArenaSize = 6 * 1024 * 1024;
constexpr VkDeviceSize kLightBufSize = sizeof(LightUBO) * kMaxLights + 4 * kTilesX * kTilesY * (1 + kTileCap);

VkShaderModule loadShader(VkCtx& ctx, const char* name, std::vector<VkShaderModule>& keep) {
  const uint32_t* code = nullptr;
  size_t size = 0;
  if (!findEmbeddedShader(name, &code, &size)) {
    LOGE("Missing embedded shader %s", name);
    return VK_NULL_HANDLE;
  }
  VkShaderModule m = ctx.createShader(code, size);
  keep.push_back(m);
  return m;
}

size_t texelBytes(TexFormat f, uint32_t w, uint32_t h) {
  switch (f) {
    case TexFormat::ASTC6x6_SRGB:
    case TexFormat::ASTC6x6_UNORM: return (size_t)((w + 5) / 6) * ((h + 5) / 6) * 16;
    case TexFormat::ASTC8x8_SRGB: return (size_t)((w + 7) / 8) * ((h + 7) / 8) * 16;
    case TexFormat::R8_UNORM: return (size_t)w * h;
    default: return (size_t)w * h * 4;
  }
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
bool Renderer::init(const RendererConfig& cfg, const SurfaceFactory& surfaceFactory, const std::vector<const char*>& exts) {
  cfg_ = cfg;
  if (!ctx_.init(cfg.headless, surfaceFactory, exts, cfg.validation)) return false;
  VkDevice dev = ctx_.device;

  // depth formats
  auto fmtOk = [&](VkFormat f, VkFormatFeatureFlags feat) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(ctx_.phys, f, &fp);
    return (fp.optimalTilingFeatures & feat) == feat;
  };
  depthFormat_ = fmtOk(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_FORMAT_D32_SFLOAT
                                                                                              : VK_FORMAT_D24_UNORM_S8_UINT;
  shadowFormat_ = fmtOk(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                      ? VK_FORMAT_D32_SFLOAT
                      : VK_FORMAT_D16_UNORM;
  // HDR scene target: packed float if renderable + filterable, else half float, else LDR fallback
  const VkFormatFeatureFlags hdrFeat = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                       VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if (fmtOk(VK_FORMAT_B10G11R11_UFLOAT_PACK32, hdrFeat)) hdrFormat_ = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  else if (fmtOk(VK_FORMAT_R16G16B16A16_SFLOAT, hdrFeat)) hdrFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
  else hdrFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
  LOGI("Scene colour format %d", (int)hdrFormat_);

  // descriptor pool
  VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8},
                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dpi.maxSets = 480;
  dpi.poolSizeCount = 4;
  dpi.pPoolSizes = ps;
  VK_CHECK(vkCreateDescriptorPool(dev, &dpi, nullptr, &pool_));

  // layouts
  auto mkLayout = [&](std::vector<VkDescriptorSetLayoutBinding> b) {
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = (uint32_t)b.size();
    li.pBindings = b.data();
    VkDescriptorSetLayout l;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &l));
    return l;
  };
  VkShaderStageFlags vf = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutGlobalsA_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, vf, nullptr}});
  layoutGlobalsB_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, vf, nullptr},
                              {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                              {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                              {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
  layoutEmpty_ = mkLayout({});
  layoutTex_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
  layoutTex2_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
  layoutTex3_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
  layoutTex4_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                          {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
  layoutBones_ = mkLayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});

  auto mkPl = [&](std::vector<VkDescriptorSetLayout> sets, uint32_t pcSize, VkShaderStageFlags pcStages) {
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount = (uint32_t)sets.size();
    pi.pSetLayouts = sets.data();
    VkPushConstantRange pr{pcStages, 0, pcSize};
    if (pcSize) { pi.pushConstantRangeCount = 1; pi.pPushConstantRanges = &pr; }
    VkPipelineLayout l;
    VK_CHECK(vkCreatePipelineLayout(dev, &pi, nullptr, &l));
    return l;
  };
  plWorld_ = mkPl({layoutGlobalsB_, layoutTex2_}, 0, 0);
  plSprite_ = mkPl({layoutGlobalsB_, layoutTex_}, 0, 0);
  plShadow_ = mkPl({layoutGlobalsA_, layoutBones_}, 80, VK_SHADER_STAGE_VERTEX_BIT);
  plMesh_ = mkPl({layoutGlobalsB_, layoutTex3_, layoutBones_}, 96, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
  plUi_ = mkPl({layoutEmpty_, layoutTex_}, 16, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
  plBlur_ = mkPl({layoutEmpty_, layoutTex_}, 16, VK_SHADER_STAGE_FRAGMENT_BIT);
  plComposite_ = mkPl({layoutEmpty_, layoutTex4_}, 128, VK_SHADER_STAGE_FRAGMENT_BIT);
  plAo_ = mkPl({layoutEmpty_, layoutTex2_}, 32, VK_SHADER_STAGE_FRAGMENT_BIT);

  // shadow sampler (hardware compare)
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  si.compareEnable = VK_TRUE;
  si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  VK_CHECK(vkCreateSampler(dev, &si, nullptr, &shadowSampler_));

  // frame resources
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cai.commandPool = ctx_.cmdPool;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;
  for (int i = 0; i < kFrames; ++i) {
    FrameRes& f = frames_[i];
    VK_CHECK(vkAllocateCommandBuffers(dev, &cai, &f.cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(dev, &fi, nullptr, &f.fence));
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(dev, &sci, nullptr, &f.imageAvailable));
    f.arena = ctx_.createBuffer(kArenaSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    f.lightBuf = ctx_.createBuffer(kLightBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    std::memset(f.lightBuf.map, 0, (size_t)kLightBufSize);
    VkDescriptorSetLayout ls[3] = {layoutGlobalsA_, layoutGlobalsB_, layoutBones_};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts = ls;
    VkDescriptorSet sets[3];
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, sets));
    f.globalsA = sets[0];
    f.globalsB = sets[1];
    f.bones = sets[2];
    {
      VkDescriptorBufferInfo bb{f.arena.buf, 0, sizeof(Mat4) * kMaxBones};
      VkWriteDescriptorSet wb{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      wb.dstSet = f.bones;
      wb.descriptorCount = 1;
      wb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      wb.pBufferInfo = &bb;
      vkUpdateDescriptorSets(dev, 1, &wb, 0, nullptr);
    }
    VkDescriptorBufferInfo bi{f.arena.buf, 0, sizeof(GlobalsUBO)};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f.globalsA;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    w.dstSet = f.globalsB;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    VkDescriptorBufferInfo lbi{f.lightBuf.buf, 0, kLightBufSize};
    VkWriteDescriptorSet lw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    lw.dstSet = f.globalsB;
    lw.dstBinding = 3;
    lw.descriptorCount = 1;
    lw.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lw.pBufferInfo = &lbi;
    vkUpdateDescriptorSets(dev, 1, &lw, 0, nullptr);
  }

  // 1x1 white dummy texture
  uint8_t white[4] = {255, 255, 255, 255};
  dummyTex_ = createTextureRGBA(1, 1, white, false, false, SamplerKind::ClampLinear);
  uint8_t flatN[4] = {128, 128, 255, 255};
  flatNormalTex_ = createTextureRGBA(1, 1, flatN, false, false, SamplerKind::Repeat);
  uint8_t orm[4] = {255, 200, 0, 255};  // occlusion 1, roughness ~0.78, dielectric
  defaultOrmTex_ = createTextureRGBA(1, 1, orm, false, false, SamplerKind::Repeat);
  {
    // 2-layer arrays so the views are VK_IMAGE_VIEW_TYPE_2D_ARRAY
    TextureData td;
    td.format = TexFormat::RGBA8_UNORM;
    td.width = td.height = 1; td.layers = 2; td.mips = 1;
    td.bytes = {255, 255, 255, 255, 255, 255, 255, 255};
    dummyArray_ = createTexture(td, SamplerKind::Repeat);
    td.bytes = {128, 128, 200, 255, 128, 128, 200, 255};  // flat normal, roughness 0.78, no cavity
    flatNormalArray_ = createTexture(td, SamplerKind::Repeat);
  }

  if (cfg_.headless) {
    outFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    outW_ = cfg_.headlessWidth;
    outH_ = cfg_.headlessHeight;
    if (!createPasses() || !createLayoutsAndPipelines()) return false;
    createOffscreenTargets();
    createRenderTargets();
  }
  return true;
}

void Renderer::shutdown() {
  if (!ctx_.device) return;
  VkDevice dev = ctx_.device;
  vkDeviceWaitIdle(dev);
  destroySwapchain();
  destroyOffscreenTargets();
  destroyRenderTargets();
  for (auto& m : meshes_) if (m.alive) { ctx_.destroyBuffer(m.vb); ctx_.destroyBuffer(m.ib); }
  for (auto& r : retired_) { ctx_.destroyBuffer(r.vb); ctx_.destroyBuffer(r.ib); }
  retired_.clear();
  for (auto& t : textures_) if (t.alive) { ctx_.destroyImage(t.img); }
  for (auto& m : models_) { ctx_.destroyBuffer(m.vb); ctx_.destroyBuffer(m.ib); }
  for (VkSampler s : samplers_) if (s) vkDestroySampler(dev, s, nullptr);
  if (shadowSampler_) vkDestroySampler(dev, shadowSampler_, nullptr);
  for (int i = 0; i < kFrames; ++i) {
    ctx_.destroyBuffer(frames_[i].arena);
    ctx_.destroyBuffer(frames_[i].lightBuf);
    vkDestroyFence(dev, frames_[i].fence, nullptr);
    vkDestroySemaphore(dev, frames_[i].imageAvailable, nullptr);
  }
  VkPipeline pipes[] = {pipeWorld_, pipeShadow_, pipeSprite_, pipeSilhouette_, pipeDecal_, pipeSky_, pipeUi_,
                        pipeBlurDown_, pipeBlurUp_, pipeComposite_, pipeMesh_, pipeMeshSkinned_, pipeShadowMesh_,
                        pipeShadowSkinned_, pipeAo_, pipeAoBlur_};
  for (auto p : pipes) if (p) vkDestroyPipeline(dev, p, nullptr);
  VkPipelineLayout pls[] = {plWorld_, plSprite_, plShadow_, plMesh_, plUi_, plBlur_, plComposite_, plAo_};
  for (auto p : pls) if (p) vkDestroyPipelineLayout(dev, p, nullptr);
  VkDescriptorSetLayout dls[] = {layoutGlobalsA_, layoutGlobalsB_, layoutEmpty_, layoutTex_, layoutTex2_, layoutTex3_, layoutTex4_, layoutBones_};
  for (auto l : dls) if (l) vkDestroyDescriptorSetLayout(dev, l, nullptr);
  for (auto m : shaderModules_) vkDestroyShaderModule(dev, m, nullptr);
  VkRenderPass rps[] = {shadowPass_, scenePass_, blurPass_, compositePass_, aoPass_};
  for (auto r : rps) if (r) vkDestroyRenderPass(dev, r, nullptr);
  if (pool_) vkDestroyDescriptorPool(dev, pool_, nullptr);
  ctx_.shutdown();
}

// ---------------------------------------------------------------------------------------------------------------------
bool Renderer::createPasses() {
  VkDevice dev = ctx_.device;
  auto dep = [](uint32_t src, uint32_t dst, VkPipelineStageFlags ss, VkPipelineStageFlags ds, VkAccessFlags sa,
                VkAccessFlags da) {
    VkSubpassDependency d{};
    d.srcSubpass = src; d.dstSubpass = dst; d.srcStageMask = ss; d.dstStageMask = ds; d.srcAccessMask = sa; d.dstAccessMask = da;
    d.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    return d;
  };
  // shadow
  {
    VkAttachmentDescription a{};
    a.format = shadowFormat_;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.pDepthStencilAttachment = &ref;
    VkSubpassDependency deps[2] = {
        dep(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
        dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 1; ri.pAttachments = &a; ri.subpassCount = 1; ri.pSubpasses = &sp; ri.dependencyCount = 2; ri.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(dev, &ri, nullptr, &shadowPass_));
  }
  // scene: color + depth
  {
    VkAttachmentDescription at[2]{};
    at[0].format = hdrFormat_;
    at[0].samples = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    at[1] = at[0];
    at[1].format = depthFormat_;
    at[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // sampled by the AO pass
    at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &cref; sp.pDepthStencilAttachment = &dref;
    VkSubpassDependency deps[2] = {
        dep(VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT),
        dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT)};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 2; ri.pAttachments = at; ri.subpassCount = 1; ri.pSubpasses = &sp; ri.dependencyCount = 2; ri.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(dev, &ri, nullptr, &scenePass_));
  }
  // blur / composite share structure (single colour attachment)
  auto makeColorPass = [&](VkFormat fmt, VkImageLayout finalLayout, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess,
                           VkRenderPass* out, bool clear = false) {
    VkAttachmentDescription a{};
    a.format = fmt;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = finalLayout;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {
        dep(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, dstStage,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, dstAccess)};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 1; ri.pAttachments = &a; ri.subpassCount = 1; ri.pSubpasses = &sp; ri.dependencyCount = 2; ri.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(dev, &ri, nullptr, out));
  };
  makeColorPass(hdrFormat_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT, &blurPass_);
  makeColorPass(VK_FORMAT_R8G8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT, &aoPass_, true);
  if (cfg_.headless)
    makeColorPass(outFormat_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  &compositePass_);
  else
    makeColorPass(outFormat_, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, &compositePass_);
  return true;
}

VkPipeline Renderer::buildPipeline(const char* vs, const char* fs, VkRenderPass rp, VkPipelineLayout layout, int vertexKind,
                                   bool depthTest, bool depthWrite, VkCompareOp depthOp, bool blend, VkCullModeFlags cull,
                                   bool depthBias, VkPrimitiveTopology topo) {
  VkDevice dev = ctx_.device;
  VkPipelineShaderStageCreateInfo st[2]{};
  int n = 0;
  st[n].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[n].stage = VK_SHADER_STAGE_VERTEX_BIT;
  st[n].module = loadShader(ctx_, vs, shaderModules_);
  st[n].pName = "main";
  ++n;
  if (fs) {
    st[n].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[n].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[n].module = loadShader(ctx_, fs, shaderModules_);
    st[n].pName = "main";
    ++n;
  }
  VkVertexInputBindingDescription bind{};
  std::vector<VkVertexInputAttributeDescription> attrs;
  auto A = [&](uint32_t loc, VkFormat f, uint32_t off) { attrs.push_back({loc, 0, f, off}); };
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  switch (vertexKind) {
    case 1:  // world
      bind = {0, sizeof(WorldVertex), VK_VERTEX_INPUT_RATE_VERTEX};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0); A(1, VK_FORMAT_R8G8B8A8_SNORM, 12); A(2, VK_FORMAT_R32G32_SFLOAT, 16);
      A(3, VK_FORMAT_R8G8B8A8_UNORM, 24); A(4, VK_FORMAT_R32_SFLOAT, 28);
      break;
    case 2:  // sprite
      bind = {0, sizeof(SpriteInst), VK_VERTEX_INPUT_RATE_INSTANCE};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0); A(1, VK_FORMAT_R32G32_SFLOAT, 12); A(2, VK_FORMAT_R32G32_SFLOAT, 20);
      A(3, VK_FORMAT_R32G32B32A32_SFLOAT, 28); A(4, VK_FORMAT_R8G8B8A8_UNORM, 44); A(5, VK_FORMAT_R32_SFLOAT, 48);
      break;
    case 3:  // decal
      bind = {0, sizeof(DecalInst), VK_VERTEX_INPUT_RATE_INSTANCE};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0); A(1, VK_FORMAT_R32_SFLOAT, 12); A(2, VK_FORMAT_R32G32_SFLOAT, 16);
      A(3, VK_FORMAT_R32G32_SFLOAT, 24);
      break;
    case 4:  // ui
      bind = {0, sizeof(UiInst), VK_VERTEX_INPUT_RATE_INSTANCE};
      A(0, VK_FORMAT_R32G32B32A32_SFLOAT, 0); A(1, VK_FORMAT_R32G32B32A32_SFLOAT, 16); A(2, VK_FORMAT_R8G8B8A8_UNORM, 32);
      A(3, VK_FORMAT_R8G8B8A8_UNORM, 36); A(4, VK_FORMAT_R32G32B32A32_SFLOAT, 40); A(5, VK_FORMAT_R32G32_SFLOAT, 56);
      break;
    case 5:  // shadow (position only from world vertices)
      bind = {0, sizeof(WorldVertex), VK_VERTEX_INPUT_RATE_VERTEX};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0);
      break;
    case 6:  // model, static
    case 7:  // model, skinned
      bind = {0, sizeof(ModelVertex), VK_VERTEX_INPUT_RATE_VERTEX};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0); A(1, VK_FORMAT_R8G8B8A8_SNORM, 12); A(2, VK_FORMAT_R8G8B8A8_SNORM, 16);
      A(3, VK_FORMAT_R32G32_SFLOAT, 20);
      if (vertexKind == 7) { A(4, VK_FORMAT_R8G8B8A8_UINT, 28); A(5, VK_FORMAT_R8G8B8A8_UNORM, 32); }
      break;
    case 8:  // model shadow, static
    case 9:  // model shadow, skinned
      bind = {0, sizeof(ModelVertex), VK_VERTEX_INPUT_RATE_VERTEX};
      A(0, VK_FORMAT_R32G32B32_SFLOAT, 0);
      if (vertexKind == 9) { A(4, VK_FORMAT_R8G8B8A8_UINT, 28); A(5, VK_FORMAT_R8G8B8A8_UNORM, 32); }
      break;
    default: break;
  }
  if (vertexKind) {
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();
  }
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = topo;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = cull;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  rs.depthBiasEnable = depthBias ? VK_TRUE : VK_FALSE;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = depthTest;
  ds.depthWriteEnable = depthWrite;
  ds.depthCompareOp = depthOp;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  if (blend) {
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  if (fs) { cb.attachmentCount = 1; cb.pAttachments = &cba; }
  std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  if (depthBias) dyn.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
  VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dsi.dynamicStateCount = (uint32_t)dyn.size();
  dsi.pDynamicStates = dyn.data();
  VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gi.stageCount = (uint32_t)n;
  gi.pStages = st;
  gi.pVertexInputState = &vi;
  gi.pInputAssemblyState = &ia;
  gi.pViewportState = &vp;
  gi.pRasterizationState = &rs;
  gi.pMultisampleState = &ms;
  gi.pDepthStencilState = (rp == compositePass_ || rp == blurPass_ || rp == aoPass_) ? nullptr : &ds;
  gi.pColorBlendState = (fs ? &cb : (const VkPipelineColorBlendStateCreateInfo*)&cb);
  gi.pDynamicState = &dsi;
  gi.layout = layout;
  gi.renderPass = rp;
  VkPipeline p = VK_NULL_HANDLE;
  VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gi, nullptr, &p));
  return p;
}

bool Renderer::createLayoutsAndPipelines() {
  const VkCompareOp LESS = VK_COMPARE_OP_LESS, LEQ = VK_COMPARE_OP_LESS_OR_EQUAL, GRT = VK_COMPARE_OP_GREATER;
  pipeWorld_ = buildPipeline("world.vert.spv", "world.frag.spv", scenePass_, plWorld_, 1, true, true, LESS, false, VK_CULL_MODE_BACK_BIT);
  pipeShadow_ = buildPipeline("shadow.vert.spv", nullptr, shadowPass_, plShadow_, 5, true, true, LESS, false, VK_CULL_MODE_NONE, true);
  pipeShadowMesh_ = buildPipeline("shadow.vert.spv", nullptr, shadowPass_, plShadow_, 8, true, true, LESS, false, VK_CULL_MODE_NONE, true);
  pipeShadowSkinned_ = buildPipeline("mesh_shadow_skinned.vert.spv", nullptr, shadowPass_, plShadow_, 9, true, true, LESS, false,
                                     VK_CULL_MODE_NONE, true);
  pipeMesh_ = buildPipeline("mesh.vert.spv", "mesh.frag.spv", scenePass_, plMesh_, 6, true, true, LESS, false, VK_CULL_MODE_NONE);
  pipeMeshSkinned_ = buildPipeline("mesh_skinned.vert.spv", "mesh.frag.spv", scenePass_, plMesh_, 7, true, true, LESS, false,
                                   VK_CULL_MODE_BACK_BIT);
  pipeSprite_ = buildPipeline("sprite.vert.spv", "sprite.frag.spv", scenePass_, plSprite_, 2, true, true, LEQ, true, VK_CULL_MODE_NONE,
                              false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  pipeSilhouette_ = buildPipeline("sprite.vert.spv", "silhouette.frag.spv", scenePass_, plSprite_, 2, true, false, GRT, true,
                                  VK_CULL_MODE_NONE, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  pipeDecal_ = buildPipeline("decal.vert.spv", "decal.frag.spv", scenePass_, plSprite_, 3, true, false, LEQ, true, VK_CULL_MODE_NONE,
                             false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  pipeSky_ = buildPipeline("fullscreen.vert.spv", "sky.frag.spv", scenePass_, plSprite_, 0, false, false, LEQ, false, VK_CULL_MODE_NONE);
  pipeUi_ = buildPipeline("ui.vert.spv", "ui.frag.spv", compositePass_, plUi_, 4, false, false, LEQ, true, VK_CULL_MODE_NONE, false,
                          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  pipeBlurDown_ = buildPipeline("fullscreen.vert.spv", "blur_down.frag.spv", blurPass_, plBlur_, 0, false, false, LEQ, false, VK_CULL_MODE_NONE);
  pipeBlurUp_ = buildPipeline("fullscreen.vert.spv", "blur_up.frag.spv", blurPass_, plBlur_, 0, false, false, LEQ, false, VK_CULL_MODE_NONE);
  pipeAo_ = buildPipeline("fullscreen.vert.spv", "ssao.frag.spv", aoPass_, plAo_, 0, false, false, LEQ, false, VK_CULL_MODE_NONE);
  pipeAoBlur_ = buildPipeline("fullscreen.vert.spv", "ssao_blur.frag.spv", aoPass_, plAo_, 0, false, false, LEQ, false, VK_CULL_MODE_NONE);
  pipeComposite_ = buildPipeline("fullscreen.vert.spv", "composite.frag.spv", compositePass_, plComposite_, 0, false, false, LEQ, false,
                                 VK_CULL_MODE_NONE);
  return pipeWorld_ && pipeShadow_ && pipeSprite_ && pipeDecal_ && pipeSky_ && pipeUi_ && pipeComposite_ && pipeMesh_ &&
         pipeMeshSkinned_ && pipeShadowMesh_ && pipeShadowSkinned_ && pipeAo_ && pipeAoBlur_;
}

// ---------------------------------------------------------------------------------------------------------------------
VkSampler Renderer::getSampler(SamplerKind k) {
  int i = (int)k;
  if (samplers_[i]) return samplers_[i];
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = k == SamplerKind::ClampNearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  si.minFilter = si.magFilter;
  si.mipmapMode = k == SamplerKind::ClampNearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode am = k == SamplerKind::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.addressModeU = si.addressModeV = si.addressModeW = am;
  if (ctx_.caps.anisotropy && k == SamplerKind::Repeat) {
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy = std::min(4.0f, ctx_.caps.maxAniso);
  }
  si.maxLod = 16.0f;
  VK_CHECK(vkCreateSampler(ctx_.device, &si, nullptr, &samplers_[i]));
  return samplers_[i];
}

VkDescriptorSet Renderer::allocTexSet(VkImageView view, VkSampler samp) {
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layoutTex_;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VK_CHECK(vkAllocateDescriptorSets(ctx_.device, &ai, &set));
  VkDescriptorImageInfo ii{samp, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = set;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  w.pImageInfo = &ii;
  vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
  return set;
}

TexHandle Renderer::createTexture(const TextureData& td, SamplerKind sk) {
  VkFormat fmt;
  switch (td.format) {
    case TexFormat::RGBA8_SRGB: fmt = VK_FORMAT_R8G8B8A8_SRGB; break;
    case TexFormat::RGBA8_UNORM: fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
    case TexFormat::R8_UNORM: fmt = VK_FORMAT_R8_UNORM; break;
    case TexFormat::ASTC6x6_SRGB:
      if (!ctx_.caps.astcLdr) return {};
      fmt = VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
      break;
    case TexFormat::ASTC6x6_UNORM:
      if (!ctx_.caps.astcLdr) return {};
      fmt = VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
      break;
    case TexFormat::ASTC8x8_SRGB:
      if (!ctx_.caps.astcLdr) return {};
      fmt = VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
      break;
    default: return {};
  }
  if (td.width > ctx_.caps.maxImageDim || td.height > ctx_.caps.maxImageDim || td.layers > ctx_.caps.maxArrayLayers) return {};
  Image img = ctx_.createImage(td.width, td.height, td.layers, td.mips, fmt,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  Buffer staging = ctx_.createBuffer(td.bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.map, td.bytes.data(), td.bytes.size());
  std::vector<VkBufferImageCopy> regions;
  VkDeviceSize off = 0;
  for (uint32_t m = 0; m < td.mips; ++m) {
    uint32_t w = std::max(1u, td.width >> m), h = std::max(1u, td.height >> m);
    for (uint32_t l = 0; l < td.layers; ++l) {
      VkBufferImageCopy r{};
      r.bufferOffset = off;
      r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, l, 1};
      r.imageExtent = {w, h, 1};
      regions.push_back(r);
      off += texelBytes(td.format, w, h);
    }
  }
  if (off != td.bytes.size()) LOGW("Texture size mismatch: expected %zu got %zu", (size_t)off, td.bytes.size());
  VkCommandBuffer cb = ctx_.beginOneShot();
  ctx_.transitionImage(cb, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0,
                       td.mips, 0, td.layers);
  vkCmdCopyBufferToImage(cb, staging.buf, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
  ctx_.transitionImage(cb, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 0, td.mips, 0, td.layers);
  ctx_.endOneShot(cb);
  ctx_.destroyBuffer(staging);

  TexRes tr;
  tr.img = img;
  tr.sampler = getSampler(sk);
  tr.set = allocTexSet(img.view, tr.sampler);
  tr.alive = true;
  textures_.push_back(tr);
  return TexHandle{(int)textures_.size() - 1};
}

TexHandle Renderer::createTextureRGBA(uint32_t w, uint32_t h, const uint8_t* rgba, bool srgb, bool mips, SamplerKind sk) {
  TextureData td;
  td.format = srgb ? TexFormat::RGBA8_SRGB : TexFormat::RGBA8_UNORM;
  td.width = w; td.height = h; td.layers = 1; td.mips = 1;
  td.bytes.assign(rgba, rgba + (size_t)w * h * 4);
  if (mips) {
    std::vector<uint8_t> prev(rgba, rgba + (size_t)w * h * 4);
    uint32_t cw = w, ch = h;
    while (cw > 1 || ch > 1) {
      uint32_t nw = std::max(1u, cw / 2), nh = std::max(1u, ch / 2);
      std::vector<uint8_t> next((size_t)nw * nh * 4);
      for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x)
          for (int c = 0; c < 4; ++c) {
            uint32_t x0 = std::min(cw - 1, x * 2), x1 = std::min(cw - 1, x * 2 + 1), y0 = std::min(ch - 1, y * 2), y1 = std::min(ch - 1, y * 2 + 1);
            int s = prev[((size_t)y0 * cw + x0) * 4 + c] + prev[((size_t)y0 * cw + x1) * 4 + c] + prev[((size_t)y1 * cw + x0) * 4 + c] +
                    prev[((size_t)y1 * cw + x1) * 4 + c];
            next[((size_t)y * nw + x) * 4 + c] = (uint8_t)((s + 2) / 4);
          }
      td.bytes.insert(td.bytes.end(), next.begin(), next.end());
      td.mips++;
      prev.swap(next);
      cw = nw; ch = nh;
    }
  }
  return createTexture(td, sk);
}

MeshHandle Renderer::createMesh(const WorldVertex* v, size_t nv, const uint32_t* idx, size_t ni) {
  MeshRes m;
  m.vb = ctx_.createBuffer(nv * sizeof(WorldVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  m.ib = ctx_.createBuffer(ni * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true);
  std::memcpy(m.vb.map, v, nv * sizeof(WorldVertex));
  std::memcpy(m.ib.map, idx, ni * sizeof(uint32_t));
  m.indexCount = (uint32_t)ni;
  m.alive = true;
  for (size_t i = 0; i < meshes_.size(); ++i)
    if (!meshes_[i].alive) { meshes_[i] = m; return MeshHandle{(int)i}; }
  meshes_.push_back(m);
  return MeshHandle{(int)meshes_.size() - 1};
}

void Renderer::destroyMesh(MeshHandle h) {
  if (!h.valid() || !meshes_[h.id].alive) return;
  vkDeviceWaitIdle(ctx_.device);
  ctx_.destroyBuffer(meshes_[h.id].vb);
  ctx_.destroyBuffer(meshes_[h.id].ib);
  meshes_[h.id] = MeshRes();
}

void Renderer::retireMesh(MeshHandle h) {
  if (!h.valid() || h.id >= (int)meshes_.size() || !meshes_[h.id].alive) return;
  retired_.push_back({meshes_[h.id].vb, meshes_[h.id].ib, frameCounter_});
  meshes_[h.id] = MeshRes();
}

void Renderer::destroyTexture(TexHandle h) {
  if (h.id < 0 || h.id >= (int)textures_.size() || !textures_[h.id].alive) return;
  vkDeviceWaitIdle(ctx_.device);
  TexRes& t = textures_[h.id];
  if (t.set) vkFreeDescriptorSets(ctx_.device, pool_, 1, &t.set);
  ctx_.destroyImage(t.img);
  t = TexRes();
}

ModelHandle Renderer::createModel(const ModelVertex* v, size_t nv, const uint32_t* idx, size_t ni, const ModelLod* lods, int lodCount,
                                  bool skinned) {
  ModelRes m;
  m.vb = ctx_.createBuffer(nv * sizeof(ModelVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  m.ib = ctx_.createBuffer(ni * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true);
  std::memcpy(m.vb.map, v, nv * sizeof(ModelVertex));
  std::memcpy(m.ib.map, idx, ni * sizeof(uint32_t));
  m.lodCount = std::max(1, std::min(lodCount, 3));
  for (int i = 0; i < m.lodCount; ++i) m.lods[i] = lods ? lods[i] : ModelLod{0, (uint32_t)ni};
  m.skinned = skinned;
  models_.push_back(m);
  return ModelHandle{(int)models_.size() - 1};
}

MaterialHandle Renderer::createWorldMaterial(TexHandle albedoArray, TexHandle normalArray) {
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layoutTex2_;
  VkDescriptorSet set;
  VK_CHECK(vkAllocateDescriptorSets(ctx_.device, &ai, &set));
  const TexRes& a = textures_[albedoArray.valid() ? albedoArray.id : dummyArray_.id];
  const TexRes& n = textures_[normalArray.valid() ? normalArray.id : flatNormalArray_.id];
  VkDescriptorImageInfo ii[2] = {{a.sampler, a.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                 {n.sampler, n.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
  VkWriteDescriptorSet w[2]{};
  for (int i = 0; i < 2; ++i) {
    w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[i].dstSet = set; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1;
    w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &ii[i];
  }
  vkUpdateDescriptorSets(ctx_.device, 2, w, 0, nullptr);
  materials_.push_back(set);
  return MaterialHandle{(int)materials_.size() - 1};
}

MaterialHandle Renderer::createModelMaterial(TexHandle albedo, TexHandle normal, TexHandle orm) {
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layoutTex3_;
  VkDescriptorSet set;
  VK_CHECK(vkAllocateDescriptorSets(ctx_.device, &ai, &set));
  const TexRes* t[3] = {&textures_[albedo.valid() ? albedo.id : dummyTex_.id], &textures_[normal.valid() ? normal.id : flatNormalTex_.id],
                        &textures_[orm.valid() ? orm.id : defaultOrmTex_.id]};
  VkDescriptorImageInfo ii[3];
  VkWriteDescriptorSet w[3]{};
  for (int i = 0; i < 3; ++i) {
    ii[i] = {t[i]->sampler, t[i]->img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[i].dstSet = set; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1;
    w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &ii[i];
  }
  vkUpdateDescriptorSets(ctx_.device, 3, w, 0, nullptr);
  materials_.push_back(set);
  return MaterialHandle{(int)materials_.size() - 1};
}

void Renderer::setShadowMapSize(int size) {
  size = clamp(size, 512, 4096);
  if (size == cfg_.shadowMapSize) return;
  cfg_.shadowMapSize = size;
  if (compositePass_ && outW_) createRenderTargets();
}

// ---------------------------------------------------------------------------------------------------------------------
void Renderer::destroyRenderTargets() {
  VkDevice dev = ctx_.device;
  if (!dev) return;
  vkDeviceWaitIdle(dev);
  for (int i = 0; i < 2; ++i) {
    if (shadowFbs_[i]) vkDestroyFramebuffer(dev, shadowFbs_[i], nullptr);
    if (shadowLayerViews_[i]) vkDestroyImageView(dev, shadowLayerViews_[i], nullptr);
    shadowFbs_[i] = VK_NULL_HANDLE;
    shadowLayerViews_[i] = VK_NULL_HANDLE;
  }
  if (sceneFb_) vkDestroyFramebuffer(dev, sceneFb_, nullptr);
  sceneFb_ = VK_NULL_HANDLE;
  ctx_.destroyImage(shadowMap_);
  ctx_.destroyImage(sceneColor_);
  ctx_.destroyImage(sceneDepth_);
  if (aoFbA_) vkDestroyFramebuffer(dev, aoFbA_, nullptr);
  if (aoFbB_) vkDestroyFramebuffer(dev, aoFbB_, nullptr);
  aoFbA_ = aoFbB_ = VK_NULL_HANDLE;
  if (aoDepthSet_) vkFreeDescriptorSets(dev, pool_, 1, &aoDepthSet_);
  if (aoBlurSet_) vkFreeDescriptorSets(dev, pool_, 1, &aoBlurSet_);
  aoDepthSet_ = aoBlurSet_ = VK_NULL_HANDLE;
  ctx_.destroyImage(aoA_);
  ctx_.destroyImage(aoB_);
  for (auto* v : {&blurDown_, &blurUp_}) {
    for (auto& b : *v) {
      if (b.fb) vkDestroyFramebuffer(dev, b.fb, nullptr);
      if (b.set) vkFreeDescriptorSets(dev, pool_, 1, &b.set);
      ctx_.destroyImage(b.img);
    }
    v->clear();
  }
  if (sceneSampleSet_) vkFreeDescriptorSets(dev, pool_, 1, &sceneSampleSet_);
  if (compositeSet_) vkFreeDescriptorSets(dev, pool_, 1, &compositeSet_);
  sceneSampleSet_ = compositeSet_ = VK_NULL_HANDLE;
}

void Renderer::createRenderTargets() {
  VkDevice dev = ctx_.device;
  destroyRenderTargets();
  sceneW_ = std::max(64u, (uint32_t)(outW_ * cfg_.renderScale + 0.5f));
  sceneH_ = std::max(64u, (uint32_t)(outH_ * cfg_.renderScale + 0.5f));

  uint32_t sm = (uint32_t)cfg_.shadowMapSize;
  // two cascades in one depth array; one framebuffer per layer
  shadowMap_ = ctx_.createImage(sm, sm, 2, 1, shadowFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT);
  VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  for (int c = 0; c < 2; ++c) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = shadowMap_.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = shadowFormat_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, (uint32_t)c, 1};
    VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &shadowLayerViews_[c]));
    fi.renderPass = shadowPass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &shadowLayerViews_[c];
    fi.width = fi.height = sm;
    fi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(dev, &fi, nullptr, &shadowFbs_[c]));
  }
  // bind the shadow map to both frames' globalsB sets
  for (int i = 0; i < kFrames; ++i) {
    VkDescriptorImageInfo ii{shadowSampler_, shadowMap_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = frames_[i].globalsB;
    w.dstBinding = 1;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
  }
  bindProbeSets();

  sceneColor_ = ctx_.createImage(sceneW_, sceneH_, 1, 1, hdrFormat_,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  aoSupported_ = depthFormat_ == VK_FORMAT_D32_SFLOAT;
  sceneDepth_ = ctx_.createImage(sceneW_, sceneH_, 1, 1, depthFormat_,
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | (aoSupported_ ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
                                 depthFormat_ == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                       : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
  VkImageView atts[2] = {sceneColor_.view, sceneDepth_.view};
  fi.renderPass = scenePass_;
  fi.attachmentCount = 2;
  fi.pAttachments = atts;
  fi.width = sceneW_;
  fi.height = sceneH_;
  VK_CHECK(vkCreateFramebuffer(dev, &fi, nullptr, &sceneFb_));

  VkSampler lin = getSampler(SamplerKind::ClampLinear);
  sceneSampleSet_ = allocTexSet(sceneColor_.view, lin);

  // blur chain
  auto mkLevel = [&](uint32_t w, uint32_t h) {
    BlurLevel b;
    b.w = std::max(1u, w); b.h = std::max(1u, h);
    b.img = ctx_.createImage(b.w, b.h, 1, 1, hdrFormat_,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkFramebufferCreateInfo bf{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    bf.renderPass = blurPass_;
    bf.attachmentCount = 1;
    bf.pAttachments = &b.img.view;
    bf.width = b.w; bf.height = b.h; bf.layers = 1;
    VK_CHECK(vkCreateFramebuffer(dev, &bf, nullptr, &b.fb));
    b.set = allocTexSet(b.img.view, lin);
    return b;
  };
  for (int i = 1; i <= 4; ++i) blurDown_.push_back(mkLevel(sceneW_ >> i, sceneH_ >> i));
  for (int i = 3; i >= 1; --i) blurUp_.push_back(mkLevel(sceneW_ >> i, sceneH_ >> i));  // sizes of down[2], down[1], down[0]

  // half-resolution AO targets (depth -> raw AO -> depth-aware blur); the composite samples the blurred one
  aoW_ = std::max(1u, sceneW_ / 2); aoH_ = std::max(1u, sceneH_ / 2);
  auto mkAo = [&](Image& img, VkFramebuffer& fb) {
    img = ctx_.createImage(aoW_, aoH_, 1, 1, VK_FORMAT_R8G8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
    VkFramebufferCreateInfo bf{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    bf.renderPass = aoPass_;
    bf.attachmentCount = 1;
    bf.pAttachments = &img.view;
    bf.width = aoW_; bf.height = aoH_; bf.layers = 1;
    VK_CHECK(vkCreateFramebuffer(dev, &bf, nullptr, &fb));
  };
  mkAo(aoA_, aoFbA_);
  mkAo(aoB_, aoFbB_);
  VkSampler nearS = getSampler(SamplerKind::ClampNearest);
  auto allocSet = [&](VkDescriptorSetLayout l, std::initializer_list<VkDescriptorImageInfo> infos) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &l;
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &set));
    std::vector<VkDescriptorImageInfo> ii(infos);
    std::vector<VkWriteDescriptorSet> w(ii.size());
    for (size_t i = 0; i < ii.size(); ++i) {
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[i].dstSet = set;
      w[i].dstBinding = (uint32_t)i;
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(dev, (uint32_t)w.size(), w.data(), 0, nullptr);
    return set;
  };
  const VkImageLayout RO = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if (aoSupported_) {
    VkDescriptorImageInfo depthInfo{nearS, sceneDepth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    aoDepthSet_ = allocSet(layoutTex2_, {depthInfo, depthInfo});
    aoBlurSet_ = allocSet(layoutTex2_, {depthInfo, VkDescriptorImageInfo{lin, aoA_.view, RO}});
  }
  // composite set: scene + final blurred level + AO / sky mask
  VkDescriptorImageInfo depthSample = aoSupported_ ? VkDescriptorImageInfo{nearS, sceneDepth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}
                                                   : VkDescriptorImageInfo{lin, aoB_.view, RO};
  compositeSet_ = allocSet(layoutTex4_, {VkDescriptorImageInfo{lin, sceneColor_.view, RO}, VkDescriptorImageInfo{lin, blurUp_.back().img.view, RO},
                                         VkDescriptorImageInfo{lin, aoB_.view, RO}, depthSample});
}

void Renderer::bindProbeSets() {
  if (!dummyArray_.valid()) return;
  const TexRes& t = textures_[probeTex_.valid() ? probeTex_.id : dummyArray_.id];
  for (int i = 0; i < kFrames; ++i) {
    VkDescriptorImageInfo ii{t.sampler, t.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = frames_[i].globalsB;
    w.dstBinding = 2;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
  }
}

void Renderer::setProbeGrid(TexHandle t) {
  vkDeviceWaitIdle(ctx_.device);   // the previous grid may still be in flight
  probeTex_ = t;
  bindProbeSets();
}

void Renderer::setRenderScale(float s) {
  s = clamp(s, 0.4f, 1.0f);
  if (std::fabs(s - cfg_.renderScale) < 0.01f) return;
  cfg_.renderScale = s;
  if (compositePass_ && outW_) createRenderTargets();
}

void Renderer::createOffscreenTargets() {
  for (int i = 0; i < 2; ++i) {
    headlessImages_[i] = ctx_.createImage(outW_, outH_, 1, 1, outFormat_,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = compositePass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &headlessImages_[i].view;
    fi.width = outW_; fi.height = outH_; fi.layers = 1;
    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(ctx_.device, &fi, nullptr, &fb));
    outFbs_.push_back(fb);
  }
  readbackBuf_ = ctx_.createBuffer((VkDeviceSize)outW_ * outH_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
}

void Renderer::destroyOffscreenTargets() {
  if (!cfg_.headless || !ctx_.device) return;
  for (auto fb : outFbs_) vkDestroyFramebuffer(ctx_.device, fb, nullptr);
  outFbs_.clear();
  for (auto& i : headlessImages_) ctx_.destroyImage(i);
  ctx_.destroyBuffer(readbackBuf_);
}

bool Renderer::resetSurface(const SurfaceFactory& factory) {
  vkDeviceWaitIdle(ctx_.device);
  destroySwapchain();
  if (ctx_.surface) { vkDestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr); ctx_.surface = VK_NULL_HANDLE; }
  ctx_.surface = factory(ctx_.instance);
  return ctx_.surface != VK_NULL_HANDLE;
}

bool Renderer::createSwapchain(uint32_t w, uint32_t h) {
  if (cfg_.headless) return true;
  VkDevice dev = ctx_.device;
  vkDeviceWaitIdle(dev);
  destroySwapchain();
  VkSurfaceCapabilitiesKHR caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_.phys, ctx_.surface, &caps));
  uint32_t fc = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.phys, ctx_.surface, &fc, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(fc);
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_.phys, ctx_.surface, &fc, fmts.data());
  VkSurfaceFormatKHR chosen = fmts[0];
  bool found = false;
  for (auto& f : fmts)
    if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; found = true; break; }
  if (!found)
    for (auto& f : fmts)
      if (f.format == VK_FORMAT_R8G8B8A8_SRGB || f.format == VK_FORMAT_B8G8R8A8_SRGB) { chosen = f; break; }
  outIsSrgb_ = chosen.format == VK_FORMAT_R8G8B8A8_SRGB || chosen.format == VK_FORMAT_B8G8R8A8_SRGB;
  bool formatChanged = outFormat_ != chosen.format || !compositePass_;
  outFormat_ = chosen.format;

  VkExtent2D ext = caps.currentExtent;
  if (ext.width == 0xFFFFFFFFu) { ext.width = w; ext.height = h; }
  ext.width = clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  ext.height = clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  outW_ = ext.width; outH_ = ext.height;

  uint32_t imgCount = caps.minImageCount + 1;
  if (caps.maxImageCount && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
  VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  si.surface = ctx_.surface;
  si.minImageCount = imgCount;
  si.imageFormat = chosen.format;
  si.imageColorSpace = chosen.colorSpace;
  si.imageExtent = ext;
  si.imageArrayLayers = 1;
  si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  si.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                                                                        : caps.currentTransform;
  si.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                           : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  si.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  si.clipped = VK_TRUE;
  VK_CHECK(vkCreateSwapchainKHR(dev, &si, nullptr, &swapchain_));
  uint32_t n = 0;
  vkGetSwapchainImagesKHR(dev, swapchain_, &n, nullptr);
  outImages_.resize(n);
  vkGetSwapchainImagesKHR(dev, swapchain_, &n, outImages_.data());

  if (formatChanged) {
    // (Re)build passes and pipelines for the chosen output format.
    VkRenderPass rps[] = {shadowPass_, scenePass_, blurPass_, compositePass_, aoPass_};
    VkPipeline pipes[] = {pipeWorld_, pipeShadow_, pipeSprite_, pipeSilhouette_, pipeDecal_, pipeSky_, pipeUi_,
                          pipeBlurDown_, pipeBlurUp_, pipeComposite_, pipeMesh_, pipeMeshSkinned_, pipeShadowMesh_,
                          pipeShadowSkinned_, pipeAo_, pipeAoBlur_};
    for (auto p : pipes) if (p) vkDestroyPipeline(dev, p, nullptr);
    for (auto r : rps) if (r) vkDestroyRenderPass(dev, r, nullptr);
    shadowPass_ = scenePass_ = blurPass_ = compositePass_ = aoPass_ = VK_NULL_HANDLE;
    if (!createPasses() || !createLayoutsAndPipelines()) return false;
  }
  for (uint32_t i = 0; i < n; ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = outImages_[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = chosen.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView v;
    VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &v));
    outViews_.push_back(v);
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = compositePass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &v;
    fi.width = outW_; fi.height = outH_; fi.layers = 1;
    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(dev, &fi, nullptr, &fb));
    outFbs_.push_back(fb);
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s;
    VK_CHECK(vkCreateSemaphore(dev, &sci, nullptr, &s));
    renderDone_.push_back(s);
  }
  createRenderTargets();
  LOGI("Swapchain %ux%u (%u images), scene %ux%u, srgb=%d", outW_, outH_, n, sceneW_, sceneH_, (int)outIsSrgb_);
  return true;
}

void Renderer::destroySwapchain() {
  VkDevice dev = ctx_.device;
  if (!dev || cfg_.headless) return;
  vkDeviceWaitIdle(dev);
  for (auto fb : outFbs_) vkDestroyFramebuffer(dev, fb, nullptr);
  for (auto v : outViews_) vkDestroyImageView(dev, v, nullptr);
  for (auto s : renderDone_) vkDestroySemaphore(dev, s, nullptr);
  outFbs_.clear(); outViews_.clear(); renderDone_.clear(); outImages_.clear();
  if (swapchain_) vkDestroySwapchainKHR(dev, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------------------------------------------------
void* Renderer::arenaAlloc(FrameRes& fr, size_t size, VkDeviceSize* outOffset) {
  VkDeviceSize off = (fr.arenaOffset + 255) & ~(VkDeviceSize)255;
  if (off + size > fr.arena.size) return nullptr;
  fr.arenaOffset = off + size;
  *outOffset = off;
  return (uint8_t*)fr.arena.map + off;
}

void Renderer::drawBatches(VkCommandBuffer cb, FrameRes& fr, const std::vector<Batch>& batches, const void* data, size_t stride,
                           VkPipeline pipe, VkPipelineLayout layout, uint32_t vertsPerInst, int setIndex) {
  if (batches.empty()) return;
  size_t total = 0;
  for (auto& b : batches) total = std::max<size_t>(total, (size_t)(b.first + b.count));
  VkDeviceSize off;
  void* dst = arenaAlloc(fr, total * stride, &off);
  if (!dst) {
    LOGW("Frame arena exhausted");
    return;
  }
  std::memcpy(dst, data, total * stride);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  vkCmdBindVertexBuffers(cb, 0, 1, &fr.arena.buf, &off);
  for (auto& b : batches) {
    if (!b.count) continue;
    int tid = b.tex.valid() ? b.tex.id : dummyTex_.id;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)setIndex, 1, &textures_[tid].set, 0, nullptr);
    vkCmdDraw(cb, vertsPerInst, b.count, 0, b.first);
  }
}

void Renderer::drawModels(VkCommandBuffer cb, FrameRes& fr, const FrameData& fd, VkDeviceSize boneBase, bool shadow, int cascade) {
  if (fd.models.empty()) return;
  VkPipelineLayout pl = shadow ? plShadow_ : plMesh_;
  VkPipeline cur = VK_NULL_HANDLE;
  int curMat = -1;
  bool globalsBound = false;
  for (const ModelDraw& d : fd.models) {
    if (!d.model.valid() || d.model.id >= (int)models_.size()) continue;
    if (shadow && !d.castShadow) continue;
    const ModelRes& m = models_[d.model.id];
    bool skinned = m.skinned && d.boneOffset >= 0 && boneBase != ~(VkDeviceSize)0;
    if (m.skinned && !skinned) continue;
    VkPipeline p = shadow ? (skinned ? pipeShadowSkinned_ : pipeShadowMesh_) : (skinned ? pipeMeshSkinned_ : pipeMesh_);
    if (p != cur) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
      cur = p;
      if (!globalsBound) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, shadow ? &fr.globalsA : &fr.globalsB, 0, nullptr);
        globalsBound = true;
      }
    }
    if (skinned) {
      uint32_t dynOff = (uint32_t)(boneBase + (VkDeviceSize)d.boneOffset * sizeof(Mat4));
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, shadow ? 1 : 2, 1, &fr.bones, 1, &dynOff);
    }
    if (!shadow) {
      int mat = d.material.valid() ? d.material.id : -1;
      if (mat < 0) continue;
      if (mat != curMat) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 1, 1, &materials_[mat], 0, nullptr);
        curMat = mat;
      }
      struct { Mat4 model; Vec4 tint, params; } pc{d.transform, d.tint, d.params};
      vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96, &pc);
    } else {
      struct { Mat4 model; int32_t cascade; int32_t pad[3]; } pc;
      pc.model = d.transform;
      pc.cascade = cascade;
      vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 80, &pc);
    }
    int lod = clamp(d.lod, 0, m.lodCount - 1);
    if (shadow) lod = std::min(m.lodCount - 1, lod + 1);  // shadows use a coarser LOD
    VkDeviceSize o = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &m.vb.buf, &o);
    vkCmdBindIndexBuffer(cb, m.ib.buf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, m.lods[lod].indexCount, 1, m.lods[lod].firstIndex, 0, 0);
  }
}

bool Renderer::renderFrame(const FrameData& fd) {
  if (!hasSwapchain()) return false;
  VkDevice dev = ctx_.device;
  FrameRes& fr = frames_[frameIndex_];
  VK_CHECK(vkWaitForFences(dev, 1, &fr.fence, VK_TRUE, UINT64_MAX));

  uint32_t imageIndex = 0;
  if (cfg_.headless) {
    imageIndex = lastImage_ ^ 1u;
  } else {
    VkResult r = vkAcquireNextImageKHR(dev, swapchain_, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { createSwapchain(outW_, outH_); return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
  }
  VK_CHECK(vkResetFences(dev, 1, &fr.fence));
  // release meshes retired at least kFrames+1 frames ago (no in-flight frame can still use them)
  for (size_t i = 0; i < retired_.size();) {
    if (retired_[i].frame + kFrames + 1 <= frameCounter_) {
      ctx_.destroyBuffer(retired_[i].vb);
      ctx_.destroyBuffer(retired_[i].ib);
      retired_[i] = retired_.back();
      retired_.pop_back();
    } else ++i;
  }
  ++frameCounter_;
  VkCommandBuffer cb = fr.cmd;
  VK_CHECK(vkResetCommandBuffer(cb, 0));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cb, &bi));
  fr.arenaOffset = 0;

  GlobalsUBO gu = fd.globals;
  // dynamic lights -> storage buffer: [128 x 3 vec4] then one list per screen tile (count + up to kTileCap light indices)
  {
    const int nl = std::min<int>((int)fd.lights.size(), kMaxLights);
    uint8_t* base = (uint8_t*)fr.lightBuf.map;
    std::memcpy(base, fd.lights.data(), sizeof(LightUBO) * (size_t)nl);
    uint32_t* tiles = (uint32_t*)(base + sizeof(LightUBO) * kMaxLights);
    const int stride = 1 + kTileCap;
    for (int t = 0; t < kTilesX * kTilesY; ++t) tiles[t * stride] = 0;
    Mat4 vp = fd.globals.viewProj;
    float sx = std::sqrt(vp.at(0, 0) * vp.at(0, 0) + vp.at(0, 1) * vp.at(0, 1) + vp.at(0, 2) * vp.at(0, 2));
    float sy = std::sqrt(vp.at(1, 0) * vp.at(1, 0) + vp.at(1, 1) * vp.at(1, 1) + vp.at(1, 2) * vp.at(1, 2));
    for (int i = 0; i < nl; ++i) {
      const LightUBO& L = fd.lights[i];
      float x = L.posRadius.x, y = L.posRadius.y, z = L.posRadius.z, r = L.posRadius.w;
      float cx = vp.at(0, 0) * x + vp.at(0, 1) * y + vp.at(0, 2) * z + vp.at(0, 3);
      float cy = vp.at(1, 0) * x + vp.at(1, 1) * y + vp.at(1, 2) * z + vp.at(1, 3);
      float cw = vp.at(3, 0) * x + vp.at(3, 1) * y + vp.at(3, 2) * z + vp.at(3, 3);
      int tx0 = 0, tx1 = kTilesX - 1, ty0 = 0, ty1 = kTilesY - 1;
      if (cw < -r) continue;                      // behind the camera
      if (cw > r) {                               // otherwise the camera is inside the sphere: every tile
        float ndx = cx / cw, ndy = cy / cw, rx = r * sx / cw * 1.15f, ry = r * sy / cw * 1.15f;
        tx0 = std::max(0, (int)std::floor((ndx - rx) * 0.5f * kTilesX + kTilesX * 0.5f));
        tx1 = std::min(kTilesX - 1, (int)std::floor((ndx + rx) * 0.5f * kTilesX + kTilesX * 0.5f));
        ty0 = std::max(0, (int)std::floor((ndy - ry) * 0.5f * kTilesY + kTilesY * 0.5f));
        ty1 = std::min(kTilesY - 1, (int)std::floor((ndy + ry) * 0.5f * kTilesY + kTilesY * 0.5f));
      }
      for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
          uint32_t* tl = tiles + (ty * kTilesX + tx) * stride;
          if (tl[0] < (uint32_t)kTileCap) { tl[1 + tl[0]] = (uint32_t)i; ++tl[0]; }   // lights come sorted by importance, so a full tile drops the least important
        }
    }
    gu.lightGrid = {(float)nl, (float)kTilesX / (float)sceneW_, (float)kTilesY / (float)sceneH_, (float)kTilesX};
  }
  VkDeviceSize goff;
  void* gdst = arenaAlloc(fr, sizeof(GlobalsUBO), &goff);  // offset 0
  std::memcpy(gdst, &gu, sizeof(GlobalsUBO));
  // skinning palettes (kMaxBones matrices per skinned draw, each block 256-byte aligned by the arena)
  VkDeviceSize boneBase = 0;
  if (!fd.bones.empty()) {
    void* bdst = arenaAlloc(fr, fd.bones.size() * sizeof(Mat4) + sizeof(Mat4) * kMaxBones, &boneBase);
    if (bdst) std::memcpy(bdst, fd.bones.data(), fd.bones.size() * sizeof(Mat4));
    else boneBase = ~(VkDeviceSize)0;
  }

  // ---- shadow pass (one render pass per cascade layer)
  const int cascades = clamp(fd.shadowCascades, 1, 2);
  for (int c = 0; c < 2; ++c) {
    uint32_t sm = (uint32_t)cfg_.shadowMapSize;
    VkClearValue cv{};
    cv.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = shadowPass_;
    rb.framebuffer = shadowFbs_[c];
    rb.renderArea = {{0, 0}, {sm, sm}};
    rb.clearValueCount = 1;
    rb.pClearValues = &cv;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    if (fd.drawShadows && shadowsEnabled_ && c < cascades) {
      VkViewport vp{0, 0, (float)sm, (float)sm, 0, 1};
      VkRect2D sc{{0, 0}, {sm, sm}};
      vkCmdSetViewport(cb, 0, 1, &vp);
      vkCmdSetScissor(cb, 0, 1, &sc);
      vkCmdSetDepthBias(cb, 1.5f, 0.0f, 2.0f);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeShadow_);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plShadow_, 0, 1, &fr.globalsA, 0, nullptr);
      struct { Mat4 model; int32_t cascade; int32_t pad[3]; } spc;
      spc.cascade = c;
      vkCmdPushConstants(cb, plShadow_, VK_SHADER_STAGE_VERTEX_BIT, 0, 80, &spc);
      for (int id : fd.shadowMeshes) {
        MeshRes& m = meshes_[id];
        if (!m.alive) continue;
        VkDeviceSize o = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &m.vb.buf, &o);
        vkCmdBindIndexBuffer(cb, m.ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, m.indexCount, 1, 0, 0, 0);
      }
      drawModels(cb, fr, fd, boneBase, true, c);
    }
    vkCmdEndRenderPass(cb);
  }

  // ---- scene pass
  {
    VkClearValue cv[2]{};
    cv[0].color = {{fd.globals.fog.x, fd.globals.fog.y, fd.globals.fog.z, 1.0f}};
    cv[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = scenePass_;
    rb.framebuffer = sceneFb_;
    rb.renderArea = {{0, 0}, {sceneW_, sceneH_}};
    rb.clearValueCount = 2;
    rb.pClearValues = cv;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)sceneW_, (float)sceneH_, 0, 1};
    VkRect2D sc{{0, 0}, {sceneW_, sceneH_}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    // sky
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeSky_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plSprite_, 0, 1, &fr.globalsB, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);

    // world
    if (fd.worldMaterial.valid()) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeWorld_);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plWorld_, 0, 1, &fr.globalsB, 0, nullptr);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plWorld_, 1, 1, &materials_[fd.worldMaterial.id], 0, nullptr);
      for (int id : fd.worldMeshes) {
        MeshRes& m = meshes_[id];
        if (!m.alive) continue;
        VkDeviceSize o = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &m.vb.buf, &o);
        vkCmdBindIndexBuffer(cb, m.ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, m.indexCount, 1, 0, 0, 0);
      }
    }
    // 3D models (vehicles, characters, props)
    drawModels(cb, fr, fd, boneBase, false, 0);

    // ground shadows / decals
    if (!fd.decals.empty()) {
      VkDeviceSize off;
      void* dst = arenaAlloc(fr, fd.decals.size() * sizeof(DecalInst), &off);
      if (dst) {
        std::memcpy(dst, fd.decals.data(), fd.decals.size() * sizeof(DecalInst));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeDecal_);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plSprite_, 0, 1, &fr.globalsB, 0, nullptr);
        vkCmdBindVertexBuffers(cb, 0, 1, &fr.arena.buf, &off);
        vkCmdDraw(cb, 4, (uint32_t)fd.decals.size(), 0, 0);
      }
    }
    // billboards (sorted far -> near by the game)
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plSprite_, 0, 1, &fr.globalsB, 0, nullptr);
    drawBatches(cb, fr, fd.spriteBatches, fd.sprites.data(), sizeof(SpriteInst), pipeSprite_, plSprite_, 4, 1);
    // occluded-silhouette pass
    drawBatches(cb, fr, fd.silhouetteBatches, fd.silhouettes.data(), sizeof(SpriteInst), pipeSilhouette_, plSprite_, 4, 1);
    vkCmdEndRenderPass(cb);
  }

  // ---- ambient occlusion + sky mask (half resolution). When off the target is only cleared to "fully visible".
  const bool doAo = aoSupported_ && (fd.aoStrength > 0.001f || fd.shaftIntensity > 0.001f);
  {
    auto aoPass = [&](VkFramebuffer fb, VkPipeline pipe, VkDescriptorSet set, bool draw) {
      VkClearValue cv{};
      cv.color = {{1.0f, 0.0f, 0.0f, 1.0f}};
      VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rb.renderPass = aoPass_;
      rb.framebuffer = fb;
      rb.renderArea = {{0, 0}, {aoW_, aoH_}};
      rb.clearValueCount = 1;
      rb.pClearValues = &cv;
      vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
      if (draw) {
        VkViewport vp{0, 0, (float)aoW_, (float)aoH_, 0, 1};
        VkRect2D sc{{0, 0}, {aoW_, aoH_}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plAo_, 1, 1, &set, 0, nullptr);
        float pc[8] = {fd.nearZ, fd.farZ, fd.tanHalfX, fd.tanHalfY, fd.aoRadius, std::max(fd.aoStrength, 0.0f) * 1.6f + 0.4f, 1.0f / aoW_, 1.0f / aoH_};
        vkCmdPushConstants(cb, plAo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc);
        vkCmdDraw(cb, 3, 1, 0, 0);
      }
      vkCmdEndRenderPass(cb);
    };
    if (doAo) {
      aoPass(aoFbA_, pipeAo_, aoDepthSet_, true);
      aoPass(aoFbB_, pipeAoBlur_, aoBlurSet_, true);
    } else {
      aoPass(aoFbB_, VK_NULL_HANDLE, VK_NULL_HANDLE, false);
    }
  }

  // ---- blur chain (Dual Kawase) only when needed
  bool doBlur = fd.blur > 0.01f;
  bool doBloom = fd.bloom > 0.001f;
  if (doBlur || doBloom) {
    auto pass = [&](BlurLevel& dstL, VkDescriptorSet src, float srcW, float srcH, VkPipeline pipe, float offset) {
      VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rb.renderPass = blurPass_;
      rb.framebuffer = dstL.fb;
      rb.renderArea = {{0, 0}, {dstL.w, dstL.h}};
      vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
      VkViewport vp{0, 0, (float)dstL.w, (float)dstL.h, 0, 1};
      VkRect2D sc{{0, 0}, {dstL.w, dstL.h}};
      vkCmdSetViewport(cb, 0, 1, &vp);
      vkCmdSetScissor(cb, 0, 1, &sc);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plBlur_, 1, 1, &src, 0, nullptr);
      float pc[4] = {0.5f / srcW, 0.5f / srcH, offset, 0};
      vkCmdPushConstants(cb, plBlur_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
      vkCmdDraw(cb, 3, 1, 0, 0);
      vkCmdEndRenderPass(cb);
    };
    float off = doBlur ? 1.6f : 1.0f;
    pass(blurDown_[0], sceneSampleSet_, (float)sceneW_, (float)sceneH_, pipeBlurDown_, off);
    for (int i = 1; i < 4; ++i)
      pass(blurDown_[i], blurDown_[i - 1].set, (float)blurDown_[i - 1].w, (float)blurDown_[i - 1].h, pipeBlurDown_, off);
    pass(blurUp_[0], blurDown_[3].set, (float)blurDown_[3].w, (float)blurDown_[3].h, pipeBlurUp_, off);
    pass(blurUp_[1], blurUp_[0].set, (float)blurUp_[0].w, (float)blurUp_[0].h, pipeBlurUp_, off);
    pass(blurUp_[2], blurUp_[1].set, (float)blurUp_[1].w, (float)blurUp_[1].h, pipeBlurUp_, off);
  }

  // ---- composite + UI
  {
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = compositePass_;
    rb.framebuffer = outFbs_[imageIndex];
    rb.renderArea = {{0, 0}, {outW_, outH_}};
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)outW_, (float)outH_, 0, 1};
    VkRect2D sc{{0, 0}, {outW_, outH_}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeComposite_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, plComposite_, 1, 1, &compositeSet_, 0, nullptr);
    float pcv[32] = {doBlur ? fd.blur : 0.0f, fd.fade, fd.vignette, outIsSrgb_ ? 1.0f : 0.0f,
                     fd.exposure * (1.0f - 0.45f * fd.dim), (doBlur || doBloom) ? fd.bloom : 0.0f, fd.bloomThreshold,
                     fd.globals.camPos.w,
                     fd.lift.x, fd.lift.y, fd.lift.z, fd.lift.w, fd.gain.x, fd.gain.y, fd.gain.z, fd.gain.w,
                     doAo ? fd.aoStrength : 0.0f, doAo ? fd.shaftIntensity : 0.0f, fd.sunUV.x, fd.sunUV.y,
                     fd.shaftColor.x, fd.shaftColor.y, fd.shaftColor.z, 0.0f,
                     fd.nearZ, fd.farZ, fd.tanHalfX, fd.tanHalfY,
                     fd.upView.x, fd.upView.y, fd.upView.z, aoSupported_ ? fd.wetness : 0.0f};
    vkCmdPushConstants(cb, plComposite_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, pcv);
    vkCmdDraw(cb, 3, 1, 0, 0);
    if (!fd.uiBatches.empty()) {
      float upc[4] = {(float)outW_, (float)outH_, outIsSrgb_ ? 1.0f : 0.0f, 0};
      vkCmdPushConstants(cb, plUi_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, upc);
      drawBatches(cb, fr, fd.uiBatches, fd.ui.data(), sizeof(UiInst), pipeUi_, plUi_, 4, 1);
    }
    vkCmdEndRenderPass(cb);
  }

  bool doReadback = cfg_.headless && wantReadback_;
  if (doReadback) {
    VkBufferImageCopy rg{};
    rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    rg.imageExtent = {outW_, outH_, 1};
    vkCmdCopyImageToBuffer(cb, headlessImages_[imageIndex].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuf_.buf, 1, &rg);
  }
  VK_CHECK(vkEndCommandBuffer(cb));

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  if (!cfg_.headless) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &fr.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderDone_[imageIndex];
  }
  VK_CHECK(vkQueueSubmit(ctx_.queue, 1, &si, fr.fence));

  if (!cfg_.headless) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex;
    VkResult r = vkQueuePresentKHR(ctx_.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) createSwapchain(outW_, outH_);
  } else if (doReadback) {
    vkWaitForFences(dev, 1, &fr.fence, VK_TRUE, UINT64_MAX);
  }
  lastImage_ = imageIndex;
  frameIndex_ = (frameIndex_ + 1) % kFrames;
  return true;
}

bool Renderer::readback(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
  if (!cfg_.headless || !wantReadback_) return false;
  wantReadback_ = false;
  w = outW_; h = outH_;
  rgba.resize((size_t)w * h * 4);
  std::memcpy(rgba.data(), readbackBuf_.map, rgba.size());
  return true;
}

}  // namespace gfx
}  // namespace gtabr
