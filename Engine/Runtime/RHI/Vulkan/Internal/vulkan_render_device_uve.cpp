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

/// One reflected combined-image-sampler binding (M2c): RHI texture "slots" mirror GL texture
/// units — the pipeline's i-th sampler binding (sorted ascending) is fed from global slot i.
struct TextureSlotRefUVE {
    std::uint32_t binding = 0U;
    std::string name; // reflected sampler name (diagnostics only; binding is by slot)
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
    struct FramePassStateUVE {
        VkClearValue clearValues[2]{}; // 0: color, 1: depth — swapchain load-CLEAR contents
        std::uint32_t swapchainImageIndex = 0U;
        bool barrierDoneForSwapchain = false;   // per-frame image/depth entry transitions
        bool swapchainPassBegunThisFrame = false;
        bool passOpen = false;
        bool openPassIsSwapchain = true;
        VkImage openOffscreenColorImage = VK_NULL_HANDLE; // restored to SHADER_READ at close
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
    static constexpr std::uint32_t kDescriptorPoolSetCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolUboCapacityUVE = 256U;
    static constexpr std::uint32_t kDescriptorPoolSamplerCapacityUVE = 256U;

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
        void* mapped = nullptr; // persistently mapped: M2a buffers are HOST_VISIBLE on purpose
    };
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
        std::vector<UniformReflectionUVE> reflectedUniforms; // served by GetPipelineUniformsUVE()
        bool uniformsDirty = true; // first bind of any frame flushes everything
        // M2c: one descriptor SET per bound-texture tuple (uniformless pipelines use the
        // static `descriptorSet` above; textured pipelines can never share one set across
        // differing bindings — updating a recorded set in place would retroactively change
        // already-recorded draws). Values are pool-owned; entries are freed explicitly when
        // the tuple's texture is destroyed. Key: concatenated slot-order texture handle ids;
        // the parallel `cachedTextureTextures` map keeps the key's tuple searchable for
        // destruction-time invalidation.
        std::map<std::string, VkDescriptorSet> cachedTextureSets;
        std::map<std::string, std::vector<std::uint32_t>> cachedTextureTextures;
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
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat vkFormat = VK_FORMAT_UNDEFINED; // the IMAGE's format (may be swapchain-typed)
        TextureDescUVE desc{};
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    std::unordered_map<std::uint32_t, TextureRecordUVE> textures;
    std::uint32_t fallbackTextureValue = 0U; // 1x1 opaque-white; used for unbound/destroyed slots

