// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "uve/math/rect_int_uve.h"
#include "uve/rhi/shader_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {

/// What a BufferDescUVE-created buffer is used for. `Storage` (Vulkan M2f) is a shader-storage
/// buffer (SSBO): arbitrary read/write GPU data bound via ICommandBufferUVE::BindStorageBufferUVE
/// and read/written from graphics-stage shaders (`readonly buffer`/`buffer` blocks) — and since
/// the M5a compute slice, from compute shaders too (the classic SSBO producer/consumer pattern:
/// dispatch writes, draws read).
/// `IndirectStorage` (CS7) is a buffer holding DrawIndexedIndirectCommandUVE records for
/// ICommandBufferUVE::DrawIndexedIndirectUVE() - and, deliberately, an SSBO at the same time.
/// A separate write-only Indirect usage was considered and rejected: the entire point of indirect
/// draw in this engine is that a COMPUTE dispatch writes the parameters (that is what lets GPU
/// culling stop round-tripping through the CPU), so a usage the compute stage cannot bind would
/// serve nothing the CPU could not already do with DrawIndexedUVE. Backends therefore create it
/// with both capabilities - VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | STORAGE_BUFFER_BIT on Vulkan,
/// and on GL it binds to both GL_DRAW_INDIRECT_BUFFER and GL_SHADER_STORAGE_BUFFER - and
/// ReadbackBufferUVE covers it exactly as it covers Storage.
enum class BufferUsageUVE : std::uint8_t { Vertex, Index, Uniform, Storage, IndirectStorage };

[[nodiscard]] constexpr bool IsBufferUsageValidUVE(const BufferUsageUVE usage) noexcept {
    switch (usage) {
        case BufferUsageUVE::Vertex:
        case BufferUsageUVE::Index:
        case BufferUsageUVE::Uniform:
        case BufferUsageUVE::Storage:
        case BufferUsageUVE::IndirectStorage:
            return true;
    }
    return false;
}

/// True for the usages a shader-storage binding accepts. IndirectStorage is deliberately included:
/// it IS an SSBO, and the compute kernel that fills it binds it as one.
[[nodiscard]] constexpr bool IsStorageBindableUsageUVE(const BufferUsageUVE usage) noexcept {
    return usage == BufferUsageUVE::Storage || usage == BufferUsageUVE::IndirectStorage;
}

/// One indexed indirect draw, laid out to match what every backend's indirect draw reads straight
/// out of GPU memory: VkDrawIndexedIndirectCommand and GL's DrawElementsIndirectCommand are the
/// same five consecutive 32-bit fields in the same order, which is what makes one struct portable
/// here. The layout is fixed by those APIs, so it is asserted below rather than merely intended.
struct DrawIndexedIndirectCommandUVE {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0;
};

static_assert(sizeof(DrawIndexedIndirectCommandUVE) == 20U,
              "DrawIndexedIndirectCommandUVE must be the five tightly packed 32-bit fields both "
              "Vulkan and GL read directly out of buffer memory");
static_assert(alignof(DrawIndexedIndirectCommandUVE) == alignof(std::uint32_t),
              "DrawIndexedIndirectCommandUVE must not acquire padding the GPU layout lacks");

/// Describes a GPU buffer to create via IRenderDeviceUVE::CreateBufferUVE(). A buffer must have
/// positive byte capacity; zero-sized GL buffers are not useful to any supported draw/update path.
/// Where a buffer's memory should live and which side writes it. A placement request, never a
/// behavioural switch: UpdateBufferUVE and ReadbackBufferUVE keep the same copy-on-call contract
/// for every value, and a backend that cannot offer the placement picks one that satisfies the
/// usage. `Auto` keeps each backend's usage-derived policy (Vulkan today: vertex/index
/// device-local behind a staging copy, uniform/storage/indirect host-visible and persistently
/// mapped; GL has one memory and folds the hint into glBufferData's usage token).
enum class BufferMemoryUVE : std::uint8_t {
    Auto = 0,
    /// GPU-resident; host updates and readbacks travel through staging copies on Vulkan.
    DeviceLocal,
    /// Host-visible and CPU-written, usually often: streamed vertices, uniform rings.
    Upload,
    /// Host-visible and GPU-written for the CPU to read (cached memory when the device offers it).
    Readback,
};

/// How often the host rewrites the buffer after creation. glBufferData's STATIC/DYNAMIC/STREAM
/// token on GL; recorded (not acted on) by Vulkan, where placement is BufferMemoryUVE's job.
enum class BufferUpdateFrequencyUVE : std::uint8_t { Static = 0, Dynamic, Stream };

