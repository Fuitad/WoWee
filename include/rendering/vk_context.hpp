#pragma once

#include "rendering/vk_utils.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL2/SDL.h>
#include <vector>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace wowee {
namespace rendering {

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

class VkContext {
public:
    VkContext() = default;
    ~VkContext();

    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;

    [[nodiscard]] bool initialize(SDL_Window* window);
    void shutdown();

    // Swapchain management
    [[nodiscard]] bool recreateSwapchain(int width, int height);

    // Frame operations
    VkCommandBuffer beginFrame(uint32_t& imageIndex);
    void endFrame(VkCommandBuffer cmd, uint32_t imageIndex);

    // Single-time command buffer helpers
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);

    // Immediate submit for one-off GPU work (descriptor pool creation, etc.)
    void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

    // Batch upload mode: records multiple upload commands into a single
    // command buffer, then submits with ONE fence wait instead of one per upload.
    void beginUploadBatch();
    void endUploadBatch();       // Async: submits but does NOT wait for fence
    void endUploadBatchSync();   // Sync: submits and waits (for load screens)
    bool isInUploadBatch() const { return inUploadBatch_; }
    void deferStagingCleanup(AllocatedBuffer staging);
    void pollUploadBatches();    // Check completed async uploads, free staging buffers
    void waitAllUploads();       // Block until all in-flight uploads complete

    // Defer resource destruction until it is safe with multiple frames in flight.
    //
    // This queues work to run after the fence for the *current frame slot* has
    // signaled the next time we enter beginFrame() for that slot (i.e. after
    // MAX_FRAMES_IN_FLIGHT submissions). Use this for resources that may still
    // be referenced by command buffers submitted in the previous frame(s),
    // such as descriptor sets and buffers freed during streaming/unload.
    void deferAfterFrameFence(std::function<void()>&& fn);
    // Like deferAfterFrameFence, but waits until ALL in-flight frame slots have
    // been fenced — safe for shared resources bound by multiple frames' command
    // buffers (material descriptor sets, vertex/index buffers, etc.).
    void deferAfterAllFrameFences(std::function<void()>&& fn);

    // Accessors
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    uint32_t getGpuVendorId() const { return gpuVendorId_; }
    const char* getGpuName() const { return gpuName_; }
    bool isAmdGpu() const { return gpuVendorId_ == 0x1002; }
    bool isNvidiaGpu() const { return gpuVendorId_ == 0x10DE; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }
    bool hasDedicatedTransferQueue() const { return hasDedicatedTransfer_; }
    VmaAllocator getAllocator() const { return allocator; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkPipelineCache getPipelineCache() const { return pipelineCache_; }

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkFormat getSwapchainFormat() const { return swapchainFormat; }
    VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
    const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    const std::vector<VkImage>& getSwapchainImages() const { return swapchainImages; }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }

    uint32_t getCurrentFrame() const { return currentFrame; }
    const FrameData& getCurrentFrameData() const { return frames[currentFrame]; }

    // For ImGui
    VkRenderPass getImGuiRenderPass() const { return imguiRenderPass; }
    // Compatible with getImGuiRenderPass() but loads the swapchain rather than
    // clearing it. Null under MSAA, where a second pass would have to resolve
    // again and could not preserve the already-resolved image.
    VkRenderPass getOverlayRenderPass() const { return overlayRenderPass; }
    VkDescriptorPool getImGuiDescriptorPool() const { return imguiDescriptorPool; }
    const std::vector<VkFramebuffer>& getSwapchainFramebuffers() const { return swapchainFramebuffers; }

    bool isSwapchainDirty() const { return swapchainDirty; }
    void markSwapchainDirty() { swapchainDirty = true; }

    // VSync (present mode)
    bool isVsyncEnabled() const { return vsync_; }
    void setVsync(bool enabled) { vsync_ = enabled; }

    bool isDeviceLost() const { return deviceLost_; }

    // MSAA
    VkSampleCountFlagBits getMsaaSamples() const { return msaaSamples_; }
    void setMsaaSamples(VkSampleCountFlagBits samples);
    VkSampleCountFlagBits getMaxUsableSampleCount() const;
    VkImage getDepthImage() const { return depthImage; }
    VkImage getDepthCopySourceImage() const {
        return (depthResolveImage != VK_NULL_HANDLE) ? depthResolveImage : depthImage;
    }
    bool isDepthCopySourceMsaa() const {
        return (depthResolveImage == VK_NULL_HANDLE) && (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT);
    }
    VkFormat getDepthFormat() const { return depthFormat; }
    VkImageView getDepthResolveImageView() const { return depthResolveImageView; }
    VkImageView getDepthImageView() const { return depthImageView; }

    // Sampler cache: returns a shared VkSampler matching the given create info.
    // Callers must NOT destroy the returned sampler — it is owned by VkContext.
    // Automatically clamps anisotropy if the device doesn't support it.
    VkSampler getOrCreateSampler(const VkSamplerCreateInfo& info);

    // Whether the physical device supports sampler anisotropy.
    bool isSamplerAnisotropySupported() const { return samplerAnisotropySupported_; }

    // Global sampler cache accessor (set during VkContext::initialize, cleared on shutdown).
    // Used by VkTexture and other code that only has a VkDevice handle.
    static VkContext* globalInstance() { return sInstance_; }

