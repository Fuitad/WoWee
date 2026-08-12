#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <limits>
#include <cstdlib>

namespace wowee {
namespace rendering {

class VkContext;

struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
};

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// Destroying a handle and forgetting it, which the renderers do about two
// hundred and twenty times between them.
//
// Every one of those sites was the same four lines: test the handle, destroy
// it, set it back to VK_NULL_HANDLE. Written out, the last step is easy to
// leave off, and a handle that still holds a destroyed object is a double
// destroy the next time a shutdown runs. Taking the handle by reference makes
// forgetting it part of destroying it.

inline void destroy(VkDevice device, VkPipeline& pipeline) {
    if (pipeline == VK_NULL_HANDLE) return;
    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
}

inline void destroy(VkDevice device, VkPipelineLayout& layout) {
    if (layout == VK_NULL_HANDLE) return;
    vkDestroyPipelineLayout(device, layout, nullptr);
    layout = VK_NULL_HANDLE;
}

inline void destroy(VkDevice device, VkDescriptorSetLayout& layout) {
    if (layout == VK_NULL_HANDLE) return;
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    layout = VK_NULL_HANDLE;
}

inline void destroy(VkDevice device, VkDescriptorPool& pool) {
    if (pool == VK_NULL_HANDLE) return;
    vkDestroyDescriptorPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;
}

inline void destroy(VkDevice device, VkSampler& sampler) {
    if (sampler == VK_NULL_HANDLE) return;
    vkDestroySampler(device, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
}

/// A buffer and its allocation go together, and forgetting one of the two is
/// the same fault as forgetting the handle.
inline void destroy(VmaAllocator allocator, VkBuffer& buffer, VmaAllocation& allocation) {
    if (buffer == VK_NULL_HANDLE) return;
    vmaDestroyBuffer(allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

// Buffer creation
AllocatedBuffer createBuffer(VmaAllocator allocator, VkDeviceSize size,
    VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

void destroyBuffer(VmaAllocator allocator, AllocatedBuffer& buffer);

// Image creation
AllocatedImage createImage(VkDevice device, VmaAllocator allocator,
    uint32_t width, uint32_t height, VkFormat format,
    VkImageUsageFlags usage, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    uint32_t mipLevels = 1);

void destroyImage(VkDevice device, VmaAllocator allocator, AllocatedImage& image);

// Image layout transitions
void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

// Staging upload helper - copies CPU data to a GPU-local buffer
AllocatedBuffer uploadBuffer(VkContext& ctx, const void* data, VkDeviceSize size,
    VkBufferUsageFlags usage);

// Environment variable utility functions
inline size_t envSizeMBOrDefault(const char* name, size_t defMb) {
    const char* v = std::getenv(name);
    if (!v || !*v) return defMb;
    char* end = nullptr;
    unsigned long long mb = std::strtoull(v, &end, 10);
    if (end == v || mb == 0) return defMb;
    if (mb > (std::numeric_limits<size_t>::max() / (1024ull * 1024ull))) return defMb;
    return static_cast<size_t>(mb);
}

inline size_t envSizeOrDefault(const char* name, size_t defValue) {
    const char* v = std::getenv(name);
    if (!v || !*v) return defValue;
    char* end = nullptr;
    unsigned long long n = std::strtoull(v, &end, 10);
    if (end == v || n == 0) return defValue;
    return static_cast<size_t>(n);
}

// Opt-in rendering diagnostics, read once per process. These exist to bisect a
// visual artifact to the subsystem that draws it: turn one off and see whether
// the artifact survives. Any value other than "0" or empty enables the flag.
inline bool envFlagEnabled(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && !(v[0] == '0' && v[1] == '\0');
}

} // namespace rendering
} // namespace wowee
