// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <span>
#include <variant>
#include <vector>
#include "buffer_handle.h"
#include "texture_handle.h"
#include "render_resource_descs.h"

namespace UVE::Render {
/// Model means validation/recording, never GPU execution or invented timing/visibility data.
enum class RenderFeatureSupport : std::uint8_t { Unsupported = 0, Model, Native };
/// Success means the host call/native command was accepted, NOT that asynchronous GPU work
/// has finished. Only successful fence/idle waits establish GPU completion. Recorded explicitly
/// identifies retained commands or the Null model. All failing output-parameter calls preserve output.
enum class RenderOperationResult : std::uint8_t {
    Success = 0,
    Recorded,
    Unsupported,
    InvalidArgument,
    InvalidHandle,
    InvalidState,
    NotReady,
    TimedOut,
    DeviceUnavailable,
    SystemError
};
struct RenderDeviceCapabilities final {
    RenderFeatureSupport fences = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport memoryBarriers = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport bufferCopy = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport textureBlit = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport mipGeneration = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport timestampQueries = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport occlusionQueries = RenderFeatureSupport::Unsupported;
    RenderFeatureSupport surface = RenderFeatureSupport::Unsupported;
    /// Command-buffer resource leases are retained until that backend has finished using them.
    RenderFeatureSupport retainedTextures = RenderFeatureSupport::Unsupported;
    /// Copy source/destination offsets and byte count must satisfy this backend's alignment.
    std::uint32_t bufferCopyAlignment = 1U;
    /// Timeline semaphore objects with CPU-visible counters. `Model` means the backend keeps
    /// real counter state and real blocking waits but never claims GPU timing; `Native` means the
    /// driver's own timeline semaphores back the calls; `Unsupported` means every semaphore call
    /// is refused explicitly.
    RenderFeatureSupport timelineSemaphores = RenderFeatureSupport::Unsupported;
    /// Optional geometry stage in a graphics pipeline; Model never means GPU execution.
    RenderFeatureSupport geometryShaders = RenderFeatureSupport::Unsupported;
    /// Descriptor-indexing / bindless sampled texture table support.
    RenderFeatureSupport bindlessTextures = RenderFeatureSupport::Unsupported;
    std::uint32_t maxBindlessTextures = 0U;
    /// Shader storage buffers (SSBOs / std430) in graphics or compute stages.
    RenderFeatureSupport storageBuffers = RenderFeatureSupport::Unsupported;
    /// Color-attachment renderability for half-float HDR textures (RGBA16Float).
    RenderFeatureSupport halfFloatColorTargets = RenderFeatureSupport::Unsupported;
};
/// Timeline semaphore: a monotonic 64-bit counter that orders work without a binary
/// signal/unsignal cycle. Binary (queue-to-queue) semaphores are deliberately NOT part of this
/// surface: they only mean something as parameters of a submission, and this RHI's Submit
/// owns its frame submission. A backend may therefore report timeline semaphores as
/// `Unsupported` without affecting fences, which remain the CPU-visible completion primitive.
struct FenceTag {};
struct SemaphoreTag {};
/// Creation parameters for CreateSemaphore. `initialValue` is the counter's starting value;
/// waiting for a value at or below it succeeds immediately, and signalling must strictly exceed
/// the current value (a timeline counter never moves backwards).
struct SemaphoreDesc final {
    std::uint64_t initialValue = 0U;
};

[[nodiscard]] constexpr bool IsSemaphoreSignalValueValid(const std::uint64_t currentValue,
                                                            const std::uint64_t value) noexcept {
    return value > currentValue;
}

/// A wait for a value already reached resolves immediately, so every value is a legal wait
/// target; only the handle and the timeout decide the outcome.
[[nodiscard]] constexpr bool IsSemaphoreWaitValueValid(const std::uint64_t /*value*/) noexcept {
    return true;
}

struct QueryTag {};
using FenceHandle = ResourceHandle<FenceTag>;
using SemaphoreHandle = ResourceHandle<SemaphoreTag>;
using QueryHandle = ResourceHandle<QueryTag>;
inline constexpr FenceHandle kInvalidFenceHandle{};
inline constexpr SemaphoreHandle kInvalidSemaphoreHandle{};
inline constexpr QueryHandle kInvalidQueryHandle{};

/// A visibility barrier, outside render passes. Backend pass/texture binding still owns image
/// layout transitions. Explicitly excludes queue ownership transfers and CPU cache management.
enum class MemoryBarrierMask : std::uint32_t {
    ShaderStorage = 1U,
    TextureFetch = 2U,
    VertexInput = 4U,
    IndirectCommands = 8U,
    Transfer = 16U,
    Framebuffer = 32U,
    All = 63U
};
[[nodiscard]] constexpr MemoryBarrierMask operator|(MemoryBarrierMask a,
                                                       MemoryBarrierMask b) noexcept {
    return static_cast<MemoryBarrierMask>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool IsMemoryBarrierMaskValid(MemoryBarrierMask mask) noexcept {
    const auto value = static_cast<std::uint32_t>(mask);
    return value != 0U && (value & ~63U) == 0U;
}
struct MemoryBarrierCommand {
    MemoryBarrierMask mask = MemoryBarrierMask::All;
};
struct BufferCopyDesc final {
    BufferHandle source, destination;
    std::uint64_t sourceOffset = 0U, destinationOffset = 0U, sizeBytes = 0U;
};
[[nodiscard]] constexpr bool IsBufferCopyRangeValid(const BufferCopyDesc &copy,
                                                       std::uint64_t sourceSize,
                                                       std::uint64_t destinationSize) noexcept {
    if (copy.sizeBytes == 0U || copy.sourceOffset > sourceSize || copy.destinationOffset > destinationSize ||
        copy.sizeBytes > sourceSize - copy.sourceOffset ||
        copy.sizeBytes > destinationSize - copy.destinationOffset)
        return false;
    return copy.source != copy.destination || copy.sourceOffset >= copy.destinationOffset + copy.sizeBytes ||
           copy.destinationOffset >= copy.sourceOffset + copy.sizeBytes;
}
struct TextureRegion final {
    std::uint32_t mip = 0U, layer = 0U, x = 0U, y = 0U, width = 0U, height = 0U;
};
[[nodiscard]] constexpr bool IsTextureRegionValid(const TextureDesc &texture,
                                                     const TextureRegion &region) noexcept {
    if (region.mip >= texture.mipLevels || region.mip >= 32U || region.layer >= texture.arrayLayers ||
        region.width == 0U || region.height == 0U)
        return false;
    const auto w = texture.width >> region.mip;
    const auto h = texture.height >> region.mip;
    const auto width = w == 0U ? 1U : w;
    const auto height = h == 0U ? 1U : h;
    return region.x <= width && region.y <= height && region.width <= width - region.x &&
           region.height <= height - region.y;
}
enum class BlitFilter : std::uint8_t { Point = 0, Linear };
struct TextureBlitDesc final {
    TextureHandle source, destination;
    TextureRegion sourceRegion, destinationRegion;
    BlitFilter filter = BlitFilter::Point;
};
/// Color-only blits between distinct, equally formatted/color-spaced, uncompressed textures.
/// Point with equal extents is an exact image copy. Depth/stencil, format conversion and
/// self-blits are deliberately rejected instead of silently taking a different path.
[[nodiscard]] constexpr bool IsTextureBlitValid(const TextureBlitDesc &blit,
                                                   const TextureDesc &source,
                                                   const TextureDesc &destination) noexcept {
    return blit.source != blit.destination && source.format == destination.format &&
           source.colorSpace == destination.colorSpace &&
           IsUncompressedColorFormat(source.format) &&
           source.type != TextureType::Texture3D && destination.type != TextureType::Texture3D &&
           HasTextureUsage(GetEffectiveTextureUsage(source), TextureUsage::TransferSrc) &&
           HasTextureUsage(GetEffectiveTextureUsage(destination), TextureUsage::TransferDst) &&
           (blit.filter == BlitFilter::Point || blit.filter == BlitFilter::Linear) &&
           IsTextureRegionValid(source, blit.sourceRegion) &&
           IsTextureRegionValid(destination, blit.destinationRegion);
}
/// The shared gate of every backend's GenerateMipmaps: a multi-level, uncompressed colour
/// texture (volumes included) whose usage allows both ends of the level-to-level copy.
[[nodiscard]] constexpr bool CanGenerateMipmaps(const TextureDesc &texture) noexcept {
    const TextureUsage usage = GetEffectiveTextureUsage(texture);
    return texture.mipLevels >= 2U && IsUncompressedColorFormat(texture.format) &&
           HasTextureUsage(usage, TextureUsage::TransferSrc) &&
           HasTextureUsage(usage, TextureUsage::TransferDst);
}
enum class RenderQueryKind : std::uint8_t { Timestamp = 0, AnySamplesPassed };
struct RenderQueryResult final {
    RenderQueryKind kind = RenderQueryKind::Timestamp;
    /// Timestamp: native GPU nanoseconds. AnySamplesPassed: 0 or 1. Never supplied by Null.
    std::uint64_t value = 0U;
};
struct RenderSurfaceInfo final {
    std::uint32_t width = 0U, height = 0U;
    bool drawable = false;
    bool headless = false;
    std::uint32_t imageCount = 0U;
};
struct GenerateMipsOperation {
    TextureHandle texture;
};
struct FenceOperation {
    FenceHandle fence;
};
/// Recorded (not simulated) CPU-side timeline operations: Null keeps their exact arguments in
/// its diagnostic history, and no backend pretends a signal happened on the GPU because one was
/// recorded here.
struct SignalSemaphoreOperation {
    SemaphoreHandle semaphore;
    std::uint64_t value = 0U;
};
struct WaitSemaphoreOperation {
    SemaphoreHandle semaphore;
    std::uint64_t value = 0U;
    std::uint64_t timeoutNs = 0U;
    bool satisfied = false;
};
struct QueryOperation {
    QueryHandle query;
    RenderQueryKind kind;
    bool begin = false;
};
/// Foundation WP4: submission-level queue synchronisation, expressed with the timeline objects
/// above (a timeline wait/signal pair is strictly more expressive than a binary semaphore, so no
/// second semaphore type exists). A wait gates the GPU execution of the submitted work until the
/// semaphore's counter reaches `value`; a signal sets the counter to `value` once that work has
/// completed on the GPU. `value` for a signal must exceed the counter at submit time.
struct SemaphoreWait {
    SemaphoreHandle semaphore;
    std::uint64_t value = 0U;
};
struct SemaphoreSignal {
    SemaphoreHandle semaphore;
    std::uint64_t value = 0U;
};
/// Parameters of IRenderDevice::SubmitWithSync. Both spans are borrowed only for the
/// duration of the call. Every semaphore handle must be unique across BOTH lists.
struct SubmitDesc {
    std::span<const SemaphoreWait> waits;
    std::span<const SemaphoreSignal> signals;
};
/// Null's verbatim record of one SubmitWithSync call. Recorded, never simulated: no counter
/// moves because a signal was recorded here.
struct SubmitOperation {
    std::vector<SemaphoreWait> waits;
    std::vector<SemaphoreSignal> signals;
};
using RecordedDeviceOperation =
    std::variant<BufferCopyDesc, TextureBlitDesc, GenerateMipsOperation, FenceOperation,
                 QueryOperation, SignalSemaphoreOperation, WaitSemaphoreOperation, SubmitOperation>;
} // namespace UVE::Render
