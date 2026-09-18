// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VulkanRenderDeviceUVE implementation — the M1 bootstrap bring-up pipeline:
//
//   1. LoadGlobalUVE()          (dlopen loader, global entry points)
//   2. vkCreateInstance         (with the window's required WSI extensions)
//   3. Physical device pickup   (first discrete-or-integrated GPU with a graphics+present queue)
//   4. vkCreateDevice           (single queue family, VK_KHR_swapchain enabled)
//   5. Window surface           (through the Window::IVulkanWindowSurfaceUVE capability bridge;
//                                opaque handles at the boundary, real VkSurfaceKHR here)
//   6. Swapchain + image views  (FIFO present mode, 8-bit RGBA/BGRA sRGB surface pick)
//   7. Render pass / framebuffers / per-frame sync (one frame in flight — header documents why)
//
// Every step's failure path is a logged bail returning nullptr from CreateUVE(); the destructor
// destroys strictly in reverse construction order, so a partially-completed bring-up is torn
// down by precisely the members that finished.
//
// Clear-colour provenance: the M1 device presents an engineering "visible life" clear — a
// muted blue-grey — as its only frame content. Deterministic, self-documenting in a screenshot,
// and impossible to confuse with real scene output; successive milestones replace it once the
// pipeline graph exists.


#include "uve/rhi_vulkan/vulkan_render_device_uve.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "spirv_reflect.h"

#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/recorded_command_uve.h"
#include "uve/window/i_vulkan_window_surface_uve.h"
#include "vk_functions_uve.h"

namespace UVE::Render {
namespace {

// The M1 engineering clear colour: muted blue-grey, linear-space shortcuts acceptable here
// because the surface format is sRGB and the values are literal displays of "the device works".
constexpr float kBootstrapClearRedUVE = 0.05F;
constexpr float kBootstrapClearGreenUVE = 0.07F;
constexpr float kBootstrapClearBlueUVE = 0.12F;
constexpr float kBootstrapClearAlphaUVE = 1.0F;

[[nodiscard]] VkSurfaceKHR ToVkSurfaceUVE(const std::uintptr_t bits) noexcept {
    // The bridge contract: 0 = failure/unavailable; otherwise the exact VkSurfaceKHR bits or
    // pointer bytes. Vulkan's headers define non-dispatchable handles two ways — an opaque
    // struct pointer when VK_USE_64_BIT_PTR_DEFINES == 1 (the default on 64-bit builds) or a
    // plain uint64_t when 0 — and the bridge is ABI-safe either way, since both fit uintptr_t.
    return std::bit_cast<VkSurfaceKHR>(bits); // both forms are exactly pointer-sized/uint64-sized
}

/// M2e: RHI load-op contract → attachment load op. The swapchain's full-frame-clear policy
/// stays its own thing (see PresentUVE); these route the offscreen/pass-level request.
[[nodiscard]] constexpr VkAttachmentLoadOp ToVkLoadOpUVE(const LoadOpUVE loadOp) noexcept {
    switch (loadOp) {
        case LoadOpUVE::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOpUVE::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOpUVE::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR; // unreachable; constexpr-safe default
}

} // namespace

namespace {

// --- M2b uniform reflection support types (module-internal) --------------------------------

/// One named uniform inside a uniform block or the push-constant block (M2b resolution unit
/// for SetUniform*): flat top-level members only — nested struct flattening is documented
/// outside this slice.
struct UniformMemberRefUVE {
    std::string name;
    ShaderDataTypeUVE type = ShaderDataTypeUVE::Float;
    std::uint32_t offset = 0U;    // byte offset inside its block
    std::uint32_t size = 0U;      // byte size (GL-style write bounds)
    std::int32_t blockIndex = -1; // index into uniformBlocks; -1 == push-constant member
    std::uint32_t arraySize = 1U;
};
/// One reflected uniform block (descriptor set 0; one binding == one block in M2b).
struct UniformBlockRefUVE {
    std::uint32_t binding = 0U;
    std::uint32_t size = 0U;
    VkShaderStageFlags stageFlags = 0U;
    std::vector<std::byte> shadow; // CPU-side live copy written by SetUniform* replay
    std::vector<UniformMemberRefUVE> members;
};
struct PushConstantBlockRefUVE {
    std::uint32_t offset = 0U;
    std::uint32_t size = 0U;
    VkShaderStageFlags stageFlags = 0U;
    std::vector<std::byte> shadow;
    std::vector<UniformMemberRefUVE> members;
    bool valid = false;
};

/// One reflected texture-family binding (M2c combined-image-sampler; M2f the separate
/// SAMPLED_IMAGE form; M5b STORAGE_IMAGE): RHI texture "slots" mirror GL texture units — the
/// pipeline's i-th texture-family binding (sorted ascending by binding number, sampled and
/// storage forms sharing ONE slot space) is fed from global BindTextureUVE slot i.
struct TextureSlotRefUVE {
    std::uint32_t binding = 0U;
    std::string name; // reflected sampler/image name (diagnostics only; binding is by slot)
    VkShaderStageFlags stageFlags = 0U;
    bool separateSampler = false; // M2f: SAMPLED_IMAGE form (pairs with the fixed device sampler)
    // M5b: STORAGE_IMAGE form (imageLoad/imageStore). Such slots write a STORAGE_IMAGE
    // descriptor with VK_IMAGE_LAYOUT_GENERAL and permanently transition their texture to
    // GENERAL (a valid — if unoptimal — sampling layout, so cached sampled descriptors of
    // the same texture get invalidated and rewritten with the GENERAL layout too).
    bool isStorageImage = false;
};

/// One reflected STORAGE_BUFFER binding (M2f): slot semantics mirror the texture slots — the
/// i-th storage binding (sorted ascending) is fed from BindStorageBufferUVE slot i.
struct StorageSlotRefUVE {
    std::uint32_t binding = 0U;
    std::string name; // reflected block name (diagnostics only; binding is by slot)
    VkShaderStageFlags stageFlags = 0U;
};

/// One reflected standalone SAMPLER binding (M2f): always written with the device's single
/// fixed sampler — the RHI exposes exactly one sampler shape (the GL-mirrored
/// linear/clamp/maxLod-0 parameters every texture record already uses).
struct SamplerSlotRefUVE {
    std::uint32_t binding = 0U;
    VkShaderStageFlags stageFlags = 0U;
};

} // namespace

struct VulkanRenderDeviceUVE::ImplUVE {
    ImplUVE(Window::IWindowManagerUVE* windowManagerIn, Window::IVulkanWindowSurfaceUVE* bridgeIn)
        : windowManager(windowManagerIn), bridge(bridgeIn) {}

    Window::IWindowManagerUVE* windowManager; // nullable: bridge-direct ("headless") construction
    Window::IVulkanWindowSurfaceUVE* bridge = nullptr;

    VkFunctionsUVE vk;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = 0;
    VkQueue presentQueue = VK_NULL_HANDLE; // graphics-capable family also used for present
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;

    // M2d: core-1.3 dynamic rendering is the offscreen-pass foundation (probed at bring-up,
    // enabled at device creation when present). When unsupported, the device keeps its full
    // classic M2c shape and offscreen passes degrade to a one-shot warning + skip. All goes-
    // through-dynamic-rendering decisions branch on useDynamicRendering, never on the probe.
    bool dynamicRenderingSupported = false;
    bool useDynamicRendering = false;

    // M2d scratch depth for color-only offscreen passes: one DEVICE_LOCAL depth image per
    // encountered extent (same 1-frame-in-flight argument that licenses the single swapchain
    // depth target). Layout is always DEPTH_STENCIL_ATTACHMENT_OPTIMAL between uses; entry
    // barriers discard content with oldLayout=UNDEFINED.
    struct DepthScratchUVE {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    std::map<std::uint64_t, DepthScratchUVE> offscreenDepthScratch; // key: w<<32 | h

    // Current-frame pass state for the dynamic-rendering flow. PresentUVE resets it; the
    // replay lazily opens the needed rendering instance at the first BeginRenderPassUVE op
    // and closes on pass switches or at frame end. Classic mode ignores all of this (its
    // single swapchain render pass is begun upfront exactly as before M2d).
    struct TextureRecordUVE; // fwd: FramePassStateUVE carries a pointer to it (M2e)
    struct FramePassStateUVE {
        VkClearValue clearValues[2]{}; // 0: color, 1: depth — swapchain load-CLEAR contents
        std::uint32_t swapchainImageIndex = 0U;
        bool barrierDoneForSwapchain = false;   // per-frame image/depth entry transitions
        bool swapchainPassBegunThisFrame = false;
        bool passOpen = false;
        bool openPassIsSwapchain = true;
        VkImage openOffscreenColorImage = VK_NULL_HANDLE; // restored to SHADER_READ at close
        // M5b: the color RECORD of the open offscreen pass (null while swapchain/scratch).
        // A mid-pass storage-image layout transition has to close the instance, barrier, and
        // REOPEN the same pass with Load semantics — the record pointer is what makes the
        // reopen possible from inside the flush (the VkImage handle alone cannot).
        TextureRecordUVE* openOffscreenColorRecord = nullptr;
        // M2e: the caller depth attached to the open offscreen pass (nullptr for scratch depth
        // — scratch content is never kept, so no tracked restore is needed). Restored to
        // SHADER_READ_ONLY at CloseCurrentPassDynamicUVE() alongside the color image so the
        // depth texture's rest invariant stays "sampleable between passes".
        TextureRecordUVE* openOffscreenDepthRecord = nullptr;
    };
    FramePassStateUVE framePassState;

    // Headless construction (CreateHeadlessUVE): no window manager and no external bridge —
    // the device created its own VK_EXT_headless_surface during bring-up and reports a fixed
    // 1280x720 framebuffer wherever a windowed device would consult its bridge.
    bool headless = false;
    static constexpr std::uint32_t kHeadlessFramebufferWidthUVE = 1280U;
    static constexpr std::uint32_t kHeadlessFramebufferHeightUVE = 720U;

    VkExtent2D swapchainExtent{0U, 0U};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    bool usable = false; // signed-off only by the very last bring-up step
    std::uint32_t lastPresentedImageIndex = UINT32_MAX; // UINT32_MAX = no frame presented yet

    // --- M2b: depth + uniforms/descriptor infrastructure -------------------------------------
    // Depth target: ONE depth image for the whole swapchain (legal and correct because the
    // M1 sync policy guarantees a single frame in flight at any moment — the depth buffer is
    // never shared across overlapping frames). Recreated with every swapchain rebuild.
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Frame uniform ring: one large persistently-mapped HOST_VISIBLE uniform buffer. Per draw,
    // each reflected block's shadow is snapshot-copied into a cursor-aligned region and bound
    // via the pipeline's dynamically-offset descriptor set — no per-draw descriptor writes and
    // no per-draw allocation (the classic dynamic-UBO pattern, deliberately the simple shape:
    // always-snapshot-per-draw; batching is a later optimization slice). The cursor is reset
    // each frame strictly AFTER the in-flight fence's wait, so a region is never overwritten
    // while the GPU might still read it for the previous frame.
    static constexpr std::uint64_t kFrameUboCapacityUVE = 1024U * 1024U; // 1 MiB per frame
    VkBuffer frameUbo = VK_NULL_HANDLE;
    VkDeviceMemory frameUboMemory = VK_NULL_HANDLE;
    void* frameUboMapped = nullptr;
    std::uint64_t frameUboCursor = 0U;
    std::uint64_t frameUboAlignment = 256U; // device-reported in init; 256 is the spec's max

    // One shared descriptor pool. M2b: 64 sets / 128 dynamic-UBO descriptors. M2c raised it
    // for the texture binding cache (one set per pipeline × texture tuple) and added FREE bit
    // so a destroyed texture's cached sets can be returned without resetting the whole pool.
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    // M5b: cached sets invalidated MID-REPLAY (a storage-image GENERAL transition stale-ing
    // their baked layouts) cannot be freed on the spot — this frame's already-recorded binds
    // may still reference them, and freeing a set bound in a pending command buffer is
    // undefined. They park here and are freed at the NEXT PresentUVE's post-fence point,
    // when the referencing submission is provably done. (DestroyTextureUVE keeps its
    // immediate free — it waits for queue idle first.)
    std::vector<VkDescriptorSet> deferredDescriptorSetFreesUVE;
    static constexpr std::uint32_t kDescriptorPoolSetCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolUboCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolSamplerCapacityUVE = 256U;
    // M2f pool types: storage buffers, split sampled images, and standalone samplers.
    static constexpr std::uint32_t kDescriptorPoolStorageCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolSampledImageCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolFixedSamplerCapacityUVE = 256U;
    // M5b pool type: storage images (imageLoad/imageStore slots, GENERAL layout).
    static constexpr std::uint32_t kDescriptorPoolStorageImageCapacityUVE = 256U;

    // M2f device-owned descriptor resources: one FIXED sampler (every RHI sampler is the same
    // GL-mirrored linear/clamp/maxLod-0 shape, so standalone SAMPLER bindings all get this one)
    // and one zero-filled fallback storage buffer (an unbound or destroyed-after-bind SSBO slot
    // reads deterministic zeros instead of undefined memory — the buffer analogue of the M2c
    // 1x1-white fallback texture; sized for the small metadata-style SSBOs the RHI contract
    // anticipates, never for bulk data).
    VkSampler fixedSampler = VK_NULL_HANDLE;
    VkBuffer fallbackStorageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory fallbackStorageMemory = VK_NULL_HANDLE;
    static constexpr std::uint64_t kFallbackStorageBytesUVE = 256U;
    // M5b: device-owned 1x1 RGBA8 sink for unbound/destroyed/depth storage-image slots —
    // imageStore into the SHARED 1x1-white sampling fallback would corrupt its invariant,
    // so storage gets its own throwaway image (created as a regular texture entry; teardown
    // reaps it with the rest of the textures map). 0 = not yet created.
    std::uint32_t fallbackStorageImageValue = 0U;

    // Finds a memory type index satisfying `typeBits` and ALL `requiredPropertyFlags`;
    // UINT32_MAX when none exists. Extracted from the M2a buffer creation path (now shared by
    // depth images and the frame UBO as well).
    [[nodiscard]] std::uint32_t FindMemoryTypeUVE(std::uint32_t typeBits,
                                                  VkMemoryPropertyFlags requiredPropertyFlags) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vk.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            if ((typeBits & (1U << index)) != 0U &&
                (memoryProperties.memoryTypes[index].propertyFlags & requiredPropertyFlags) == requiredPropertyFlags) {
                return index;
            }
        }
        return UINT32_MAX;
    }

    // --- M2a resource tables ---------------------------------------------------------------
    struct BufferRecordUVE {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::uint64_t sizeBytes = 0;
        BufferUsageUVE usage = BufferUsageUVE::Vertex;
        void* mapped = nullptr; // persistently mapped HOST_VISIBLE buffers; nullptr = DEVICE_LOCAL (M3)
    };

    // M3: one-shot staging upload into a DEVICE_LOCAL buffer (vertex/index). Mirrors the M2c
    // texture-staging discipline exactly: a transient HOST_VISIBLE|COHERENT TRANSFER_SRC
    // buffer, a one-time command buffer on the present queue (graphics queues accept transfer
    // commands; the texture path already proved this on both software stacks), buffer
    // barriers around the copy, and a full-idle wait before anything is torn down — so the
    // caller-visible update contract ("what you wrote is what the GPU reads next") is
    // identical to the host-visible path; only placement/traffic differs.
    [[nodiscard]] bool UploadToDeviceLocalBufferUVE(const BufferRecordUVE& record,
                                                    std::span<const std::byte> data,
                                                    std::size_t offset) {
        if (data.empty()) {
            return true;
        }
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const auto freeStaging = [&]() {
            if (stagingBuffer != VK_NULL_HANDLE) {
                vk.vkDestroyBuffer(device, stagingBuffer, nullptr);
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                vk.vkFreeMemory(device, stagingMemory, nullptr);
            }
        };
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = data.size();
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vk.vkCreateBuffer(device, &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements stagingRequirements{};
        vk.vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingRequirements);
        const std::uint32_t stagingMemoryType = FindMemoryTypeUVE(
            stagingRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo stagingAllocateInfo{};
        stagingAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocateInfo.allocationSize = stagingRequirements.size;
        stagingAllocateInfo.memoryTypeIndex = stagingMemoryType;
        void* stagingMapped = nullptr;
        if (stagingMemoryType == UINT32_MAX ||
            vk.vkAllocateMemory(device, &stagingAllocateInfo, nullptr, &stagingMemory) != VK_SUCCESS ||
            vk.vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0U) != VK_SUCCESS ||
            vk.vkMapMemory(device, stagingMemory, 0U, data.size(), 0U, &stagingMapped) != VK_SUCCESS ||
            stagingMapped == nullptr) {
            freeStaging();
            return false;
        }
        std::memcpy(stagingMapped, data.data(), data.size());
        vk.vkUnmapMemory(device, stagingMemory);

        VkCommandBufferAllocateInfo commandAllocateInfo{};
        commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAllocateInfo.commandPool = commandPool;
        commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocateInfo.commandBufferCount = 1U;
        VkCommandBuffer transferCommands = VK_NULL_HANDLE;
        if (vk.vkAllocateCommandBuffers(device, &commandAllocateInfo, &transferCommands) != VK_SUCCESS ||
            transferCommands == VK_NULL_HANDLE) {
            freeStaging();
            return false;
        }
        const auto freeCommands = [&]() {
            vk.vkFreeCommandBuffers(device, commandPool, 1U, &transferCommands);
        };
        VkCommandBufferBeginInfo transferBegin{};
        transferBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        transferBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vk.vkBeginCommandBuffer(transferCommands, &transferBegin) != VK_SUCCESS) {
            freeCommands();
            freeStaging();
            return false;
        }
        const VkAccessFlags readAccess = (record.usage == BufferUsageUVE::Index)
                                             ? VK_ACCESS_INDEX_READ_BIT
                                             : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        VkBufferMemoryBarrier acquireDst{};
        acquireDst.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        acquireDst.srcAccessMask = 0U;
        acquireDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        acquireDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        acquireDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        acquireDst.buffer = record.buffer;
        acquireDst.offset = static_cast<VkDeviceSize>(offset);
        acquireDst.size = data.size();
        vk.vkCmdPipelineBarrier(transferCommands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 1U, &acquireDst,
                                0U, nullptr);
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0U;
        copyRegion.dstOffset = offset;
        copyRegion.size = data.size();
        vk.vkCmdCopyBuffer(transferCommands, stagingBuffer, record.buffer, 1U, &copyRegion);
        VkBufferMemoryBarrier publishCopy{};
        publishCopy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        publishCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        publishCopy.dstAccessMask = readAccess;
        publishCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        publishCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        publishCopy.buffer = record.buffer;
        publishCopy.offset = static_cast<VkDeviceSize>(offset);
        publishCopy.size = data.size();
        vk.vkCmdPipelineBarrier(transferCommands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0U, 0U, nullptr, 1U,
                                &publishCopy, 0U, nullptr);
        if (vk.vkEndCommandBuffer(transferCommands) != VK_SUCCESS) {
            freeCommands();
            freeStaging();
            return false;
        }
        VkSubmitInfo transferSubmit{};
        transferSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        transferSubmit.commandBufferCount = 1U;
        transferSubmit.pCommandBuffers = &transferCommands;
        if (vk.vkQueueSubmit(presentQueue, 1U, &transferSubmit, VK_NULL_HANDLE) != VK_SUCCESS ||
            vk.vkQueueWaitIdle(presentQueue) != VK_SUCCESS) {
            freeCommands();
            freeStaging();
            return false;
        }
        freeCommands();
        freeStaging();
        return true;
    }

    struct ShaderRecordUVE {
        VkShaderModule module = VK_NULL_HANDLE;
        ShaderStageUVE stage = ShaderStageUVE::Vertex;
        std::string entryPoint = "main";
        std::string spirvBytes; // retained for SPIRV-Reflect at pipeline-creation time
    };
    struct PipelineRecordUVE {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE; // owned, may be null
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;             // pool-owned; null = none
        std::vector<UniformBlockRefUVE> uniformBlocks;
        PushConstantBlockRefUVE pushBlock;
        std::vector<TextureSlotRefUVE> textureSlots; // sorted by binding (slot index = position)
        std::vector<StorageSlotRefUVE> storageSlots;  // M2f SSBOs, same slot rule
        std::vector<SamplerSlotRefUVE> samplerBindings; // M2f standalone samplers (fixed sampler)
        std::vector<UniformReflectionUVE> reflectedUniforms; // served by GetPipelineUniformsUVE()
        bool uniformsDirty = true; // first bind of any frame flushes everything
        // M5a: true for pipelines from CreateComputePipelineUVE(). Chooses the
        // VK_PIPELINE_BIND_POINT_COMPUTE side at bind/flush time; texture/sampler slots stay
        // empty (the compute reflection accepts uniform + storage buffers only until M5b).
        bool isCompute = false;
        // M2c: one descriptor SET per bound-resource tuple (uniformless pipelines use the
        // static `descriptorSet` above; textured pipelines can never share one set across
        // differing bindings — updating a recorded set in place would retroactively change
        // already-recorded draws). Values are pool-owned; entries are freed explicitly when
        // a tuple resource is destroyed. Key: concatenated slot-order texture handle ids
        // (M2f: plus an 's'-prefixed storage-buffer section); the parallel
        // `cachedTextureTextures`/`cachedStorageBuffers` maps keep the key's tuple searchable
        // for destruction-time invalidation.
        std::map<std::string, VkDescriptorSet> cachedTextureSets;
        std::map<std::string, std::vector<std::uint32_t>> cachedTextureTextures;
        std::map<std::string, std::vector<std::uint32_t>> cachedStorageBuffers;
    };
    // M2c: texture records live in DEVICE_LOCAL images, uploaded through a HOST_VISIBLE
    // staging buffer + one-shot transfer submission (upload-time queueWaitIdle keeps every
    // transition trivially race-free under the documented 1-frame-in-flight policy).
    // `currentLayout` is tracked so a later milestone (offscreen render targets) can add
    // transitions without re-deriving state; M2c itself never re-transitions after upload.
    struct TextureRecordUVE {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;           // the SAMPLING view (unorm-aliased)
        VkImageView attachmentView = VK_NULL_HANDLE; // the image-native-format view (RT use)
        // M5b fix: the STORAGE_IMAGE descriptor view. A storage descriptor's imageView must
        // have a format matching the SPIR-V image-format qualifier (rgba8 ⇒ R8G8B8A8_UNORM)
        // with VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT support — and SPIR-V has NO B,G,R,A storage
        // format at all. On devices whose swapchain-format aliasing (M2d policy) made the
        // image B8G8R8A8-family or sRGB, this is a dedicated R8G8B8A8_UNORM alias view (legal:
        // the image is mutable-format and every 4x8 format shares one compatibility class);
        // VK_NULL_HANDLE when `view` already IS R8G8B8A8_UNORM (descriptor writes use `view`).
        VkImageView storageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat vkFormat = VK_FORMAT_UNDEFINED; // the IMAGE's format (may be swapchain-typed)
        TextureDescUVE desc{};
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // M5b: set once the texture has been transitioned to GENERAL for storage-image use.
        // GENERAL is a permanent rest state from then on (legal — if unoptimal — for sampling
        // and attachment entry too), cached descriptor sets referring to it are invalidated at
        // the transition, and CloseCurrentPassDynamicUVE restores pinned textures to GENERAL
        // instead of SHADER_READ_ONLY so tracked-layout barriers stay truthful.
        bool pinnedGeneral = false;
    };
    std::unordered_map<std::uint32_t, TextureRecordUVE> textures;
    std::uint32_t fallbackTextureValue = 0U; // 1x1 opaque-white; used for unbound/destroyed slots

    // --- M2d dynamic-rendering pass helpers (used only when useDynamicRendering) ---------
    // Opens the swapchain rendering instance for the frame's acquired image; the very first
    // open of a frame runs the UNDEFINED-entry barriers, every reopened instance resumes
    // with LOAD (content preserved, matching GL's interleaved FBO semantics).
    [[nodiscard]] bool BeginSwapchainPassDynamicUVE();
    // Opens an offscreen rendering instance against `color` (+ caller `depth` or the scratch
    // target of matching extent). Entry barriers run here; the restore barriers back to
    // SHADER_READ_ONLY for the color (and caller-depth) images run at
    // CloseCurrentPassDynamicUVE(). M2e: `colorLoadOp`/`depthLoadOp` are the real attachment
    // load ops now (Clear/Load/DontCare — GL's FBO clear-once-vs-accumulate semantics).
    [[nodiscard]] bool BeginOffscreenPassDynamicUVE(TextureRecordUVE& color,
                                                    TextureRecordUVE* depth,
                                                    VkExtent2D extent,
                                                    const std::array<float, 4>& clearColor,
                                                    float clearDepth,
                                                    LoadOpUVE colorLoadOp,
                                                    LoadOpUVE depthLoadOp);
    // Closes whichever rendering instance is open (if any) and restores offscreen layouts.
    void CloseCurrentPassDynamicUVE();
    // Scratch depth lookup-or-allocate for one extent. Null on failure (logged once).
    [[nodiscard]] DepthScratchUVE* GetDepthScratchUVE(std::uint32_t width, std::uint32_t height);

    std::unordered_map<std::uint32_t, BufferRecordUVE> buffers;
    std::unordered_map<std::uint32_t, ShaderRecordUVE> shaders;
    std::unordered_map<std::uint32_t, PipelineRecordUVE> pipelines;
    std::uint32_t nextHandleValue = 1; // one monotonically-increasing domain per kind is fine

    // Frame submissions: engine records command buffers between presents; SubmitUVE() moves
    // each one into this FIFO, and PresentUVE() replays the whole queue inside the frame's
    // single swapchain render pass (see the documented M2a integration contract in the same
    // method). Bounded by consumption: PresentUVE() drains it every frame.
    // M4: this FIFO is the ONLY state shared between recording threads and the present
    // thread — submissionMutex guards every access. Recording itself is per-command-buffer
    // (VulkanCommandBufferUVE objects carry no device state at all), so N threads may create,
    // record, and submit concurrently; PresentUVE() drains the FIFO into a local snapshot
    // under the lock and replays strictly main-thread.
    std::mutex submissionMutex;
    std::deque<std::vector<RecordedCommandUVE>> frameSubmissions;