    // UI texture upload: creates a Vulkan texture from RGBA data and returns
    // a VkDescriptorSet suitable for use as ImTextureID.
    // The caller does NOT need to free the result — resources are tracked and
    // cleaned up when the VkContext is destroyed.
    VkDescriptorSet uploadImGuiTexture(const uint8_t* rgba, int width, int height);

private:
    bool createInstance(SDL_Window* window);
    bool createSurface(SDL_Window* window);
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createSwapchain(int width, int height);
    void destroySwapchain();
    bool createCommandPools();
    bool createSyncObjects();
    bool createPipelineCache();
    void savePipelineCache();
    bool createImGuiResources();
    void destroyImGuiResources();

    // vk-bootstrap objects (kept alive for swapchain recreation etc.)
    vkb::Instance vkbInstance_;
    vkb::PhysicalDevice vkbPhysicalDevice_;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Pipeline cache (persisted to disk for faster startup)
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    uint32_t gpuVendorId_ = 0;
    char gpuName_[256] = {};

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    uint32_t presentQueueFamily = 0;

    // Dedicated transfer queue (second queue from same graphics family)
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    VkCommandPool transferCommandPool_ = VK_NULL_HANDLE;
    bool hasDedicatedTransfer_ = false;
    uint32_t graphicsQueueFamilyQueueCount_ = 1; // queried in selectPhysicalDevice

    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent = {0, 0};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    bool swapchainDirty = false;
    bool deviceLost_ = false;
    bool vsync_ = true;

    // Per-frame resources
    FrameData frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame = 0;

    // Per-swapchain-image semaphores (avoids reuse while presentation engine holds them)
    std::vector<VkSemaphore> imageAcquiredSemaphores_;   // [swapchainImageCount], per-image
    std::vector<VkSemaphore> renderFinishedSemaphores_;  // [swapchainImageCount], per-image
    VkSemaphore nextAcquireSemaphore_ = VK_NULL_HANDLE;  // free semaphore for next acquire
    VkSemaphore currentAcquireSemaphore_ = VK_NULL_HANDLE; // the one used for the current frame

    // Immediate submit resources
    VkCommandPool immCommandPool = VK_NULL_HANDLE;
    VkFence immFence = VK_NULL_HANDLE;
    // Cached, reusable cmd buffer for beginSingleTimeCommands. Pool was created
    // with RESET_COMMAND_BUFFER_BIT so we can reset and reuse instead of
    // round-tripping through vkAllocateCommandBuffers + vkFreeCommandBuffers
    // every immediate submit (the M2 frustum-cull dispatch fires this per frame).
    VkCommandBuffer immCmdBuf_ = VK_NULL_HANDLE;

    // Batch upload state (nesting-safe via depth counter)
    int uploadBatchDepth_ = 0;
    bool inUploadBatch_ = false;
    VkCommandBuffer batchCmd_ = VK_NULL_HANDLE;
    std::vector<AllocatedBuffer> batchStagingBuffers_;

    // Async upload: in-flight batches awaiting GPU completion
    struct InFlightBatch {
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        std::vector<AllocatedBuffer> stagingBuffers;
    };
    std::vector<InFlightBatch> inFlightBatches_;

    void runDeferredCleanup(uint32_t frameIndex);
    std::vector<std::function<void()>> deferredCleanup_[MAX_FRAMES_IN_FLIGHT];

    // Depth buffer (shared across all framebuffers)
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    bool createDepthBuffer();
    void destroyDepthBuffer();

    // MSAA resources
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaaColorImage_ = VK_NULL_HANDLE;
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    VmaAllocation msaaColorAllocation_ = VK_NULL_HANDLE;

    bool createMsaaColorImage();
    void destroyMsaaColorImage();
    bool createDepthResolveImage();
    void destroyDepthResolveImage();

    // Actual Vulkan API version the instance was created with (gates core 1.2 calls)
    uint32_t instanceApiVersion_ = VK_API_VERSION_1_1;

    // MSAA depth resolve support (for sampling/copying resolved depth)
    bool depthResolveSupported_ = false;
    VkResolveModeFlagBits depthResolveMode_ = VK_RESOLVE_MODE_NONE;
    VkImage depthResolveImage = VK_NULL_HANDLE;
    VkImageView depthResolveImageView = VK_NULL_HANDLE;
    VmaAllocation depthResolveAllocation = VK_NULL_HANDLE;

    // ImGui resources
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    VkRenderPass overlayRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    // Shared sampler for UI textures (created on first uploadImGuiTexture call)
    VkSampler uiTextureSampler_ = VK_NULL_HANDLE;

    // Tracked UI textures for cleanup
    struct UiTexture {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
    };
    std::vector<UiTexture> uiTextures_;

    // Sampler cache — deduplicates VkSamplers by configuration hash.
    std::mutex samplerCacheMutex_;
    std::unordered_map<uint64_t, VkSampler> samplerCache_;
    bool samplerAnisotropySupported_ = false;

    static VkContext* sInstance_;

#ifndef NDEBUG
    bool enableValidation = true;
#else
    bool enableValidation = false;
#endif
};

} // namespace rendering
} // namespace wowee