[[nodiscard]] constexpr bool IsBufferMemoryValidUVE(const BufferMemoryUVE memory) noexcept {
    switch (memory) {
        case BufferMemoryUVE::Auto:
        case BufferMemoryUVE::DeviceLocal:
        case BufferMemoryUVE::Upload:
        case BufferMemoryUVE::Readback:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsBufferUpdateFrequencyValidUVE(const BufferUpdateFrequencyUVE frequency) noexcept {
    switch (frequency) {
        case BufferUpdateFrequencyUVE::Static:
        case BufferUpdateFrequencyUVE::Dynamic:
        case BufferUpdateFrequencyUVE::Stream:
            return true;
    }
    return false;
}

struct BufferDescUVE {
    std::uint64_t sizeBytes = 0;
    BufferUsageUVE usage = BufferUsageUVE::Vertex;
    BufferMemoryUVE memory = BufferMemoryUVE::Auto;
    BufferUpdateFrequencyUVE updateFrequency = BufferUpdateFrequencyUVE::Static;
};

/// Every enumerator on the descriptor is a known one. Backends check this before allocating so
/// an out-of-range value coming across the RHI boundary is refused instead of hitting a default.
[[nodiscard]] constexpr bool IsBufferDescValidUVE(const BufferDescUVE& desc) noexcept {
    return IsBufferUsageValidUVE(desc.usage) && IsBufferMemoryValidUVE(desc.memory) &&
           IsBufferUpdateFrequencyValidUVE(desc.updateFrequency);
}

[[nodiscard]] constexpr bool ValidateBufferUploadUVE(const BufferDescUVE& desc,
                                                       const std::span<const std::byte> initialData) noexcept {
    return desc.sizeBytes > 0U && initialData.size() <= desc.sizeBytes;
}

[[nodiscard]] constexpr bool ValidateBufferUpdateUVE(const std::uint64_t bufferSizeBytes,
                                                       const std::size_t dataSize,
                                                       const std::uint64_t offsetBytes) noexcept {
    return offsetBytes <= bufferSizeBytes && dataSize <= bufferSizeBytes - offsetBytes;
}

/// Pixel formats an IRenderDeviceUVE texture can use. `Depth32Float` exists here (even though no
/// loadable asset ever uses it — see the deliberately separate Asset::TextureAssetFormatUVE) because
/// depth render targets are created directly through this RHI, never loaded from disk. Compressed
/// formats are sampled resources only: they cannot be framebuffer attachments or storage images.
/// The one- and two-channel formats (10..16) are linear-only: a missing channel samples as 0
/// (alpha as 1), uploads are tightly packed rows (a 3-wide `R8Unorm` level is 3 bytes per row),
/// and they are color-renderable on desktop GL; Vulkan attachment of any non-swapchain format
/// still waits on per-target pipeline formats. Append-only: values cross the RHI boundary.
enum class TextureFormatUVE : std::uint8_t {
    RGBA8Unorm = 0,
    RGBA16Float = 1,
    Depth32Float = 2,
    BC1RGB = 3,
    BC3RGBA = 4,
    BC7RGBA = 5,
    ETC2RGB8 = 6,
    ETC2RGBA8 = 7,
    ASTC4x4RGBA = 8,
    Depth24Stencil8 = 9,
    R8Unorm = 10,
    RG8Unorm = 11,
    R16Float = 12,
    RG16Float = 13,
    R32Float = 14,
    RG32Float = 15,
    RGBA32Float = 16,
};

/// Sampled interpretation of normalized color-channel values. This is separate from the stored
/// pixel layout; supported RGBA8 and compressed color formats can use sRGB decoding.
enum class TextureColorSpaceUVE : std::uint8_t {
    Linear = 0,
    Srgb = 1,
};

/// Pixel/block geometry for one mip level. All currently supported block-compressed formats use
/// 4x4 texel blocks; edge blocks are allocated in full.
struct TextureFormatBlockInfoUVE {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t bytes = 0U;
    bool compressed = false;
};

[[nodiscard]] inline constexpr TextureFormatBlockInfoUVE GetTextureFormatBlockInfoUVE(
    const TextureFormatUVE format) noexcept {
    switch (format) {
        case TextureFormatUVE::R8Unorm:
            return {1U, 1U, 1U, false};
        case TextureFormatUVE::RG8Unorm:
        case TextureFormatUVE::R16Float:
            return {1U, 1U, 2U, false};
        case TextureFormatUVE::RGBA8Unorm:
        case TextureFormatUVE::Depth32Float:
        case TextureFormatUVE::Depth24Stencil8:
        case TextureFormatUVE::RG16Float:
        case TextureFormatUVE::R32Float:
            return {1U, 1U, 4U, false};
        case TextureFormatUVE::RGBA16Float:
        case TextureFormatUVE::RG32Float:
            return {1U, 1U, 8U, false};
        case TextureFormatUVE::RGBA32Float:
            return {1U, 1U, 16U, false};
        case TextureFormatUVE::BC1RGB:
        case TextureFormatUVE::ETC2RGB8:
            return {4U, 4U, 8U, true};
        case TextureFormatUVE::BC3RGBA:
        case TextureFormatUVE::BC7RGBA:
        case TextureFormatUVE::ETC2RGBA8:
        case TextureFormatUVE::ASTC4x4RGBA:
            return {4U, 4U, 16U, true};
    }
    return {};
}

[[nodiscard]] inline constexpr bool IsTextureFormatCompressedUVE(const TextureFormatUVE format) noexcept {
    return GetTextureFormatBlockInfoUVE(format).compressed;
}

[[nodiscard]] inline constexpr bool IsTextureFormatSrgbCapableUVE(const TextureFormatUVE format) noexcept {
    switch (format) {
        case TextureFormatUVE::RGBA8Unorm:
        case TextureFormatUVE::BC1RGB:
        case TextureFormatUVE::BC3RGBA:
        case TextureFormatUVE::BC7RGBA:
        case TextureFormatUVE::ETC2RGB8:
        case TextureFormatUVE::ETC2RGBA8:
        case TextureFormatUVE::ASTC4x4RGBA:
            return true;
        case TextureFormatUVE::RGBA16Float:
        case TextureFormatUVE::Depth32Float:
        case TextureFormatUVE::Depth24Stencil8:
        case TextureFormatUVE::R8Unorm:
        case TextureFormatUVE::RG8Unorm:
        case TextureFormatUVE::R16Float:
        case TextureFormatUVE::RG16Float:
        case TextureFormatUVE::R32Float:
        case TextureFormatUVE::RG32Float:
        case TextureFormatUVE::RGBA32Float:
            return false;
    }
    return false;
}

[[nodiscard]] inline constexpr bool IsDepthFormatUVE(const TextureFormatUVE format) noexcept {
    return format == TextureFormatUVE::Depth32Float || format == TextureFormatUVE::Depth24Stencil8;
}

/// An uncompressed colour format: sampled, filterable where the device allows, and the only kind
/// a colour attachment, storage image, colour blit or generated mip chain may use.
[[nodiscard]] inline constexpr bool IsUncompressedColorFormatUVE(const TextureFormatUVE format) noexcept {
    const TextureFormatBlockInfoUVE info = GetTextureFormatBlockInfoUVE(format);
    return info.bytes != 0U && !info.compressed && !IsDepthFormatUVE(format);
}

/// Number of colour channels an uncompressed colour format stores (0 for anything else).
[[nodiscard]] inline constexpr std::uint32_t GetTextureFormatChannelCountUVE(const TextureFormatUVE format) noexcept {
    switch (format) {
        case TextureFormatUVE::R8Unorm:
        case TextureFormatUVE::R16Float:
        case TextureFormatUVE::R32Float:
            return 1U;
        case TextureFormatUVE::RG8Unorm:
        case TextureFormatUVE::RG16Float:
        case TextureFormatUVE::RG32Float:
            return 2U;
        case TextureFormatUVE::RGBA8Unorm:
        case TextureFormatUVE::RGBA16Float:
        case TextureFormatUVE::RGBA32Float:
            return 4U;
        case TextureFormatUVE::Depth32Float:
        case TextureFormatUVE::Depth24Stencil8:
        case TextureFormatUVE::BC1RGB:
        case TextureFormatUVE::BC3RGBA:
        case TextureFormatUVE::BC7RGBA:
        case TextureFormatUVE::ETC2RGB8:
        case TextureFormatUVE::ETC2RGBA8:
        case TextureFormatUVE::ASTC4x4RGBA:
            return 0U;
    }
    return 0U;
}

[[nodiscard]] inline constexpr bool IsDepthStencilFormatUVE(const TextureFormatUVE format) noexcept {
    return format == TextureFormatUVE::Depth24Stencil8;
}

/// Describes a GPU texture to create via IRenderDeviceUVE::CreateTextureUVE(). `mipLevels` is
/// the total number of levels including level 0; it must not exceed the complete chain for the
/// base dimensions. Color assets may provide tightly concatenated texel or block bytes for every level.
/// Texture dimensionality (Tier 2.3). `Texture2D` is the only pre-2.3 shape; arrays stack
/// `arrayLayers` same-sized 2D slices (shadow cascades, texture atlases), and cubemaps are the
/// 6-face sampling shape (reflection probes, sky). Cube faces upload and attach in
/// `CubemapFaceUVE` order (+X,-X,+Y,-Y,+Z,-Z), which matches the GL/VK hardware face order.
enum class TextureTypeUVE : std::uint8_t { Texture2D, Texture2DArray, Cubemap };

[[nodiscard]] constexpr bool IsTextureTypeValidUVE(const TextureTypeUVE type) noexcept {
    switch (type) {
        case TextureTypeUVE::Texture2D:
        case TextureTypeUVE::Texture2DArray:
        case TextureTypeUVE::Cubemap:
            return true;
    }
    return false;
}

struct TextureDescUVE {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormatUVE format = TextureFormatUVE::RGBA8Unorm;
    std::uint32_t mipLevels = 1;
    // Appended after existing members to preserve aggregate initialization of legacy descriptors.
    TextureColorSpaceUVE colorSpace = TextureColorSpaceUVE::Linear;
    // Tier 2.3: dimensionality. Defaults describe the only pre-2.3 shape (one 2D slice).
    TextureTypeUVE type = TextureTypeUVE::Texture2D;
    std::uint32_t arrayLayers = 1;
};

/// How a sampler reads within one mip level. Shared by magnification and minification: GL and
/// Vulkan both take the same NEAREST/LINEAR choice on each axis, so one enum serves both.
enum class SamplerFilterUVE : std::uint8_t { Point, Linear };

[[nodiscard]] constexpr bool IsSamplerFilterValidUVE(const SamplerFilterUVE filter) noexcept {
    switch (filter) {
        case SamplerFilterUVE::Point:
        case SamplerFilterUVE::Linear:
            return true;
    }
    return false;
}

/// How a sampler blends (or refuses to blend) between mip levels. `None` pins sampling to
/// level 0 — the honest choice for single-level textures like shadow maps and render targets.
enum class SamplerMipModeUVE : std::uint8_t { None, Point, Linear };

[[nodiscard]] constexpr bool IsSamplerMipModeValidUVE(const SamplerMipModeUVE mode) noexcept {
    switch (mode) {
        case SamplerMipModeUVE::None:
        case SamplerMipModeUVE::Point:
        case SamplerMipModeUVE::Linear:
            return true;
    }
    return false;
}

/// How a sampler resolves UVs outside [0, 1]. No clamp-to-border in v1: a border needs a
/// border-COLOR knob the RHI was deliberately not given (follow-up alongside 2.3 cubemaps,
/// whose seams are the first real border consumer).
enum class SamplerWrapUVE : std::uint8_t { ClampToEdge, Repeat, MirroredRepeat };

[[nodiscard]] constexpr bool IsSamplerWrapValidUVE(const SamplerWrapUVE wrap) noexcept {
    switch (wrap) {
        case SamplerWrapUVE::ClampToEdge:
        case SamplerWrapUVE::Repeat:
        case SamplerWrapUVE::MirroredRepeat:
            return true;
    }
    return false;
}

/// Describes a sampler object to create via IRenderDeviceUVE::CreateSamplerUVE(). Filtering,
/// mip blending, wrapping, and anisotropy live here — NOT on the texture — so one texture can
/// be sampled differently from different slots (a shadow map point-sampled by the PCF pass
/// while a debug view linear-samples the same target). Defaults reproduce the pre-2.2
/// hardcoded behavior (linear/trilinear/clamp), so pipelines that never bind a sampler render
/// byte-identical pixels to before.
struct SamplerDescUVE {
    SamplerFilterUVE magFilter = SamplerFilterUVE::Linear;
    SamplerFilterUVE minFilter = SamplerFilterUVE::Linear;
    SamplerMipModeUVE mipMode = SamplerMipModeUVE::Linear;
    SamplerWrapUVE wrapU = SamplerWrapUVE::ClampToEdge;
    SamplerWrapUVE wrapV = SamplerWrapUVE::ClampToEdge;
    SamplerWrapUVE wrapW = SamplerWrapUVE::ClampToEdge;
    /// Maximum anisotropy; 1.0 disables. Must be finite and >= 1.0 — backends clamp values
    /// above their device limit (with a warn-once), but below-1.0/NaN is a malformed desc.
    float maxAnisotropy = 1.0F;
};

[[nodiscard]] inline bool IsSamplerDescValidUVE(const SamplerDescUVE& desc) noexcept {
    return IsSamplerFilterValidUVE(desc.magFilter) && IsSamplerFilterValidUVE(desc.minFilter) &&
           IsSamplerMipModeValidUVE(desc.mipMode) && IsSamplerWrapValidUVE(desc.wrapU) &&
           IsSamplerWrapValidUVE(desc.wrapV) && IsSamplerWrapValidUVE(desc.wrapW) &&
           std::isfinite(desc.maxAnisotropy) && desc.maxAnisotropy >= 1.0F;
}

struct TextureMipExtentUVE {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

[[nodiscard]] inline constexpr std::uint64_t TextureFormatBytesPerPixelUVE(
    const TextureFormatUVE format) noexcept {
    const TextureFormatBlockInfoUVE block = GetTextureFormatBlockInfoUVE(format);
    return !block.compressed && block.width == 1U && block.height == 1U ? block.bytes : 0U;
}

[[nodiscard]] inline bool CalculateTextureMipByteCountUVE(const TextureFormatUVE format,
                                                           const std::uint32_t width,
                                                           const std::uint32_t height,
                                                           std::uint64_t& outBytes) noexcept {
    const TextureFormatBlockInfoUVE block = GetTextureFormatBlockInfoUVE(format);
    if (width == 0U || height == 0U || block.width == 0U || block.height == 0U || block.bytes == 0U) {
        return false;
    }
    const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + block.width - 1U) / block.width;
    const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + block.height - 1U) / block.height;
    if (blocksX > std::numeric_limits<std::uint64_t>::max() / blocksY) {
        return false;
    }
    const std::uint64_t blockCount = blocksX * blocksY;
    if (blockCount > std::numeric_limits<std::uint64_t>::max() / block.bytes) {
        return false;
    }
    outBytes = blockCount * block.bytes;
    return outBytes <= std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] inline constexpr TextureMipExtentUVE GetTextureMipExtentUVE(
    std::uint32_t width, std::uint32_t height, std::uint32_t level) noexcept {
    while (level > 0U && (width > 1U || height > 1U)) {
        width = width > 1U ? width / 2U : 1U;
        height = height > 1U ? height / 2U : 1U;
        --level;
    }
    return {width, height};
}

[[nodiscard]] inline constexpr std::uint32_t MaximumTextureMipLevelCountUVE(
    std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return 0U;
    }
    std::uint32_t count = 1U;
    while (width > 1U || height > 1U) {
        width = width > 1U ? width / 2U : 1U;
        height = height > 1U ? height / 2U : 1U;
        ++count;
    }
    return count;
}