    // One-shot warning bits so replay-time discoveries (not-yet-implemented paths) log exactly
    // once per process instead of flooding every frame.
    static constexpr std::uint32_t kWarnedUniformNoopUVE = 1U << 0U;
    static constexpr std::uint32_t kWarnedTextureNoopUVE = 1U << 1U;
    static constexpr std::uint32_t kWarnedOffscreenPassUVE = 1U << 2U;
    static constexpr std::uint32_t kWarnedUnknownHandleUVE = 1U << 3U;
    static constexpr std::uint32_t kWarnedDepthIgnoredUVE = 1U << 4U;
    static constexpr std::uint32_t kWarnedDroppedSubmissionsUVE = 1U << 5U;
    static constexpr std::uint32_t kWarnedLoadOpUnhonoredUVE = 1U << 6U;
    static constexpr std::uint32_t kWarnedUboExhaustedUVE = 1U << 7U;
    static constexpr std::uint32_t kWarnedUniformNameMissUVE = 1U << 8U;
    static constexpr std::uint32_t kWarnedUniformTypeMissUVE = 1U << 9U;
    static constexpr std::uint32_t kWarnedUnknownTextureUVE = 1U << 10U;
    static constexpr std::uint32_t kWarnedTextureSlotOobUVE = 1U << 11U;
    static constexpr std::uint32_t kWarnedTextureSetExhaustedUVE = 1U << 12U;
    static constexpr std::uint32_t kWarnedOffscreenUnsupportedUVE = 1U << 13U;
    static constexpr std::uint32_t kWarnedOffscreenDepthIncompatibleUVE = 1U << 14U;
    static constexpr std::uint32_t kWarnedDepthTextureSampledUVE = 1U << 15U;
    static constexpr std::uint32_t kWarnedOffscreenFeedbackUVE = 1U << 16U;
    static constexpr std::uint32_t kWarnedUnknownStorageUVE = 1U << 17U;
    static constexpr std::uint32_t kWarnedStorageSlotOobUVE = 1U << 18U;
    static constexpr std::uint32_t kWarnedDispatchClassicUVE = 1U << 19U; // M5a
    static constexpr std::uint32_t kWarnedDrawWithComputeUVE = 1U << 20U; // M5a
    static constexpr std::uint32_t kWarnedStorageImageDepthUVE = 1U << 21U; // M5b
    static constexpr std::uint32_t kWarnedStorageImageTransitionUVE = 1U << 22U; // M5b
    static constexpr std::uint32_t kWarnedIndirectBufferUVE = 1U << 23U; // CS7
    std::uint32_t replayWarningsEmitted = 0U;

    // Replay-local pipeline binding state (valid only inside PresentUVE()'s record window).
    std::uint32_t activePipelineValue = 0U; // 0 = none bound this replay

    void WarnOnceUVE(const std::uint32_t bit, const char* message) {
        if ((replayWarningsEmitted & bit) == 0U) {
            replayWarningsEmitted |= bit;
            UVE_WARNING("VulkanRenderDeviceUVE: {}", message);
        }
    }

    void DestroyAllResourcesUVE();

    [[nodiscard]] bool LogBailUVE(const char* reason) {
        UVE_WARNING("VulkanRenderDeviceUVE: {}", reason);
        return false;
    }

    void DestroySwapchainResourcesUVE();
    [[nodiscard]] bool CreateSwapchainResourcesUVE();
    [[nodiscard]] bool InitializeUVE();
};

bool VulkanRenderDeviceUVE::ImplUVE::BeginSwapchainPassDynamicUVE() {
    FramePassStateUVE& state = framePassState;
    if (!state.barrierDoneForSwapchain) {
        // Per-frame entry transitions (once): the acquired image and the per-swapchain depth
        // target enter their attachment layouts. oldLayout=UNDEFINED is always legal (it only
        // forfeits content preservation), which frees us from per-image layout bookkeeping.
        VkImageMemoryBarrier entryBarriers[2]{};
        for (VkImageMemoryBarrier& barrier : entryBarriers) {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        entryBarriers[0].srcAccessMask = 0U;
        entryBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        entryBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        entryBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        entryBarriers[0].image = swapchainImages[state.swapchainImageIndex];
        entryBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        entryBarriers[1].srcAccessMask = 0U;
        entryBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        entryBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        entryBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        entryBarriers[1].image = depthImage;
        entryBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 1U, 0U, 1U};
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                0U, 0U, nullptr, 0U, nullptr, 2U, entryBarriers);
        state.barrierDoneForSwapchain = true;
    }
    const bool resume = state.swapchainPassBegunThisFrame;
    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = swapchainImageViews[state.swapchainImageIndex];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = resume ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue = state.clearValues[0];
    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depthImageView;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = resume ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentInfo.clearValue = state.clearValues[1];
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, swapchainExtent};
    renderingInfo.layerCount = 1U;
    renderingInfo.colorAttachmentCount = 1U;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;
    vk.vkCmdBeginRendering(commandBuffer, &renderingInfo);

    // Same full-extent dynamic state the classic path applies right after begin.
    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    vk.vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vk.vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);

    state.swapchainPassBegunThisFrame = true;
    state.passOpen = true;
    state.openPassIsSwapchain = true;
    return true;
}

VulkanRenderDeviceUVE::ImplUVE::DepthScratchUVE*
VulkanRenderDeviceUVE::ImplUVE::GetDepthScratchUVE(const std::uint32_t width, const std::uint32_t height) {
    const std::uint64_t key =
        (static_cast<std::uint64_t>(width) << 32U) | static_cast<std::uint64_t>(height);
    const auto found = offscreenDepthScratch.find(key);
    if (found != offscreenDepthScratch.end()) {
        return &found->second;
    }
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent = {width, height, 1U};
    imageInfo.mipLevels = 1U;
    imageInfo.arrayLayers = 1U;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // every use re-enters via barriers
    VkImage image = VK_NULL_HANDLE;
    if (vk.vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS || image == VK_NULL_HANDLE) {
        UVE_WARNING("VulkanRenderDeviceUVE: vkCreateImage failed for an offscreen scratch depth target");
        return nullptr;
    }
    VkMemoryRequirements requirements{};
    vk.vkGetImageMemoryRequirements(device, image, &requirements);
    const std::uint32_t memoryType = FindMemoryTypeUVE(requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        vk.vkDestroyImage(device, image, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE: no DEVICE_LOCAL memory type for scratch depth");
        return nullptr;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vk.vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS ||
        vk.vkBindImageMemory(device, image, memory, 0U) != VK_SUCCESS) {
        if (memory != VK_NULL_HANDLE) {
            vk.vkFreeMemory(device, memory, nullptr);
        }
        vk.vkDestroyImage(device, image, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE: scratch depth memory allocation/bind failed");
        return nullptr;
    }
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 1U, 0U, 1U};
    VkImageView view = VK_NULL_HANDLE;
    if (vk.vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS || view == VK_NULL_HANDLE) {
        vk.vkFreeMemory(device, memory, nullptr);
        vk.vkDestroyImage(device, image, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE: vkCreateImageView failed for scratch depth");
        return nullptr;
    }
    DepthScratchUVE scratch{image, memory, view};
    const auto [inserted, ok] = offscreenDepthScratch.emplace(key, scratch);
    (void)ok;
    return &inserted->second;
}

bool VulkanRenderDeviceUVE::ImplUVE::BeginOffscreenPassDynamicUVE(
    TextureRecordUVE& color, TextureRecordUVE* depth, const VkExtent2D extent,
    const std::array<float, 4>& clearColor, const float clearDepth,
    const LoadOpUVE colorLoadOp, const LoadOpUVE depthLoadOp) {
    FramePassStateUVE& state = framePassState;
    DepthScratchUVE* scratch = nullptr;
    if (depth == nullptr) {
        scratch = GetDepthScratchUVE(extent.width, extent.height);
        if (scratch == nullptr) {
            return false;
        }
    }

    // Entry transitions: the color image leaves its invariant SHADER_READ_ONLY (previous
    // sampled reads ordered by the fragment-shader source scope), depth enters attachment.
    VkImageMemoryBarrier entryBarriers[2]{};
    for (VkImageMemoryBarrier& barrier : entryBarriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    }
    entryBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    entryBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    entryBarriers[0].oldLayout = color.currentLayout; // SHADER_READ_ONLY by M2c invariant
    entryBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    entryBarriers[0].image = color.image;
    entryBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
    entryBarriers[1].srcAccessMask = 0U;
    entryBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (depth != nullptr) {
        entryBarriers[1].oldLayout = depth->currentLayout; // SHADER_READ_ONLY by M2e invariant
    } else {
        entryBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // scratch: content never kept
    }
    entryBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    entryBarriers[1].image = depth != nullptr ? depth->image : scratch->image;
    entryBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 1U, 0U, 1U};
    vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                            0U, 0U, nullptr, 0U, nullptr, 2U, entryBarriers);

    // M2e: real load ops. The entry barriers above are layout transitions only and already
    // preserve content (oldLayout = the record's tracked layout — never UNDEFINED-discard for
    // caller textures), so LOAD preserves prior contents exactly like GL re-bound FBOs do,
    // CLEAR runs the one clear the caller asked for, and DontCare is a free forward of the
    // driver's discard prerogative.
    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = color.attachmentView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = ToVkLoadOpUVE(colorLoadOp);
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color.float32[0] = clearColor[0];
    colorAttachmentInfo.clearValue.color.float32[1] = clearColor[1];
    colorAttachmentInfo.clearValue.color.float32[2] = clearColor[2];
    colorAttachmentInfo.clearValue.color.float32[3] = clearColor[3];
    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depth != nullptr ? depth->attachmentView : scratch->view;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = ToVkLoadOpUVE(depthLoadOp);
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentInfo.clearValue.depthStencil = {clearDepth, 0U};
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1U;
    renderingInfo.colorAttachmentCount = 1U;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;
    vk.vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vk.vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vk.vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);

    state.passOpen = true;
    state.openPassIsSwapchain = false;
    state.openOffscreenColorImage = color.image;
    state.openOffscreenColorRecord = &color;
    state.openOffscreenDepthRecord = depth; // restored to SHADER_READ_ONLY at pass close
    return true;
}

void VulkanRenderDeviceUVE::ImplUVE::CloseCurrentPassDynamicUVE() {
    FramePassStateUVE& state = framePassState;
    if (!state.passOpen) {
        return;
    }
    vk.vkCmdEndRendering(commandBuffer);
    if (!state.openPassIsSwapchain && state.openOffscreenColorImage != VK_NULL_HANDLE) {
        // Restore the color image's invariant sampling layout so later passes may bind it.
        // M5b: storage-pinned textures rest in GENERAL instead (their cached descriptors say
        // GENERAL; restoring SHADER_READ_ONLY would contradict the tracked layout).
        const VkImageLayout restLayout =
            (state.openOffscreenColorRecord != nullptr &&
             state.openOffscreenColorRecord->pinnedGeneral)
                ? VK_IMAGE_LAYOUT_GENERAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageMemoryBarrier backToSample{};
        backToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        backToSample.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        backToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        backToSample.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        backToSample.newLayout = restLayout;
        backToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToSample.image = state.openOffscreenColorImage;
        backToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, nullptr, 0U,
                                nullptr, 1U, &backToSample);
    }
    if (!state.openPassIsSwapchain && state.openOffscreenDepthRecord != nullptr) {
        // M2e symmetric restore: caller depth textures return to their sampleable rest
        // layout, so a later pass/shader can bind them. (Scratch depth is excluded: its
        // content is throwaway by design and it never leaves the device-internal layout
        // rotation.)
        VkImageMemoryBarrier depthToSample{};
        depthToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthToSample.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthToSample.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToSample.image = state.openOffscreenDepthRecord->image;
        depthToSample.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 1U, 0U, 1U};
        vk.vkCmdPipelineBarrier(commandBuffer,
                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, nullptr, 0U,
                                nullptr, 1U, &depthToSample);
        state.openOffscreenDepthRecord->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    state.passOpen = false;
    state.openOffscreenColorImage = VK_NULL_HANDLE;
    state.openOffscreenColorRecord = nullptr;
    state.openOffscreenDepthRecord = nullptr;
}

void VulkanRenderDeviceUVE::ImplUVE::DestroySwapchainResourcesUVE() {
    // Caller guarantees the queue has been drained (vkQueueWaitIdle) before entry.
    for (const VkFramebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vk.vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    framebuffers.clear();
    // The render pass outlives individual recreations (it's format-dependent, not
    // extent-dependent) — destroyed only from the destructor, never here.
    for (const VkImageView imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, imageView, nullptr);
        }
    }
    swapchainImageViews.clear();
    swapchainImages.clear(); // the swapchain owns the images themselves; nothing to destroy here
    if (depthImageView != VK_NULL_HANDLE) {
        vk.vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vk.vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vk.vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }
    if (swapchain != VK_NULL_HANDLE) {
        vk.vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    swapchainExtent = {0U, 0U};
}

bool VulkanRenderDeviceUVE::ImplUVE::CreateSwapchainResourcesUVE() {
    // Extent: the surface capabilities win over the window's raw framebuffer size whenever the
    // driver reports a fixed extent (non-minimized Wayland and ioctl'd DRM clients do); only
    // the "currentExtent must be chosen" sentinel uses the framebuffer size, clamped into the
    // capabilities range.
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
        return LogBailUVE("swapchain recreation: surface capabilities query failed");
    }
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFU) { // VK_WHOLE_SURFACE sentinel — pick from the window.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (headless) {
            width = kHeadlessFramebufferWidthUVE;
            height = kHeadlessFramebufferHeightUVE;
        } else {
            bridge->GetVulkanFramebufferSizeUVE(width, height);
        }
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, height));
    }
    if (extent.width == 0U || extent.height == 0U) {
        return LogBailUVE("swapchain recreation: zero-sized extent (minimized window?); refusing to build");
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1U; // one ahead of the driver minimum
    if (capabilities.maxImageCount > 0U && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = swapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1U;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // one queue family total (M1)
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed present; FIFO == vsync
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vk.vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &newSwapchain) != VK_SUCCESS ||
        newSwapchain == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateSwapchainKHR failed");
    }
    swapchain = newSwapchain;
    swapchainExtent = extent;

    std::uint32_t actualImageCount = 0;
    vk.vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
    swapchainImages.resize(actualImageCount);
    vk.vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, swapchainImages.data());

    swapchainImageViews.resize(actualImageCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < actualImageCount; ++index) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0U;
        viewInfo.subresourceRange.levelCount = 1U;
        viewInfo.subresourceRange.baseArrayLayer = 0U;
        viewInfo.subresourceRange.layerCount = 1U;
        if (vk.vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[index]) != VK_SUCCESS) {
            return LogBailUVE("vkCreateImageView for a swapchain image failed");
        }
    }

    // Depth target: one image shared by ALL framebuffers — correct because one frame is in
    // flight at a time (see the M1 sync notice at the field declaration).
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = depthFormat;
    depthImageInfo.extent = {extent.width, extent.height, 1U};
    depthImageInfo.mipLevels = 1U;
    depthImageInfo.arrayLayers = 1U;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vk.vkCreateImage(device, &depthImageInfo, nullptr, &depthImage) != VK_SUCCESS) {
        return LogBailUVE("vkCreateImage for the depth target failed");
    }
    VkMemoryRequirements depthRequirements{};
    vk.vkGetImageMemoryRequirements(device, depthImage, &depthRequirements);
    const std::uint32_t depthMemoryType =
        FindMemoryTypeUVE(depthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (depthMemoryType == UINT32_MAX) {
        return LogBailUVE("no device-local memory type for the depth target");
    }
    VkMemoryAllocateInfo depthAllocateInfo{};
    depthAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAllocateInfo.allocationSize = depthRequirements.size;
    depthAllocateInfo.memoryTypeIndex = depthMemoryType;
    if (vk.vkAllocateMemory(device, &depthAllocateInfo, nullptr, &depthImageMemory) != VK_SUCCESS) {
        return LogBailUVE("vkAllocateMemory for the depth target failed");
    }
    vk.vkBindImageMemory(device, depthImage, depthImageMemory, 0U);

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0U;
    depthViewInfo.subresourceRange.levelCount = 1U;
    depthViewInfo.subresourceRange.baseArrayLayer = 0U;
    depthViewInfo.subresourceRange.layerCount = 1U;
    if (vk.vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        return LogBailUVE("vkCreateImageView for the depth target failed");
    }

    framebuffers.resize(actualImageCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < actualImageCount; ++index) {
        const VkImageView framebufferAttachments[] = {swapchainImageViews[index], depthImageView};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2U;
        framebufferInfo.pAttachments = framebufferAttachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1U;
        if (vk.vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[index]) != VK_SUCCESS) {
            return LogBailUVE("vkCreateFramebuffer for a swapchain image failed");
        }
    }
    return true;
}

bool VulkanRenderDeviceUVE::ImplUVE::InitializeUVE() {
    // --- bridge capability: accepted directly or RTTI-queried off the window manager --------
    // (skipped entirely in headless mode: there is no bridge by design — the device drives
    // VK_EXT_headless_surface itself)
    if (bridge == nullptr && !headless) {
        bridge = dynamic_cast<Window::IVulkanWindowSurfaceUVE*>(windowManager);
        if (bridge == nullptr) {
            return LogBailUVE("window manager offers no IVulkanWindowSurfaceUVE capability "
                              "(headless or stub backend); Vulkan windowed rendering needs it");
        }
    }
    if (windowManager != nullptr && !windowManager->IsValidUVE()) {
        return LogBailUVE("window manager is not valid; Vulkan needs a real window first");
    }

    // --- global level ----------------------------------------------------------------------
    if (!vk.LoadGlobalUVE()) {
        return LogBailUVE("Vulkan global entry points failed to resolve");
    }

    // API version: prefer the loader-reported ceiling, but only the MAJOR.MINOR the M1 code
    // was written against — newer minors remain valid to request at 1.x (loader validates).
    std::uint32_t apiVersion = VK_API_VERSION_1_0;
    if (vk.vkEnumerateInstanceVersion != nullptr &&
        vk.vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) {
        apiVersion = VK_API_VERSION_1_0; // query present but failed: pin to the guaranteed floor
    }
    if (apiVersion > VK_API_VERSION_1_3) {
        apiVersion = VK_API_VERSION_1_3;
    }

    // --- instance --------------------------------------------------------------------------
    std::vector<const char*> instanceExtensions;
    if (headless) {
        instanceExtensions = {"VK_KHR_surface", "VK_EXT_headless_surface"};
    } else {
        instanceExtensions = bridge->GetRequiredVulkanInstanceExtensionsUVE();
    }
    if (instanceExtensions.empty()) {
        return LogBailUVE("surface bridge reported no required Vulkan instance extensions — "
                          "Vulkan WSI support is unavailable on this platform");
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "UniVex Engine";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "UniVex Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = apiVersion;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    // No validation layer is ever requested by engine code: correctness tooling belongs to the
    // developer's environment (VK_INSTANCE_LAYERS), never to a shipped engine's defaults.
    if (vk.vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateInstance failed (is an ICD/driver installed?)");
    }
    if (!vk.LoadInstanceUVE(instance)) {
        return LogBailUVE("Vulkan instance-level entry points failed to resolve");
    }

    // --- window surface --------------------------------------------------------------------
    if (headless) {
        // The CPU-ICD headless WSI entry point, pulled fresh from this instance (it is by
        // definition an instance-extension function). Present but unreachable ICDs refuse
        // with a null proc address — the honest "this ICD cannot headless" answer.
        const auto vkCreateHeadlessSurfaceEXT = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            vk.ResolveVkProcUVE(instance, "vkCreateHeadlessSurfaceEXT"));
        if (vkCreateHeadlessSurfaceEXT == nullptr) {
            return LogBailUVE("VK_EXT_headless_surface entry point unavailable "
                              "(ICD lacks headless WSI)");
        }
        VkHeadlessSurfaceCreateInfoEXT surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
        if (vkCreateHeadlessSurfaceEXT(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS) {
            surface = VK_NULL_HANDLE;
        }
    } else {
        surface = ToVkSurfaceUVE(bridge->CreateVulkanWindowSurfaceUVE(
            reinterpret_cast<std::uintptr_t>(instance)));
    }
    if (surface == VK_NULL_HANDLE) {
        return LogBailUVE("window surface creation failed through the surface bridge");
    }

    // --- physical device + one queue family that both draws and presents --------------------
    std::uint32_t physicalCount = 0;
    if (vk.vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr) != VK_SUCCESS || physicalCount == 0U) {
        return LogBailUVE("no Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    vk.vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data());

    bool found = false;
    for (const VkPhysicalDevice candidate : physicalDevices) {
        std::uint32_t queueFamilyCount = 0;
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());
        for (std::uint32_t family = 0; family < queueFamilyCount; ++family) {
            if ((queueFamilies[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U) {
                continue;
            }
            VkBool32 presentSupported = VK_FALSE;
            if (vk.vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentSupported) != VK_SUCCESS ||
                presentSupported != VK_TRUE) {
                continue;
            }
            // Swapchain extension must be advertised by this physical device.
            std::uint32_t extensionCount = 0;
            vk.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vk.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
            const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(),
                [](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                });
            if (!hasSwapchain) {
                continue;
            }
            physicalDevice = candidate;
            queueFamilyIndex = family;
            found = true;
            break;
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        return LogBailUVE("no physical device with a graphics+present queue family and "
                          "VK_KHR_swapchain was found");
    }

    VkPhysicalDeviceProperties deviceProperties{};
    vk.vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    UVE_INFO("VulkanRenderDeviceUVE: selected physical device \"{}\"", deviceProperties.deviceName);

    // --- logical device --------------------------------------------------------------------
    // M2d probe first: is core dynamic rendering available on this physical device+instance?
    // (Instance apiVersion was already clamped at 1.3 above; the feature query needs the
    // 1.1+ vkGetPhysicalDeviceFeatures2 entry point, resolved optionally at instance load.)
    dynamicRenderingSupported = false;
    if (apiVersion >= VK_API_VERSION_1_3 && vk.vkGetPhysicalDeviceFeatures2 != nullptr) {
        VkPhysicalDeviceVulkan13Features v13Features{};
        v13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &v13Features;
        vk.vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
        dynamicRenderingSupported = v13Features.dynamicRendering == VK_TRUE;
    }

    const float queuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1U;
    queueInfo.pQueuePriorities = &queuePriority;

    static constexpr const char* kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features enabledV13Features{};
    enabledV13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    if (dynamicRenderingSupported) {
        enabledV13Features.dynamicRendering = VK_TRUE;
    }
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1U;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1U;
    deviceInfo.ppEnabledExtensionNames = kDeviceExtensions;
    if (dynamicRenderingSupported) {
        deviceInfo.pNext = &enabledV13Features;
    }

    if (vk.vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS || device == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateDevice failed");
    }
    if (!vk.LoadDeviceUVE(device)) {
        return LogBailUVE("Vulkan device-level entry points failed to resolve");
    }
    vk.vkGetDeviceQueue(device, queueFamilyIndex, 0U, &presentQueue);

    useDynamicRendering = dynamicRenderingSupported && vk.vkCmdBeginRendering != nullptr &&
                          vk.vkCmdEndRendering != nullptr;
    if (dynamicRenderingSupported && !useDynamicRendering) {
        UVE_WARNING("VulkanRenderDeviceUVE: dynamic rendering was reported but its device "
                    "entry points did not resolve; the device continues without offscreen "
                    "render-target support (classic render-pass mode)");
    }

    // --- surface format/present-mode picks ---------------------------------------------------
    std::uint32_t formatCount = 0;
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosenFormat{};
    bool formatChosen = false;
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = format;
            formatChosen = true;
            break;
        }
    }
    if (!formatChosen && !formats.empty()) {
        chosenFormat = formats.front(); // any advertised format beats failing the backend
        formatChosen = true;
    }
    if (!formatChosen) {
        return LogBailUVE("surface advertises no usable formats");
    }
    swapchainFormat = chosenFormat.format;

    // FIFO is mandatory by spec — no present-mode enumeration needed for M1 (mailbox relaxes
    // vsync; FIFO matches the GL device default exactly).

    // --- depth format pick ------------------------------------------------------------------
    // Color-only M2a render pass grew the M2b depth attachment here: prefer 32-bit float
    // depth, fall back to the packed 24-bit variant — both must be verified against the
    // physical device's format properties before the render pass binds to one.
    depthFormat = VK_FORMAT_UNDEFINED;
    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32}) {
        VkFormatProperties formatProperties{};
        vk.vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            depthFormat = candidate;
            break;
        }
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        return LogBailUVE("no supported depth format (D32_SFLOAT / D24_UNORM) on this physical device");
    }

    // --- render pass + swapchain -------------------------------------------------------------
    // Attachment 0: color (clear-on-load, store for present). Attachment 1: depth — per-frame
    // transient content, so STORE is DONT_CARE (avoiding a wasted write-back). M2d+: classic
    // mode only — dynamic rendering needs no VkRenderPass; the declaration lives on pipelines
    // (VkPipelineRenderingCreateInfo) and on the frame's lazy rendering instances instead.
    if (!useDynamicRendering) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // the M1 "render": an initial clear
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // presented afterwards, so keep the bits
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // per-frame depth always fresh-clears
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0U;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1U;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1U;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0U;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0U;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2U;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1U;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1U;
    renderPassInfo.pDependencies = &dependency;

    if (vk.vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS || renderPass == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateRenderPass failed");
    }
    } // end classic-only render pass creation

    if (!CreateSwapchainResourcesUVE()) {
        return false; // already logged inside
    }

    // --- command pool + one-shot command buffer (single frame in flight) --------------------
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vk.vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS || commandPool == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1U;
    if (vk.vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS || commandBuffer == VK_NULL_HANDLE) {
        return LogBailUVE("vkAllocateCommandBuffers failed");
    }

    // --- synchronization: one image-available semaphore, one render-finished semaphore,
    //     one in-flight fence — deliberately the 1-in-flight shape the header documents. -----
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first PresentUVE()'s wait returns instantly
    if (vk.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vk.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS ||
        vk.vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        return LogBailUVE("sync primitive creation failed");
    }

    // --- M2b: frame uniform ring + shared descriptor pool ------------------------------------
    // UBO alignment comes from the device (drivers commonly require 16/64/256-byte regions).
    frameUboAlignment = std::max<std::uint64_t>(
        16U, deviceProperties.limits.minUniformBufferOffsetAlignment);

    VkBufferCreateInfo uboInfo{};
    uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboInfo.size = kFrameUboCapacityUVE;
    uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk.vkCreateBuffer(device, &uboInfo, nullptr, &frameUbo) != VK_SUCCESS) {
        return LogBailUVE("vkCreateBuffer for the frame uniform ring failed");
    }
    VkMemoryRequirements uboRequirements{};
    vk.vkGetBufferMemoryRequirements(device, frameUbo, &uboRequirements);
    const std::uint32_t uboMemoryType = FindMemoryTypeUVE(
        uboRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (uboMemoryType == UINT32_MAX) {
        return LogBailUVE("no host-visible+coherent memory type for the frame uniform ring");
    }
    VkMemoryAllocateInfo uboAllocateInfo{};
    uboAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    uboAllocateInfo.allocationSize = uboRequirements.size;
    uboAllocateInfo.memoryTypeIndex = uboMemoryType;
    if (vk.vkAllocateMemory(device, &uboAllocateInfo, nullptr, &frameUboMemory) != VK_SUCCESS) {
        return LogBailUVE("vkAllocateMemory for the frame uniform ring failed");
    }
    vk.vkBindBufferMemory(device, frameUbo, frameUboMemory, 0U);
    if (vk.vkMapMemory(device, frameUboMemory, 0U, kFrameUboCapacityUVE, 0U, &frameUboMapped) != VK_SUCCESS ||
        frameUboMapped == nullptr) {
        return LogBailUVE("vkMapMemory for the frame uniform ring failed");
    }

    // --- M2f device-owned descriptor resources -------------------------------------------
    // The fixed sampler for standalone SAMPLER bindings: identical parameters to every texture
    // record's sampler (the RHI's one GL-mirrored sampler shape), created once at bring-up.
    VkSamplerCreateInfo fixedSamplerInfo{};
    fixedSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    fixedSamplerInfo.magFilter = VK_FILTER_LINEAR;
    fixedSamplerInfo.minFilter = VK_FILTER_LINEAR;
    fixedSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    fixedSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    fixedSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    fixedSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    fixedSamplerInfo.minLod = 0.0F;
    fixedSamplerInfo.maxLod = 0.0F;
    fixedSamplerInfo.maxAnisotropy = 1.0F;
    fixedSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vk.vkCreateSampler(device, &fixedSamplerInfo, nullptr, &fixedSampler) != VK_SUCCESS ||
        fixedSampler == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateSampler for the M2f fixed sampler failed");
    }
    // The zero-filled SSBO fallback: an unbound (or destroyed-after-bind) storage slot's draws
    // read deterministic zeros from this buffer instead of undefined memory.
    VkBufferCreateInfo fallbackSsboInfo{};
    fallbackSsboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    fallbackSsboInfo.size = kFallbackStorageBytesUVE;
    fallbackSsboInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    fallbackSsboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk.vkCreateBuffer(device, &fallbackSsboInfo, nullptr, &fallbackStorageBuffer) != VK_SUCCESS) {
        return LogBailUVE("vkCreateBuffer for the M2f storage fallback failed");
    }
    VkMemoryRequirements fallbackSsboRequirements{};
    vk.vkGetBufferMemoryRequirements(device, fallbackStorageBuffer, &fallbackSsboRequirements);
    const std::uint32_t fallbackSsboMemoryType = FindMemoryTypeUVE(
        fallbackSsboRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (fallbackSsboMemoryType == UINT32_MAX) {
        return LogBailUVE("no host-visible+coherent memory type for the M2f storage fallback");
    }
    VkMemoryAllocateInfo fallbackSsboAllocateInfo{};
    fallbackSsboAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    fallbackSsboAllocateInfo.allocationSize = fallbackSsboRequirements.size;
    fallbackSsboAllocateInfo.memoryTypeIndex = fallbackSsboMemoryType;
    if (vk.vkAllocateMemory(device, &fallbackSsboAllocateInfo, nullptr, &fallbackStorageMemory) != VK_SUCCESS) {
        return LogBailUVE("vkAllocateMemory for the M2f storage fallback failed");
    }
    vk.vkBindBufferMemory(device, fallbackStorageBuffer, fallbackStorageMemory, 0U);
    void* fallbackSsboMapped = nullptr;
    if (vk.vkMapMemory(device, fallbackStorageMemory, 0U, kFallbackStorageBytesUVE, 0U,
                       &fallbackSsboMapped) != VK_SUCCESS || fallbackSsboMapped == nullptr) {
        return LogBailUVE("vkMapMemory for the M2f storage fallback failed");
    }
    std::memset(fallbackSsboMapped, 0, static_cast<std::size_t>(kFallbackStorageBytesUVE));
    vk.vkUnmapMemory(device, fallbackStorageMemory); // written once; no persistent map needed

    VkDescriptorPoolSize poolSizes[6]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = kDescriptorPoolUboCapacityUVE;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kDescriptorPoolSamplerCapacityUVE;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kDescriptorPoolStorageCapacityUVE;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[3].descriptorCount = kDescriptorPoolSampledImageCapacityUVE;
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[4].descriptorCount = kDescriptorPoolFixedSamplerCapacityUVE;
    poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[5].descriptorCount = kDescriptorPoolStorageImageCapacityUVE;
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolInfo.maxSets = kDescriptorPoolSetCapacityUVE;
    descriptorPoolInfo.poolSizeCount = 6U;
    descriptorPoolInfo.pPoolSizes = poolSizes;
    if (vk.vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS ||
        descriptorPool == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateDescriptorPool failed");
    }

    usable = true;
    UVE_INFO("VulkanRenderDeviceUVE: M1 bootstrap initialized ({}x{}, format {}, {} swapchain images)",
        swapchainExtent.width, swapchainExtent.height,
        static_cast<int>(swapchainFormat), swapchainImages.size());
    return true;
}

