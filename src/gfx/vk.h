// Thin Vulkan helper layer: instance/device, memory, buffers, images, one-shot commands.
#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/log.h"

#define VK_CHECK(x)                                                                  \
  do {                                                                               \
    VkResult _r = (x);                                                               \
    if (_r != VK_SUCCESS) {                                                          \
      LOGE("Vulkan error %d at %s:%d (%s)", (int)_r, __FILE__, __LINE__, #x);        \
    }                                                                                \
  } while (0)

namespace gtabr {
namespace gfx {

struct Buffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  void* map = nullptr;
  VkDeviceSize size = 0;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0, layers = 1, mips = 1;
};

struct DeviceCaps {
  bool astcLdr = false;
  bool etc2 = false;
  bool anisotropy = false;
  float maxAniso = 1.0f;
  uint32_t maxImageDim = 4096;
  uint32_t maxArrayLayers = 256;
  char deviceName[256] = {};
  uint32_t apiVersion = 0;
};

// Callback that creates a presentation surface for the given instance (Android window / none for headless).
using SurfaceFactory = std::function<VkSurfaceKHR(VkInstance)>;

class VkCtx {
 public:
  bool init(bool headless, const SurfaceFactory& surfaceFactory, const std::vector<const char*>& extraInstanceExts,
            bool enableValidation);
  void shutdown();

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
  Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
  void destroyBuffer(Buffer& b);
  // Creates a 2D / 2D-array image (device local) with an image view.
  Image createImage(uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, VkFormat fmt, VkImageUsageFlags usage,
                    VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
  void destroyImage(Image& i);
  VkShaderModule createShader(const uint32_t* code, size_t sizeBytes);

  // One-shot command buffer helpers (blocking).
  VkCommandBuffer beginOneShot();
  void endOneShot(VkCommandBuffer cb);
  void transitionImage(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkImageAspectFlags aspect,
                       uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount);

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memProps{};
  VkPhysicalDeviceProperties devProps{};
  DeviceCaps caps;
  bool headless = false;
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
};

}  // namespace gfx
}  // namespace gtabr