/// Computes the byte count for a tightly packed level-0..N upload. The output is unchanged on
/// failure; mips beyond the complete dimension-derived chain and multi-level depth textures are
/// rejected because this slice does not expose depth-mip attachment selection.
[[nodiscard]] inline bool CalculateTextureUploadByteCountUVE(const TextureDescUVE& desc,
                                                              std::uint64_t& outBytes) noexcept {
    const TextureFormatBlockInfoUVE block = GetTextureFormatBlockInfoUVE(desc.format);
    if (desc.width == 0U || desc.height == 0U || desc.mipLevels == 0U || block.bytes == 0U ||
        desc.mipLevels > MaximumTextureMipLevelCountUVE(desc.width, desc.height) ||
        (IsDepthFormatUVE(desc.format) && desc.mipLevels > 1U)) {
        return false;
    }
    // Tier 2.3 dimensionality rules. Layer-count CAPS are device properties, so backends
    // enforce them at creation; these shape rules hold on every device. (Cube arrays — 6N
    // layers with cube sampling — are a follow-up; v1 cubes are exactly one cube.)
    switch (desc.type) {
        case TextureTypeUVE::Texture2D:
            if (desc.arrayLayers != 1U) {
                return false;
            }
            break;
        case TextureTypeUVE::Texture2DArray:
            if (desc.arrayLayers == 0U) {
                return false;
            }
            break;
        case TextureTypeUVE::Cubemap:
            if (desc.width != desc.height || desc.arrayLayers != 6U) {
                return false;
            }
            break;
        default:
            return false;
    }

    std::uint64_t totalBytes = 0U;
    TextureMipExtentUVE extent{desc.width, desc.height};
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        std::uint64_t levelBytes = 0U;
        if (!CalculateTextureMipByteCountUVE(desc.format, extent.width, extent.height, levelBytes) ||
            levelBytes > std::numeric_limits<std::uint64_t>::max() - totalBytes) {
            return false;
        }
        totalBytes += levelBytes;
        extent.width = extent.width > 1U ? extent.width / 2U : 1U;
        extent.height = extent.height > 1U ? extent.height / 2U : 1U;
    }
    // Every layer carries the full mip chain (level-major upload: all layers of L0, then L1…).
    if (desc.arrayLayers > 0U &&
        totalBytes > std::numeric_limits<std::uint64_t>::max() / desc.arrayLayers) {
        return false;
    }
    totalBytes *= desc.arrayLayers;
    if (totalBytes > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    outBytes = totalBytes;
    return true;
}