namespace {

/// The unsigned-normalized sibling of an sRGB-typed (or already-unorm) 8-bit RGBA swapchain
/// format, used as the SAMPLED view format of M2c+: RGBA8 textures are allocated in the
/// swapchain's exact format so pipelines and dynamic-rendering instances match, while the
/// sampling view reads raw values (never sRGB-decoded). UNDEFINED for anything outside the
/// 4x8 family — textures then fall back to their native format and stay sample-only in M2d.
[[nodiscard]] VkFormat UnormSiblingFormatUVE(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

/// Maps one reflected block member to the RHI's public ShaderDataTypeUVE; unsupported types
/// return Unsupported and are skipped by the builder below (a shader boolean is 4 bytes in
/// SPIR-V blocks, a Mat3 stride rule is honored by the reflected offset metadata, so the
/// common GL-era shapes — float/int/bool/vec/mat — translate one-to-one).
[[nodiscard]] ShaderDataTypeUVE ToRhiUniformTypeUVE(const SpvReflectBlockVariable& member) {
    const SpvReflectTypeDescription* type = member.type_description;
    if (type == nullptr) {
        return ShaderDataTypeUVE::Unsupported;
    }
    const SpvReflectNumericTraits& numeric = member.numeric;
    const std::uint32_t rows = numeric.matrix.row_count;
    const std::uint32_t columns = numeric.matrix.column_count;
    const std::uint32_t vectorWidth = numeric.vector.component_count;
    if (rows > 0U && columns > 0U) {
        if (rows == 3U && columns == 3U) { return ShaderDataTypeUVE::Mat3; }
        if (rows == 4U && columns == 4U) { return ShaderDataTypeUVE::Mat4; }
        return ShaderDataTypeUVE::Unsupported;
    }
    // Scalars report vector component_count = 0 in SPIRV-Reflect (only true vectors carry
    // 2-4) — every scalar check must accept 0 OR 1, or plain float/int/bool members silently
    // map to Unsupported (this bug dropped scalar uniforms from the table entirely).
    if (numeric.scalar.signedness == 0U && numeric.scalar.width == 32U && vectorWidth <= 1U &&
        (type->type_flags & SPV_REFLECT_TYPE_FLAG_BOOL) != 0U) {
        return ShaderDataTypeUVE::Bool;
    }
    if (numeric.scalar.width == 32U && numeric.scalar.signedness == 1U && vectorWidth <= 1U &&
        (type->type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0U) {
        return ShaderDataTypeUVE::Int;
    }
    if ((type->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0U) {
        switch (vectorWidth) {
            case 0U: // scalar
            case 1U: return ShaderDataTypeUVE::Float;
            case 2U: return ShaderDataTypeUVE::Vec2;
            case 3U: return ShaderDataTypeUVE::Vec3;
            case 4U: return ShaderDataTypeUVE::Vec4;
            default: break;
        }
    }
    return ShaderDataTypeUVE::Unsupported;
}

/// Gathers the flat top-level members of one reflected block variable (block-level wrapping
/// struct), internet into `outMembers` with each member's stage-unknown push/UBO context set
/// by the caller. Nested-struct members are intentionally skipped for M2b (documented in the
/// pipeline reflection contract): flat members cover the RHI's whole GL-era uniform shape.
void CollectBlockMembersUVE(const SpvReflectBlockVariable& block, const VkShaderStageFlags stageFlags,
                            const std::int32_t blockIndex, std::vector<UniformMemberRefUVE>& outMembers) {
    for (std::uint32_t index = 0; index < block.member_count; ++index) {
        const SpvReflectBlockVariable& member = block.members[index];
        const char* memberName = member.name != nullptr ? member.name
            : (member.type_description != nullptr && member.type_description->struct_member_name != nullptr
                   ? member.type_description->struct_member_name : nullptr);
        if (memberName == nullptr || memberName[0] == '\0') {
            continue;
        }
        if (member.member_count != 0U) {
            continue; // nested structs: out of the M2b flat-member contract
        }
        const ShaderDataTypeUVE type = ToRhiUniformTypeUVE(member);
        if (type == ShaderDataTypeUVE::Unsupported) {
            continue;
        }
        UniformMemberRefUVE ref;
        ref.name = memberName;
        ref.type = type;
        ref.offset = member.offset;
        ref.size = member.padded_size != 0U ? member.padded_size : member.size;
        ref.blockIndex = blockIndex;
        ref.arraySize = member.array.dims_count != 0U ? member.array.dims[0] : 1U;
        outMembers.push_back(std::move(ref));
    }
    (void)stageFlags;
}

} // namespace

void VulkanRenderDeviceUVE::ImplUVE::DestroyAllResourcesUVE() {
    // Caller (the destructor) has already drained the queue — destruction must never race an
    // in-flight submit holding any of these objects.
    for (auto& [handle, record] : pipelines) {
        if (record.pipeline != VK_NULL_HANDLE) {
            vk.vkDestroyPipeline(device, record.pipeline, nullptr);
        }
        if (record.layout != VK_NULL_HANDLE) {
            vk.vkDestroyPipelineLayout(device, record.layout, nullptr);
        }
        if (record.descriptorSetLayout != VK_NULL_HANDLE) {
            vk.vkDestroyDescriptorSetLayout(device, record.descriptorSetLayout, nullptr);
        }
    }
    pipelines.clear();
    if (descriptorPool != VK_NULL_HANDLE) {
        // Destroying the pool implicitly frees every descriptor set allocated from it — the
        // per-pipeline descriptorSet handles are deliberately never individually freed.
        vk.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    deferredDescriptorSetFreesUVE.clear(); // M5b: parked sets died with the pool above
    if (frameUbo != VK_NULL_HANDLE) {
        if (frameUboMapped != nullptr) {
            vk.vkUnmapMemory(device, frameUboMemory);
            frameUboMapped = nullptr;
        }
        vk.vkDestroyBuffer(device, frameUbo, nullptr);
        frameUbo = VK_NULL_HANDLE;
    }
    if (frameUboMemory != VK_NULL_HANDLE) {
        vk.vkFreeMemory(device, frameUboMemory, nullptr);
        frameUboMemory = VK_NULL_HANDLE;
    }
    // M2f device-owned descriptor resources (created in InitializeUVE's bring-up tail).
    if (fixedSampler != VK_NULL_HANDLE) {
        vk.vkDestroySampler(device, fixedSampler, nullptr);
        fixedSampler = VK_NULL_HANDLE;
    }
    if (fallbackStorageBuffer != VK_NULL_HANDLE) {
        vk.vkDestroyBuffer(device, fallbackStorageBuffer, nullptr);
        fallbackStorageBuffer = VK_NULL_HANDLE;
    }
    if (fallbackStorageMemory != VK_NULL_HANDLE) {
        vk.vkFreeMemory(device, fallbackStorageMemory, nullptr);
        fallbackStorageMemory = VK_NULL_HANDLE;
    }
    for (auto& [handle, record] : shaders) {
        if (record.module != VK_NULL_HANDLE) {
            vk.vkDestroyShaderModule(device, record.module, nullptr);
        }
    }
    shaders.clear();
    for (auto& [handle, record] : buffers) {
        if (record.buffer != VK_NULL_HANDLE) {
            if (record.mapped != nullptr) {
                vk.vkUnmapMemory(device, record.memory);
            }
            vk.vkDestroyBuffer(device, record.buffer, nullptr);
        }
        if (record.memory != VK_NULL_HANDLE) {
            vk.vkFreeMemory(device, record.memory, nullptr);
        }
    }
    buffers.clear();
    frameSubmissions.clear(); // recorded-only state; nothing borrowed from Vulkan
    // M2c textures: after the pool (their cached sets live there - pool destruction already
    // freed them implicitly) and after pipelines (their layouts are gone above). The queue is
    // drained by the destructor before this runs, so sampler/view/image/memory go directly.
    for (auto& [handle, record] : textures) {
        if (record.sampler != VK_NULL_HANDLE) {
            vk.vkDestroySampler(device, record.sampler, nullptr);
        }
        if (record.attachmentView != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, record.attachmentView, nullptr);
        }
        if (record.storageView != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, record.storageView, nullptr);
        }
        if (record.view != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, record.view, nullptr);
        }
        if (record.image != VK_NULL_HANDLE) {
            vk.vkDestroyImage(device, record.image, nullptr);
        }
        if (record.memory != VK_NULL_HANDLE) {
            vk.vkFreeMemory(device, record.memory, nullptr);
        }
    }
    textures.clear();
    fallbackTextureValue = 0U;
    fallbackStorageImageValue = 0U; // M5b: reaped with the textures map above
    // M2d offscreen scratch depth targets (extent-keyed; independent of the swapchain).
    for (auto& [key, scratch] : offscreenDepthScratch) {
        (void)key;
        if (scratch.view != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, scratch.view, nullptr);
        }
        if (scratch.image != VK_NULL_HANDLE) {
            vk.vkDestroyImage(device, scratch.image, nullptr);
        }
        if (scratch.memory != VK_NULL_HANDLE) {
            vk.vkFreeMemory(device, scratch.memory, nullptr);
        }
    }
    offscreenDepthScratch.clear();
}

VulkanRenderDeviceUVE::VulkanRenderDeviceUVE(Window::IWindowManagerUVE* windowManager,
                                             Window::IVulkanWindowSurfaceUVE* bridge)
    : m_impl(std::make_unique<ImplUVE>(windowManager, bridge)) {}

VulkanRenderDeviceUVE::~VulkanRenderDeviceUVE() {
    if (m_impl->device != VK_NULL_HANDLE) {
        // Everything below requires a drained queue — never destroy objects mid-frame.
        if (m_impl->presentQueue != VK_NULL_HANDLE) {
            m_impl->vk.vkQueueWaitIdle(m_impl->presentQueue);
        }
        if (m_impl->inFlightFence != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroyFence(m_impl->device, m_impl->inFlightFence, nullptr);
        }
        if (m_impl->renderFinishedSemaphore != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroySemaphore(m_impl->device, m_impl->renderFinishedSemaphore, nullptr);
        }
        if (m_impl->imageAvailableSemaphore != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroySemaphore(m_impl->device, m_impl->imageAvailableSemaphore, nullptr);
        }
        m_impl->DestroyAllResourcesUVE();
        if (m_impl->commandPool != VK_NULL_HANDLE) {
            // Destroying the pool implicitly frees every command buffer allocated from it —
            // an explicit vkFreeCommandBuffers call for commandBuffer is therefore omitted.
            m_impl->vk.vkDestroyCommandPool(m_impl->device, m_impl->commandPool, nullptr);
        }
        m_impl->DestroySwapchainResourcesUVE();
        if (m_impl->renderPass != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroyRenderPass(m_impl->device, m_impl->renderPass, nullptr);
        }
        m_impl->vk.vkDestroyDevice(m_impl->device, nullptr);
    }
    if (m_impl->surface != VK_NULL_HANDLE && m_impl->instance != VK_NULL_HANDLE) {
        // Surface destruction goes through this device's own function table, not the bridge
        // (see DestroyVulkanWindowSurfaceUVE's documented bridge no-op contract).
        m_impl->vk.vkDestroySurfaceKHR(m_impl->instance, m_impl->surface, nullptr);
    }
    if (m_impl->instance != VK_NULL_HANDLE) {
        m_impl->vk.vkDestroyInstance(m_impl->instance, nullptr);
    }
}

std::unique_ptr<VulkanRenderDeviceUVE> VulkanRenderDeviceUVE::CreateUVE(
    Window::IWindowManagerUVE& windowManager) {
    auto device = std::unique_ptr<VulkanRenderDeviceUVE>(
        new VulkanRenderDeviceUVE(&windowManager, nullptr));
    if (!device->m_impl->InitializeUVE()) {
        // Partially-constructed state is torn down by the destructor — every bail in
        // InitializeUVE() has already logged its own reason.
        return nullptr;
    }
    if (!device->CreateFallbackTextureUVE()) {
        return nullptr;
    }
    return device;
}

// M2c fallback texture (1x1 opaque white): any sampler binding left unbound — or bound to a
// texture destroyed mid-sequence — resolves to this record so every descriptor set the replay
// binds is fully populated; sampling an unbound slot yields a deterministic value instead of
// undefined behavior (strictly better than GL's texture-unit-0-unbound analogue).
bool VulkanRenderDeviceUVE::CreateFallbackTextureUVE() {
    TextureDescUVE fallbackDesc{};
    fallbackDesc.width = 1U;
    fallbackDesc.height = 1U;
    fallbackDesc.format = TextureFormatUVE::RGBA8Unorm;
    fallbackDesc.mipLevels = 1U;
    const std::byte whitePixel[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    const TextureHandleUVE fallbackHandle =
        CreateTextureUVE(fallbackDesc, std::span<const std::byte>(whitePixel, 4U));
    if (fallbackHandle == kInvalidTextureHandleUVE) {
        UVE_WARNING("VulkanRenderDeviceUVE: fallback texture creation failed");
        return false;
    }
    m_impl->fallbackTextureValue = fallbackHandle.value;
    // M5b: the storage-image sink is a SEPARATE 1x1 black image — imageStore into the shared
    // white sampling fallback would corrupt its "unbound slots sample white" invariant. It is
    // an ordinary texture entry (teardown reaps it with the textures map); its transition to
    // GENERAL happens lazily at first storage-slot use, exactly like any caller texture's.
    const std::byte blackPixel[4] = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    const TextureHandleUVE storageFallbackHandle =
        CreateTextureUVE(fallbackDesc, std::span<const std::byte>(blackPixel, 4U));
    if (storageFallbackHandle == kInvalidTextureHandleUVE) {
        UVE_WARNING("VulkanRenderDeviceUVE: fallback storage-image creation failed");
        return false;
    }
    m_impl->fallbackStorageImageValue = storageFallbackHandle.value;
    return true;
}

std::unique_ptr<VulkanRenderDeviceUVE> VulkanRenderDeviceUVE::CreateFromBridgeUVE(
    Window::IVulkanWindowSurfaceUVE& surfaceBridge) {
    auto device = std::unique_ptr<VulkanRenderDeviceUVE>(
        new VulkanRenderDeviceUVE(nullptr, &surfaceBridge));
    if (!device->m_impl->InitializeUVE()) {
        return nullptr; // partially-initialized state torn down by the destructor
    }
    if (!device->CreateFallbackTextureUVE()) {
        return nullptr; // teardown covers anything the fallback path created
    }
    return device;
}

std::unique_ptr<VulkanRenderDeviceUVE> VulkanRenderDeviceUVE::CreateHeadlessUVE() {
    auto device = std::unique_ptr<VulkanRenderDeviceUVE>(
        new VulkanRenderDeviceUVE(nullptr, nullptr));
    device->m_impl->headless = true; // InitializeUVE() branches on this at the WSI edges
    if (!device->m_impl->InitializeUVE()) {
        return nullptr; // partially-initialized state torn down by the destructor
    }
    if (!device->CreateFallbackTextureUVE()) {
        return nullptr; // teardown covers anything the fallback path created
    }
    return device;
}

// ---------------------------------------------------------------------------
// VulkanCommandBufferUVE — the M2a recorded command buffer. Exactly the GL/Null
// pattern: every ICommandBufferUVE call is recorded as a RecordedCommandUVE
// variant (the shared RHI type), and SubmitUVE() moves the list into the
// device's per-frame FIFO; the actual VkCommandBuffer translation happens once,
// at PresentUVE() time, inside the swapchain render pass. Nothing touches
// Vulkan from this class's methods — matching both the null model and the
// documented interface thread-safety ("recording" is a CPU-only act here).
// ---------------------------------------------------------------------------
class VulkanCommandBufferUVE final : public ICommandBufferUVE {
public:
    void BeginRenderPassUVE(const RenderPassDescUVE& renderPassDesc) override {
        m_commands.emplace_back(BeginRenderPassCommandUVE{renderPassDesc});
    }
    void EndRenderPassUVE() override { m_commands.emplace_back(EndRenderPassCommandUVE{}); }
    void BindPipelineUVE(const PipelineHandleUVE pipeline) override {
        m_commands.emplace_back(BindPipelineCommandUVE{pipeline});
    }
    void BindVertexBufferUVE(const BufferHandleUVE buffer, const std::uint32_t slot) override {
        m_commands.emplace_back(BindVertexBufferCommandUVE{buffer, slot});
    }
    void BindIndexBufferUVE(const BufferHandleUVE buffer) override {
        m_commands.emplace_back(BindIndexBufferCommandUVE{buffer});
    }
    void BindTextureUVE(const TextureHandleUVE texture, const std::uint32_t slot) override {
        m_commands.emplace_back(BindTextureCommandUVE{texture, slot});
    }
    void BindUniformBufferUVE(const BufferHandleUVE buffer, const std::uint32_t slot) override {
        m_commands.emplace_back(BindUniformBufferCommandUVE{buffer, slot});
    }
    void BindStorageBufferUVE(const BufferHandleUVE buffer, const std::uint32_t slot) override {
        m_commands.emplace_back(BindStorageBufferCommandUVE{buffer, slot});
    }
    void SetUniformFloatUVE(std::string_view name, const float value) override {
        m_commands.emplace_back(SetUniformFloatCommandUVE{std::string(name), value});
    }
    void SetUniformIntUVE(std::string_view name, const std::int32_t value) override {
        m_commands.emplace_back(SetUniformIntCommandUVE{std::string(name), value});
    }
    void SetUniformBoolUVE(std::string_view name, const bool value) override {
        m_commands.emplace_back(SetUniformBoolCommandUVE{std::string(name), value});
    }
    void SetUniformVector3UVE(std::string_view name, const Math::Vector3UVE& value) override {
        m_commands.emplace_back(SetUniformVector3CommandUVE{std::string(name), value});
    }
    void SetUniformMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE& value) override {
        m_commands.emplace_back(SetUniformMatrix4x4CommandUVE{std::string(name), value});
    }
    void DrawIndexedUVE(const std::uint32_t indexCount, const std::uint32_t instanceCount) override {
        m_commands.emplace_back(DrawIndexedCommandUVE{indexCount, instanceCount});
    }
    void DrawUVE(const std::uint32_t vertexCount, const std::uint32_t instanceCount) override {
        m_commands.emplace_back(DrawCommandUVE{vertexCount, instanceCount});
    }
    void DrawIndexedIndirectUVE(const BufferHandleUVE buffer, const std::uint64_t offsetBytes) override {
        m_commands.emplace_back(DrawIndexedIndirectCommandRecordUVE{buffer, offsetBytes});
    }
    // M5a: recorded ungated like every other Vulkan-side command — this backend has no
    // record-time pass state; the replay decides (closing any lazily-open rendering instance
    // before dispatching, since compute inside a pass instance is illegal).
    void DispatchUVE(const std::uint32_t groupCountX, const std::uint32_t groupCountY,
                     const std::uint32_t groupCountZ) override {
        m_commands.emplace_back(DispatchCommandUVE{groupCountX, groupCountY, groupCountZ});
    }

    [[nodiscard]] const std::vector<RecordedCommandUVE>& GetCommandsUVE() const noexcept {
        return m_commands;
    }

    /// Moves the recorded list out (SubmitUVE's entire job — the buffer is empty afterwards,
    /// exactly per the interface's "submitted buffers are consumed" contract).
    [[nodiscard]] std::vector<RecordedCommandUVE> TakeCommandsUVE() { return std::move(m_commands); }

private:
    std::vector<RecordedCommandUVE> m_commands;
};

// ---------------------------------------------------------------------------
// M2a "draw slice" resource methods: buffers, shaders, and pipelines are real
// implementations; textures, uniform/descriptor bindings, reflection, and
// pipeline binaries remain documented later-milestone stubs.
// ---------------------------------------------------------------------------