    // --- M2d dynamic-rendering pass helpers (used only when useDynamicRendering) ---------
    // Opens the swapchain rendering instance for the frame's acquired image; the very first
    // open of a frame runs the UNDEFINED-entry barriers, every reopened instance resumes
    // with LOAD (content preserved, matching GL's interleaved FBO semantics).
    [[nodiscard]] bool BeginSwapchainPassDynamicUVE();
    // Opens an offscreen rendering instance against `color` (+ caller `depth` or the scratch
    // target of matching extent). Entry barriers run here; the restore barrier back to
    // SHADER_READ_ONLY for the color image runs at CloseCurrentPassDynamicUVE().
    [[nodiscard]] bool BeginOffscreenPassDynamicUVE(TextureRecordUVE& color,
                                                    TextureRecordUVE* depth,
                                                    VkExtent2D extent,
                                                    const std::array<float, 4>& clearColor,
                                                    float clearDepth);
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
    const std::array<float, 4>& clearColor, const float clearDepth) {
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
        entryBarriers[1].oldLayout = depth->currentLayout; // DS_ATTACHMENT by M2c invariant
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

    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = color.attachmentView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color.float32[0] = clearColor[0];
    colorAttachmentInfo.clearValue.color.float32[1] = clearColor[1];
    colorAttachmentInfo.clearValue.color.float32[2] = clearColor[2];
    colorAttachmentInfo.clearValue.color.float32[3] = clearColor[3];
    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depth != nullptr ? depth->attachmentView : scratch->view;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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
        VkImageMemoryBarrier backToSample{};
        backToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        backToSample.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        backToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        backToSample.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        backToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        backToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToSample.image = state.openOffscreenColorImage;
        backToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, nullptr, 0U,
                                nullptr, 1U, &backToSample);
    }
    state.passOpen = false;
    state.openOffscreenColorImage = VK_NULL_HANDLE;
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

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = kDescriptorPoolUboCapacityUVE;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kDescriptorPoolSamplerCapacityUVE;
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolInfo.maxSets = kDescriptorPoolSetCapacityUVE;
    descriptorPoolInfo.poolSizeCount = 2U;
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
        case BufferUsageUVE::Vertex:  usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
        case BufferUsageUVE::Index:   usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;  break;
        case BufferUsageUVE::Uniform: usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
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
    // M2a memory policy: HOST_VISIBLE + HOST_COHERENT with a persistent map. Deliberately not
    // DEVICE_LOCAL + staging uploads — that performance shape needs transfer-queue plumbing
    // the draw slice doesn't have yet, documented as later-milestone work. The exposed
    // behavior (copy-on-update correctness) is identical, only placement/traffic differs.
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    impl.vk.vkGetPhysicalDeviceMemoryProperties(impl.physicalDevice, &memoryProperties);
    std::uint32_t memoryTypeIndex = UINT32_MAX;
    constexpr VkMemoryPropertyFlags kWanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
            (memoryProperties.memoryTypes[index].propertyFlags & kWanted) == kWanted) {
            memoryTypeIndex = index;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: no host-visible+coherent memory type");
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
    if (impl.vk.vkMapMemory(impl.device, memory, 0U, desc.sizeBytes, 0U, &mapped) != VK_SUCCESS || mapped == nullptr) {
        impl.vk.vkFreeMemory(impl.device, memory, nullptr);
        impl.vk.vkDestroyBuffer(impl.device, buffer, nullptr);
        UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: vkMapMemory failed");
        return kInvalidBufferHandleUVE;
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
    // Never write into host memory the GPU might still be reading via a pending submit.
    (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
    std::memcpy(static_cast<std::byte*>(record.mapped) + offset, data.data(), data.size());
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
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::uint32_t bytesPerPixel = 0U;
    switch (desc.format) {
        case TextureFormatUVE::RGBA8Unorm: {
            const VkFormat sibling = UnormSiblingFormatUVE(impl.swapchainFormat);
            if (sibling != VK_FORMAT_UNDEFINED) {
                format = impl.swapchainFormat;
                sampledFormat = sibling;
                if (sampledFormat != format) {
                    imageFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                }
            } else {
                format = VK_FORMAT_R8G8B8A8_UNORM;
                sampledFormat = format;
            }
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
            // created attachment-ready (initial uploads of depth data are rejected — a host
            // depth upload has no RHI consumer today, so refusing it is the honest boundary).
            format = VK_FORMAT_D32_SFLOAT;
            sampledFormat = format;
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
                : (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
        if ((formatProperties.optimalTilingFeatures & required) != required) {
            return fail("device lacks required format features for the requested texture format");
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
        impl.vk.vkDestroyImageView(impl.device, attachmentView, nullptr);
        impl.vk.vkDestroyImageView(impl.device, view, nullptr);
        destroyImageResources();
        return fail("vkCreateSampler failed for the texture");
    }

    const std::uint32_t handleValue = impl.nextHandleValue++;
    impl.textures.emplace(handleValue,
        ImplUVE::TextureRecordUVE{image, imageMemory, view, attachmentView, sampler, format,
                                  desc, finalLayout});
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
    if (!IsShaderStageValidUVE(desc.stage) ||
        desc.stage == ShaderStageUVE::Compute || desc.stage == ShaderStageUVE::Geometry) {
        return fail("only Vertex/Fragment stages are supported by the M2a draw slice");
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
                // M2c: combined image samplers now bind through the per-tuple set cache.
                if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    TextureSlotRefUVE& slot = textureSlotsByBinding[binding->binding];
                    if (slot.stageFlags != 0U &&
                        (slot.name != (binding->name != nullptr ? binding->name : ""))) {
                        reflectionError = "the same texture binding carries different names "
                                          "across stages";
                        reflectionFailed = true;
                        break;
                    }
                    slot.binding = binding->binding;
                    slot.name = binding->name != nullptr ? binding->name : "";
                    slot.stageFlags |= stageModule.flag;
                    continue;
                }
                if (binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                    binding->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                    reflectionError = std::string("SPIR-V binding [") +
                        (binding->name != nullptr ? binding->name : "?") +
                        "] is neither a uniform buffer nor a combined image sampler "
                        "(SSBOs/storage images/separate samplers land with a later slice; "
                        "M2c binds uniform blocks and textures only)";
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

    // Descriptor set layout: one dynamic-UBO binding per uniform block + one combined image
    // sampler binding per texture slot (all set 0). Pipelines without texture slots allocate
    // their single static set right here (M2b contract, never rewritten); textured pipelines
    // allocate per bound-texture-tuple sets lazily at first draw flush instead.
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (!record.uniformBlocks.empty() || !record.textureSlots.empty()) {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(
            record.uniformBlocks.size() + record.textureSlots.size());
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
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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
        if (record.textureSlots.empty()) {
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
            std::vector<VkWriteDescriptorSet> writes(record.uniformBlocks.size());
            std::vector<VkDescriptorBufferInfo> bufferInfos(record.uniformBlocks.size());
            for (std::size_t index = 0; index < record.uniformBlocks.size(); ++index) {
                VkDescriptorBufferInfo& bufferInfo = bufferInfos[index];
                bufferInfo = {};
                bufferInfo.buffer = impl.frameUbo;
                bufferInfo.offset = 0U;
                bufferInfo.range = record.uniformBlocks[index].size;
                VkWriteDescriptorSet& write = writes[index];
                write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSet;
                write.dstBinding = record.uniformBlocks[index].binding;
                write.dstArrayElement = 0U;
                write.descriptorCount = 1U;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                write.pBufferInfo = &bufferInfo;
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
    m_impl->frameSubmissions.push_back(vulkanCommands->TakeCommandsUVE());
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

    const auto flushStateForActivePipelineUVE = [&impl, &currentTextureValues]() -> bool {
        const auto found = impl.pipelines.find(impl.activePipelineValue);
        if (found == impl.pipelines.end()) {
            return true; // no pipeline bound by this replay: nothing to flush
        }
        ImplUVE::PipelineRecordUVE& record = found->second;
        if (record.uniformBlocks.empty() && !record.pushBlock.valid &&
            record.textureSlots.empty()) {
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
        // built at creation. Textured pipelines: the set cached for THIS bound-texture tuple
        // (allocated + fully written on first use; never rewritten in place — a recorded bind
        // would otherwise retroactively change earlier draws).
        VkDescriptorSet setToBind = record.descriptorSet;
        if (!record.textureSlots.empty()) {
            static thread_local std::vector<const ImplUVE::TextureRecordUVE*> tupleRecords;
            tupleRecords.clear();
            std::string tupleKey;
            std::vector<std::uint32_t> tupleValues;
            tupleValues.reserve(record.textureSlots.size());
            tupleRecords.reserve(record.textureSlots.size());
            for (std::size_t slotIndex = 0; slotIndex < record.textureSlots.size(); ++slotIndex) {
                std::uint32_t value = impl.fallbackTextureValue;
                const auto bound = currentTextureValues.find(static_cast<std::uint32_t>(slotIndex));
                if (bound != currentTextureValues.end() &&
                    impl.textures.find(bound->second) != impl.textures.end()) {
                    value = bound->second; // destroyed-after-bind resolves to the fallback
                }
                ImplUVE::TextureRecordUVE& slotRecord = impl.textures.at(value);
                if (slotRecord.desc.format == TextureFormatUVE::Depth32Float) {
                    // M2d boundary: depth textures are attachment-ready but SAMPLING them is
                    // the M2e slice (the descriptor would need a special depth-comparison
                    // declaration); they sample as the 1x1 white fallback for now.
                    impl.WarnOnceUVE(
                        VulkanRenderDeviceUVE::ImplUVE::kWarnedDepthTextureSampledUVE,
                        "BindTextureUVE: a Depth32Float texture was bound to a sampled shader "
                        "slot; depth-texture sampling lands in the M2e slice — the 1x1 white "
                        "fallback is sampled instead");
                    value = impl.fallbackTextureValue;
                }
                tupleValues.push_back(value);
                tupleRecords.push_back(&impl.textures.at(value));
                tupleKey.append(std::to_string(value)).push_back('#');
            }
            const auto cached = record.cachedTextureSets.find(tupleKey);
            if (cached != record.cachedTextureSets.end()) {
                setToBind = cached->second;
            } else {
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
                writes.reserve(record.uniformBlocks.size() + record.textureSlots.size());
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
                    imageInfo.sampler = tupleRecords[index]->sampler;
                    imageInfo.imageView = tupleRecords[index]->view;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = freshSet;
                    write.dstBinding = record.textureSlots[index].binding;
                    write.descriptorCount = 1U;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &imageInfo;
                    writes.push_back(write);
                }
                impl.vk.vkUpdateDescriptorSets(impl.device, static_cast<std::uint32_t>(writes.size()),
                                               writes.data(), 0U, nullptr);
                record.cachedTextureSets.emplace(tupleKey, freshSet);
                record.cachedTextureTextures.emplace(tupleKey, std::move(tupleValues));
                setToBind = freshSet;
            }
        }
        if (setToBind != VK_NULL_HANDLE) {
            impl.vk.vkCmdBindDescriptorSets(impl.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
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
                                if (op.desc.colorLoadOp != LoadOpUVE::Clear ||
                                    op.desc.depthLoadOp != LoadOpUVE::Clear) {
                                    impl.WarnOnceUVE(
                                        VulkanRenderDeviceUVE::ImplUVE::kWarnedLoadOpUnhonoredUVE,
                                        "BeginRenderPassUVE requested Load/DontCare on an "
                                        "offscreen pass; UVE clears such passes exactly once "
                                        "per pass (see the swapchain contract)");
                                }
                                if (impl.framePassState.passOpen) {
                                    impl.CloseCurrentPassDynamicUVE();
                                }
                                if (impl.BeginOffscreenPassDynamicUVE(
                                        foundColor->second,
                                        degradeToScratchDepth ? nullptr : depthRecord,
                                        VkExtent2D{color.desc.width, color.desc.height},
                                        op.desc.clearColor, op.desc.clearDepth)) {
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
            } else if (!passActiveForThisList) {
                // Anything outside an accepted pass is ignored: the same rule GL's world has,
                // where draws outside a pass bind land nowhere meaningful.
            } else if constexpr (std::is_same_v<OpT, BindPipelineCommandUVE>) {
                const auto found = impl.pipelines.find(op.pipeline.value);
                if (found == impl.pipelines.end()) {
                    impl.WarnOnceUVE(VulkanRenderDeviceUVE::ImplUVE::kWarnedUnknownHandleUVE,
                        "submit replay: unknown pipeline handle; draws bound to it are skipped");
                    impl.activePipelineValue = 0U;
                } else {
                    impl.vk.vkCmdBindPipeline(impl.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              found->second.pipeline);
                    impl.activePipelineValue = op.pipeline.value;
                }
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
                if (flushStateForActivePipelineUVE()) {
                    impl.vk.vkCmdDraw(impl.commandBuffer, op.vertexCount, op.instanceCount, 0U, 0U);
                }
            } else if constexpr (std::is_same_v<OpT, DrawIndexedCommandUVE>) {
                if (flushStateForActivePipelineUVE()) {
                    impl.vk.vkCmdDrawIndexed(impl.commandBuffer, op.indexCount, op.instanceCount,
                                             0U, 0, 0U);
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

    const auto dropSubmissionsUVE = [&impl](const char* /*why*/) {
        if (!impl.frameSubmissions.empty()) {
            impl.WarnOnceUVE(ImplUVE::kWarnedDroppedSubmissionsUVE,
                "PresentUVE: recorded command buffers dropped because the frame was skipped "
                "(minimized/resizing/acquire failure); submissions carry per-frame content");
            impl.frameSubmissions.clear();
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
    for (const std::vector<RecordedCommandUVE>& submission : impl.frameSubmissions) {
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
    }

    // Replay every submitted recorded command buffer in submission order, inside THIS pass —
    // the M2a integration contract documented at ReplayRecordedCommandsUVE. Weakest-design
    // point honored deliberately: IRenderDeviceUVE's submission model assumes one pass chain
    // per frame against the back buffer (it matches how GlRenderDeviceUVE's FBO-0 world
    // works today); anything outside that shape is warned-about once, never silently mangled.
    impl.activePipelineValue = 0U; // pipeline binding is fresh command-buffer state each frame
    for (const std::vector<RecordedCommandUVE>& submission : impl.frameSubmissions) {
        ReplayRecordedCommandsUVE(submission);
    }
    impl.frameSubmissions.clear(); // consumed — submissions are per-frame content by contract

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
    return m_impl->useDynamicRendering ? "Vulkan (M2d offscreen RT)"
                                       : "Vulkan (M2c textures+staging)";
}

} // namespace UVE::Render