/// Validates a descriptor and optional tightly concatenated upload before a backend allocates a
/// GPU resource. Empty data is legal only for one-level render targets; a non-empty upload must
/// exactly match the sum of all declared levels. The helper performs no allocation or backend calls.
[[nodiscard]] inline bool ValidateTextureUploadUVE(const TextureDescUVE& desc,
                                                    const std::span<const std::byte> initialData) noexcept {
    if (desc.width == 0U || desc.height == 0U || desc.mipLevels == 0U) {
        return false;
    }
    switch (desc.colorSpace) {
        case TextureColorSpaceUVE::Linear:
            break;
        case TextureColorSpaceUVE::Srgb:
            if (!IsTextureFormatSrgbCapableUVE(desc.format)) {
                return false;
            }
            break;
        default:
            return false;
    }
    std::uint64_t expectedBytes = 0U;
    if (!CalculateTextureUploadByteCountUVE(desc, expectedBytes) ||
        (initialData.empty() &&
         (desc.mipLevels > 1U || IsTextureFormatCompressedUVE(desc.format)))) {
        return false;
    }
    return initialData.empty() || initialData.size() == static_cast<std::size_t>(expectedBytes);
}

/// Which programmable stage a ShaderDescUVE belongs to. `Compute` is real since the M5a
/// compute slice: CreateShaderUVE accepts compute-stage shaders and CreateComputePipelineUVE
/// links exactly one of them. `Geometry` can be attached through PipelineDescUVE's optional
/// geometryShader slot. Query RenderDeviceCapabilitiesUVE::geometryShaders before using it:
/// Null models it, desktop GL executes it, and unsupported backends reject it explicitly.
/// Append-only: never renumber existing values, since ShaderStageUVE crosses the RHI boundary.
enum class ShaderStageUVE : std::uint8_t { Vertex, Fragment, Compute, Geometry };