BufferHandleUVE VulkanRenderDeviceUVE::CreateBufferUVE(const BufferDescUVE& desc,
                                                       std::span<const std::byte> initialData) {
    ImplUVE& impl = *m_impl;
    if (!impl.usable) {
        return kInvalidBufferHandleUVE;
    }
    if (!ValidateBufferUploadUVE(desc, initialData) || !IsBufferUsageValidUVE(desc.usage)) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: invalid buffer descriptor "
                    "(size {} bytes, {} bytes initial data, usage {})",
                    desc.sizeBytes, initialData.size(), static_cast<int>(desc.usage));
        return kInvalidBufferHandleUVE;
    }

    VkBufferUsageFlags usageFlags = 0;
    switch (desc.usage) {
        // M3: staged usages carry TRANSFER_DST so the one-shot staging copy can feed them.
        case BufferUsageUVE::Vertex:  usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; break;
        case BufferUsageUVE::Index:   usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;  break;
        case BufferUsageUVE::Uniform: usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
        case BufferUsageUVE::Storage: usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
        // CS7: both bits, because an indirect buffer whose parameters compute cannot write is an
        // indirect buffer with no reason to exist - see BufferUsageUVE's doc comment.
        case BufferUsageUVE::IndirectStorage:
            usageFlags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.sizeBytes;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (impl.vk.vkCreateBuffer(impl.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: vkCreateBuffer failed");
        return kInvalidBufferHandleUVE;
    }

    VkMemoryRequirements requirements{};
    impl.vk.vkGetBufferMemoryRequirements(impl.device, buffer, &requirements);
    // M3 memory policy: VERTEX/INDEX buffers live in DEVICE_LOCAL memory — the performance
    // shape the M2a host-visible policy explicitly documented as later-milestone work. Their
    // initial data and every UpdateBufferUVE reach them through the one-shot staging-copy
    // discipline M2c established for textures (UploadToDeviceLocalBufferUVE). UNIFORM buffers
    // stay HOST_VISIBLE + persistently mapped: they feed the SetUniform ring/dynamic-offset
    // machinery, where host writes ARE the mechanism. STORAGE buffers stay host-visible in
    // this slice as well — SSBO traffic (whole-buffer author-side writes, frequent updates)
    // benefits least from device placement, and the M2f zero-fallback contract stays simple.
    // The exposed behavior (copy-on-update correctness) is identical for every usage.
    const bool deviceLocal =
        (desc.usage == BufferUsageUVE::Vertex || desc.usage == BufferUsageUVE::Index);
    const VkMemoryPropertyFlags wantedFlags =
        deviceLocal
            ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}
            : VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    const std::uint32_t memoryTypeIndex =
        impl.FindMemoryTypeUVE(requirements.memoryTypeBits, wantedFlags);
    if (memoryTypeIndex == UINT32_MAX) {
        impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: no {} memory type satisfies the "
                    "buffer allocation",
                    deviceLocal ? "DEVICE_LOCAL" : "host-visible+coherent");
        return kInvalidBufferHandleUVE;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (impl.vk.vkAllocateMemory(impl.device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: vkAllocateMemory failed");
        return kInvalidBufferHandleUVE;
    }
    if (impl.vk.vkBindBufferMemory(impl.device, buffer, memory, 0U) != VK_SUCCESS) {
        impl.vk.vkFreeMemory(impl.device, memory, nullptr);
        impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: vkBindBufferMemory failed");
        return kInvalidBufferHandleUVE;
    }
    void* mapped = nullptr;
    if (!deviceLocal) {
        // Host-visible usages keep the M2a persistent map. DEVICE_LOCAL memory is not
        // mappable — record.mapped stays nullptr, which routes UpdateBufferUVE through the
        // staging copy (and the teardown unmap guard skips it).
        if (impl.vk.vkMapMemory(impl.device, memory, 0U, desc.sizeBytes, 0U, &mapped) != VK_SUCCESS ||
            mapped == nullptr) {
            impl.vk.vkFreeMemory(impl.device, memory, nullptr);
            impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
            UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: vkMapMemory failed");
            return kInvalidBufferHandleUVE;
        }
    }

    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.buffers.emplace(handleValue, ImplUVE::BufferRecordUVE{buffer, memory, desc.sizeBytes, desc.usage, mapped});
    if (!initialData.empty() && !UpdateBufferUVE(BufferHandleUVE{handleValue}, initialData, 0U)) {
        // Creation did succeed — the failed upload is reported but the handle stays valid,
        // following GlRenderDeviceUVE's convention of treating upload failure as non-fatal.
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: initial upload failed; buffer is zero-initialized");
    }
    return BufferHandleUVE{handleValue};
}

void VulkanRenderDeviceUVE::DestroyBufferUVE(const BufferHandleUVE buffer) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.buffers.find(buffer.value);
    if (found == impl.buffers.end()) {
        return; // safe no-op for invalid/already-destroyed handles, per interface contract
    }
    // An in-flight submit may still read this buffer: drain first. M2a simplicity — the queue
    // is fully serialized anyway (one frame in flight), so the wait is generally a no-op.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
    // M2f: invalidate every cached descriptor set that mentions this buffer as a storage
    // binding — after destruction their VkBuffer handles dangle, and rebinding a stale set
    // would be undefined. Sets with older snapshots are freed back to the pool (FREE bit is
    // set); a live GL-style binding-point bind of the destroyed buffer falls back to the
    // zero-filled storage fallback on the next flush. (Mirrors DestroyTextureUVE's sweep.)
    for (auto& [pipelineValue, pipelineRecord] : impl.pipelines) {
        (void)pipelineValue;
        for (auto cacheIt = pipelineRecord.cachedTextureSets.begin();
             cacheIt != pipelineRecord.cachedTextureSets.end();) {
            const auto mentioned = pipelineRecord.cachedStorageBuffers.find(cacheIt->first);
            if (mentioned != pipelineRecord.cachedStorageBuffers.end() &&
                std::find(mentioned->second.begin(), mentioned->second.end(), buffer.value) !=
                    mentioned->second.end()) {
                (void)impl.vk.vkFreeDescriptorSets(impl.device, impl.descriptorPool, 1U,
                                                   &cacheIt->second);
                pipelineRecord.cachedStorageBuffers.erase(mentioned);
                pipelineRecord.cachedTextureTextures.erase(cacheIt->first);
                cacheIt = pipelineRecord.cachedTextureSets.erase(cacheIt);
            } else {
                ++cacheIt;
            }
        }
    }
    if (found->second.mapped != nullptr) {
        impl.vk.vkUnmapMemory(impl.device, found->second.memory);
    }
    impl.vk.vkDestroyBuffer(impl.device, found->second.buffer, nullptr);
    impl.vk.vkFreeMemory(impl.device, found->second.memory, nullptr);
    impl.buffers.erase(found);
}

bool VulkanRenderDeviceUVE::UpdateBufferUVE(const BufferHandleUVE buffer,
                                            const std::span<const std::byte> data,
                                            const std::size_t offset) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.buffers.find(buffer.value);
    if (found == impl.buffers.end()) {
        return false; // silent false for unknown handles, per interface contract
    }
    const ImplUVE::BufferRecordUVE& record = found->second;
    if (!ValidateBufferUpdateUVE(record.sizeBytes, data.size(), offset)) {
        UVE_WARNING("VulkanRenderDeviceUVE::UpdateBufferUVE: out-of-range update "
                    "(buffer {} bytes, {} bytes at offset {})", record.sizeBytes, data.size(), offset);
        return false;
    }
    if (data.empty()) {
        return true;
    }
    if (record.mapped == nullptr) {
        // M3: DEVICE_LOCAL vertex/index buffers are never host-mapped — the update re-stages
        // through the one-shot transfer copy, which does its own queue-idle wait around the
        // submission, so the copy-on-update contract matches the host-visible path exactly.
        if (!impl.UploadToDeviceLocalBufferUVE(record, data, offset)) {
            UVE_WARNING("VulkanRenderDeviceUVE::UpdateBufferUVE: staged device-local upload "
                        "failed ({} bytes at offset {})", data.size(), offset);
            return false;
        }
        return true;
    }
    // Never write into host memory the GPU might still be reading via a pending submit.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
    std::memcpy(static_cast<std::byte*>(record.mapped) + offset, data.data(), data.size());
    return true;
}

bool VulkanRenderDeviceUVE::ReadbackBufferUVE(const BufferHandleUVE buffer,
                                              const std::span<std::byte> outData,
                                              const std::uint64_t offsetBytes) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.buffers.find(buffer.value);
    if (found == impl.buffers.end()) {
        UVE_WARNING("VulkanRenderDeviceUVE::ReadbackBufferUVE: unknown buffer handle ({})", buffer.value);
        return false;
    }
    const ImplUVE::BufferRecordUVE& record = found->second;
    if (!ValidateBufferUpdateUVE(record.sizeBytes, outData.size(), offsetBytes)) {
        UVE_WARNING("VulkanRenderDeviceUVE::ReadbackBufferUVE: out-of-range read "
                    "(buffer {} bytes, {} bytes at offset {})", record.sizeBytes, outData.size(), offsetBytes);
        return false;
    }
    if (outData.empty()) {
        return true;
    }
    if (record.mapped == nullptr) {
        // M3 placement: VERTEX/INDEX buffers are DEVICE_LOCAL with no TRANSFER_SRC usage, so
        // there is no legal way to copy out of them. Refusing loudly is the honest answer - the
        // interface documents readback as guaranteed for Uniform/Storage only, which is exactly
        // the set this backend keeps host-visible (and Storage is what compute writes through).
        UVE_WARNING("VulkanRenderDeviceUVE::ReadbackBufferUVE: buffer is device-local "
                    "(vertex/index placement) and cannot be read back; readback is supported for "
                    "Uniform and Storage buffers");
        return false;
    }
    // Cold path, as documented: drain the queue so any submitted compute/graphics work that
    // could still be writing this buffer has completed before the host reads it. The memory is
    // HOST_COHERENT, so no explicit invalidate is required once the queue is idle.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
    std::memcpy(outData.data(), static_cast<const std::byte*>(record.mapped) + offsetBytes,
                outData.size());
    return true;
}

