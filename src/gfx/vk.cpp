#include "vk.h"

#include <cstring>

namespace gtabr {
namespace gfx {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
  if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) LOGW("[VVL] %s", d->pMessage);
  return VK_FALSE;
}

bool VkCtx::init(bool headless_, const SurfaceFactory& surfaceFactory, const std::vector<const char*>& extraExts,
                 bool validation) {
  headless = headless_;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "GTABR";
  app.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> exts(extraExts.begin(), extraExts.end());
  std::vector<const char*> layers;
  uint32_t n = 0;
  vkEnumerateInstanceLayerProperties(&n, nullptr);
  std::vector<VkLayerProperties> lp(n);
  vkEnumerateInstanceLayerProperties(&n, lp.data());
  bool haveVVL = false;
  for (auto& l : lp)
    if (!strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) haveVVL = true;
  bool useValidation = validation && haveVVL;
  if (useValidation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = (uint32_t)exts.size();
  ici.ppEnabledExtensionNames = exts.data();
  ici.enabledLayerCount = (uint32_t)layers.size();
  ici.ppEnabledLayerNames = layers.data();
  VkResult r = vkCreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS) {
    LOGE("vkCreateInstance failed (%d)", (int)r);
    return false;
  }
  if (useValidation) {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn) {
      VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      mi.pfnUserCallback = debugCb;
      fn(instance, &mi, nullptr, &debugMessenger);
    }
  }

  if (!headless && surfaceFactory) surface = surfaceFactory(instance);
  if (!headless && surface == VK_NULL_HANDLE) {
    LOGE("No surface");
    return false;
  }

  uint32_t pc = 0;
  vkEnumeratePhysicalDevices(instance, &pc, nullptr);
  if (!pc) {
    LOGE("No Vulkan devices");
    return false;
  }
  std::vector<VkPhysicalDevice> pds(pc);
  vkEnumeratePhysicalDevices(instance, &pc, pds.data());
  // Prefer a device with a graphics queue that can present; prefer discrete/integrated over CPU.
  int bestScore = -1;
  for (auto pd : pds) {
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(pd, &pp);
    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qf.data());
    for (uint32_t i = 0; i < qc; ++i) {
      if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
      VkBool32 present = headless;
      if (!headless) vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
      if (!present) continue;
      int score = pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                  : pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
      if (score > bestScore) { bestScore = score; phys = pd; queueFamily = i; }
      break;
    }
  }
  if (!phys) {
    LOGE("No suitable device");
    return false;
  }
  vkGetPhysicalDeviceProperties(phys, &devProps);
  vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
  VkPhysicalDeviceFeatures feats;
  vkGetPhysicalDeviceFeatures(phys, &feats);
  caps.astcLdr = feats.textureCompressionASTC_LDR;
  caps.etc2 = feats.textureCompressionETC2;
  caps.anisotropy = feats.samplerAnisotropy;
  caps.maxAniso = devProps.limits.maxSamplerAnisotropy;
  caps.maxImageDim = devProps.limits.maxImageDimension2D;
  caps.maxArrayLayers = devProps.limits.maxImageArrayLayers;
  snprintf(caps.deviceName, sizeof(caps.deviceName), "%s", devProps.deviceName);
  caps.apiVersion = devProps.apiVersion;
  LOGI("GPU: %s (api %u.%u.%u) astc=%d etc2=%d aniso=%d", caps.deviceName, VK_VERSION_MAJOR(caps.apiVersion),
       VK_VERSION_MINOR(caps.apiVersion), VK_VERSION_PATCH(caps.apiVersion), caps.astcLdr, caps.etc2, caps.anisotropy);

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkPhysicalDeviceFeatures enable{};
  enable.samplerAnisotropy = feats.samplerAnisotropy;
  enable.textureCompressionASTC_LDR = feats.textureCompressionASTC_LDR;
  enable.textureCompressionETC2 = feats.textureCompressionETC2;
  std::vector<const char*> dexts;
  if (!headless) dexts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.pEnabledFeatures = &enable;
  dci.enabledExtensionCount = (uint32_t)dexts.size();
  dci.ppEnabledExtensionNames = dexts.data();
  r = vkCreateDevice(phys, &dci, nullptr, &device);
  if (r != VK_SUCCESS) {
    LOGE("vkCreateDevice failed (%d)", (int)r);
    return false;
  }
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.queueFamilyIndex = queueFamily;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(device, &cpi, nullptr, &cmdPool));
  return true;
}