[[nodiscard]] constexpr bool IsShaderStageValidUVE(const ShaderStageUVE stage) noexcept {
    switch (stage) {
        case ShaderStageUVE::Vertex:
        case ShaderStageUVE::Fragment:
        case ShaderStageUVE::Compute:
        case ShaderStageUVE::Geometry:
            return true;
    }
    return false;
}

/// Describes a shader to create via IRenderDeviceUVE::CreateShaderUVE(). `sourceCode` is stored
/// and validated as-is; NullRenderDeviceUVE never compiles it — no glslang/shaderc/DXC is
/// available in this environment (see docs/CODING_STANDARDS.md).
struct ShaderDescUVE {
    ShaderStageUVE stage = ShaderStageUVE::Vertex;
    std::string sourceCode;
    std::string entryPointName = "main";
};

/// The shape of one vertex attribute inside a PipelineDescUVE's vertex layout. The three
/// `Float*` entries are 32-bit floats; the packed entries reach the shader as floats too
/// (`UByte4Norm` / `Short2Norm` normalised to [0,1] / [-1,1], `Half2` / `Half4` converted),
/// except the two integer entries `UShort4` / `UInt4`, which stay integers and must be declared
/// `uvec4` in the shader — joint indices, not weights. Each entry's byte width comes from
/// `GetVertexAttributeFormatByteSizeUVE`; backends fetch every one through their native
/// attribute-pointer / VkFormat path, so a layout never needs CPU repacking.
enum class VertexAttributeFormatUVE : std::uint8_t {
    Float2,
    Float3,
    Float4,
    UByte4Norm, ///< 4 x uint8, normalised (vertex colours)
    UShort4,    ///< 4 x uint16, integer (joint indices)
    UInt4,      ///< 4 x uint32, integer (joint indices)
    Short2Norm, ///< 2 x int16, normalised (packed UVs / octahedral normals)
    Half2,      ///< 2 x float16
    Half4,      ///< 4 x float16
};

/// Byte width of one attribute of `format`; 0 for unknown values.
[[nodiscard]] constexpr std::uint32_t GetVertexAttributeFormatByteSizeUVE(const VertexAttributeFormatUVE format) noexcept {
    switch (format) {
        case VertexAttributeFormatUVE::Float2:
            return 8U;
        case VertexAttributeFormatUVE::Float3:
            return 12U;
        case VertexAttributeFormatUVE::Float4:
            return 16U;
        case VertexAttributeFormatUVE::UByte4Norm:
            return 4U;
        case VertexAttributeFormatUVE::UShort4:
            return 8U;
        case VertexAttributeFormatUVE::UInt4:
            return 16U;
        case VertexAttributeFormatUVE::Short2Norm:
            return 4U;
        case VertexAttributeFormatUVE::Half2:
            return 4U;
        case VertexAttributeFormatUVE::Half4:
            return 8U;
    }
    return 0U;
}

/// Component count of `format` (2, 3 or 4); 0 for unknown values.
[[nodiscard]] constexpr std::uint32_t GetVertexAttributeFormatComponentCountUVE(
    const VertexAttributeFormatUVE format) noexcept {
    switch (format) {
        case VertexAttributeFormatUVE::Float2:
        case VertexAttributeFormatUVE::Short2Norm:
        case VertexAttributeFormatUVE::Half2:
            return 2U;
        case VertexAttributeFormatUVE::Float3:
            return 3U;
        case VertexAttributeFormatUVE::Float4:
        case VertexAttributeFormatUVE::UByte4Norm:
        case VertexAttributeFormatUVE::UShort4:
        case VertexAttributeFormatUVE::UInt4:
        case VertexAttributeFormatUVE::Half4:
            return 4U;
    }
    return 0U;
}

/// True for the formats a shader must declare as an integer vector (`uvec4`): the fetch skips
/// float conversion entirely.
[[nodiscard]] constexpr bool IsVertexAttributeFormatIntegerUVE(const VertexAttributeFormatUVE format) noexcept {
    return format == VertexAttributeFormatUVE::UShort4 || format == VertexAttributeFormatUVE::UInt4;
}

/// One vertex attribute (e.g. "position", "normal", "uv") within a pipeline's vertex layout.
struct VertexAttributeUVE {
    std::string semanticName;
    VertexAttributeFormatUVE format = VertexAttributeFormatUVE::Float3;
    std::uint32_t offset = 0;
};

[[nodiscard]] constexpr bool IsVertexAttributeFormatValidUVE(const VertexAttributeFormatUVE format) noexcept {
    return GetVertexAttributeFormatByteSizeUVE(format) != 0U;
}