TextureHandleUVE VulkanRenderDeviceUVE::CreateTextureUVE(const TextureDescUVE& desc,
                                                         std::span<const std::byte> initialData) {
    ImplUVE& impl = *m_impl;
    const auto fail = [](std::string reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreateTextureUVE: {}", reason);
        return kInvalidTextureHandleUVE;
    };
    if (!ValidateTextureUploadUVE(desc, initialData)) {
        return fail("invalid descriptor or initial upload (rejected by ValidateTextureUploadUVE)");
    }
    if (desc.mipLevels > 1) {
        // Mirrors GlRenderDeviceUVE verbatim: only level 0 is populated in this slice.
        UVE_WARNING("VulkanRenderDeviceUVE::CreateTextureUVE: {} mip levels requested - only level "
                    "0 is populated (GL backend documents the same policy)", desc.mipLevels);
    }
    // M2d format policy: every RGBA8 color texture's IMAGE takes the swapchain's exact
    // format whenever it belongs to the 4x8 RGBA family (sRGB or unorm) — pipelines and
    // dynamic-rendering instances declare that same format, so every texture can be a legal
    // render target. The SAMPLED view uses the unnormalized sibling (raw reads, no implicit
    // sRGB decode); the ATTACHMENT view uses the image-native format. Everything outside the
    // 4x8 family (and RGBA16Float) keeps its native format: still fully sampleable, but
    // attaching it to a render pass warns and skips (documented M2d boundary). RGBA8 initial
    // uploads are swizzled in the staging copy when the image is B,G,R,A-typed (the RHI's
    // byte-order contract is R,G,B,A), so the stored texels are identical either way.
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat sampledFormat = VK_FORMAT_UNDEFINED;
    VkImageCreateFlags imageFlags = 0U;
    // M5b fix: every non-depth texture is storage-capable — the unified texture-slot space
    // lets any color texture bind into a STORAGE_IMAGE slot, and writing a storage descriptor
    // against an image created WITHOUT VK_IMAGE_USAGE_STORAGE_BIT is undefined behavior (it
    // crashed lavapipe at execution in the first storage-image pixel proofs). The Depth32Float
    // arm below reassigns usage without STORAGE: depth images are never storage-bound (the
    // deterministic black-sink substitution intercepts them at tuple resolution).
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::uint32_t bytesPerPixel = 0U;
    switch (desc.format) {
        case TextureFormatUVE::RGBA8Unorm: {
            const VkFormat sibling = UnormSiblingFormatUVE(impl.swapchainFormat);
            if (sibling != VK_FORMAT_UNDEFINED) {
                format = impl.swapchainFormat;
                sampledFormat = sibling;
            } else {
                format = VK_FORMAT_R8G8B8A8_UNORM;
                sampledFormat = format;
            }
            // M5b fix: RGBA8 images are ALWAYS mutable-format — the storage alias view
            // (R8G8B8A8_UNORM over a B,G,R,A-family or sRGB image) needs
            // VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT even when the sampled sibling equals the
            // image format.
            imageFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            bytesPerPixel = 4U;
            break;
        }
        case TextureFormatUVE::RGBA16Float:
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            sampledFormat = format;
            bytesPerPixel = 8U;
            break;
        case TextureFormatUVE::Depth32Float:
            // Depth textures exist so the render-target milestone can attach them; they are
            // created attachment-ready AND sampled-ready (M2e: their rest layout is the
            // shader-readable one, and the dynamic offscreen arm transitions around attachment
            // use — initial uploads of depth data are still rejected: a host depth upload has
            // no RHI consumer today, so refusing it is the honest boundary).
            format = VK_FORMAT_D32_SFLOAT;
            sampledFormat = format;
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bytesPerPixel = 4U;
            if (!initialData.empty()) {
                return fail("Depth32Float initial uploads are not supported (attachment-ready "
                            "only; the render-target slice owns population)");
            }
            break;
    }
    {
        VkFormatProperties formatProperties{};
        impl.vk.vkGetPhysicalDeviceFormatProperties(impl.physicalDevice, format, &formatProperties);
        const VkFormatFeatureFlags required =
            (desc.format == TextureFormatUVE::Depth32Float)
                ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
            : (desc.format == TextureFormatUVE::RGBA8Unorm)
                ? (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            : (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
               VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
        if ((formatProperties.optimalTilingFeatures & required) != required) {
            return fail("device lacks required format features for the requested texture format");
        }
        if (desc.format == TextureFormatUVE::RGBA8Unorm) {
            // M5b fix: the storage alias format (R8G8B8A8_UNORM — the ONLY format matching
            // SPIR-V's rgba8 image qualifier) must support storage images. Its
            // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT is a spec-REQUIRED format feature, so every
            // conformant device passes — refusing loudly here beats undefined descriptor
            // writes later. The IMAGE format itself is not checked for storage: it may be an
            // sRGB or B,G,R,A swapchain alias, which storage access never goes through.
            VkFormatProperties storageAliasProperties{};
            impl.vk.vkGetPhysicalDeviceFormatProperties(
                impl.physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &storageAliasProperties);
            if ((storageAliasProperties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U) {
                return fail("device lacks VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for "
                            "R8G8B8A8_UNORM (storage-image slots need it since M5b)");
            }
        }
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = imageFlags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {desc.width, desc.height, 1U};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = 1U;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    if (impl.vk.vkCreateImage(impl.device, &imageInfo, nullptr, &image) != VK_SUCCESS ||
        image == VK_NULL_HANDLE) {
        return fail("vkCreateImage failed");
    }
    VkMemoryRequirements imageRequirements{};
    impl.vk.vkGetImageMemoryRequirements(impl.device, image, &imageRequirements);
    const std::uint32_t imageMemoryType = impl.FindMemoryTypeUVE(
        imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageMemoryType == UINT32_MAX) {
        impl.vk.vkDestroyImage(impl.device, image, nullptr);
        return fail("no DEVICE_LOCAL memory type satisfies the texture image");
    }
    VkMemoryAllocateInfo imageAllocateInfo{};
    imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocateInfo.allocationSize = imageRequirements.size;
    imageAllocateInfo.memoryTypeIndex = imageMemoryType;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    if (impl.vk.vkAllocateMemory(impl.device, &imageAllocateInfo, nullptr, &imageMemory) != VK_SUCCESS ||
        imageMemory == VK_NULL_HANDLE) {
        impl.vk.vkDestroyImage(impl.device, image, nullptr);
        return fail("vkAllocateMemory failed for the texture image");
    }
    if (impl.vk.vkBindImageMemory(impl.device, image, imageMemory, 0U) != VK_SUCCESS) {
        impl.vk.vkFreeMemory(impl.device, imageMemory, nullptr);
        impl.vk.vkDestroyImage(impl.device, image, nullptr);
        return fail("vkBindImageMemory failed");
    }

    // Staging buffer + upload lives entirely inside this call: the documented M2c upload
    // contract is "create/upload synchronously" — a load-time path, never a per-frame one.
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMapped = nullptr;
    const std::uint64_t uploadBytes = initialData.empty()
        ? 0U : static_cast<std::uint64_t>(desc.width) * desc.height * bytesPerPixel;
    const auto destroyImageResources = [&]() {
        if (stagingBuffer != VK_NULL_HANDLE) {
            impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        }
        if (stagingMemory != VK_NULL_HANDLE) {
            impl.vk.vkFreeMemory(impl.device, stagingMemory, nullptr);
        }
        impl.vk.vkFreeMemory(impl.device, imageMemory, nullptr);
        impl.vk.vkDestroyImage(impl.device, image, nullptr);
    };
    if (uploadBytes != 0U) {
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = uploadBytes;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (impl.vk.vkCreateBuffer(impl.device, &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            destroyImageResources();
            return fail("vkCreateBuffer failed for the staging upload");
        }
        VkMemoryRequirements stagingRequirements{};
        impl.vk.vkGetBufferMemoryRequirements(impl.device, stagingBuffer, &stagingRequirements);
        const std::uint32_t stagingMemoryType = impl.FindMemoryTypeUVE(
            stagingRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (stagingMemoryType == UINT32_MAX) {
            destroyImageResources();
            return fail("no HOST_VISIBLE|COHERENT memory type satisfies the staging buffer");
        }
        VkMemoryAllocateInfo stagingAllocateInfo{};
        stagingAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocateInfo.allocationSize = stagingRequirements.size;
        stagingAllocateInfo.memoryTypeIndex = stagingMemoryType;
        if (impl.vk.vkAllocateMemory(impl.device, &stagingAllocateInfo, nullptr, &stagingMemory) != VK_SUCCESS ||
            impl.vk.vkBindBufferMemory(impl.device, stagingBuffer, stagingMemory, 0U) != VK_SUCCESS ||
            impl.vk.vkMapMemory(impl.device, stagingMemory, 0U, uploadBytes, 0U, &stagingMapped) != VK_SUCCESS ||
            stagingMapped == nullptr) {
            destroyImageResources();
            return fail("staging buffer allocation/bind/map failed");
        }
        if (desc.format == TextureFormatUVE::RGBA8Unorm &&
            (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM)) {
            // The RHI's RGBA8 initial-data contract is R,G,B,A byte order (GL parity). When the
            // swapchain-format allocation picked the B,G,R,A family (SwiftShader's pick, and
            // the most common desktop surface class), swizzle host bytes here in the copy
            // path; the same code already exists in the present-side readback.
            auto* dst = static_cast<std::byte*>(stagingMapped);
            const auto* src = initialData.data();
            const std::size_t texelCount = initialData.size() / 4U;
            for (std::size_t texel = 0; texel < texelCount; ++texel) {
                dst[texel * 4U + 0U] = src[texel * 4U + 2U]; // blue channel to slot 0
                dst[texel * 4U + 1U] = src[texel * 4U + 1U];
                dst[texel * 4U + 2U] = src[texel * 4U + 0U]; // red channel to slot 2
                dst[texel * 4U + 3U] = src[texel * 4U + 3U];
            }
        } else {
            std::memcpy(stagingMapped, initialData.data(), initialData.size());
        }
        impl.vk.vkUnmapMemory(impl.device, stagingMemory);
        stagingMapped = nullptr;
    }

    // One-shot transfer submission on the present queue (graphics queues accept transfer
    // commands; SwiftShader exposes a unified family, and the device already submits every
    // legal command type there), then a full-idle wait so the returned texture is ready.
    VkCommandBufferAllocateInfo commandAllocateInfo{};
    commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocateInfo.commandPool = impl.commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1U;
    VkCommandBuffer transferCommands = VK_NULL_HANDLE;
    if (impl.vk.vkAllocateCommandBuffers(impl.device, &commandAllocateInfo, &transferCommands) != VK_SUCCESS ||
        transferCommands == VK_NULL_HANDLE) {
        destroyImageResources();
        return fail("vkAllocateCommandBuffers failed for the upload submission");
    }
    VkCommandBufferBeginInfo transferBegin{};
    transferBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    transferBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (impl.vk.vkBeginCommandBuffer(transferCommands, &transferBegin) != VK_SUCCESS) {
        impl.vk.vkFreeCommandBuffers(impl.device, impl.commandPool, 1U, &transferCommands);
        destroyImageResources();
        return fail("vkBeginCommandBuffer failed for the upload submission");
    }
    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.srcAccessMask = 0U;
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = image;
    toTransferDst.subresourceRange = {aspect, 0U, desc.mipLevels, 0U, 1U};
    if (uploadBytes != 0U) {
        impl.vk.vkCmdPipelineBarrier(transferCommands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr,
                                     1U, &toTransferDst);
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0U;
        copyRegion.bufferRowLength = 0U;   // tightly packed, matching the upload contract
        copyRegion.bufferImageHeight = 0U; // (validated to be width*height*bpp exactly)
        copyRegion.imageSubresource = {aspect, 0U, 0U, 1U};
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {desc.width, desc.height, 1U};
        impl.vk.vkCmdCopyBufferToImage(transferCommands, stagingBuffer, image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copyRegion);
    }
    VkImageMemoryBarrier toFinal{};
    toFinal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toFinal.srcAccessMask = uploadBytes != 0U ? VK_ACCESS_TRANSFER_WRITE_BIT : 0U;
    toFinal.dstAccessMask = (finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                ? VK_ACCESS_SHADER_READ_BIT
                                : (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    toFinal.oldLayout =
        uploadBytes != 0U ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    toFinal.newLayout = finalLayout;
    toFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.image = image;
    toFinal.subresourceRange = {aspect, 0U, desc.mipLevels, 0U, 1U};
    const VkPipelineStageFlags finalStage =
        finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    impl.vk.vkCmdPipelineBarrier(transferCommands,
                                 uploadBytes != 0U ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                   : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 finalStage, 0U, 0U, nullptr, 0U, nullptr, 1U, &toFinal);
    bool uploadSubmitted = true;
    if (impl.vk.vkEndCommandBuffer(transferCommands) != VK_SUCCESS) {
        uploadSubmitted = false;
    } else {
        VkSubmitInfo transferSubmit{};
        transferSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        transferSubmit.commandBufferCount = 1U;
        transferSubmit.pCommandBuffers = &transferCommands;
        if (impl.vk.vkQueueSubmit(impl.presentQueue, 1U, &transferSubmit, VK_NULL_HANDLE) != VK_SUCCESS ||
            impl.vk.vkQueueWaitIdle(impl.presentQueue) != VK_SUCCESS) {
            uploadSubmitted = false;
        }
    }
    impl.vk.vkFreeCommandBuffers(impl.device, impl.commandPool, 1U, &transferCommands);
    if (!uploadSubmitted) {
        destroyImageResources();
        return fail("the one-shot upload submission failed");
    }
    if (stagingBuffer != VK_NULL_HANDLE) {
        impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        impl.vk.vkFreeMemory(impl.device, stagingMemory, nullptr);
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = sampledFormat;
    viewInfo.subresourceRange = {aspect, 0U, desc.mipLevels, 0U, 1U};
    VkImageView view = VK_NULL_HANDLE;
    if (impl.vk.vkCreateImageView(impl.device, &viewInfo, nullptr, &view) != VK_SUCCESS ||
        view == VK_NULL_HANDLE) {
        destroyImageResources();
        return fail("vkCreateImageView failed for the texture");
    }
    viewInfo.format = format; // image-native: this is the view dynamic rendering attaches
    VkImageView attachmentView = VK_NULL_HANDLE;
    if (impl.vk.vkCreateImageView(impl.device, &viewInfo, nullptr, &attachmentView) != VK_SUCCESS ||
        attachmentView == VK_NULL_HANDLE) {
        impl.vk.vkDestroyImageView(impl.device, view, nullptr);
        destroyImageResources();
        return fail("vkCreateImageView failed for the texture's attachment view");
    }
    // M5b fix: the storage alias view. STORAGE_IMAGE descriptors need a view format matching
    // the shader's SPIR-V image-format qualifier (rgba8 ⇒ R8G8B8A8_UNORM, the only 4x8
    // storage format SPIR-V has) whose format supports VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT.
    // When swapchain-format aliasing made this image B,G,R,A-family or sRGB, the sampled
    // sibling view cannot serve storage descriptors (no matching qualifier exists for it, and
    // B8G8R8A8 formats do not universally advertise storage support) — a dedicated
    // R8G8B8A8_UNORM alias view over the mutable-format image can, and is component-name
    // consistent: writes through it land in the same texels the BGRA-named views address by
    // their own component names. Textures whose sampled view already IS R8G8B8A8_UNORM keep
    // storageView null (descriptor writes fall back to `view`).
    VkImageView storageView = VK_NULL_HANDLE;
    if (desc.format == TextureFormatUVE::RGBA8Unorm &&
        sampledFormat != VK_FORMAT_R8G8B8A8_UNORM) {
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        if (impl.vk.vkCreateImageView(impl.device, &viewInfo, nullptr, &storageView) != VK_SUCCESS ||
            storageView == VK_NULL_HANDLE) {
            impl.vk.vkDestroyImageView(impl.device, attachmentView, nullptr);
            impl.vk.vkDestroyImageView(impl.device, view, nullptr);
            destroyImageResources();
            return fail("vkCreateImageView failed for the texture's storage alias view");
        }
    }

    // Sampler state mirrors GlRenderDeviceUVE's fixed parameters exactly: linear/linear,
    // clamp-to-edge, and effectively no mipmapping (maxLod 0 while only level 0 is populated).
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0F;
    samplerInfo.maxLod = 0.0F;
    samplerInfo.maxAnisotropy = 1.0F;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VkSampler sampler = VK_NULL_HANDLE;
    if (impl.vk.vkCreateSampler(impl.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS ||
        sampler == VK_NULL_HANDLE) {
        if (storageView != VK_NULL_HANDLE) {
            impl.vk.vkDestroyImageView(impl.device, storageView, nullptr);
        }
        impl.vk.vkDestroyImageView(impl.device, attachmentView, nullptr);
        impl.vk.vkDestroyImageView(impl.device, view, nullptr);
        destroyImageResources();
        return fail("vkCreateSampler failed for the texture");
    }

    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.textures.emplace(handleValue,
        ImplUVE::TextureRecordUVE{image, imageMemory, view, attachmentView, storageView,
                                  sampler, format, desc, finalLayout});
    return TextureHandleUVE{handleValue};
}

void VulkanRenderDeviceUVE::DestroyTextureUVE(const TextureHandleUVE texture) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.textures.find(texture.value);
    if (found == impl.textures.end()) {
        // Safe no-op for invalid/already-destroyed handles, matching the interface contract
        // and the GL backend's logged-but-tolerant destruction policy.
        return;
    }
    const std::uint32_t destroyedValue = texture.value;
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue); // replay/transfer may still sample it
    // Invalidate every cached descriptor set that mentions this texture: after destruction
    // their image views dangle, and rebinding a stale set would be undefined. Sets with
    // older snapshots of the same texture are freed back to the pool (FREE bit is set).
    for (auto& [pipelineValue, record] : impl.pipelines) {
        (void)pipelineValue;
        for (auto cacheIt = record.cachedTextureSets.begin();
             cacheIt != record.cachedTextureSets.end();) {
            const auto mentioned = record.cachedTextureTextures.find(cacheIt->first);
            if (mentioned != record.cachedTextureTextures.end() &&
                std::find(mentioned->second.begin(), mentioned->second.end(), destroyedValue) !=
                    mentioned->second.end()) {
                (void)impl.vk.vkFreeDescriptorSets(impl.device, impl.descriptorPool, 1U,
                                                   &cacheIt->second);
                cacheIt = record.cachedTextureSets.erase(cacheIt);
            } else {
                ++cacheIt;
            }
        }
    }
    // A live GL-style texture-unit binding of the destroyed texture falls back to the
    // fallback texture on the next flush rather than referencing a dead view.
    impl.vk.vkDestroySampler(impl.device, found->second.sampler, nullptr);
    if (found->second.storageView != VK_NULL_HANDLE) {
        impl.vk.vkDestroyImageView(impl.device, found->second.storageView, nullptr);
    }
    if (found->second.attachmentView != VK_NULL_HANDLE) {
        impl.vk.vkDestroyImageView(impl.device, found->second.attachmentView, nullptr);
    }
    impl.vk.vkDestroyImageView(impl.device, found->second.view, nullptr);
    impl.vk.vkDestroyImage(impl.device, found->second.image, nullptr);
    impl.vk.vkFreeMemory(impl.device, found->second.memory, nullptr);
    impl.textures.erase(found);
}

ShaderHandleUVE VulkanRenderDeviceUVE::CreateShaderUVE(const ShaderDescUVE& desc,
                                                       std::string* outInfoLog) {
    ImplUVE& impl = *m_impl;
    const auto fail = [outInfoLog](std::string reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreateShaderUVE: {}", reason);
        if (outInfoLog != nullptr) {
            *outInfoLog = std::move(reason);
        }
        return kInvalidShaderHandleUVE;
    };
    if (!impl.usable) {
        return fail("device is not usable");
    }
    // M5a: Compute is real now (CreateComputePipelineUVE consumes it). Geometry stays rejected —
    // it needs a tessellation/geometry pipeline-state story this RHI does not have yet.
    if (!IsShaderStageValidUVE(desc.stage) || desc.stage == ShaderStageUVE::Geometry) {
        return fail("only Vertex/Fragment/Compute stages are supported (Geometry needs a future slice)");
    }
    if (desc.entryPointName.empty()) {
        return fail("entry point name must not be empty");
    }
    // Contract: `sourceCode` carries SPIR-V BYTECODE as raw bytes (little-endian words),
    // never GLSL source text — unlike GlRenderDeviceUVE, which compiles GLSL with the
    // driver's compiler. GLSL->SPIR-V cross-compilation is a separately tracked ROADMAP item
    // (shader toolchain); until then, Vulkan callers pass pre-compiled SPIR-V. Validation:
    // word alignment + the little-endian SPIR-V magic in the first word.
    constexpr std::uint32_t kSpirvMagicLe = 0x07230203U;
    if (desc.sourceCode.size() < 4U || (desc.sourceCode.size() % 4U) != 0U) {
        return fail("sourceCode does not look like SPIR-V bytecode (size must be a nonzero "
                    "multiple of 4 bytes); the Vulkan backend takes SPIR-V, not GLSL text");
    }
    std::uint32_t magic = 0;
    std::memcpy(&magic, desc.sourceCode.data(), sizeof(magic));
    if (magic != kSpirvMagicLe) {
        return fail("sourceCode lacks the SPIR-V magic word (0x07230203 LE); the Vulkan "
                    "backend takes SPIR-V bytecode, not GLSL text");
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc.sourceCode.size();
    moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(desc.sourceCode.data());
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (impl.vk.vkCreateShaderModule(impl.device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS ||
        shaderModule == VK_NULL_HANDLE) {
        return fail("vkCreateShaderModule rejected the SPIR-V module");
    }
    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.shaders.emplace(handleValue,
        ImplUVE::ShaderRecordUVE{shaderModule, desc.stage, desc.entryPointName, desc.sourceCode});
    return ShaderHandleUVE{handleValue};
}

void VulkanRenderDeviceUVE::DestroyShaderUVE(const ShaderHandleUVE shader) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.shaders.find(shader.value);
    if (found == impl.shaders.end()) {
        return; // safe no-op for invalid/already-destroyed handles, per interface contract
    }
    // A pipeline referencing this module may still be mid-replay: drain before destroying.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
    impl.vk.vkDestroyShaderModule(impl.device, found->second.module, nullptr);
    impl.shaders.erase(found);
}

PipelineHandleUVE VulkanRenderDeviceUVE::CreatePipelineUVE(const PipelineDescUVE& desc,
                                                           std::string* outInfoLog) {
    ImplUVE& impl = *m_impl;
    const auto fail = [outInfoLog](std::string reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreatePipelineUVE: {}", reason);
        if (outInfoLog != nullptr) {
            *outInfoLog = std::move(reason);
        }
        return kInvalidPipelineHandleUVE;
    };
    if (!impl.usable) {
        return fail("device is not usable");
    }
    const auto vertexFound = impl.shaders.find(desc.vertexShader.value);
    const auto fragmentFound = impl.shaders.find(desc.fragmentShader.value);
    if (vertexFound == impl.shaders.end() || fragmentFound == impl.shaders.end()) {
        return fail("vertex/fragment shader handles do not reference live shaders");
    }
    if (vertexFound->second.stage != ShaderStageUVE::Vertex ||
        fragmentFound->second.stage != ShaderStageUVE::Fragment) {
        return fail("shader handles bound to the wrong stages");
    }
    if (!IsVertexLayoutValidUVE(desc.vertexLayout) ||
        !IsVertexLayoutWithinStrideUVE(desc.vertexLayout, desc.vertexStride) ||
        !IsPrimitiveTopologyValidUVE(desc.topology) || !IsPipelineBlendModeValidUVE(desc.blendMode)) {
        return fail("malformed pipeline descriptor (vertex layout/stride, topology, or blend mode)");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexFound->second.module;
    stages[0].pName = vertexFound->second.entryPoint.c_str();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentFound->second.module;
    stages[1].pName = fragmentFound->second.entryPoint.c_str();

    // Vertex input: attribute location = index in desc.vertexLayout (matching GlRenderDeviceUVE's
    // glVertexAttribPointer layout-location upper pathology — the RHI semantics carry the meaning;
    // the Vulkan shader is bound positionally by layout(loc=<index>)).
    std::vector<VkVertexInputAttributeDescription> attributes(desc.vertexLayout.size());
    for (std::size_t index = 0; index < desc.vertexLayout.size(); ++index) {
        VkFormat format = VK_FORMAT_UNDEFINED;
        switch (desc.vertexLayout[index].format) {
            case VertexAttributeFormatUVE::Float2: format = VK_FORMAT_R32G32_SFLOAT; break;
            case VertexAttributeFormatUVE::Float3: format = VK_FORMAT_R32G32B32_SFLOAT; break;
            case VertexAttributeFormatUVE::Float4: format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
        }
        attributes[index].location = static_cast<std::uint32_t>(index);
        attributes[index].binding = 0U;
        attributes[index].format = format;
        attributes[index].offset = desc.vertexLayout[index].offset;
    }
    VkVertexInputBindingDescription binding{};
    binding.binding = 0U;
    binding.stride = desc.vertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1U;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // the only RHI topology today
    assembly.primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor are DYNAMIC (set once per frame inside PresentUVE): the swapchain
    // render pass owns pixel coverage, never an individual pipeline.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1U;
    viewportState.scissorCount = 1U;
    constexpr VkDynamicState kDynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2U;
    dynamicState.pDynamicStates = kDynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // GL device default: no culling policy in the RHI
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // M2b: the swapchain render pass now carries a real depth attachment, so the RHI's two
    // depth flags bind exactly as they do in GlRenderDeviceUVE (LESS_OR_EQUAL compare matches
    // its GL depth-func policy).
    depthStencil.depthTestEnable = desc.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (desc.blendMode) {
        case PipelineBlendModeUVE::Opaque:
            break; // blendEnable stays false
        case PipelineBlendModeUVE::SourceAlphaOver:
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case PipelineBlendModeUVE::Additive: // GL contract: glBlendFunc(GL_ONE, GL_ONE)
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case PipelineBlendModeUVE::Multiply: // GL contract: glBlendFunc(GL_DST_COLOR, GL_ZERO)
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            // Alpha: GL's one-call blend function applies the same factors to alpha; mirror it
            // exactly (dst alpha *= src alpha channel behavior of GL_DST_COLOR/GL_ZERO on A is
            // documented-modeled as keep-destination for a color-only render pass).
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1U; // color-only M2a render pass: exactly one attachment
    colorBlend.pAttachments = &blendAttachment;

    // --- M2b reflection: SPIR-V resource layout -> descriptor-set layout + push ranges --------
    ImplUVE::PipelineRecordUVE record;
    {
        struct StageModuleUVE {
            const ImplUVE::ShaderRecordUVE* record;
            VkShaderStageFlagBits flag;
        };
        const StageModuleUVE stageModules[2] = {
            {&vertexFound->second, VK_SHADER_STAGE_VERTEX_BIT},
            {&fragmentFound->second, VK_SHADER_STAGE_FRAGMENT_BIT},
        };
        std::map<std::uint32_t, UniformBlockRefUVE> blocksByBinding; // sorted by binding by map
        std::map<std::uint32_t, TextureSlotRefUVE> textureSlotsByBinding; // same ordering rule
        std::map<std::uint32_t, StorageSlotRefUVE> storageSlotsByBinding;  // M2f, same rule
        std::map<std::uint32_t, SamplerSlotRefUVE> samplerSlotsByBinding;  // M2f, same rule
        // NOTE: members are extracted into an owning vector IMMEDIATELY — the SPIRV-Reflect
        // block pointers dangle the moment spvReflectDestroyShaderModule() runs at the end of
        // each stage's scope (a copied SpvReflectBlockVariable keeps a borrowed members
        // pointer). Storing the member list itself is the safe pattern.
        struct PushRangeUVE {
            std::uint32_t offset = 0U;
            std::uint32_t size = 0U;
            VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
            std::vector<UniformMemberRefUVE> members;
        };
        std::vector<PushRangeUVE> pushRanges;
        bool reflectionFailed = false;
        std::string reflectionError;
        for (const StageModuleUVE& stageModule : stageModules) {
            SpvReflectShaderModule reflection{};
            if (spvReflectCreateShaderModule(stageModule.record->spirvBytes.size(),
                                             stageModule.record->spirvBytes.data(),
                                             &reflection) != SPV_REFLECT_RESULT_SUCCESS) {
                reflectionError = "SPIRV-Reflect could not parse a shader module";
                reflectionFailed = true;
                break;
            }
            std::uint32_t bindingCount = 0;
            spvReflectEnumerateDescriptorBindings(&reflection, &bindingCount, nullptr);
            std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
            spvReflectEnumerateDescriptorBindings(&reflection, &bindingCount, bindings.data());
            for (const SpvReflectDescriptorBinding* binding : bindings) {
                if (binding->set != 0U) {
                    reflectionError = "SPIR-V uses descriptor set " + std::to_string(binding->set) +
                        " — the M2b layout contract is set-0-only (later slices cover more sets)";
                    reflectionFailed = true;
                    break;
                }
                // M2c: combined image samplers bind through the per-tuple set cache.
                // M2f: the separate SAMPLED_IMAGE form joins the same slot rule (the RHI's one
                // sampler shape is written into the pipeline's standalone SAMPLER bindings).
                // M5b: STORAGE_IMAGE joins the SAME slot space too — one ascending-binding
                // ordering across the whole texture family, each slot remembering its form.
                const bool isCombinedSampler =
                    binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                const bool isSampledImage =
                    binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                const bool isStorageImage =
                    binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                if (isCombinedSampler || isSampledImage || isStorageImage) {
                    TextureSlotRefUVE& slot = textureSlotsByBinding[binding->binding];
                    if (slot.stageFlags != 0U &&
                        (slot.name != (binding->name != nullptr ? binding->name : "") ||
                         slot.separateSampler != isSampledImage ||
                         slot.isStorageImage != isStorageImage)) {
                        reflectionError = "the same texture binding carries different names or "
                                          "descriptor kinds (combined vs separate vs storage) "
                                          "across stages";
                        reflectionFailed = true;
                        break;
                    }
                    slot.binding = binding->binding;
                    slot.name = binding->name != nullptr ? binding->name : "";
                    slot.separateSampler = isSampledImage;
                    slot.isStorageImage = isStorageImage;
                    slot.stageFlags |= stageModule.flag;
                    continue;
                }
                if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER) {
                    SamplerSlotRefUVE& samplerSlot = samplerSlotsByBinding[binding->binding];
                    samplerSlot.binding = binding->binding;
                    samplerSlot.stageFlags |= stageModule.flag;
                    continue;
                }
                if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                    StorageSlotRefUVE& storageSlot = storageSlotsByBinding[binding->binding];
                    if (storageSlot.stageFlags != 0U &&
                        storageSlot.name != (binding->name != nullptr ? binding->name : "")) {
                        reflectionError = "the same storage-buffer binding carries different "
                                          "names across stages";
                        reflectionFailed = true;
                        break;
                    }
                    storageSlot.binding = binding->binding;
                    storageSlot.name = binding->name != nullptr ? binding->name : "";
                    storageSlot.stageFlags |= stageModule.flag;
                    continue;
                }
                if (binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                    binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                    reflectionError = std::string("SPIR-V binding [") +
                        (binding->name != nullptr ? binding->name : "?") +
                        "] is not a descriptor form the RHI binds: through M5b the compute "
                        "milestone this RHI accepts uniform blocks, combined image samplers, "
                        "separate sampled-image+sampler pairs, storage buffers (SSBOs), and "
                        "storage images; any other descriptor type is not bound by this RHI";
                    reflectionFailed = true;
                    break;
                }
                UniformBlockRefUVE& block = blocksByBinding[binding->binding];
                if (block.size != 0U && block.size != binding->block.padded_size) {
                    reflectionError = "the same uniform binding disagrees across stages on its "
                                      "block size — inconsistent block declarations";
                    reflectionFailed = true;
                    break;
                }
                block.binding = binding->binding;
                block.size = binding->block.padded_size;
                block.stageFlags |= stageModule.flag;
                std::vector<UniformMemberRefUVE> membersFromThisStage;
                CollectBlockMembersUVE(binding->block, stageModule.flag, -1, membersFromThisStage);
                for (UniformMemberRefUVE& member : membersFromThisStage) {
                    bool merged = false;
                    for (UniformMemberRefUVE& existing : block.members) {
                        if (existing.name == member.name) {
                            if (existing.offset != member.offset || existing.type != member.type) {
                                reflectionError = "uniform member [" + member.name +
                                    "] disagrees across stages on offset/type";
                                reflectionFailed = true;
                            }
                            merged = true;
                            break;
                        }
                    }
                    if (reflectionFailed) {
                        break;
                    }
                    if (!merged) {
                        block.members.push_back(std::move(member));
                    }
                }
                if (reflectionFailed) {
                    break;
                }
            }
            if (reflectionFailed) {
                spvReflectDestroyShaderModule(&reflection);
                break;
            }
            std::uint32_t pushCount = 0;
            spvReflectEnumeratePushConstantBlocks(&reflection, &pushCount, nullptr);
            std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount);
            spvReflectEnumeratePushConstantBlocks(&reflection, &pushCount, pushBlocks.data());
            for (SpvReflectBlockVariable* block : pushBlocks) {
                PushRangeUVE range;
                range.offset = block->offset;
                range.size = block->padded_size != 0U ? block->padded_size : block->size;
                range.stage = stageModule.flag;
                CollectBlockMembersUVE(*block, stageModule.flag, -1, range.members);
                pushRanges.push_back(std::move(range));
            }
            spvReflectDestroyShaderModule(&reflection);
        }
        if (reflectionFailed) {
            return fail(reflectionError);
        }

        // Uniform blocks -> one dynamic-UBO binding each, ordered by binding (order matters:
        // vkCmdBindDescriptorSets' dynamicOffsets array is indexed in binding order).
        for (auto& [binding, block] : blocksByBinding) {
            UniformBlockRefUVE finalBlock = std::move(block);
            const std::int32_t blockIndex = static_cast<std::int32_t>(record.uniformBlocks.size());
            for (UniformMemberRefUVE& member : finalBlock.members) {
                member.blockIndex = blockIndex;
            }
            finalBlock.shadow.assign(finalBlock.size, std::byte{0});
            record.uniformBlocks.push_back(std::move(finalBlock));
        }
        // Texture slots: sorted by binding; the RHI "slot" is the position in this list.
        for (auto& [binding, slot] : textureSlotsByBinding) {
            record.textureSlots.push_back(std::move(slot));
        }
        // M2f: storage-buffer slots and standalone sampler bindings follow the same rule.
        for (auto& [binding, slot] : storageSlotsByBinding) {
            record.storageSlots.push_back(std::move(slot));
        }
        for (auto& [binding, slot] : samplerSlotsByBinding) {
            record.samplerBindings.push_back(std::move(slot));
        }
        // Push constants: at most one block per entry point by SPIR-V rules; take the first,
        // and verify every additional range matches the same block extent across stages.
        if (!pushRanges.empty()) {
            record.pushBlock.valid = true;
            record.pushBlock.offset = pushRanges.front().offset;
            record.pushBlock.size = pushRanges.front().size;
            for (const PushRangeUVE& range : pushRanges) {
                if (range.offset != record.pushBlock.offset || range.size != record.pushBlock.size) {
                    return fail("inconsistent push-constant ranges across stages");
                }
                record.pushBlock.stageFlags |= range.stage;
                for (const UniformMemberRefUVE& member : range.members) { // blockIndex already -1
                    bool merged = false;
                    for (UniformMemberRefUVE& existing : record.pushBlock.members) {
                        if (existing.name == member.name) {
                            if (existing.offset != member.offset || existing.type != member.type) {
                                return fail("push-constant member [" + member.name +
                                            "] disagrees across stages on offset/type");
                            }
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) {
                        record.pushBlock.members.push_back(std::move(member));
                    }
                }
            }
            record.pushBlock.shadow.assign(record.pushBlock.size, std::byte{0});
        }
    }

    // Descriptor set layout: one dynamic-UBO binding per uniform block, one image binding per
    // texture slot (combined, or M2f separate sampled-image), one M2f STORAGE_BUFFER binding
    // per storage slot, and one M2f SAMPLER binding per standalone sampler (all set 0).
    // Pipelines without texture/storage slots allocate their single static set right here
    // (M2b contract, never rewritten); tuple-bearing pipelines allocate per bound-resource-
    // tuple sets lazily at first draw flush instead.
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (!record.uniformBlocks.empty() || !record.textureSlots.empty() ||
        !record.storageSlots.empty() || !record.samplerBindings.empty()) {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(
            record.uniformBlocks.size() + record.textureSlots.size() +
            record.storageSlots.size() + record.samplerBindings.size());
        std::size_t layoutIndex = 0;
        for (const UniformBlockRefUVE& block : record.uniformBlocks) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = block.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = block.stageFlags;
        }
        for (const TextureSlotRefUVE& slot : record.textureSlots) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = slot.binding;
            // M5b: STORAGE_IMAGE joins the two sampled forms in the one slot space.
            layoutBinding.descriptorType = slot.isStorageImage
                ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                : (slot.separateSampler ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                        : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = slot.stageFlags;
        }
        for (const StorageSlotRefUVE& slot : record.storageSlots) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = slot.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = slot.stageFlags;
        }
        for (const SamplerSlotRefUVE& slot : record.samplerBindings) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = slot.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = slot.stageFlags;
        }
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
        setLayoutInfo.pBindings = layoutBindings.data();
        if (impl.vk.vkCreateDescriptorSetLayout(impl.device, &setLayoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS ||
            descriptorSetLayout == VK_NULL_HANDLE) {
            return fail("vkCreateDescriptorSetLayout failed");
        }
        if (record.textureSlots.empty() && record.storageSlots.empty()) {
            VkDescriptorSetAllocateInfo setAllocateInfo{};
            setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setAllocateInfo.descriptorPool = impl.descriptorPool;
            setAllocateInfo.descriptorSetCount = 1U;
            setAllocateInfo.pSetLayouts = &descriptorSetLayout;
            if (impl.vk.vkAllocateDescriptorSets(impl.device, &setAllocateInfo, &descriptorSet) != VK_SUCCESS ||
                descriptorSet == VK_NULL_HANDLE) {
                impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
                return fail("vkAllocateDescriptorSets failed (shared M2c pool exhausted?)");
            }
            // Bind the whole frame UBO once per binding; per-draw selection happens purely
            // through vkCmdBindDescriptorSets' dynamic offsets - no per-draw descriptor writes.
            // M2f: standalone SAMPLER bindings get the device's fixed sampler here — it never
            // changes, so the static-set contract ("written once, never rewritten") holds.
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorBufferInfo> bufferInfos(record.uniformBlocks.size());
            std::vector<VkDescriptorImageInfo> samplerInfos(record.samplerBindings.size());
            writes.reserve(record.uniformBlocks.size() + record.samplerBindings.size());
            for (std::size_t index = 0; index < record.uniformBlocks.size(); ++index) {
                VkDescriptorBufferInfo& bufferInfo = bufferInfos[index];
                bufferInfo = {};
                bufferInfo.buffer = impl.frameUbo;
                bufferInfo.offset = 0U;
                bufferInfo.range = record.uniformBlocks[index].size;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSet;
                write.dstBinding = record.uniformBlocks[index].binding;
                write.dstArrayElement = 0U;
                write.descriptorCount = 1U;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                write.pBufferInfo = &bufferInfo;
                writes.push_back(write);
            }
            for (std::size_t index = 0; index < record.samplerBindings.size(); ++index) {
                VkDescriptorImageInfo& samplerInfo = samplerInfos[index];
                samplerInfo = {};
                samplerInfo.sampler = impl.fixedSampler;
                samplerInfo.imageView = VK_NULL_HANDLE; // ignored for SAMPLER-type writes
                samplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSet;
                write.dstBinding = record.samplerBindings[index].binding;
                write.dstArrayElement = 0U;
                write.descriptorCount = 1U;
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                write.pImageInfo = &samplerInfo;
                writes.push_back(write);
            }
            impl.vk.vkUpdateDescriptorSets(impl.device, static_cast<std::uint32_t>(writes.size()),
                                           writes.data(), 0U, nullptr);
        }
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        layoutInfo.setLayoutCount = 1U;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
    }
    VkPushConstantRange pushRange{};
    if (record.pushBlock.valid) {
        pushRange.stageFlags = record.pushBlock.stageFlags;
        pushRange.offset = record.pushBlock.offset;
        pushRange.size = record.pushBlock.size;
        layoutInfo.pushConstantRangeCount = 1U;
        layoutInfo.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (impl.vk.vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS ||
        pipelineLayout == VK_NULL_HANDLE) {
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
        }
        return fail("vkCreatePipelineLayout failed");
    }

    // M2d: in dynamic-rendering mode the pipeline never names a VkRenderPass; instead it
    // declares the one-color(RGBA8)-plus-depth attachment contract that every rendering
    // instance it will ever join satisfies (swapchain pass or offscreen RT pass — the whole
    // reason textures here allocate in the swapchain's exact format). Classic mode is
    // byte-identical to M2c: the device render pass is bound as before.
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1U;
    renderingCreateInfo.pColorAttachmentFormats = &impl.swapchainFormat;
    renderingCreateInfo.depthAttachmentFormat = impl.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = impl.useDynamicRendering ? &renderingCreateInfo : nullptr;
    pipelineInfo.stageCount = 2U;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass =
        impl.useDynamicRendering ? VK_NULL_HANDLE : impl.renderPass;
    pipelineInfo.subpass = 0U;

    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = impl.vk.vkCreateGraphicsPipelines(
        impl.device, VK_NULL_HANDLE /*no pipeline cache in M2a*/, 1U, &pipelineInfo, nullptr, &graphicsPipeline);
    if (pipelineResult != VK_SUCCESS || graphicsPipeline == VK_NULL_HANDLE) {
        impl.vk.vkDestroyPipelineLayout(impl.device, pipelineLayout, nullptr);
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
        }
        return fail("vkCreateGraphicsPipelines failed (driver validation error)");
    }
    // Reflection table for GetPipelineUniformsUVE(): one entry per reflected member (UBO first,
    // then push constants), `location` is the internal resolve-table index consumed at replay.
    for (const UniformBlockRefUVE& block : record.uniformBlocks) {
        for (const UniformMemberRefUVE& member : block.members) {
            UniformReflectionUVE reflectionEntry{};
            reflectionEntry.name = member.name;
            reflectionEntry.type = member.type;
            reflectionEntry.location = static_cast<int>(record.reflectedUniforms.size());
            reflectionEntry.arraySize = member.arraySize;
            record.reflectedUniforms.push_back(std::move(reflectionEntry));
        }
    }
    for (const UniformMemberRefUVE& member : record.pushBlock.members) {
        UniformReflectionUVE reflectionEntry{};
        reflectionEntry.name = member.name;
        reflectionEntry.type = member.type;
        reflectionEntry.location = static_cast<int>(record.reflectedUniforms.size());
        reflectionEntry.arraySize = member.arraySize;
        record.reflectedUniforms.push_back(std::move(reflectionEntry));
    }
    record.pipeline = graphicsPipeline;
    record.layout = pipelineLayout;
    record.descriptorSetLayout = descriptorSetLayout;
    record.descriptorSet = descriptorSet;
    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandleUVE{handleValue};
}

PipelineHandleUVE VulkanRenderDeviceUVE::CreateComputePipelineUVE(const ComputePipelineDescUVE& desc,
                                                                  std::string* outInfoLog) {
    ImplUVE& impl = *m_impl;
    const auto fail = [outInfoLog](std::string reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::CreateComputePipelineUVE: {}", reason);
        if (outInfoLog != nullptr) {
            *outInfoLog = std::move(reason);
        }
        return kInvalidPipelineHandleUVE;
    };
    if (!impl.usable) {
        return fail("device is not usable");
    }
    const auto shaderFound = impl.shaders.find(desc.computeShader.value);
    if (shaderFound == impl.shaders.end()) {
        return fail("compute shader handle does not reference a live shader");
    }
    if (shaderFound->second.stage != ShaderStageUVE::Compute) {
        return fail("shader handle is bound to the wrong stage (Compute required)");
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shaderFound->second.module;
    stage.pName = shaderFound->second.entryPoint.c_str();

    // --- M5a/M5b compute reflection ------------------------------------------------------
    // Single stage, set-0-only, and deliberately narrower than the graphics reflection:
    // uniform blocks (ring-dynamic), storage buffers (slot-bound), and — since M5b —
    // storage images (texture-slot-bound, VK_IMAGE_LAYOUT_GENERAL) are the descriptor forms
    // a compute slice honestly covers today. Sampled textures/samplers still refuse
    // (compute sampling lands when a real workload needs it).
    ImplUVE::PipelineRecordUVE record;
    record.isCompute = true;
    {
        SpvReflectShaderModule reflection{};
        if (spvReflectCreateShaderModule(shaderFound->second.spirvBytes.size(),
                                         shaderFound->second.spirvBytes.data(),
                                         &reflection) != SPV_REFLECT_RESULT_SUCCESS) {
            return fail("SPIRV-Reflect could not parse the compute shader module");
        }
        std::map<std::uint32_t, UniformBlockRefUVE> blocksByBinding;
        std::map<std::uint32_t, StorageSlotRefUVE> storageSlotsByBinding;
        std::map<std::uint32_t, TextureSlotRefUVE> textureSlotsByBinding; // M5b: STORAGE_IMAGE
        bool reflectionFailed = false;
        std::string reflectionError;
        std::uint32_t bindingCount = 0;
        spvReflectEnumerateDescriptorBindings(&reflection, &bindingCount, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        spvReflectEnumerateDescriptorBindings(&reflection, &bindingCount, bindings.data());
        for (const SpvReflectDescriptorBinding* binding : bindings) {
            if (binding->set != 0U) {
                reflectionError = "SPIR-V uses descriptor set " + std::to_string(binding->set) +
                    " — the compute layout contract is set-0-only (same as the graphics side)";
                reflectionFailed = true;
                break;
            }
            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                StorageSlotRefUVE& slot = storageSlotsByBinding[binding->binding];
                slot.binding = binding->binding;
                slot.name = binding->name != nullptr ? binding->name : "";
                slot.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
                continue;
            }
            if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                // M5b: storage images join compute (imageLoad/imageStore sinks/sources) through
                // the same slot space and GENERAL-layout policy as the graphics side.
                TextureSlotRefUVE& slot = textureSlotsByBinding[binding->binding];
                slot.binding = binding->binding;
                slot.name = binding->name != nullptr ? binding->name : "";
                slot.isStorageImage = true;
                slot.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
                continue;
            }
            if (binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                reflectionError = std::string("SPIR-V binding [") +
                    (binding->name != nullptr ? binding->name : "?") +
                    "] is not a descriptor form this RHI's compute pipelines bind: through M5b "
                    "the compute slice accepts uniform blocks, storage buffers (SSBOs), and "
                    "storage images; sampled textures/samplers are not bound by compute "
                    "pipelines yet";
                reflectionFailed = true;
                break;
            }
            UniformBlockRefUVE& block = blocksByBinding[binding->binding];
            block.binding = binding->binding;
            block.size = binding->block.padded_size;
            block.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
            // Single stage — no cross-stage merge checks needed; members are copied out
            // immediately (the reflection module dies at the end of this scope).
            CollectBlockMembersUVE(binding->block, VK_SHADER_STAGE_COMPUTE_BIT, -1, block.members);
        }
        std::uint32_t pushCount = 0;
        if (!reflectionFailed) {
            spvReflectEnumeratePushConstantBlocks(&reflection, &pushCount, nullptr);
            std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount);
            spvReflectEnumeratePushConstantBlocks(&reflection, &pushCount, pushBlocks.data());
            for (const SpvReflectBlockVariable* pushBlock : pushBlocks) {
                const std::uint32_t offset = pushBlock->offset;
                const std::uint32_t size =
                    pushBlock->padded_size != 0U ? pushBlock->padded_size : pushBlock->size;
                if (record.pushBlock.valid &&
                    (record.pushBlock.offset != offset || record.pushBlock.size != size)) {
                    reflectionError = "inconsistent push-constant ranges in the compute shader";
                    reflectionFailed = true;
                    break;
                }
                record.pushBlock.valid = true;
                record.pushBlock.offset = offset;
                record.pushBlock.size = size;
                record.pushBlock.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
                CollectBlockMembersUVE(*pushBlock, VK_SHADER_STAGE_COMPUTE_BIT, -1,
                                       record.pushBlock.members);
            }
        }
        spvReflectDestroyShaderModule(&reflection);
        if (reflectionFailed) {
            return fail(reflectionError);
        }
        for (auto& [binding, block] : blocksByBinding) {
            UniformBlockRefUVE finalBlock = std::move(block);
            const std::int32_t blockIndex = static_cast<std::int32_t>(record.uniformBlocks.size());
            for (UniformMemberRefUVE& member : finalBlock.members) {
                member.blockIndex = blockIndex;
            }
            finalBlock.shadow.assign(finalBlock.size, std::byte{0});
            record.uniformBlocks.push_back(std::move(finalBlock));
        }
        for (auto& [binding, slot] : storageSlotsByBinding) {
            record.storageSlots.push_back(std::move(slot));
        }
        // M5b: storage-image slots follow the same ascending-binding slot rule.
        for (auto& [binding, slot] : textureSlotsByBinding) {
            record.textureSlots.push_back(std::move(slot));
        }
        if (record.pushBlock.valid) {
            record.pushBlock.shadow.assign(record.pushBlock.size, std::byte{0});
        }
    }

    // Descriptor set layout: dynamic-UBO bindings + STORAGE_BUFFER bindings + M5b
    // STORAGE_IMAGE bindings, all set 0. Uniform-only compute pipelines get their static set
    // right here (written once, never rewritten); storage/image-bearing ones allocate
    // per-tuple sets lazily at dispatch flush — the identical M2c/M2f caching contract the
    // graphics side uses.
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (!record.uniformBlocks.empty() || !record.storageSlots.empty() ||
        !record.textureSlots.empty()) {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(
            record.uniformBlocks.size() + record.storageSlots.size() + record.textureSlots.size());
        std::size_t layoutIndex = 0;
        for (const UniformBlockRefUVE& block : record.uniformBlocks) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = block.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = block.stageFlags;
        }
        for (const StorageSlotRefUVE& slot : record.storageSlots) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = slot.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layoutBinding.descriptorCount = 1U;
            layoutBinding.stageFlags = slot.stageFlags;
        }
        for (const TextureSlotRefUVE& slot : record.textureSlots) {
            VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings[layoutIndex++];
            layoutBinding = {};
            layoutBinding.binding = slot.binding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; // M5b: only form
            layoutBinding.descriptorCount = 1U;                              // compute binds
            layoutBinding.stageFlags = slot.stageFlags;
        }
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
        setLayoutInfo.pBindings = layoutBindings.data();
        if (impl.vk.vkCreateDescriptorSetLayout(impl.device, &setLayoutInfo, nullptr,
                                                &descriptorSetLayout) != VK_SUCCESS ||
            descriptorSetLayout == VK_NULL_HANDLE) {
            return fail("vkCreateDescriptorSetLayout failed");
        }
        if (record.storageSlots.empty() && record.textureSlots.empty()) {
            VkDescriptorSetAllocateInfo setAllocateInfo{};
            setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setAllocateInfo.descriptorPool = impl.descriptorPool;
            setAllocateInfo.descriptorSetCount = 1U;
            setAllocateInfo.pSetLayouts = &descriptorSetLayout;
            if (impl.vk.vkAllocateDescriptorSets(impl.device, &setAllocateInfo, &descriptorSet) !=
                    VK_SUCCESS ||
                descriptorSet == VK_NULL_HANDLE) {
                impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
                return fail("vkAllocateDescriptorSets failed (shared pool exhausted?)");
            }
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorBufferInfo> bufferInfos(record.uniformBlocks.size());
            writes.reserve(record.uniformBlocks.size());
            for (std::size_t index = 0; index < record.uniformBlocks.size(); ++index) {
                VkDescriptorBufferInfo& bufferInfo = bufferInfos[index];
                bufferInfo = {};
                bufferInfo.buffer = impl.frameUbo;
                bufferInfo.offset = 0U;
                bufferInfo.range = record.uniformBlocks[index].size;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSet;
                write.dstBinding = record.uniformBlocks[index].binding;
                write.dstArrayElement = 0U;
                write.descriptorCount = 1U;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                write.pBufferInfo = &bufferInfo;
                writes.push_back(write);
            }
            impl.vk.vkUpdateDescriptorSets(impl.device, static_cast<std::uint32_t>(writes.size()),
                                           writes.data(), 0U, nullptr);
        }
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        layoutInfo.setLayoutCount = 1U;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
    }
    VkPushConstantRange pushRange{};
    if (record.pushBlock.valid) {
        pushRange.stageFlags = record.pushBlock.stageFlags;
        pushRange.offset = record.pushBlock.offset;
        pushRange.size = record.pushBlock.size;
        layoutInfo.pushConstantRangeCount = 1U;
        layoutInfo.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (impl.vk.vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr, &pipelineLayout) !=
            VK_SUCCESS ||
        pipelineLayout == VK_NULL_HANDLE) {
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
        }
        return fail("vkCreatePipelineLayout failed");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = pipelineLayout;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = impl.vk.vkCreateComputePipelines(
        impl.device, VK_NULL_HANDLE /*no pipeline cache in M5a*/, 1U, &pipelineInfo, nullptr,
        &computePipeline);
    if (pipelineResult != VK_SUCCESS || computePipeline == VK_NULL_HANDLE) {
        impl.vk.vkDestroyPipelineLayout(impl.device, pipelineLayout, nullptr);
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            impl.vk.vkDestroyDescriptorSetLayout(impl.device, descriptorSetLayout, nullptr);
        }
        return fail("vkCreateComputePipelines failed (driver validation error)");
    }

    // Reflection table for GetPipelineUniformsUVE() — same shape as the graphics side.
    for (const UniformBlockRefUVE& block : record.uniformBlocks) {
        for (const UniformMemberRefUVE& member : block.members) {
            UniformReflectionUVE reflectionEntry{};
            reflectionEntry.name = member.name;
            reflectionEntry.type = member.type;
            reflectionEntry.location = static_cast<int>(record.reflectedUniforms.size());
            reflectionEntry.arraySize = member.arraySize;
            record.reflectedUniforms.push_back(std::move(reflectionEntry));
        }
    }
    for (const UniformMemberRefUVE& member : record.pushBlock.members) {
        UniformReflectionUVE reflectionEntry{};
        reflectionEntry.name = member.name;
        reflectionEntry.type = member.type;
        reflectionEntry.location = static_cast<int>(record.reflectedUniforms.size());
        reflectionEntry.arraySize = member.arraySize;
        record.reflectedUniforms.push_back(std::move(reflectionEntry));
    }
    record.pipeline = computePipeline;
    record.layout = pipelineLayout;
    record.descriptorSetLayout = descriptorSetLayout;
    record.descriptorSet = descriptorSet;
    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.pipelines.emplace(handleValue, std::move(record));
    return PipelineHandleUVE{handleValue};
}

void VulkanRenderDeviceUVE::DestroyPipelineUVE(const PipelineHandleUVE pipeline) {
    ImplUVE& impl = *m_impl;
    const auto found = impl.pipelines.find(pipeline.value);
    if (found == impl.pipelines.end()) {
        return; // safe no-op for invalid/already-destroyed handles, per interface contract
    }
    // Descriptor-set layout destruction requires no in-flight use; the queue-idle above
    // covers it. The descriptor SET is freed implicitly with the pool at shutdown.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue); // replay may reference the pipeline
    impl.vk.vkDestroyPipeline(impl.device, found->second.pipeline, nullptr);
    impl.vk.vkDestroyPipelineLayout(impl.device, found->second.layout, nullptr);
    if (found->second.descriptorSetLayout != VK_NULL_HANDLE) {
        impl.vk.vkDestroyDescriptorSetLayout(impl.device, found->second.descriptorSetLayout, nullptr);
    }
    impl.pipelines.erase(found);
}

std::vector<UniformReflectionUVE> VulkanRenderDeviceUVE::GetPipelineUniformsUVE(
    const PipelineHandleUVE pipeline) const {
    const ImplUVE& impl = *m_impl;
    const auto found = impl.pipelines.find(pipeline.value);
    if (found == impl.pipelines.end()) {
        return {}; // invalid handle: empty reflection, per interface contract
    }
    return found->second.reflectedUniforms;
}

bool VulkanRenderDeviceUVE::GetPipelineBinaryUVE(PipelineHandleUVE /*pipeline*/,
                                                 std::vector<std::byte>& /*outBinary*/,
                                                 std::uint32_t& /*outFormat*/) const {
    return false;
}

PipelineHandleUVE VulkanRenderDeviceUVE::CreatePipelineFromBinaryUVE(std::span<const std::byte> /*binary*/,
                                                                     std::uint32_t /*format*/,
                                                                     const PipelineBinaryDescUVE& /*desc*/) {
    // Not logged as loudly as the other M1 gaps: the shader-cache caller treats a miss here as
    // an ordinary cache miss (documented in IRenderDeviceUVE) — log at info level for traces
    // but keep the warning channel reserved for truly unimplemented paths.
    UVE_INFO("VulkanRenderDeviceUVE::CreatePipelineFromBinaryUVE: no Vulkan pipeline binaries yet (M1)");
    return kInvalidPipelineHandleUVE;
}

std::unique_ptr<ICommandBufferUVE> VulkanRenderDeviceUVE::CreateCommandBufferUVE() {
    if (!m_impl->usable) {
        return nullptr;
    }
    return std::make_unique<VulkanCommandBufferUVE>();
}

void VulkanRenderDeviceUVE::SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) {
    if (commandBuffer == nullptr) {
        return; // null submissions are ignored, per interface contract
    }
    auto* vulkanCommands = dynamic_cast<VulkanCommandBufferUVE*>(commandBuffer.get());
    if (vulkanCommands == nullptr) {
        // A foreign implementation's recording would be dropped by GlRenderDeviceUVE the same
        // way (it static_casts its own type); warn rather than silently discard.
        UVE_WARNING("VulkanRenderDeviceUVE::SubmitUVE: command buffer was not created by this "
                    "device; ignoring submission");
        return;
    }
    if (!m_impl->usable) {
        return;
    }
    {
        // M4: safe to call from any thread — the FIFO push is the only shared mutation and
        // it happens under the submission lock (PresentUVE drains under the same lock).
        const std::lock_guard<std::mutex> submissionLock(m_impl->submissionMutex);
        m_impl->frameSubmissions.push_back(vulkanCommands->TakeCommandsUVE());
    }
}

/// Replays one submitted recorded command buffer's ops into the frame's already-open swapchain
/// render pass. The M2a model is a single shared native render pass per frame (matching what
/// GlRenderDeviceUVE does with the default framebuffer): a submitted pass that targets the
/// "default framebuffer" (invalid color+depth attachments) maps to pass-already-open ops —
/// BeginRenderPassUVE/EndRenderPassUVE become scheduling markers, never native begin/end.
/// Submitted passes that name real texture attachments are skipped with a one-shot warning —
/// offscreen targets land with the texture milestone.
void VulkanRenderDeviceUVE::ReplayRecordedCommandsUVE(const std::vector<RecordedCommandUVE>& commands) {
    ImplUVE& impl = *m_impl;
    bool passActiveForThisList = false;
    (void)passActiveForThisList; // referenced inside the visit; silence when empty list

    // --- M2b uniform machinery -------------------------------------------------------
    // Per-draw flush: snapshot every reflected UBO shadow into a fresh cursor-aligned
    // region of the frame UBO, bind the pipeline's descriptor set with those dynamic
    // offsets, and push the constant-block shadow. No per-draw descriptor writes, no
    // per-frame descriptor churn — the simple dynamic-UBO pattern. Returns false when
    // the frame UBO is exhausted: the caller SKIPS the draw loudly (never binds stale
    // regions, since the GPU could still read them).
    // GL-global texture-unit state for this submission replay (slot -> texture handle id):
    // BindTextureUVE mirrors glActiveTexture+glBindTexture — program-independent. A pipeline's
    // i-th reflected sampler binding reads slot i at draw time.
    std::map<std::uint32_t, std::uint32_t> currentTextureValues;
    // M2f: replay-local storage-buffer binds, the SSBO analogue of currentTextureValues
    // (GL shader-storage binding-point semantics; validated at the command handler below).
    std::map<std::uint32_t, std::uint32_t> currentStorageValues;

    // M5b: invalidate every cached descriptor set that references `textureValue` — a
    // storage-image GENERAL transition changes the texture's layout permanently and any
    // baked sampled/storage descriptor would contradict it. Invalidated sets park in
    // deferredDescriptorSetFreesUVE instead of being freed here: this frame's already-
    // recorded binds may still reference them (freeing a set bound in a pending command
    // buffer is undefined); the next PresentUVE's post-fence point frees them.
    const auto invalidateCachedSetsReferencingTextureUVE = [&impl](const std::uint32_t textureValue) {
        for (auto& [pipelineValue, pipelineRecord] : impl.pipelines) {
            (void)pipelineValue;
            for (auto cacheIt = pipelineRecord.cachedTextureSets.begin();
                 cacheIt != pipelineRecord.cachedTextureSets.end();) {
                const auto mentioned = pipelineRecord.cachedTextureTextures.find(cacheIt->first);
                if (mentioned != pipelineRecord.cachedTextureTextures.end() &&
                    std::find(mentioned->second.begin(), mentioned->second.end(), textureValue) !=
                        mentioned->second.end()) {
                    impl.deferredDescriptorSetFreesUVE.push_back(cacheIt->second);
                    pipelineRecord.cachedTextureTextures.erase(mentioned);
                    pipelineRecord.cachedStorageBuffers.erase(cacheIt->first);
                    cacheIt = pipelineRecord.cachedTextureSets.erase(cacheIt);
                } else {
                    ++cacheIt;
                }
            }
        }
    };

    // M5b: transition a storage-image-bound texture to GENERAL — its permanent rest state
    // from here on (GENERAL stays legal for later sampling/attachment; CloseCurrentPass-
    // DynamicUVE restores pinned textures to it). The barrier must be recorded OUTSIDE any
    // rendering instance, so an open pass is closed first and reopened with Load semantics
    // (content preserved) afterwards. Returns false when the transition is impossible
    // (classic arm: its one native pass spans the whole replay) or the reopen failed —
    // callers skip the offending draw/dispatch loudly rather than record an invalid layout.
    const auto ensureStorageImageGeneralUVE =
        [&impl, &passActiveForThisList, &invalidateCachedSetsReferencingTextureUVE](
            const std::uint32_t textureValue) -> bool {
        ImplUVE::TextureRecordUVE& textureRecord = impl.textures.at(textureValue);
        if (textureRecord.pinnedGeneral) {
            return true; // already GENERAL — permanently
        }
        if (!impl.useDynamicRendering) {
            impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedStorageImageTransitionUVE,
                "replay: a storage-image slot was used on a device without core 1.3 dynamic "
                "rendering; the classic arm's single native pass spans the whole replay, so "
                "the GENERAL transition — and the draw/dispatch needing it — is skipped");
            return false;
        }
        const bool passWasOpen = impl.framePassState.passOpen;
        const bool wasSwapchain = impl.framePassState.openPassIsSwapchain;
        ImplUVE::TextureRecordUVE* colorRecord = impl.framePassState.openOffscreenColorRecord;
        ImplUVE::TextureRecordUVE* depthRecord = impl.framePassState.openOffscreenDepthRecord;
        if (passWasOpen) {
            impl.CloseCurrentPassDynamicUVE(); // layout barriers are illegal inside an instance
        }
        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.srcAccessMask = textureRecord.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                      ? VkAccessFlags{0U}
                                      : VkAccessFlags{VK_ACCESS_SHADER_READ_BIT};
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        toGeneral.oldLayout = textureRecord.currentLayout; // rest layouts: UNDEFINED (fresh,
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;     // content discarded) or SHADER_READ
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = textureRecord.image;
        toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        impl.vk.vkCmdPipelineBarrier(impl.commandBuffer,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0U, 0U, nullptr, 0U, nullptr, 1U, &toGeneral);
        textureRecord.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        textureRecord.pinnedGeneral = true;
        // Cached sets anywhere that baked this texture's old layout now contradict it.
        invalidateCachedSetsReferencingTextureUVE(textureValue);
        if (passWasOpen) {
            const bool reopened =
                wasSwapchain
                    ? impl.BeginSwapchainPassDynamicUVE()
                    : (colorRecord != nullptr &&
                       impl.BeginOffscreenPassDynamicUVE(
                           *colorRecord, depthRecord,
                           VkExtent2D{colorRecord->desc.width, colorRecord->desc.height},
                           {0.0F, 0.0F, 0.0F, 0.0F}, 1.0F, LoadOpUVE::Load, LoadOpUVE::Load));
            if (!reopened) {
                impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedStorageImageTransitionUVE,
                    "replay: reopening the rendering instance after a storage-image layout "
                    "transition failed; the offending draw/dispatch is skipped and later "
                    "in-pass commands are ignored until the next Begin marker");
                passActiveForThisList = false;
                return false;
            }
        }
        return true;
    };

    const auto flushStateForActivePipelineUVE =
        [&impl, &currentTextureValues, &currentStorageValues, &ensureStorageImageGeneralUVE]() -> bool {
        const auto found = impl.pipelines.find(impl.activePipelineValue);
        if (found == impl.pipelines.end()) {
            return true; // no pipeline bound by this replay: nothing to flush
        }
        ImplUVE::PipelineRecordUVE& record = found->second;
        // M5a fix: storage-only (and sampler-only) pipelines DO carry shader-bound state — the
        // old M2c-era condition skipped their set binding entirely, which a storage-only compute
        // pipeline (the M5a palette writer) would have turned into silent fallback-buffer reads.
        if (record.uniformBlocks.empty() && !record.pushBlock.valid &&
            record.textureSlots.empty() && record.storageSlots.empty() &&
            record.samplerBindings.empty()) {
            return true; // pipeline carries no shader-bound state at all
        }
        static thread_local std::vector<std::uint32_t> dynamicOffsets; // replay thread only
        dynamicOffsets.clear();
        for (const UniformBlockRefUVE& block : record.uniformBlocks) {
            const std::uint64_t alignment = impl.frameUboAlignment;
            const std::uint64_t aligned =
                (impl.frameUboCursor + (alignment - 1U)) & ~(alignment - 1U);
            if (aligned + block.size > ImplUVE::kFrameUboCapacityUVE) {
                impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUboExhaustedUVE,
                    "replay: frame uniform ring (1 MiB) exhausted; the offending draw and later "
                    "uniform-carrying draws this frame are skipped - split draws across frames "
                    "or shrink uniform traffic (cursor resets every frame)");
                return false;
            }
            std::memcpy(static_cast<std::byte*>(impl.frameUboMapped) + aligned,
                        block.shadow.data(), block.size);
            dynamicOffsets.push_back(static_cast<std::uint32_t>(aligned));
            impl.frameUboCursor = aligned + block.size;
        }

        // Which descriptor set does this draw bind? Uniform-only pipelines: the static set
        // built at creation. Tuple-bearing pipelines (textures and/or M2f storage buffers):
        // the set cached for THIS bound-resource tuple (allocated + fully written on first
        // use; never rewritten in place — a recorded bind would otherwise retroactively
        // change earlier draws).
        VkDescriptorSet setToBind = record.descriptorSet;
        if (!record.textureSlots.empty() || !record.storageSlots.empty()) {
            static thread_local std::vector<const ImplUVE::TextureRecordUVE*> tupleRecords;
            tupleRecords.clear();
            std::string tupleKey;
            std::vector<std::uint32_t> tupleValues;
            tupleValues.reserve(record.textureSlots.size());
            tupleRecords.reserve(record.textureSlots.size());
            for (std::size_t slotIndex = 0; slotIndex < record.textureSlots.size(); ++slotIndex) {
                // M5b: storage-image slots resolve to their OWN deterministic sink (1x1 black,
                // write-throwaway) — imageStore into the shared white sampling fallback would
                // corrupt its "unbound slots sample white" invariant.
                const bool storageImageSlot = record.textureSlots[slotIndex].isStorageImage;
                std::uint32_t value = storageImageSlot ? impl.fallbackStorageImageValue
                                                       : impl.fallbackTextureValue;
                const auto bound = currentTextureValues.find(static_cast<std::uint32_t>(slotIndex));
                if (bound != currentTextureValues.end() &&
                    impl.textures.find(bound->second) != impl.textures.end()) {
                    value = bound->second; // destroyed-after-bind resolves to the fallback
                }
                ImplUVE::TextureRecordUVE& slotRecord = impl.textures.at(value);
                if (slotRecord.desc.format == TextureFormatUVE::Depth32Float &&
                    (storageImageSlot || !impl.useDynamicRendering)) {
                    if (storageImageSlot) {
                        // M5b scope: depth images are never storage-bound (imageStore into a
                        // depth attachment needs a layout/aspect story this slice refuses to
                        // fake) — writes go to the black sink, deterministically.
                        impl.WarnOnceUVE(
                            VulkanRenderDeviceUVE::ImplUVE::kWarnedStorageImageDepthUVE,
                            "BindTextureUVE: a Depth32Float texture was bound to a storage-image "
                            "slot; depth images are never storage-bound by this RHI — the 1x1 "
                            "black storage sink receives the writes instead");
                        value = impl.fallbackStorageImageValue;
                    } else {
                        // M2e boundary, classic arm only: depth textures are sampleable for real
                        // on the dynamic-rendering path. On pre-1.3 devices no offscreen pass can
                        // ever run (bit13 degrades them), so no real depth content can exist
                        // there — the 1x1-white fallback keeps the classic contract honest
                        // instead of faking a sample of an unrenderable image.
                        impl.WarnOnceUVE(
                            VulkanRenderDeviceUVE::ImplUVE::kWarnedDepthTextureSampledUVE,
                            "BindTextureUVE: a Depth32Float texture was bound on a device without "
                            "core dynamic rendering; depth-texture sampling needs the M2e dynamic "
                            "arm — the 1x1 white fallback is sampled instead");
                        value = impl.fallbackTextureValue;
                    }
                } else if (impl.framePassState.passOpen && !impl.framePassState.openPassIsSwapchain &&
                           (&slotRecord == impl.framePassState.openOffscreenDepthRecord ||
                            slotRecord.image == impl.framePassState.openOffscreenColorImage)) {
                    // M2e feedback guard: sampling OR storage-writing a texture WHILE it is
                    // attached to the open pass is the Vulkan-illegal/GL-undefined feedback
                    // loop. Keep the frame deterministic: use the kind-matched fallback once,
                    // warn loudly — never corrupt.
                    impl.WarnOnceUVE(
                        VulkanRenderDeviceUVE::ImplUVE::kWarnedOffscreenFeedbackUVE,
                        "BindTextureUVE: texture is attached to the currently-open offscreen "
                        "pass (feedback loop, undefined in GL as well) — the deterministic "
                        "fallback (1x1 white for sampling, black sink for storage writes) is "
                        "used for this draw");
                    value = storageImageSlot ? impl.fallbackStorageImageValue
                                             : impl.fallbackTextureValue;
                }
                tupleValues.push_back(value);
                tupleRecords.push_back(&impl.textures.at(value));
                tupleKey.append(std::to_string(value)).push_back('#');
            }
            // M2f: storage-buffer section of the tuple key ('s'-prefixed so the concatenated
            // key stays unambiguous next to the texture section). 0 is never a live handle
            // (nextHandleValue starts at 1): an unbound — or destroyed-after-bind — slot
            // resolves to the deterministic zero-filled fallback buffer at write time, the
            // buffer analogue of the white-fallback texture (the replay handler already
            // rejected unknown handles and non-Storage usage with a warn-once, so anything
            // still in currentStorageValues is a live Storage buffer).
            static thread_local std::vector<const ImplUVE::BufferRecordUVE*> tupleStorageRecords;
            tupleStorageRecords.clear();
            std::vector<std::uint32_t> tupleStorageValues;
            tupleStorageValues.reserve(record.storageSlots.size());
            tupleStorageRecords.reserve(record.storageSlots.size());
            for (std::size_t slotIndex = 0; slotIndex < record.storageSlots.size(); ++slotIndex) {
                std::uint32_t value = 0U;
                const auto bound = currentStorageValues.find(static_cast<std::uint32_t>(slotIndex));
                if (bound != currentStorageValues.end() &&
                    impl.buffers.find(bound->second) != impl.buffers.end()) {
                    value = bound->second;
                }
                tupleStorageValues.push_back(value);
                tupleStorageRecords.push_back(value != 0U ? &impl.buffers.at(value) : nullptr);
                tupleKey.push_back('s');
                tupleKey.append(std::to_string(value));
                tupleKey.push_back('#');
            }
            const auto cached = record.cachedTextureSets.find(tupleKey);
            if (cached != record.cachedTextureSets.end()) {
                setToBind = cached->second;
            } else {
                // M5b: first use of this tuple — every storage-image texture must be GENERAL
                // before its descriptor is baked. The transition records outside any rendering
                // instance (the helper closes/reopens an open pass) and invalidates stale
                // cached sets; on failure this flush skips the draw/dispatch loudly. (A cache
                // HIT never needs this: pinning is permanent and happened at the set's own
                // first-write, and any set predating the pin was invalidated then.)
                for (std::size_t slotIndex = 0; slotIndex < record.textureSlots.size();
                     ++slotIndex) {
                    if (record.textureSlots[slotIndex].isStorageImage &&
                        !ensureStorageImageGeneralUVE(tupleValues[slotIndex])) {
                        return false;
                    }
                }
                VkDescriptorSetAllocateInfo allocateInfo{};
                allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocateInfo.descriptorPool = impl.descriptorPool;
                allocateInfo.descriptorSetCount = 1U;
                allocateInfo.pSetLayouts = &record.descriptorSetLayout;
                VkDescriptorSet freshSet = VK_NULL_HANDLE;
                if (impl.vk.vkAllocateDescriptorSets(impl.device, &allocateInfo, &freshSet) != VK_SUCCESS ||
                    freshSet == VK_NULL_HANDLE) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedTextureSetExhaustedUVE,
                        "replay: descriptor pool exhausted while caching a texture binding set; "
                        "the offending draw is skipped (raise the M2c pool capacities if a real "
                        "workload legitimately exceeds them)");
                    return false;
                }
                std::vector<VkWriteDescriptorSet> writes;
                std::vector<VkDescriptorBufferInfo> bufferInfos(record.uniformBlocks.size());
                std::vector<VkDescriptorImageInfo> imageInfos(record.textureSlots.size());
                std::vector<VkDescriptorImageInfo> samplerInfos(record.samplerBindings.size());
                std::vector<VkDescriptorBufferInfo> storageInfos(record.storageSlots.size());
                writes.reserve(record.uniformBlocks.size() + record.textureSlots.size() +
                               record.samplerBindings.size() + record.storageSlots.size());
                for (std::size_t index = 0; index < record.uniformBlocks.size(); ++index) {
                    VkDescriptorBufferInfo& bufferInfo = bufferInfos[index];
                    bufferInfo = {};
                    bufferInfo.buffer = impl.frameUbo;
                    bufferInfo.offset = 0U;
                    bufferInfo.range = record.uniformBlocks[index].size;
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = freshSet;
                    write.dstBinding = record.uniformBlocks[index].binding;
                    write.descriptorCount = 1U;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    write.pBufferInfo = &bufferInfo;
                    writes.push_back(write);
                }
                for (std::size_t index = 0; index < record.textureSlots.size(); ++index) {
                    VkDescriptorImageInfo& imageInfo = imageInfos[index];
                    imageInfo = {};
                    const bool storageImageSlot = record.textureSlots[index].isStorageImage;
                    // M2f: the separate SAMPLED_IMAGE form ignores the sampler member (the
                    // standalone SAMPLER bindings below carry the fixed device sampler).
                    // M5b: STORAGE_IMAGE ignores it too.
                    imageInfo.sampler =
                        (storageImageSlot || record.textureSlots[index].separateSampler)
                            ? VK_NULL_HANDLE
                            : tupleRecords[index]->sampler;
                    // M5b fix: storage descriptors take the rgba8-qualified alias view when
                    // the swapchain-format policy aliased the image into the B,G,R,A or sRGB
                    // family (the sampled view's format has no matching SPIR-V storage
                    // qualifier); sampled descriptors keep the sampling view.
                    imageInfo.imageView =
                        (storageImageSlot && tupleRecords[index]->storageView != VK_NULL_HANDLE)
                            ? tupleRecords[index]->storageView
                            : tupleRecords[index]->view;
                    // M5b: storage descriptors always say GENERAL (guaranteed by the
                    // transitions above). Sampled descriptors must match the texture's ACTUAL
                    // layout — a storage-pinned texture sampled through another pipeline is
                    // GENERAL now (its stale cached sets were invalidated at pin time and
                    // rewrite through this same branch).
                    imageInfo.imageLayout = storageImageSlot
                        ? VK_IMAGE_LAYOUT_GENERAL
                        : (tupleRecords[index]->pinnedGeneral
                               ? VK_IMAGE_LAYOUT_GENERAL
                               : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = freshSet;
                    write.dstBinding = record.textureSlots[index].binding;
                    write.descriptorCount = 1U;
                    write.descriptorType = storageImageSlot
                        ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                        : (record.textureSlots[index].separateSampler
                               ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                               : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                    write.pImageInfo = &imageInfo;
                    writes.push_back(write);
                }
                for (std::size_t index = 0; index < record.samplerBindings.size(); ++index) {
                    VkDescriptorImageInfo& samplerInfo = samplerInfos[index];
                    samplerInfo = {};
                    samplerInfo.sampler = impl.fixedSampler;
                    samplerInfo.imageView = VK_NULL_HANDLE; // ignored for SAMPLER-type writes
                    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = freshSet;
                    write.dstBinding = record.samplerBindings[index].binding;
                    write.descriptorCount = 1U;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    write.pImageInfo = &samplerInfo;
                    writes.push_back(write);
                }
                for (std::size_t index = 0; index < record.storageSlots.size(); ++index) {
                    VkDescriptorBufferInfo& storageInfo = storageInfos[index];
                    storageInfo = {};
                    storageInfo.buffer = tupleStorageRecords[index] != nullptr
                        ? tupleStorageRecords[index]->buffer
                        : impl.fallbackStorageBuffer;
                    storageInfo.offset = 0U;
                    storageInfo.range = tupleStorageRecords[index] != nullptr
                        ? tupleStorageRecords[index]->sizeBytes
                        : VulkanRenderDeviceUVE::ImplUVE::kFallbackStorageBytesUVE;
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = freshSet;
                    write.dstBinding = record.storageSlots[index].binding;
                    write.descriptorCount = 1U;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &storageInfo;
                    writes.push_back(write);
                }
                impl.vk.vkUpdateDescriptorSets(impl.device, static_cast<std::uint32_t>(writes.size()),
                                               writes.data(), 0U, nullptr);
                record.cachedTextureSets.emplace(tupleKey, freshSet);
                record.cachedTextureTextures.emplace(tupleKey, std::move(tupleValues));
                record.cachedStorageBuffers.emplace(tupleKey, std::move(tupleStorageValues));
                setToBind = freshSet;
            }
        }
        if (setToBind != VK_NULL_HANDLE) {
            // M5a: descriptor sets bind against the pipeline's OWN bind point — a compute
            // pipeline's set is invisible to VK_PIPELINE_BIND_POINT_GRAPHICS and vice versa.
            impl.vk.vkCmdBindDescriptorSets(impl.commandBuffer,
                                            record.isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                             : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            record.layout, 0U, 1U, &setToBind,
                                            static_cast<std::uint32_t>(dynamicOffsets.size()),
                                            dynamicOffsets.data());
        }
        if (record.pushBlock.valid) {
            impl.vk.vkCmdPushConstants(impl.commandBuffer, record.layout,
                                       record.pushBlock.stageFlags, record.pushBlock.offset,
                                       record.pushBlock.size, record.pushBlock.shadow.data());
        }
        record.uniformsDirty = false;
        return true;
    };

    // Resolve a named member across the pipeline's UBO blocks and push-constant block, then
    // bounds-and-type-checked-write into the owner shadow. Misses are one-shot warnings, not
    // crashes: the submit-side recording API passes names through opaquely and a typo'd name
    // must degrade to a logged no-op exactly the way unknown GL uniform locations do.
    const auto writeUniformUVE = [&impl](const std::string& name,
                                         const ShaderDataTypeUVE writtenType,
                                         const void* bytes, const std::size_t byteCount,
                                         const bool acceptIntBoolCross) {
        const auto found = impl.pipelines.find(impl.activePipelineValue);
        if (found == impl.pipelines.end()) {
            impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUniformNoopUVE,
                "SetUniform* replay: no pipeline is bound at this point in the submission; the "
                "uniform write is dropped (bind the pipeline before setting its uniforms)");
            return;
        }
        ImplUVE::PipelineRecordUVE& record = found->second;
        UniformMemberRefUVE* target = nullptr;
        std::vector<std::byte>* ownerShadow = nullptr;
        for (UniformBlockRefUVE& block : record.uniformBlocks) {
            for (UniformMemberRefUVE& member : block.members) {
                if (member.name == name) {
                    target = &member;
                    ownerShadow = &block.shadow;
                    break;
                }
            }
            if (target != nullptr) {
                break;
            }
        }
        if (target == nullptr) {
            for (UniformMemberRefUVE& member : record.pushBlock.members) {
                if (member.name == name) {
                    target = &member;
                    ownerShadow = &record.pushBlock.shadow;
                    break;
                }
            }
        }
        if (target == nullptr) {
            impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUniformNameMissUVE,
                "SetUniform* replay: a uniform name does not exist in the bound pipeline's "
                "reflected table (typo, or optimized out by the shader compiler); write dropped");
            return;
        }
        const bool isIntBoolPair =
            (target->type == ShaderDataTypeUVE::Int || target->type == ShaderDataTypeUVE::Bool) &&
            (writtenType == ShaderDataTypeUVE::Int || writtenType == ShaderDataTypeUVE::Bool);
        if (target->type != writtenType && !(acceptIntBoolCross && isIntBoolPair)) {
            impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUniformTypeMissUVE,
                "SetUniform* replay: value type does not match the reflected member type "
                "(e.g. float write onto a mat4); write dropped");
            return;
        }
        if (static_cast<std::uint64_t>(target->offset) + byteCount > ownerShadow->size() ||
            byteCount > target->size) {
            impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUniformTypeMissUVE,
                "SetUniform* replay: value would overflow its reflected member slot "
                "(suspicious layout); write dropped");
            return;
        }
        std::memcpy(ownerShadow->data() + target->offset, bytes, byteCount);
        record.uniformsDirty = true; // informational today; the draw path snapshots every draw
    };

    // Shared viewport-override application (GL top-origin rect -> Vulkan positive-Y-down);
    // `surfaceHeight` is the CURRENT pass's attachment height (swapchain or offscreen).
    const auto applyViewportOverrideUVE = [&impl](const ViewportRectUVE& rect,
                                                  const std::uint32_t surfaceHeight) {
        VkViewport viewport{};
        viewport.x = static_cast<float>(rect.x);
        viewport.y = static_cast<float>(static_cast<std::int64_t>(surfaceHeight) -
                                        static_cast<std::int64_t>(rect.y + rect.height));
        viewport.width = static_cast<float>(rect.width);
        viewport.height = static_cast<float>(rect.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        VkRect2D scissor{};
        scissor.offset = {static_cast<std::int32_t>(rect.x),
                          static_cast<std::int32_t>(viewport.y)};
        scissor.extent = {rect.width, rect.height};
        impl.vk.vkCmdSetViewport(impl.commandBuffer, 0U, 1U, &viewport);
        impl.vk.vkCmdSetScissor(impl.commandBuffer, 0U, 1U, &scissor);
    };

    // M5a: the outside-pass gate below relaxes while a compute pipeline is bound — the
    // storage-buffer binds and uniform writes feeding a dispatch are recorded OUTSIDE pass
    // markers (Vulkan forbids compute inside a rendering instance) and must survive replay.
    const auto activePipelineIsComputeUVE = [&impl]() {
        const auto found = impl.pipelines.find(impl.activePipelineValue);
        return found != impl.pipelines.end() && found->second.isCompute;
    };

    for (const RecordedCommandUVE& command : commands) {
        std::visit([&](const auto& op) {
            using OpT = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<OpT, BeginRenderPassCommandUVE>) {
                if (impl.useDynamicRendering) {
                    // ------------- M2d dynamic-rendering pass scheduler ------------------
                    // Pass markers lazily open the needed rendering instance: the default
                    // pass resumes the frame's swapchain instance, an offscreen pass borrows
                    // the command stream with its own layout transitions. Re-entering the
                    // ALREADY-open kind is a no-op marker (matches GL's coalesced FBO reuse);
                    // switching kinds closes the previous instance first.
                    const bool isOffscreen =
                        (op.desc.colorAttachment != kInvalidTextureHandleUVE ||
                         op.desc.depthAttachment != kInvalidTextureHandleUVE);
                    if (!isOffscreen) {
                        if (impl.framePassState.passOpen &&
                            !impl.framePassState.openPassIsSwapchain) {
                            impl.CloseCurrentPassDynamicUVE();
                        }
                        if (!impl.framePassState.passOpen) {
                            (void)impl.BeginSwapchainPassDynamicUVE();
                        }
                        // else: coalesced marker — already inside the swapchain instance.
                        passActiveForThisList = impl.framePassState.passOpen;
                        if (passActiveForThisList && op.desc.viewportOverride.has_value()) {
                            applyViewportOverrideUVE(*op.desc.viewportOverride,
                                                     impl.swapchainExtent.height);
                        }
                    } else {
                        // Offscreen target: validate the contract, then swap instances.
                        passActiveForThisList = false;
                        if (op.desc.colorAttachment == kInvalidTextureHandleUVE) {
                            impl.WarnOnceUVE(
                                VulkanRenderDeviceUVE::ImplUVE::kWarnedOffscreenPassUVE,
                                "BeginRenderPassUVE: depth-only offscreen passes are not "
                                "supported (attach an RGBA8 color texture, or omit the depth "
                                "attachment for a color pass; submission's draws are skipped)");
                        } else {
                            const auto foundColor =
                                impl.textures.find(op.desc.colorAttachment.value);
                            ImplUVE::TextureRecordUVE* depthRecord = nullptr;
                            bool degradeToScratchDepth = false;
                            bool skipPass = false;
                            if (foundColor == impl.textures.end()) {
                                impl.WarnOnceUVE(
                                    VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                                    "BeginRenderPassUVE: unknown color texture handle; "
                                    "submission's draws are skipped");
                                skipPass = true;
                            } else if (foundColor->second.vkFormat != impl.swapchainFormat) {
                                // RGBA16Float and RGBA8 textures created while the swapchain
                                // was not a 4x8 RGBA format cannot be attached (the device
                                // pipeline contract is the swapchain format).
                                impl.WarnOnceUVE(
                                    VulkanRenderDeviceUVE::ImplUVE::kWarnedOffscreenUnsupportedUVE,
                                    "BeginRenderPassUVE: this texture is not attachable on the "
                                    "device (only RGBA8Unorm textures in the swapchain's format "
                                    "can be offscreen color targets; submission's draws are "
                                    "skipped)");
                                skipPass = true;
                            }
                            if (!skipPass && op.desc.depthAttachment != kInvalidTextureHandleUVE) {
                                const auto foundDepth =
                                    impl.textures.find(op.desc.depthAttachment.value);
                                if (foundDepth == impl.textures.end()) {
                                    impl.WarnOnceUVE(
                                        VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                                        "BeginRenderPassUVE: unknown depth texture handle; "
                                        "the pass runs depth-tested on a per-extent internal "
                                        "target instead");
                                    degradeToScratchDepth = true;
                                } else if (impl.depthFormat != VK_FORMAT_D32_SFLOAT) {
                                    // Caller depth textures are D32-only; a D24 device cannot
                                    // attach them. Scratch depth keeps the pass (test results
                                    // are simply unobservable — documented boundary).
                                    impl.WarnOnceUVE(
                                        VulkanRenderDeviceUVE::ImplUVE::kWarnedOffscreenDepthIncompatibleUVE,
                                        "BeginRenderPassUVE: caller depth textures require a "
                                        "D32 depth device; this device is D24 — the pass runs "
                                        "depth-tested on an internal target instead");
                                    degradeToScratchDepth = true;
                                } else {
                                    depthRecord = &foundDepth->second;
                                }
                            }
                            if (!skipPass) {
                                const ImplUVE::TextureRecordUVE& color = foundColor->second;
                                // M2e: offscreen loadOps are REAL (Clear/Load/DontCare map to
                                // the attachment load ops; entry transitions preserve content).
                                if (impl.framePassState.passOpen) {
                                    impl.CloseCurrentPassDynamicUVE();
                                }
                                if (impl.BeginOffscreenPassDynamicUVE(
                                        foundColor->second,
                                        degradeToScratchDepth ? nullptr : depthRecord,
                                        VkExtent2D{color.desc.width, color.desc.height},
                                        op.desc.clearColor, op.desc.clearDepth,
                                        op.desc.colorLoadOp, op.desc.depthLoadOp)) {
                                    passActiveForThisList = true;
                                    if (op.desc.viewportOverride.has_value()) {
                                        applyViewportOverrideUVE(*op.desc.viewportOverride,
                                                                 color.desc.height);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // ------------- classic M1-M2c pass scheduling (unchanged) -------------
                    if (op.desc.colorAttachment != kInvalidTextureHandleUVE ||
                        op.desc.depthAttachment != kInvalidTextureHandleUVE) {
                        impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedOffscreenPassUVE,
                            "BeginRenderPassUVE targeting actual texture attachments requires "
                            "core 1.3 dynamic rendering, which this device/instance does not "
                            "offer; this submission's draws are skipped");
                    } else {
                        passActiveForThisList = true;
                        if (op.desc.viewportOverride.has_value()) {
                            // Vulkan's positive-Y-down viewport convention places the same
                            // GL-style top-origin rect by flipping from the framebuffer's
                            // bottom edge.
                            applyViewportOverrideUVE(*op.desc.viewportOverride,
                                                     impl.swapchainExtent.height);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<OpT, EndRenderPassCommandUVE>) {
                passActiveForThisList = false;
            } else if constexpr (std::is_same_v<OpT, DispatchCommandUVE>) {
                // ------------- M5a compute dispatch (outside pass markers) --------------
                if (!impl.useDynamicRendering) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedDispatchClassicUVE,
                        "compute requires core 1.3 dynamic rendering on this backend: the "
                        "classic arm wraps the whole replay in one native render pass instance "
                        "where vkCmdDispatch is illegal; compute commands are skipped");
                } else {
                    const auto found = impl.pipelines.find(impl.activePipelineValue);
                    if (found == impl.pipelines.end() || !found->second.isCompute) {
                        impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                            "submit replay: DispatchUVE without a compute pipeline bound; the "
                            "dispatch is skipped");
                    } else {
                        if (impl.framePassState.passOpen) {
                            // A lazily-open rendering instance is still active (EndRenderPass is
                            // only a marker) — dispatch inside one is a Vulkan error, so close it
                            // first; a later Begin reopens with LOAD, content preserved.
                            impl.CloseCurrentPassDynamicUVE();
                            passActiveForThisList = false;
                        }
                        // Conservative global barriers around the dispatch: earlier shader
                        // writes (graphics or compute) must be visible to the compute shader,
                        // and its writes must be visible to every later reader — fragment SSBO
                        // sampling in the same frame, later dispatches, and the queue-idle
                        // host readback path.
                        VkMemoryBarrier preBarrier{};
                        preBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        preBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        preBarrier.dstAccessMask =
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        impl.vk.vkCmdPipelineBarrier(
                            impl.commandBuffer,
                            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 1U, &preBarrier, 0U, nullptr,
                            0U, nullptr);
                        if (flushStateForActivePipelineUVE()) {
                            impl.vk.vkCmdDispatch(impl.commandBuffer, op.groupCountX, op.groupCountY,
                                                  op.groupCountZ);
                        }
                        VkMemoryBarrier postBarrier{};
                        postBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        postBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        // M5b widening: compute writes (imageStore into a texture that a LATER
                        // pass may ATTACH with Load semantics) must also be visible to the
                        // color/depth attachment stage, not only to shader readers.
                        postBarrier.dstAccessMask =
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        impl.vk.vkCmdPipelineBarrier(
                            impl.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            0U, 1U, &postBarrier, 0U, nullptr, 0U, nullptr);
                    }
                }
            } else if constexpr (std::is_same_v<OpT, BindPipelineCommandUVE>) {
                // M5a: handled before the inside-pass gate — compute pipelines bind OUTSIDE
                // pass markers (a COMPUTE bind inside a rendering instance is illegal), while
                // graphics binds keep the M2a rule (ignored outside an accepted pass).
                const auto found = impl.pipelines.find(op.pipeline.value);
                if (found == impl.pipelines.end()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                        "submit replay: unknown pipeline handle; draws bound to it are skipped");
                    impl.activePipelineValue = 0U;
                } else if (found->second.isCompute) {
                    if (!impl.useDynamicRendering) {
                        impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedDispatchClassicUVE,
                            "compute requires core 1.3 dynamic rendering on this backend: the "
                            "classic arm wraps the whole replay in one native render pass instance "
                            "where vkCmdDispatch is illegal; compute commands are skipped");
                    } else {
                        if (impl.framePassState.passOpen) {
                            impl.CloseCurrentPassDynamicUVE(); // COMPUTE bind inside an open
                            passActiveForThisList = false;     // instance would be illegal
                        }
                        impl.vk.vkCmdBindPipeline(impl.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                  found->second.pipeline);
                        impl.activePipelineValue = op.pipeline.value;
                    }
                } else if (passActiveForThisList) {
                    impl.vk.vkCmdBindPipeline(impl.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              found->second.pipeline);
                    impl.activePipelineValue = op.pipeline.value;
                }
                // else: graphics bind outside a pass — ignored (M2a rule, unchanged).
            } else if (!passActiveForThisList && !activePipelineIsComputeUVE()) {
                // Anything outside an accepted pass is ignored: the same rule GL's world has,
                // where draws outside a pass bind land nowhere meaningful. M5a exception: while
                // a COMPUTE pipeline is bound, outside-pass storage binds and uniform writes
                // are real — they feed the next dispatch.
            } else if constexpr (std::is_same_v<OpT, BindVertexBufferCommandUVE>) {
                const auto found = impl.buffers.find(op.buffer.value);
                if (found == impl.buffers.end()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                        "submit replay: unknown vertex-buffer handle; subsequent draws are skipped");
                } else {
                    const VkDeviceSize offset = 0U;
                    impl.vk.vkCmdBindVertexBuffers(impl.commandBuffer, op.slot, 1U,
                                                   &found->second.buffer, &offset);
                }
            } else if constexpr (std::is_same_v<OpT, BindIndexBufferCommandUVE>) {
                const auto found = impl.buffers.find(op.buffer.value);
                if (found == impl.buffers.end()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                        "submit replay: unknown index-buffer handle; subsequent draws are skipped");
                } else {
                    // 32-bit indices, matching GlRenderDeviceUVE's GL_UNSIGNED_INT policy.
                    impl.vk.vkCmdBindIndexBuffer(impl.commandBuffer, found->second.buffer, 0U,
                                                 VK_INDEX_TYPE_UINT32);
                }
            } else if constexpr (std::is_same_v<OpT, BindTextureCommandUVE>) {
                if (impl.textures.find(op.texture.value) == impl.textures.end()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownTextureUVE,
                        "submit replay: unknown texture handle; that BindTextureUVE call is "
                        "dropped (affected draws sample the fallback texture instead)");
                } else {
                    currentTextureValues[op.slot] = op.texture.value;
                    const auto activePipeline = impl.pipelines.find(impl.activePipelineValue);
                    if (activePipeline != impl.pipelines.end() &&
                        op.slot >= activePipeline->second.textureSlots.size()) {
                        impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedTextureSlotOobUVE,
                            "submit replay: BindTextureUVE slot exceeds the bound pipeline's "
                            "reflected sampler count; the bind is recorded (GL texture-unit "
                            "semantics) but no sampler reads it in this pipeline");
                    }
                }
            } else if constexpr (std::is_same_v<OpT, BindUniformBufferCommandUVE>) {
                impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedTextureNoopUVE,
                    "submit replay: BindUniformBufferUVE (external uniform BUFFERS bound by "
                    "handle) stays a no-op in M2c: uniforms flow through SetUniform* plus the "
                    "reflected frame ring; inside-pass UBO rebinding lands with a later slice");
            } else if constexpr (std::is_same_v<OpT, BindStorageBufferCommandUVE>) {
                const auto foundBuffer = impl.buffers.find(op.buffer.value);
                if (foundBuffer == impl.buffers.end() ||
                    !IsStorageBindableUsageUVE(foundBuffer->second.usage)) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownStorageUVE,
                        "submit replay: BindStorageBufferUVE referenced an unknown handle or a "
                        "buffer not created with Storage usage; that bind is dropped (affected "
                        "draws read the deterministic zero-filled fallback buffer instead)");
                } else {
                    currentStorageValues[op.slot] = op.buffer.value;
                    const auto activePipeline = impl.pipelines.find(impl.activePipelineValue);
                    if (activePipeline != impl.pipelines.end() &&
                        op.slot >= activePipeline->second.storageSlots.size()) {
                        impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedStorageSlotOobUVE,
                            "submit replay: BindStorageBufferUVE slot exceeds the bound "
                            "pipeline's reflected storage-buffer count; the bind is recorded "
                            "(GL binding-point semantics) but no SSBO reads it in this pipeline");
                    }
                }
            } else if constexpr (std::is_same_v<OpT, SetUniformFloatCommandUVE>) {
                writeUniformUVE(op.name, ShaderDataTypeUVE::Float, &op.value, sizeof(op.value), false);
            } else if constexpr (std::is_same_v<OpT, SetUniformIntCommandUVE>) {
                writeUniformUVE(op.name, ShaderDataTypeUVE::Int, &op.value, sizeof(op.value), true);
            } else if constexpr (std::is_same_v<OpT, SetUniformBoolCommandUVE>) {
                // SPIR-V bool block members occupy a 4-byte slot (0/1) in every standard layout.
                const std::int32_t packed = op.value ? 1 : 0;
                writeUniformUVE(op.name, ShaderDataTypeUVE::Bool, &packed, sizeof(packed), true);
            } else if constexpr (std::is_same_v<OpT, SetUniformVector3CommandUVE>) {
                // std140/std430 vec3: 12 meaningful bytes inside a 16-byte slot; the reflected
                // member size is 16 but sits at a padded offset, so write just the three lanes.
                const float lanes[3] = {op.value.x, op.value.y, op.value.z};
                writeUniformUVE(op.name, ShaderDataTypeUVE::Vec3, lanes, sizeof(lanes), false);
            } else if constexpr (std::is_same_v<OpT, SetUniformMatrix4x4CommandUVE>) {
                // Std140 mat4 = four vec4 columns stride-16 == 64 dense bytes, the same dense
                // float[16] packing the RHI's matrix type stores. Column-major either way.
                writeUniformUVE(op.name, ShaderDataTypeUVE::Mat4, op.value.m,
                                sizeof(op.value.m), false);
            } else if constexpr (std::is_same_v<OpT, DrawCommandUVE>) {
                if (activePipelineIsComputeUVE()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedDrawWithComputeUVE,
                        "submit replay: DrawUVE/DrawIndexedUVE reached replay with a COMPUTE "
                        "pipeline bound; the draw is skipped (bind a graphics pipeline first)");
                } else if (flushStateForActivePipelineUVE()) {
                    impl.vk.vkCmdDraw(impl.commandBuffer, op.vertexCount, op.instanceCount, 0U, 0U);
                }
            } else if constexpr (std::is_same_v<OpT, DrawIndexedCommandUVE>) {
                if (activePipelineIsComputeUVE()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedDrawWithComputeUVE,
                        "submit replay: DrawUVE/DrawIndexedUVE reached replay with a COMPUTE "
                        "pipeline bound; the draw is skipped (bind a graphics pipeline first)");
                } else if (flushStateForActivePipelineUVE()) {
                    impl.vk.vkCmdDrawIndexed(impl.commandBuffer, op.indexCount, op.instanceCount,
                                             0U, 0, 0U);
                }
            } else if constexpr (std::is_same_v<OpT, DrawIndexedIndirectCommandRecordUVE>) {
                // CS7. Validation happens HERE rather than at record time because this backend
                // has no record-time resource state at all - the recorder is a pure op list.
                const auto foundIndirect = impl.buffers.find(op.buffer.value);
                if (activePipelineIsComputeUVE()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedDrawWithComputeUVE,
                        "submit replay: DrawIndexedIndirectUVE reached replay with a COMPUTE "
                        "pipeline bound; the draw is skipped (bind a graphics pipeline first)");
                } else if (foundIndirect == impl.buffers.end() ||
                           foundIndirect->second.usage != BufferUsageUVE::IndirectStorage) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedIndirectBufferUVE,
                        "submit replay: DrawIndexedIndirectUVE needs a live IndirectStorage "
                        "buffer; the draw is skipped");
                } else if (op.offsetBytes > foundIndirect->second.sizeBytes ||
                           foundIndirect->second.sizeBytes - op.offsetBytes <
                               sizeof(DrawIndexedIndirectCommandUVE)) {
                    // Reading parameters off the end would draw with garbage counts - and with
                    // indirect draw nothing on the CPU ever sees those numbers, so this must be
                    // caught here rather than left to produce inexplicable geometry.
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedIndirectBufferUVE,
                        "submit replay: DrawIndexedIndirectUVE offset leaves no whole command "
                        "inside the buffer; the draw is skipped");
                } else if (flushStateForActivePipelineUVE()) {
                    // One command, so no stride is consulted; passing the struct's size keeps the
                    // call honest if a future slice raises drawCount.
                    impl.vk.vkCmdDrawIndexedIndirect(
                        impl.commandBuffer, foundIndirect->second.buffer,
                        static_cast<VkDeviceSize>(op.offsetBytes), 1U,
                        static_cast<std::uint32_t>(sizeof(DrawIndexedIndirectCommandUVE)));
                }
            }
        }, command);
    }
}