void VkCtx::shutdown() {
  if (device) {
    vkDeviceWaitIdle(device);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
  }
  if (instance) {
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (debugMessenger) {
      auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
      if (fn) fn(instance, debugMessenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
  }
  *this = VkCtx();
}

uint32_t VkCtx::findMemoryType(uint32_t bits, VkMemoryPropertyFlags props) const {
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
  return 0xFFFFFFFFu;
}

GpuMemStats gGpuMem;

Buffer VkCtx::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
  Buffer b;
  b.size = size;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(device, &bi, nullptr, &b.buf));
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(device, b.buf, &mr);
  VkMemoryPropertyFlags want = hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                           : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  uint32_t mt = findMemoryType(mr.memoryTypeBits, want);
  if (mt == 0xFFFFFFFFu) mt = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = mt;
  VK_CHECK(vkAllocateMemory(device, &ai, nullptr, &b.mem));
  VK_CHECK(vkBindBufferMemory(device, b.buf, b.mem, 0));
  b.alloc = mr.size;
  gGpuMem.bytes += (long long)mr.size; gGpuMem.allocs++;
  if (hostVisible) gGpuMem.hostBytes += (long long)mr.size;
  if (hostVisible) VK_CHECK(vkMapMemory(device, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
  return b;
}

void VkCtx::destroyBuffer(Buffer& b) {
  if (b.mem) { gGpuMem.bytes -= (long long)b.alloc; gGpuMem.allocs--; if (b.map) gGpuMem.hostBytes -= (long long)b.alloc; }
  if (b.map) vkUnmapMemory(device, b.mem);
  if (b.buf) vkDestroyBuffer(device, b.buf, nullptr);
  if (b.mem) vkFreeMemory(device, b.mem, nullptr);
  b = Buffer();
}

Image VkCtx::createImage(uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, VkFormat fmt, VkImageUsageFlags usage,
                         VkImageAspectFlags aspect, VkSampleCountFlagBits samples) {
  Image im;
  im.width = w; im.height = h; im.layers = layers; im.mips = mips; im.format = fmt;
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {w, h, 1};
  ii.mipLevels = mips;
  ii.arrayLayers = layers;
  ii.samples = samples;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = usage;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(device, &ii, nullptr, &im.image));
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(device, im.image, &mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (ai.memoryTypeIndex == 0xFFFFFFFFu) ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, 0);
  VK_CHECK(vkAllocateMemory(device, &ai, nullptr, &im.mem));
  VK_CHECK(vkBindImageMemory(device, im.image, im.mem, 0));
  im.alloc = mr.size;
  gGpuMem.bytes += (long long)mr.size; gGpuMem.allocs++;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = im.image;
  vi.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {aspect, 0, mips, 0, layers};
  VK_CHECK(vkCreateImageView(device, &vi, nullptr, &im.view));
  return im;
}

void VkCtx::destroyImage(Image& i) {
  if (i.mem) { gGpuMem.bytes -= (long long)i.alloc; gGpuMem.allocs--; }
  if (i.view) vkDestroyImageView(device, i.view, nullptr);
  if (i.image) vkDestroyImage(device, i.image, nullptr);
  if (i.mem) vkFreeMemory(device, i.mem, nullptr);
  i = Image();
}

VkShaderModule VkCtx::createShader(const uint32_t* code, size_t sizeBytes) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = sizeBytes;
  ci.pCode = code;
  VkShaderModule m = VK_NULL_HANDLE;
  VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
  return m;
}

VkCommandBuffer VkCtx::beginOneShot() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cb));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cb, &bi));
  return cb;
}

void VkCtx::endOneShot(VkCommandBuffer cb) {
  VK_CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  VK_CHECK(vkQueueWaitIdle(queue));
  vkFreeCommandBuffers(device, cmdPool, 1, &cb);
}

void VkCtx::transitionImage(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags aspect,
                            uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {aspect, baseMip, mipCount, baseLayer, layerCount};
  VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  switch (from) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; src = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; src = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
    default: break;
  }
  switch (to) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
    default: break;
  }
  vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace gfx
}  // namespace gtabr
