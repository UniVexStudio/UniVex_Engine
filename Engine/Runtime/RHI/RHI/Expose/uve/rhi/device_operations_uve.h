// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <variant>
#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render {
/// Model means validation/recording, never GPU execution or invented timing/visibility data.
enum class RenderFeatureSupportUVE : std::uint8_t { Unsupported = 0, Model, Native };
/// Success means the host call/native command was accepted, NOT that asynchronous GPU work
/// has finished. Only successful fence/idle waits establish GPU completion. Recorded explicitly
/// identifies retained commands or the Null model. All failing output-parameter calls preserve output.
enum class RenderOperationResultUVE : std::uint8_t {
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
struct RenderDeviceCapabilitiesUVE final {
    RenderFeatureSupportUVE fences = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE memoryBarriers = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE bufferCopy = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE textureBlit = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE mipGeneration = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE timestampQueries = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE occlusionQueries = RenderFeatureSupportUVE::Unsupported;
    RenderFeatureSupportUVE surface = RenderFeatureSupportUVE::Unsupported;
    /// Copy source/destination offsets and byte count must satisfy this backend's alignment.
    std::uint32_t bufferCopyAlignment = 1U;
};
struct FenceTagUVE {};
struct QueryTagUVE {};
using FenceHandleUVE = ResourceHandleUVE<FenceTagUVE>;
using QueryHandleUVE = ResourceHandleUVE<QueryTagUVE>;
inline constexpr FenceHandleUVE kInvalidFenceHandleUVE{};
inline constexpr QueryHandleUVE kInvalidQueryHandleUVE{};

/// A visibility barrier, outside render passes. Backend pass/texture binding still owns image
/// layout transitions. Explicitly excludes queue ownership transfers and CPU cache management.
enum class MemoryBarrierMaskUVE : std::uint32_t {
    ShaderStorage = 1U,
    TextureFetch = 2U,
    VertexInput = 4U,
    IndirectCommands = 8U,
    Transfer = 16U,
    Framebuffer = 32U,
    All = 63U
};
[[nodiscard]] constexpr MemoryBarrierMaskUVE operator|(MemoryBarrierMaskUVE a,
                                                       MemoryBarrierMaskUVE b) noexcept {
    return static_cast<MemoryBarrierMaskUVE>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool IsMemoryBarrierMaskValidUVE(MemoryBarrierMaskUVE mask) noexcept {
    const auto value = static_cast<std::uint32_t>(mask);
    return value != 0U && (value & ~63U) == 0U;
}
struct MemoryBarrierCommandUVE {
    MemoryBarrierMaskUVE mask = MemoryBarrierMaskUVE::All;
};
struct BufferCopyDescUVE final {
    BufferHandleUVE source, destination;
    std::uint64_t sourceOffset = 0U, destinationOffset = 0U, sizeBytes = 0U;
};
[[nodiscard]] constexpr bool IsBufferCopyRangeValidUVE(const BufferCopyDescUVE &copy,
                                                       std::uint64_t sourceSize,
                                                       std::uint64_t destinationSize) noexcept {
    if (copy.sizeBytes == 0U || copy.sourceOffset > sourceSize || copy.destinationOffset > destinationSize ||
        copy.sizeBytes > sourceSize - copy.sourceOffset ||
        copy.sizeBytes > destinationSize - copy.destinationOffset)
        return false;
    return copy.source != copy.destination || copy.sourceOffset >= copy.destinationOffset + copy.sizeBytes ||
           copy.destinationOffset >= copy.sourceOffset + copy.sizeBytes;
}
struct TextureRegionUVE final {
    std::uint32_t mip = 0U, layer = 0U, x = 0U, y = 0U, width = 0U, height = 0U;
};
[[nodiscard]] constexpr bool IsTextureRegionValidUVE(const TextureDescUVE &texture,
                                                     const TextureRegionUVE &region) noexcept {
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
enum class BlitFilterUVE : std::uint8_t { Point = 0, Linear };
struct TextureBlitDescUVE final {
    TextureHandleUVE source, destination;
    TextureRegionUVE sourceRegion, destinationRegion;
    BlitFilterUVE filter = BlitFilterUVE::Point;
};
/// Color-only blits between distinct, equally formatted/color-spaced, uncompressed textures.
/// Point with equal extents is an exact image copy. Depth/stencil, format conversion and
/// self-blits are deliberately rejected instead of silently taking a different path.
[[nodiscard]] constexpr bool IsTextureBlitValidUVE(const TextureBlitDescUVE &blit,
                                                   const TextureDescUVE &source,
                                                   const TextureDescUVE &destination) noexcept {
    return blit.source != blit.destination && source.format == destination.format &&
           source.colorSpace == destination.colorSpace &&
           (source.format == TextureFormatUVE::RGBA8Unorm ||
            source.format == TextureFormatUVE::RGBA16Float) &&
           (blit.filter == BlitFilterUVE::Point || blit.filter == BlitFilterUVE::Linear) &&
           IsTextureRegionValidUVE(source, blit.sourceRegion) &&
           IsTextureRegionValidUVE(destination, blit.destinationRegion);
}
enum class RenderQueryKindUVE : std::uint8_t { Timestamp = 0, AnySamplesPassed };
struct RenderQueryResultUVE final {
    RenderQueryKindUVE kind = RenderQueryKindUVE::Timestamp;
    /// Timestamp: native GPU nanoseconds. AnySamplesPassed: 0 or 1. Never supplied by Null.
    std::uint64_t value = 0U;
};
struct RenderSurfaceInfoUVE final {
    std::uint32_t width = 0U, height = 0U;
    bool drawable = false;
    bool headless = false;
    std::uint32_t imageCount = 0U;
};
struct GenerateMipsOperationUVE {
    TextureHandleUVE texture;
};
struct FenceOperationUVE {
    FenceHandleUVE fence;
};
struct QueryOperationUVE {
    QueryHandleUVE query;
    RenderQueryKindUVE kind;
    bool begin = false;
};
using RecordedDeviceOperationUVE =
    std::variant<BufferCopyDescUVE, TextureBlitDescUVE, GenerateMipsOperationUVE, FenceOperationUVE,
                 QueryOperationUVE>;
} // namespace UVE::Render