void VulkanRenderDeviceUVE::PresentUVE() {
    ImplUVE& impl = *m_impl;
    if (!impl.usable) {
        return;
    }

    // Single frame in flight: wait for the previous frame's fence, but NEVER reset it yet —
    // every bail-out below (minimized window, acquire failure, mid-frame error) must leave the
    // fence still signaled by the previous frame's submit so the next PresentUVE()'s wait also
    // completes. Only the path that truly reaches vkQueueSubmit resets it first. (Simplicity
    // over pipelining is the documented M1 trade-off; the fence starts signaled at creation so
    // the very first frame also passes.)
    (void)impl.vk.vkWaitForFences(impl.device, 1U, &impl.inFlightFence, VK_TRUE, UINT64_MAX);
    // The fence proves the GPU finished the previous frame's reads of the uniform ring;
    // ONLY now is rewinding the bump cursor legal (M2b ring contract at the field comment).
    impl.frameUboCursor = 0U;
    // M5b: same fence logic frees the mid-replay-invalidated descriptor sets — the submission
    // whose recorded binds referenced them is provably complete, so vkFreeDescriptorSets is
    // safe now (freeing while a pending command buffer binds them would be undefined).
    if (!impl.deferredDescriptorSetFreesUVE.empty()) {
        impl.vk.vkFreeDescriptorSets(impl.device, impl.descriptorPool,
                                     static_cast<std::uint32_t>(impl.deferredDescriptorSetFreesUVE.size()),
                                     impl.deferredDescriptorSetFreesUVE.data());
        impl.deferredDescriptorSetFreesUVE.clear();
    }

    // M4: drain the cross-thread submission FIFO under the lock. Worker threads may have
    // created/recorded/submitted command buffers while this frame was being assembled;
    // replay below is strictly main-thread GPU work operating on the local snapshot. A
    // submit racing THIS PresentUVE lands in the next frame's FIFO — honest queue order.
    std::deque<std::vector<RecordedCommandUVE>> frameSubmissions;
    {
        const std::lock_guard<std::mutex> submissionLock(impl.submissionMutex);
        frameSubmissions.swap(impl.frameSubmissions);
    }

    const auto dropSubmissionsUVE = [&impl, &frameSubmissions](const char* /*why*/) {
        if (!frameSubmissions.empty()) {
            impl.WarnOnceUVE(ImplUVE::kWarnedDroppedSubmissionsUVE,
                "PresentUVE: recorded command buffers dropped because the frame was skipped "
                "(minimized/resizing/acquire failure); submissions carry per-frame content");
            frameSubmissions.clear();
        }
    };

    // Minimized window: skip the frame silently — mirrors the GL device contract that size
    // changes come from the WindowResizedEventUVE poll and zero-size means "not drawable".
    std::uint32_t framebufferWidth = 0;
    std::uint32_t framebufferHeight = 0;
    if (impl.headless) {
        framebufferWidth = ImplUVE::kHeadlessFramebufferWidthUVE;
        framebufferHeight = ImplUVE::kHeadlessFramebufferHeightUVE;
    } else {
        impl.bridge->GetVulkanFramebufferSizeUVE(framebufferWidth, framebufferHeight);
    }
    if (framebufferWidth == 0U || framebufferHeight == 0U) {
        dropSubmissionsUVE("");
        return;
    }
    if (framebufferWidth != impl.swapchainExtent.width ||
        framebufferHeight != impl.swapchainExtent.height) {
        // Same pattern as GlRenderDeviceUVE's on-resize FBO rebuild: drain, destroy, recreate.
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false; // logged inside; device falls back to inert for later frames
            return;
        }
    }

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = impl.vk.vkAcquireNextImageKHR(
        impl.device, impl.swapchain, UINT64_MAX, impl.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Driver-side repaint judgment (e.g. display mode change): rebuild now, retry next frame.
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false;
        }
        dropSubmissionsUVE("");
        return; // fence left signaled — see the invariant at the top of this method
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkAcquireNextImageKHR failed (result {})",
                    static_cast<int>(acquireResult));
        dropSubmissionsUVE("");
        return;
    }

    // Committing to a submit this frame: reset the fence to unsignaled for the queue to signal.
    (void)impl.vk.vkResetFences(impl.device, 1U, &impl.inFlightFence);
    (void)impl.vk.vkResetCommandBuffer(impl.commandBuffer, 0U); // pool has RESET_COMMAND_BUFFER_BIT

    // Record this frame's clear: a one-shot command buffer bound to the acquired image.
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (impl.vk.vkBeginCommandBuffer(impl.commandBuffer, &beginInfo) != VK_SUCCESS) {
        // Fence is already reset for this frame and no submit will re-signal it; the device
        // goes inert here so no later frame deadlocks waiting for that fence (recovering is an
        // M2 concern — a record-time failure indicates driver trouble, not transient state).
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkBeginCommandBuffer failed; device going inert");
        impl.usable = false;
        return;
    }
    // Clear value: the M1 engineering clear stays the DEFAULT — but the first submitted pass
    // that targets the default framebuffer with LoadOpUVE::Clear supplies the frame's clear
    // color, exactly as GL's world treats the caller's own BeginRenderPassUVE clear request on
    // the back buffer. Scan (never consume) the submission FIFO for that first clear request.
    VkClearValue clearValues[2]{};
    clearValues[0].color.float32[0] = kBootstrapClearRedUVE;
    clearValues[0].color.float32[1] = kBootstrapClearGreenUVE;
    clearValues[0].color.float32[2] = kBootstrapClearBlueUVE;
    clearValues[0].color.float32[3] = kBootstrapClearAlphaUVE;
    clearValues[1].depthStencil = {1.0F, 0U}; // far plane, matching the GL depth-clear default
    for (const std::vector<RecordedCommandUVE>& submission : frameSubmissions) {
        bool clearPicked = false;
        for (const RecordedCommandUVE& command : submission) {
            if (const auto* begin = std::get_if<BeginRenderPassCommandUVE>(&command)) {
                if (begin->desc.colorAttachment == kInvalidTextureHandleUVE &&
                    begin->desc.depthAttachment == kInvalidTextureHandleUVE) {
                    if (begin->desc.colorLoadOp != LoadOpUVE::Clear ||
                        begin->desc.depthLoadOp != LoadOpUVE::Clear) {
                        impl.WarnOnceUVE(ImplUVE::kWarnedLoadOpUnhonoredUVE,
                            "BeginRenderPassUVE requested Load/DontCare, but the swapchain "
                            "render pass bakes a full-frame clear (later slices make loadOps "
                            "real); the frame still starts from the pass's clear values");
                    }
                    const std::array<float, 4U>& requested = begin->desc.clearColor;
                    for (std::size_t channel = 0; channel < 4U; ++channel) {
                        clearValues[0].color.float32[channel] = requested[channel];
                    }
                    clearValues[1].depthStencil = {begin->desc.clearDepth, 0U};
                    clearPicked = true;
                }
                break; // only the first pass of the earliest submission decides
            }
        }
        if (clearPicked) {
            break;
        }
    }

    if (!impl.useDynamicRendering) {
        // Classic M1-M2c frame shape, unchanged: ONE swapchain render pass is opened upfront
        // and every submission replays inside it.
        VkRenderPassBeginInfo renderPassBegin{};
        renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBegin.renderPass = impl.renderPass;
        renderPassBegin.framebuffer = impl.framebuffers[imageIndex];
        renderPassBegin.renderArea.offset = {0, 0};
        renderPassBegin.renderArea.extent = impl.swapchainExtent;
        renderPassBegin.clearValueCount = 2U; // one per attachment (color + depth)
        renderPassBegin.pClearValues = clearValues;
        impl.vk.vkCmdBeginRenderPass(impl.commandBuffer, &renderPassBegin,
                                     VK_SUBPASS_CONTENTS_INLINE);

        // Dynamic viewport/scissor cover the whole surface by default; per-pass
        // viewportOverride replays apply their own rects from here on.
        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(impl.swapchainExtent.width);
        viewport.height = static_cast<float>(impl.swapchainExtent.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = impl.swapchainExtent;
        impl.vk.vkCmdSetViewport(impl.commandBuffer, 0U, 1U, &viewport);
        impl.vk.vkCmdSetScissor(impl.commandBuffer, 0U, 1U, &scissor);
    } else {
        // M2d dynamic-rendering frame: NO pass is opened here. The replay opens the correct
        // rendering instance lazily at each pass marker (they know the chosen clear values
        // through framePassState), the tail below closes it and transitions to PRESENT.
        ImplUVE::FramePassStateUVE& state = impl.framePassState;
        state.clearValues[0] = clearValues[0];
        state.clearValues[1] = clearValues[1];
        state.swapchainImageIndex = imageIndex;
        state.barrierDoneForSwapchain = false;
        state.swapchainPassBegunThisFrame = false;
        state.passOpen = false;
        state.openPassIsSwapchain = true;
        state.openOffscreenColorImage = VK_NULL_HANDLE;
        state.openOffscreenColorRecord = nullptr;
    }

    // Replay every submitted recorded command buffer in submission order, inside THIS pass —
    // the M2a integration contract documented at ReplayRecordedCommandsUVE. Weakest-design
    // point honored deliberately: IRenderDeviceUVE's submission model assumes one pass chain
    // per frame against the back buffer (it matches how GlRenderDeviceUVE's FBO-0 world
    // works today); anything outside that shape is warned-about once, never silently mangled.
    impl.activePipelineValue = 0U; // pipeline binding is fresh command-buffer state each frame
    for (const std::vector<RecordedCommandUVE>& submission : frameSubmissions) {
        ReplayRecordedCommandsUVE(submission);
    }
    frameSubmissions.clear(); // consumed — the local snapshot dies with this frame (M4: the
                              // shared FIFO was already drained under the submission lock)

    if (!impl.useDynamicRendering) {
        impl.vk.vkCmdEndRenderPass(impl.commandBuffer);
    } else {
        // Dynamic tail: close whatever rendering instance the replay left open; guarantee
        // the acquired image actually got an attachment instance this frame (GL parity: the
        // swapchain is ALWAYS cleared/ready-to-present, even when no default pass posted —
        // e.g. offscreen-only frames or empty submission streams).
        impl.CloseCurrentPassDynamicUVE();
        if (!impl.framePassState.swapchainPassBegunThisFrame) {
            (void)impl.BeginSwapchainPassDynamicUVE(); // runs the per-frame entry barriers
            impl.CloseCurrentPassDynamicUVE();
        }
        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0U;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = impl.swapchainImages[imageIndex];
        toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        impl.vk.vkCmdPipelineBarrier(impl.commandBuffer,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, nullptr, 0U,
                                     nullptr, 1U, &toPresent);
    }
    if (impl.vk.vkEndCommandBuffer(impl.commandBuffer) != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkEndCommandBuffer failed; device going inert");
        impl.usable = false;
        return;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1U;
    submitInfo.pWaitSemaphores = &impl.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &impl.commandBuffer;
    submitInfo.signalSemaphoreCount = 1U;
    submitInfo.pSignalSemaphores = &impl.renderFinishedSemaphore;
    if (impl.vk.vkQueueSubmit(impl.presentQueue, 1U, &submitInfo, impl.inFlightFence) != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkQueueSubmit failed; device going inert");
        impl.usable = false;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1U;
    presentInfo.pWaitSemaphores = &impl.renderFinishedSemaphore;
    presentInfo.swapchainCount = 1U;
    presentInfo.pSwapchains = &impl.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = impl.vk.vkQueuePresentKHR(impl.presentQueue, &presentInfo);
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) {
        impl.lastPresentedImageIndex = imageIndex;
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // Resize visible to the driver after acquire: rebuild now so next frame uses a valid
        // swapchain (the just-completed present attempt was still in sync — the fence stands).
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false;
        }
    } else if (presentResult != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkQueuePresentKHR failed (result {})",
                    static_cast<int>(presentResult));
    }
}