[[nodiscard]] inline bool IsVertexLayoutValidUVE(const std::span<const VertexAttributeUVE> vertexLayout) noexcept {
    for (const VertexAttributeUVE& attribute : vertexLayout) {
        if (!IsVertexAttributeFormatValidUVE(attribute.format)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool IsVertexLayoutWithinStrideUVE(const std::span<const VertexAttributeUVE> vertexLayout,
                                                         const std::uint32_t vertexStride) noexcept {
    for (const VertexAttributeUVE& attribute : vertexLayout) {
        const std::uint32_t attributeByteWidth = GetVertexAttributeFormatByteSizeUVE(attribute.format);
        if (attribute.offset > vertexStride || attributeByteWidth > vertexStride - attribute.offset) {
            return false;
        }
    }
    return true;
}

/// How a pipeline's bound vertex/index buffers are assembled into primitives. `Triangles` is
/// the mesh default; `Lines` assembles consecutive vertex/index pairs as 1px line segments for
/// the debug renderer (Tier 2.8). `Points` rasterises one square per vertex whose side is the
/// vertex shader's `gl_PointSize` (GL enables program point size for such pipelines; Vulkan
/// always reads the built-in); `LineStrip` / `TriangleStrip` connect consecutive vertices
/// (N vertices → N-1 segments / N-2 triangles, no primitive restart). There is no fan mode, no
/// adjacency and no line-width state. Vertices that do not complete a primitive are dropped by
/// the driver — `GetPrimitiveCountUVE` / `IsVertexCountCompleteForTopologyUVE` tell a caller
/// how a count will be consumed before it draws.
enum class PrimitiveTopologyUVE : std::uint8_t { Triangles, Lines, Points, LineStrip, TriangleStrip };

[[nodiscard]] constexpr bool IsPrimitiveTopologyValidUVE(const PrimitiveTopologyUVE topology) noexcept {
    switch (topology) {
        case PrimitiveTopologyUVE::Triangles:
        case PrimitiveTopologyUVE::Lines:
        case PrimitiveTopologyUVE::Points:
        case PrimitiveTopologyUVE::LineStrip:
        case PrimitiveTopologyUVE::TriangleStrip:
            return true;
    }
    return false;
}

/// Number of primitives `vertexCount` vertices (or indices) assemble into under `topology`.
/// Unknown topologies yield 0.
[[nodiscard]] constexpr std::uint32_t GetPrimitiveCountUVE(const PrimitiveTopologyUVE topology,
                                                          const std::uint32_t vertexCount) noexcept {
    switch (topology) {
        case PrimitiveTopologyUVE::Triangles:
            return vertexCount / 3U;
        case PrimitiveTopologyUVE::Lines:
            return vertexCount / 2U;
        case PrimitiveTopologyUVE::Points:
            return vertexCount;
        case PrimitiveTopologyUVE::LineStrip:
            return vertexCount >= 2U ? vertexCount - 1U : 0U;
        case PrimitiveTopologyUVE::TriangleStrip:
            return vertexCount >= 3U ? vertexCount - 2U : 0U;
    }
    return 0U;
}

/// True when every one of `vertexCount` vertices takes part in a primitive under `topology`: a
/// multiple of 3 / 2 for the list modes, anything for points, and 0 or at least 2 / 3 for the
/// strips. Zero is complete (an empty draw). Unknown topologies are never complete.
[[nodiscard]] constexpr bool IsVertexCountCompleteForTopologyUVE(const PrimitiveTopologyUVE topology,
                                                                const std::uint32_t vertexCount) noexcept {
    switch (topology) {
        case PrimitiveTopologyUVE::Triangles:
            return vertexCount % 3U == 0U;
        case PrimitiveTopologyUVE::Lines:
            return vertexCount % 2U == 0U;
        case PrimitiveTopologyUVE::Points:
            return true;
        case PrimitiveTopologyUVE::LineStrip:
            return vertexCount == 0U || vertexCount >= 2U;
        case PrimitiveTopologyUVE::TriangleStrip:
            return vertexCount == 0U || vertexCount >= 3U;
    }
    return false;
}

/// Explicit color blending policy for a pipeline. SourceAlphaOver is the conventional premultiplied-
/// independent source-alpha composite used by editor-only visual overlays; Additive (destination +=
/// source, `glBlendFunc(GL_ONE, GL_ONE)`) composites the Phase 2b bloom blur result onto the HDR
/// scene color; Multiply (destination *= source, `glBlendFunc(GL_DST_COLOR, GL_ZERO)`) composites
/// the Phase 2b SSAO occlusion term the same way. All three remain opt-in so ordinary scene and
/// tone-mapping pipelines preserve their existing opaque behavior.
enum class PipelineBlendModeUVE : std::uint8_t { Opaque, SourceAlphaOver, Additive, Multiply };

[[nodiscard]] constexpr bool IsPipelineBlendModeValidUVE(const PipelineBlendModeUVE blendMode) noexcept {
    switch (blendMode) {
        case PipelineBlendModeUVE::Opaque:
        case PipelineBlendModeUVE::SourceAlphaOver:
        case PipelineBlendModeUVE::Additive:
        case PipelineBlendModeUVE::Multiply:
            return true;
    }
    return false;
}

/// Which triangle faces the rasterizer discards. `None` (no culling) is the default both
/// backends already implement: GL never enables `GL_CULL_FACE` for RHI pipelines, and Vulkan
/// hardcodes `VK_CULL_MODE_NONE`.
enum class CullModeUVE : std::uint8_t { None, Front, Back, FrontAndBack };

[[nodiscard]] constexpr bool IsCullModeValidUVE(const CullModeUVE mode) noexcept {
    switch (mode) {
        case CullModeUVE::None:
        case CullModeUVE::Front:
        case CullModeUVE::Back:
        case CullModeUVE::FrontAndBack:
            return true;
    }
    return false;
}

/// Which vertex winding counts as front-facing for culling. Counter-clockwise is the default
/// both backends already implement (and the winding glTF importers produce).
enum class FrontFaceUVE : std::uint8_t { CounterClockwise, Clockwise };

[[nodiscard]] constexpr bool IsFrontFaceValidUVE(const FrontFaceUVE face) noexcept {
    switch (face) {
        case FrontFaceUVE::CounterClockwise:
        case FrontFaceUVE::Clockwise:
            return true;
    }
    return false;
}

/// How triangles fill their pixels. `Fill` is the default both backends already implement.
/// There is no point mode: points need a point-size state neither backend was given.
enum class FillModeUVE : std::uint8_t { Fill, Wireframe };

[[nodiscard]] constexpr bool IsFillModeValidUVE(const FillModeUVE mode) noexcept {
    switch (mode) {
        case FillModeUVE::Fill:
        case FillModeUVE::Wireframe:
            return true;
    }
    return false;
}

/// How an incoming fragment's depth compares against the depth buffer when the depth test is
/// on. Ordered to match `VkCompareOp`'s numbering (Never=0 … Always=7) — backends still map
/// explicitly, never by static_cast, so the order is a reading convenience, not a contract.
///
/// The default is `Less`, matching what the GL backend has always done (it never calls
/// `glDepthFunc`, so the context default rules). Note this CHANGES the Vulkan backend, which
/// hardcoded `LESS_OR_EQUAL` under a comment claiming it matched GL — it did not.
enum class DepthCompareUVE : std::uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

[[nodiscard]] constexpr bool IsDepthCompareValidUVE(const DepthCompareUVE compare) noexcept {
    switch (compare) {
        case DepthCompareUVE::Never:
        case DepthCompareUVE::Less:
        case DepthCompareUVE::Equal:
        case DepthCompareUVE::LessOrEqual:
        case DepthCompareUVE::Greater:
        case DepthCompareUVE::NotEqual:
        case DepthCompareUVE::GreaterOrEqual:
        case DepthCompareUVE::Always:
            return true;
    }
    return false;
}

/// What a stencil operation does to the stored stencil value.
enum class StencilOpUVE : std::uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

[[nodiscard]] constexpr bool IsStencilOpValidUVE(const StencilOpUVE op) noexcept {
    switch (op) {
        case StencilOpUVE::Keep:
        case StencilOpUVE::Zero:
        case StencilOpUVE::Replace:
        case StencilOpUVE::IncrementClamp:
        case StencilOpUVE::DecrementClamp:
        case StencilOpUVE::Invert:
        case StencilOpUVE::IncrementWrap:
        case StencilOpUVE::DecrementWrap:
            return true;
    }
    return false;
}

/// One face's stencil test. The comparison is `(reference & compareMask) OP (stored & compareMask)`;
/// masks and reference are 8-bit because Depth24Stencil8 is the stencil-carrying format.
struct StencilFaceStateUVE {
    DepthCompareUVE compare = DepthCompareUVE::Always;
    StencilOpUVE stencilFail = StencilOpUVE::Keep;
    StencilOpUVE depthFail = StencilOpUVE::Keep;
    StencilOpUVE depthPass = StencilOpUVE::Keep;
    std::uint32_t compareMask = 0xFFU;
    std::uint32_t writeMask = 0xFFU;
    std::uint32_t reference = 0U;
};

[[nodiscard]] constexpr bool IsStencilFaceStateValidUVE(const StencilFaceStateUVE& state) noexcept {
    return IsDepthCompareValidUVE(state.compare) && IsStencilOpValidUVE(state.stencilFail) &&
           IsStencilOpValidUVE(state.depthFail) && IsStencilOpValidUVE(state.depthPass) &&
           state.compareMask <= 0xFFU && state.writeMask <= 0xFFU && state.reference <= 0xFFU;
}

/// Front/back stencil state. Disabled is the legacy behavior and ignores both faces. Enabled
/// state is meaningful only for a pass whose depth attachment uses Depth24Stencil8 (or a native
/// default surface that actually has stencil bits); backends reject malformed enum/mask values.
struct StencilStateUVE {
    bool enabled = false;
    StencilFaceStateUVE front;
    StencilFaceStateUVE back;
};

[[nodiscard]] constexpr bool IsStencilStateValidUVE(const StencilStateUVE& state) noexcept {
    return IsStencilFaceStateValidUVE(state.front) && IsStencilFaceStateValidUVE(state.back);
}

/// Describes a pipeline state object to create via IRenderDeviceUVE::CreatePipelineUVE().
/// Fixed-function state remains deliberately small; blending is explicit because editor visual
/// composition is the first proven consumer rather than an implicit global OpenGL side effect.
struct PipelineDescUVE {
    ShaderHandleUVE vertexShader;
    ShaderHandleUVE fragmentShader;
    std::vector<VertexAttributeUVE> vertexLayout;
    PrimitiveTopologyUVE topology = PrimitiveTopologyUVE::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendModeUVE blendMode = PipelineBlendModeUVE::Opaque;

    /// Byte distance between consecutive vertices in the bound vertex buffer — required by a real
    /// backend to interleave `vertexLayout`'s attributes correctly (e.g. GL's
    /// `glVertexAttribPointer` stride parameter). `0` (the default) is only ever valid for
    /// `NullRenderDeviceUVE`, which ignores this field like every other one it merely bookkeeps.
    std::uint32_t vertexStride = 0;

    // Appended after existing members to preserve aggregate initialization of legacy descriptors.
    // Defaults reproduce what both backends already did — except depthCompare, where Less
    // matches GL and moves Vulkan off its hardcoded LessOrEqual (see DepthCompareUVE).
    CullModeUVE cullMode = CullModeUVE::None;
    FrontFaceUVE frontFace = FrontFaceUVE::CounterClockwise;
    FillModeUVE fillMode = FillModeUVE::Fill;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0F;
    float depthBiasSlopeFactor = 0.0F;
    DepthCompareUVE depthCompare = DepthCompareUVE::Less;
    StencilStateUVE stencil;

    /// Optional live Geometry-stage shader. Invalid means the legacy vertex/fragment pair.
    /// Appended for aggregate/source compatibility. Unsupported backends must reject a supplied
    /// handle, never silently omit it. Shader objects need only live through pipeline creation;
    /// shader-specific input/output topology and limits are validated by the native linker.
    ShaderHandleUVE geometryShader = kInvalidShaderHandleUVE;
};

/// Describes a COMPUTE pipeline to create via IRenderDeviceUVE::CreateComputePipelineUVE()
/// (M5a compute slice). Deliberately its own struct: a compute pipeline has no vertex layout,
/// no fixed-function state, and exactly one shader stage, so folding it into PipelineDescUVE
/// would make every graphics field a lie. The resulting PipelineHandleUVE lives in the SAME
/// handle domain as graphics pipelines — BindPipelineUVE binds either kind, and the backend
/// selects the correct bind point internally (Vulkan GRAPHICS vs COMPUTE).
struct ComputePipelineDescUVE {
    /// Must reference a live shader created with ShaderStageUVE::Compute. An invalid handle,
    /// or a shader of any other stage, is a loud creation failure on every real backend.
    ShaderHandleUVE computeShader;
};

/// Fixed-function state accompanying a pre-compiled GL program binary passed to
/// IRenderDeviceUVE::CreatePipelineFromBinaryUVE() (Increment 21's program-binary cache). Deliberately
/// has no shader-handle fields — loading from binary skips shader-object attachment entirely, so
/// there is nothing analogous to PipelineDescUVE::vertexShader/fragmentShader here.
struct PipelineBinaryDescUVE {
    std::vector<VertexAttributeUVE> vertexLayout;
    std::uint32_t vertexStride = 0;
    PrimitiveTopologyUVE topology = PrimitiveTopologyUVE::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendModeUVE blendMode = PipelineBlendModeUVE::Opaque;

    // Same appended-rasterizer-state tail as PipelineDescUVE: a cache-loaded pipeline must be
    // able to express everything a compiled one can, or cache hits would silently drop state.
    CullModeUVE cullMode = CullModeUVE::None;
    FrontFaceUVE frontFace = FrontFaceUVE::CounterClockwise;
    FillModeUVE fillMode = FillModeUVE::Fill;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0F;
    float depthBiasSlopeFactor = 0.0F;
    DepthCompareUVE depthCompare = DepthCompareUVE::Less;
    StencilStateUVE stencil;
};

/// What happens to a render pass attachment's existing contents at the start of the pass.
enum class LoadOpUVE : std::uint8_t { Clear, Load, DontCare };

[[nodiscard]] constexpr bool IsLoadOpValidUVE(const LoadOpUVE loadOp) noexcept {
    switch (loadOp) {
        case LoadOpUVE::Clear:
        case LoadOpUVE::Load:
        case LoadOpUVE::DontCare:
            return true;
    }
    return false;
}

/// What happens to a render pass attachment's contents at the END of the pass (Tier 2.5).
/// `Store` keeps the rendered bits for later sampling — the only pre-2.5 behavior, since every
/// backend stored unconditionally. `DontCare` lets the driver discard them (tiler efficiency);
/// the contents become undefined. There is deliberately no Resolve option: multisampled
/// textures do not exist in the RHI yet.
enum class StoreOpUVE : std::uint8_t { Store, DontCare };

[[nodiscard]] constexpr bool IsStoreOpValidUVE(const StoreOpUVE storeOp) noexcept {
    switch (storeOp) {
        case StoreOpUVE::Store:
        case StoreOpUVE::DontCare:
            return true;
    }
    return false;
}

/// Describes one BeginRenderPassUVE() call: which color/depth textures are rendered into and how
/// they're cleared. `depthAttachment` may be `kInvalidTextureHandleUVE` for a color-only pass.
/// `colorAttachment` itself may also be `kInvalidTextureHandleUVE`, meaning "render into the
/// backend's default framebuffer" (the window's back buffer) rather than an offscreen texture —
/// a backend-specific meaning `GlRenderDeviceUVE` acts on (binding GL framebuffer object `0`);
/// `NullRenderDeviceUVE` ignores it like every other field it bookkeeps without interpreting.
/// A pixel-space sub-rectangle of a render pass's target (attachment or, for the default
/// framebuffer, the window). Introduced for Phase 3's ViewportManagerUVE, where multiple panes
/// render into different regions of the same window in a single frame - but it's a general
/// RenderPassDescUVE capability, not split-view-specific, since any caller may want to render into
/// less than the full attachment.
/// Implemented as Math::RectIntUVE under an RHI name: the old four-uint32 struct retired in
/// favour of the one shared integer-rect type. Position/size are signed now; backends reject
/// a negative or zero-size rect through the same viewport fit-check as an out-of-bounds one.
using ViewportRectUVE = Math::RectIntUVE;

/// How many color locations past location 0 a pass may target (Tier 2.4): locations 1..3, so
/// four total — inside every GL (MAX_DRAW_BUFFERS >= 8) and Vulkan (maxColorAttachments, queried
/// at init) device, and no G-buffer needs more.
inline constexpr std::uint32_t kExtraColorAttachmentCountUVE = 3U;

/// One of locations 1..3 of a multi-target pass (Tier 2.4). Location 0 keeps the legacy
/// top-level fields below (colorAttachment/colorLoadOp/clearColor/...), so single-target passes
/// — every pass that exists today — compile and behave exactly as before. An extra slot whose
/// target is invalid is simply unused, and gaps are NOT allowed: location 2 set while location 1
/// is invalid is malformed, and backends reject it like any other malformed pass descriptor.
struct ColorAttachmentUVE {
    TextureHandleUVE target;
    std::uint32_t layer = 0;
    LoadOpUVE loadOp = LoadOpUVE::Clear;
    StoreOpUVE storeOp = StoreOpUVE::Store;
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
};

struct RenderPassDescUVE {
    TextureHandleUVE colorAttachment;
    TextureHandleUVE depthAttachment;
    LoadOpUVE colorLoadOp = LoadOpUVE::Clear;
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    LoadOpUVE depthLoadOp = LoadOpUVE::Clear;
    float clearDepth = 1.0F;
    /// When set, the pass's GL viewport is this pixel rect instead of the full attachment/window
    /// size. Must fit entirely within the target: a zero-size, negative, or out-of-bounds rect is
    /// rejected the same way as any other malformed pass descriptor, rather than silently
    /// clamped. See Math::ContainsUVE for the exact fit-check the backends apply.
    std::optional<ViewportRectUVE> viewportOverride;
    // Tier 2.3: which layer of an array/cube attachment this pass renders into (cube faces are
    // layers 0-5 in CubemapFaceUVE order). Must be 0 for Texture2D attachments and below
    // `arrayLayers` otherwise; backends reject out-of-range layers like any other malformed
    // pass descriptor. Defaults preserve the only pre-2.3 behavior (layer 0 of a 2D target).
    std::uint32_t colorLayer = 0;
    std::uint32_t depthLayer = 0;
    // Tier 2.5: end-of-pass behavior for location 0 and depth. Defaults preserve the only
    // pre-2.5 behavior (everything stored).
    StoreOpUVE colorStoreOp = StoreOpUVE::Store;
    StoreOpUVE depthStoreOp = StoreOpUVE::Store;
    // Tier 2.4: locations 1..3 (see ColorAttachmentUVE). Empty by default: a single target.
    std::array<ColorAttachmentUVE, kExtraColorAttachmentCountUVE> extraColorAttachments{};
    /// Used only when depthLoadOp is Clear and the depth attachment carries stencil bits.
    /// The existing depth load/store operations govern both aspects of a combined attachment.
    std::uint32_t clearStencil = 0U;
};

} // namespace UVE::Render