bool VulkanRenderDeviceUVE::IsUsableUVE() const noexcept {
    return m_impl->usable;
}

bool VulkanRenderDeviceUVE::ReadbackLatestPresentedImageUVE(std::span<std::byte> outRGBA8,
                                                            std::uint32_t& outWidth,
                                                            std::uint32_t& outHeight) {
    ImplUVE& impl = *m_impl;
    outWidth = 0U;
    outHeight = 0U;

    const auto fail = [](const char* reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::ReadbackLatestPresentedImageUVE: {}", reason);
        return false;
    };

    if (!impl.usable) {
        return fail("device is not usable");
    }
    if (impl.lastPresentedImageIndex == UINT32_MAX ||
        impl.lastPresentedImageIndex >= impl.swapchainImages.size()) {
        return fail("no frame has been presented yet");
    }
    const std::size_t requiredBytes =
        static_cast<std::size_t>(impl.swapchainExtent.width) * impl.swapchainExtent.height * 4U;
    if (outRGBA8.size() != requiredBytes) {
        // Fill the extent out-parameters even on this documented failure: drivers that lock the
        // surface extent to their own pick (SwiftShader's headless surface, for one) mean the
        // real size is only knowable here, and the two-call pattern (query with an empty span,
        // resize, call again) must stay possible for cold-path tooling.
        outWidth = impl.swapchainExtent.width;
        outHeight = impl.swapchainExtent.height;
        return fail("output span must be exactly width*height*4 bytes of the framebuffer extent");
    }
    outWidth = impl.swapchainExtent.width;
    outHeight = impl.swapchainExtent.height;

    const VkImage sourceImage = impl.swapchainImages[impl.lastPresentedImageIndex];

    // Staging buffer: host-visible since the goal is a CPU-side pixel copy, never performance.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = requiredBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    if (impl.vk.vkCreateBuffer(impl.device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return fail("vkCreateBuffer for the staging buffer failed");
    }
    VkMemoryRequirements memoryRequirements{};
    impl.vk.vkGetBufferMemoryRequirements(impl.device, stagingBuffer, &memoryRequirements);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    impl.vk.vkGetPhysicalDeviceMemoryProperties(impl.physicalDevice, &memoryProperties);
    std::uint32_t memoryTypeIndex = UINT32_MAX;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((memoryRequirements.memoryTypeBits & (1U << index)) != 0U &&
            (memoryProperties.memoryTypes[index].propertyFlags & wanted) == wanted) {
            memoryTypeIndex = index;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        return fail("no host-visible+coherent memory type reported by the physical device");
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (impl.vk.vkAllocateMemory(impl.device, &allocateInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        return fail("vkAllocateMemory for the staging buffer failed");
    }
    impl.vk.vkBindBufferMemory(impl.device, stagingBuffer, stagingMemory, 0U);

    // Transient command buffer: PRESENT_SRC -> TRANSFER_SRC barrier, copy, barrier back.
    VkCommandBuffer copyCommands = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandAllocateInfo{};
    commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocateInfo.commandPool = impl.commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1U;
    bool ok = impl.vk.vkAllocateCommandBuffers(impl.device, &commandAllocateInfo, &copyCommands) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = impl.vk.vkBeginCommandBuffer(copyCommands, &beginInfo) == VK_SUCCESS;
    }
    if (ok) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = 0U; // queue drained below — nothing pending to order against
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = sourceImage;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0U;
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.baseArrayLayer = 0U;
        toTransfer.subresourceRange.layerCount = 1U;
        impl.vk.vkCmdPipelineBarrier(copyCommands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0U;
        copyRegion.bufferRowLength = 0U;  // tightly packed
        copyRegion.bufferImageHeight = 0U;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0U;
        copyRegion.imageSubresource.baseArrayLayer = 0U;
        copyRegion.imageSubresource.layerCount = 1U;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {impl.swapchainExtent.width, impl.swapchainExtent.height, 1U};
        impl.vk.vkCmdCopyImageToBuffer(copyCommands, sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1U, &copyRegion);

        VkImageMemoryBarrier backToPresent = toTransfer;
        backToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        backToPresent.dstAccessMask = 0U;
        backToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        impl.vk.vkCmdPipelineBarrier(copyCommands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &backToPresent);

        ok = impl.vk.vkEndCommandBuffer(copyCommands) == VK_SUCCESS;
    }
    VkFence readbackFence = VK_NULL_HANDLE;
    if (ok) {
        // The readback rides its own TRANSIENT fence; the frame's in-flight fence stays
        // strictly PresentUVE-owned. Reusing the presented-frame fence here was a latent M1
        // hazard with M2c+: presenting destroys its SubmissionSync pool objects mid-wait
        // paths and a second readback-twiddle could observe/perturb the frame fence between
        // its reset and signal, which deadlocked the following PresentUVE's infinite wait.
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        ok = impl.vk.vkCreateFence(impl.device, &fenceInfo, nullptr, &readbackFence) ==
             VK_SUCCESS;
    }
    if (ok) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1U;
        submitInfo.pCommandBuffers = &copyCommands;
        ok = impl.vk.vkQueueSubmit(impl.presentQueue, 1U, &submitInfo, readbackFence) ==
             VK_SUCCESS;
    }
    if (ok) {
        ok = impl.vk.vkWaitForFences(impl.device, 1U, &readbackFence, VK_TRUE, UINT64_MAX) ==
             VK_SUCCESS;
    }
    if (readbackFence != VK_NULL_HANDLE) {
        impl.vk.vkDestroyFence(impl.device, readbackFence, nullptr);
    }
    if (ok) {
        void* mapped = nullptr;
        if (impl.vk.vkMapMemory(impl.device, stagingMemory, 0U, requiredBytes, 0U, &mapped) == VK_SUCCESS &&
            mapped != nullptr) {
            std::memcpy(outRGBA8.data(), mapped, requiredBytes);
            impl.vk.vkUnmapMemory(impl.device, stagingMemory);
            if (impl.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
                // Surface is B,G,R,A-ordered; the contract promises R,G,B,A bytes — swap in place.
                for (std::size_t px = 0; px + 3U < outRGBA8.size(); px += 4U) {
                    std::swap(outRGBA8[px], outRGBA8[px + 2]);
                }
            }
            outWidth = impl.swapchainExtent.width;
            outHeight = impl.swapchainExtent.height;
        } else {
            ok = false;
        }
    }
    if (copyCommands != VK_NULL_HANDLE) {
        impl.vk.vkFreeCommandBuffers(impl.device, impl.commandPool, 1U, &copyCommands);
    }
    impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
    impl.vk.vkFreeMemory(impl.device, stagingMemory, nullptr);
    if (!ok) {
        return fail("a Vulkan call in the staging/copy path failed (see device logs)");
    }
    return true;
}

std::string_view VulkanRenderDeviceUVE::GetBackendNameUVE() const noexcept {
    // Never the unqualified "Vulkan": the current slice must be identifiable in editor
    // overlays and bug reports (see the header's capability-reporting contract).
    return m_impl->useDynamicRendering ? "Vulkan (M5b storage images)"
                                       : "Vulkan (M2c textures+staging)";
}

} // namespace UVE::Render
