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

#include "rect_int.h"
#include "shader_handle.h"
#include "texture_handle.h"

namespace UVE::Render {

/// What a BufferDesc-created buffer is used for. `Storage` (Vulkan M2f) is a shader-storage
/// buffer (SSBO): arbitrary read/write GPU data bound via ICommandBuffer::BindStorageBuffer
/// and read/written from graphics-stage shaders (`readonly buffer`/`buffer` blocks) — and since
/// the M5a compute slice, from compute shaders too (the classic SSBO producer/consumer pattern:
/// dispatch writes, draws read).
/// `IndirectStorage` (CS7) is a buffer holding DrawIndexedIndirectCommand records for
/// ICommandBuffer::DrawIndexedIndirect() - and, deliberately, an SSBO at the same time.
/// A separate write-only Indirect usage was considered and rejected: the entire point of indirect
/// draw in this engine is that a COMPUTE dispatch writes the parameters (that is what lets GPU
/// culling stop round-tripping through the CPU), so a usage the compute stage cannot bind would
/// serve nothing the CPU could not already do with DrawIndexed. Backends therefore create it
/// with both capabilities - VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | STORAGE_BUFFER_BIT on Vulkan,
/// and on GL it binds to both GL_DRAW_INDIRECT_BUFFER and GL_SHADER_STORAGE_BUFFER - and
/// ReadbackBuffer covers it exactly as it covers Storage.
enum class BufferUsage : std::uint8_t { Vertex, Index, Uniform, Storage, IndirectStorage };

[[nodiscard]] constexpr bool IsBufferUsageValid(const BufferUsage usage) noexcept {
    switch (usage) {
        case BufferUsage::Vertex:
        case BufferUsage::Index:
        case BufferUsage::Uniform:
        case BufferUsage::Storage:
        case BufferUsage::IndirectStorage:
            return true;
    }
    return false;
}

/// True for the usages a shader-storage binding accepts. IndirectStorage is deliberately included:
/// it IS an SSBO, and the compute kernel that fills it binds it as one.
[[nodiscard]] constexpr bool IsStorageBindableUsage(const BufferUsage usage) noexcept {
    return usage == BufferUsage::Storage || usage == BufferUsage::IndirectStorage;
}

/// One indexed indirect draw, laid out to match what every backend's indirect draw reads straight
/// out of GPU memory: VkDrawIndexedIndirectCommand and GL's DrawElementsIndirectCommand are the
/// same five consecutive 32-bit fields in the same order, which is what makes one struct portable
/// here. The layout is fixed by those APIs, so it is asserted below rather than merely intended.
struct DrawIndexedIndirectCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0;
};

static_assert(sizeof(DrawIndexedIndirectCommand) == 20U,
              "DrawIndexedIndirectCommand must be the five tightly packed 32-bit fields both "
              "Vulkan and GL read directly out of buffer memory");
static_assert(alignof(DrawIndexedIndirectCommand) == alignof(std::uint32_t),
              "DrawIndexedIndirectCommand must not acquire padding the GPU layout lacks");

/// Describes a GPU buffer to create via IRenderDevice::CreateBuffer(). A buffer must have
/// positive byte capacity; zero-sized GL buffers are not useful to any supported draw/update path.
/// Where a buffer's memory should live and which side writes it. A placement request, never a
/// behavioural switch: UpdateBuffer and ReadbackBuffer keep the same copy-on-call contract
/// for every value, and a backend that cannot offer the placement picks one that satisfies the
/// usage. `Auto` keeps each backend's usage-derived policy (Vulkan today: vertex/index
/// device-local behind a staging copy, uniform/storage/indirect host-visible and persistently
/// mapped; GL has one memory and folds the hint into glBufferData's usage token).
enum class BufferMemory : std::uint8_t {
    Auto = 0,
    /// GPU-resident; host updates and readbacks travel through staging copies on Vulkan.
    DeviceLocal,
    /// Host-visible and CPU-written, usually often: streamed vertices, uniform rings.
    Upload,
    /// Host-visible and GPU-written for the CPU to read (cached memory when the device offers it).
    Readback,
};

/// How often the host rewrites the buffer after creation. glBufferData's STATIC/DYNAMIC/STREAM
/// token on GL; recorded (not acted on) by Vulkan, where placement is BufferMemory's job.
enum class BufferUpdateFrequency : std::uint8_t { Static = 0, Dynamic, Stream };

[[nodiscard]] constexpr bool IsBufferMemoryValid(const BufferMemory memory) noexcept {
    switch (memory) {
        case BufferMemory::Auto:
        case BufferMemory::DeviceLocal:
        case BufferMemory::Upload:
        case BufferMemory::Readback:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsBufferUpdateFrequencyValid(const BufferUpdateFrequency frequency) noexcept {
    switch (frequency) {
        case BufferUpdateFrequency::Static:
        case BufferUpdateFrequency::Dynamic:
        case BufferUpdateFrequency::Stream:
            return true;
    }
    return false;
}

struct BufferDesc {
    std::uint64_t sizeBytes = 0;
    BufferUsage usage = BufferUsage::Vertex;
    BufferMemory memory = BufferMemory::Auto;
    BufferUpdateFrequency updateFrequency = BufferUpdateFrequency::Static;
};

/// Every enumerator on the descriptor is a known one. Backends check this before allocating so
/// an out-of-range value coming across the RHI boundary is refused instead of hitting a default.
[[nodiscard]] constexpr bool IsBufferDescValid(const BufferDesc& desc) noexcept {
    return IsBufferUsageValid(desc.usage) && IsBufferMemoryValid(desc.memory) &&
           IsBufferUpdateFrequencyValid(desc.updateFrequency);
}

[[nodiscard]] constexpr bool ValidateBufferUpload(const BufferDesc& desc,
                                                       const std::span<const std::byte> initialData) noexcept {
    return desc.sizeBytes > 0U && initialData.size() <= desc.sizeBytes;
}

[[nodiscard]] constexpr bool ValidateBufferUpdate(const std::uint64_t bufferSizeBytes,
                                                       const std::size_t dataSize,
                                                       const std::uint64_t offsetBytes) noexcept {
    return offsetBytes <= bufferSizeBytes && dataSize <= bufferSizeBytes - offsetBytes;
}

/// Pixel formats an IRenderDevice texture can use. `Depth32Float` exists here (even though no
/// loadable asset ever uses it — see the deliberately separate Asset::TextureAssetFormat) because
/// depth render targets are created directly through this RHI, never loaded from disk. Compressed
/// formats are sampled resources only: they cannot be framebuffer attachments or storage images.
/// The one- and two-channel formats (10..16) are linear-only: a missing channel samples as 0
/// (alpha as 1), uploads are tightly packed rows (a 3-wide `R8Unorm` level is 3 bytes per row),
/// and they are color-renderable on desktop GL; Vulkan attachment of any non-swapchain format
/// still waits on per-target pipeline formats. Append-only: values cross the RHI boundary.
enum class TextureFormat : std::uint8_t {
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
enum class TextureColorSpace : std::uint8_t {
    Linear = 0,
    Srgb = 1,
};

/// Pixel/block geometry for one mip level. All currently supported block-compressed formats use
/// 4x4 texel blocks; edge blocks are allocated in full.
struct TextureFormatBlockInfo {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t bytes = 0U;
    bool compressed = false;
};

[[nodiscard]] inline constexpr TextureFormatBlockInfo GetTextureFormatBlockInfo(
    const TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::R8Unorm:
            return {1U, 1U, 1U, false};
        case TextureFormat::RG8Unorm:
        case TextureFormat::R16Float:
            return {1U, 1U, 2U, false};
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::Depth32Float:
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::RG16Float:
        case TextureFormat::R32Float:
            return {1U, 1U, 4U, false};
        case TextureFormat::RGBA16Float:
        case TextureFormat::RG32Float:
            return {1U, 1U, 8U, false};
        case TextureFormat::RGBA32Float:
            return {1U, 1U, 16U, false};
        case TextureFormat::BC1RGB:
        case TextureFormat::ETC2RGB8:
            return {4U, 4U, 8U, true};
        case TextureFormat::BC3RGBA:
        case TextureFormat::BC7RGBA:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ASTC4x4RGBA:
            return {4U, 4U, 16U, true};
    }
    return {};
}

[[nodiscard]] inline constexpr bool IsTextureFormatCompressed(const TextureFormat format) noexcept {
    return GetTextureFormatBlockInfo(format).compressed;
}

[[nodiscard]] inline constexpr bool IsTextureFormatSrgbCapable(const TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::BC1RGB:
        case TextureFormat::BC3RGBA:
        case TextureFormat::BC7RGBA:
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ASTC4x4RGBA:
            return true;
        case TextureFormat::RGBA16Float:
        case TextureFormat::Depth32Float:
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::R8Unorm:
        case TextureFormat::RG8Unorm:
        case TextureFormat::R16Float:
        case TextureFormat::RG16Float:
        case TextureFormat::R32Float:
        case TextureFormat::RG32Float:
        case TextureFormat::RGBA32Float:
            return false;
    }
    return false;
}

[[nodiscard]] inline constexpr bool IsDepthFormat(const TextureFormat format) noexcept {
    return format == TextureFormat::Depth32Float || format == TextureFormat::Depth24Stencil8;
}

/// An uncompressed colour format: sampled, filterable where the device allows, and the only kind
/// a colour attachment, storage image, colour blit or generated mip chain may use.
[[nodiscard]] inline constexpr bool IsUncompressedColorFormat(const TextureFormat format) noexcept {
    const TextureFormatBlockInfo info = GetTextureFormatBlockInfo(format);
    return info.bytes != 0U && !info.compressed && !IsDepthFormat(format);
}

/// Number of colour channels an uncompressed colour format stores (0 for anything else).
[[nodiscard]] inline constexpr std::uint32_t GetTextureFormatChannelCount(const TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::R8Unorm:
        case TextureFormat::R16Float:
        case TextureFormat::R32Float:
            return 1U;
        case TextureFormat::RG8Unorm:
        case TextureFormat::RG16Float:
        case TextureFormat::RG32Float:
            return 2U;
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA16Float:
        case TextureFormat::RGBA32Float:
            return 4U;
        case TextureFormat::Depth32Float:
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::BC1RGB:
        case TextureFormat::BC3RGBA:
        case TextureFormat::BC7RGBA:
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ASTC4x4RGBA:
            return 0U;
    }
    return 0U;
}

[[nodiscard]] inline constexpr bool IsDepthStencilFormat(const TextureFormat format) noexcept {
    return format == TextureFormat::Depth24Stencil8;
}

/// Describes a GPU texture to create via IRenderDevice::CreateTexture(). `mipLevels` is
/// the total number of levels including level 0; it must not exceed the complete chain for the
/// base dimensions. Color assets may provide tightly concatenated texel or block bytes for every level.
/// Texture dimensionality (Tier 2.3). `Texture2D` is the only pre-2.3 shape; arrays stack
/// `arrayLayers` same-sized 2D slices (shadow cascades, texture atlases), and cubemaps are the
/// 6-face sampling shape (reflection probes, sky). Cube faces upload and attach in
/// `CubemapFace` order (+X,-X,+Y,-Y,+Z,-Z), which matches the GL/VK hardware face order.
/// `Texture3D` is a volume of `depth` slices (LUTs, volumetric noise): sampled with a 3D
/// coordinate, uploaded slice-major per level, mip levels halve depth as well as width and
/// height; it is never a render target or storage image in this slice.
enum class TextureType : std::uint8_t { Texture2D, Texture2DArray, Cubemap, Texture3D };

[[nodiscard]] constexpr bool IsTextureTypeValid(const TextureType type) noexcept {
    switch (type) {
        case TextureType::Texture2D:
        case TextureType::Texture2DArray:
        case TextureType::Cubemap:
        case TextureType::Texture3D:
            return true;
    }
    return false;
}

/// How a texture may be used, as a bit set. `Default` means "everything this format and shape
/// can honour" (see `GetSupportedTextureUsage`), which is exactly what every texture was
/// before the flags existed, so aggregate-initialised descriptors are unchanged. A narrower set
/// lets Vulkan allocate with precisely those `VkImageUsageFlags`, and every backend refuses a
/// use outside the set at the operation that would need it (attach, sample, image-bind, blit,
/// mip generation, readback) instead of letting the driver fail silently.
enum class TextureUsage : std::uint8_t {
    None = 0U,
    Sampled = 1U << 0U,      ///< bound through BindTexture / sampled in shaders
    RenderTarget = 1U << 1U, ///< attached by RenderPassDesc (colour or depth)
    Storage = 1U << 2U,      ///< bound as a storage image (imageLoad/imageStore)
    TransferSrc = 1U << 3U,  ///< blit source, mip-generation source, readback
    TransferDst = 1U << 4U,  ///< blit destination, mip-generation destination (uploads are always allowed)
    Default = Sampled | RenderTarget | Storage | TransferSrc | TransferDst,
};

[[nodiscard]] constexpr TextureUsage operator|(const TextureUsage left, const TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr TextureUsage operator&(const TextureUsage left, const TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool HasTextureUsage(const TextureUsage usage, const TextureUsage flag) noexcept {
    return (usage & flag) == flag && flag != TextureUsage::None;
}

/// Non-empty and made only of known bits.
[[nodiscard]] constexpr bool IsTextureUsageValid(const TextureUsage usage) noexcept {
    const auto bits = static_cast<std::uint8_t>(usage);
    return bits != 0U && (bits & ~static_cast<std::uint8_t>(TextureUsage::Default)) == 0U;
}

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8Unorm;
    std::uint32_t mipLevels = 1;
    // Appended after existing members to preserve aggregate initialization of legacy descriptors.
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    // Tier 2.3: dimensionality. Defaults describe the only pre-2.3 shape (one 2D slice).
    TextureType type = TextureType::Texture2D;
    std::uint32_t arrayLayers = 1;
    // Usage set (Default = everything the format/shape supports) and the Texture3D depth
    // (must be 1 for every other type). Appended for the same aggregate-initialisation reason.
    TextureUsage usage = TextureUsage::Default;
    std::uint32_t depth = 1;
};

/// The usage set a format/shape pair can honour on every backend: compressed formats are
/// sampled and copied but never rendered to or image-bound, depth formats are never storage
/// images or transfer sources, volumes are sampled and copied only.
[[nodiscard]] constexpr TextureUsage GetSupportedTextureUsage(const TextureFormat format,
                                                                   const TextureType type) noexcept {
    if (IsTextureFormatCompressed(format) || type == TextureType::Texture3D) {
        return TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst;
    }
    if (IsDepthFormat(format)) {
        return TextureUsage::Sampled | TextureUsage::RenderTarget | TextureUsage::TransferDst;
    }
    return TextureUsage::Default;
}

/// The usage a texture created from `desc` actually carries: the supported set under
/// `Default`, otherwise the requested set verbatim (validation guarantees it is a subset).
[[nodiscard]] constexpr TextureUsage GetEffectiveTextureUsage(const TextureDesc& desc) noexcept {
    return desc.usage == TextureUsage::Default ? GetSupportedTextureUsage(desc.format, desc.type) : desc.usage;
}

/// Valid when the bits are known and, unless `Default`, a subset of what the format/shape
/// supports: asking for a render-target BC1 or a storage volume is a caller error, not a
/// backend limitation to warn about later.
[[nodiscard]] constexpr bool IsTextureUsageSupported(const TextureDesc& desc) noexcept {
    if (!IsTextureUsageValid(desc.usage)) {
        return false;
    }
    const TextureUsage supported = GetSupportedTextureUsage(desc.format, desc.type);
    return desc.usage == TextureUsage::Default || (desc.usage & supported) == desc.usage;
}

/// How a sampler reads within one mip level. Shared by magnification and minification: GL and
/// Vulkan both take the same NEAREST/LINEAR choice on each axis, so one enum serves both.
enum class SamplerFilter : std::uint8_t { Point, Linear };

[[nodiscard]] constexpr bool IsSamplerFilterValid(const SamplerFilter filter) noexcept {
    switch (filter) {
        case SamplerFilter::Point:
        case SamplerFilter::Linear:
            return true;
    }
    return false;
}

/// How a sampler blends (or refuses to blend) between mip levels. `None` pins sampling to
/// level 0 — the honest choice for single-level textures like shadow maps and render targets.
enum class SamplerMipMode : std::uint8_t { None, Point, Linear };

[[nodiscard]] constexpr bool IsSamplerMipModeValid(const SamplerMipMode mode) noexcept {
    switch (mode) {
        case SamplerMipMode::None:
        case SamplerMipMode::Point:
        case SamplerMipMode::Linear:
            return true;
    }
    return false;
}

/// How a sampler resolves UVs outside [0, 1]. No clamp-to-border in v1: a border needs a
/// border-COLOR knob the RHI was deliberately not given (follow-up alongside 2.3 cubemaps,
/// whose seams are the first real border consumer).
enum class SamplerWrap : std::uint8_t { ClampToEdge, Repeat, MirroredRepeat };

[[nodiscard]] constexpr bool IsSamplerWrapValid(const SamplerWrap wrap) noexcept {
    switch (wrap) {
        case SamplerWrap::ClampToEdge:
        case SamplerWrap::Repeat:
        case SamplerWrap::MirroredRepeat:
            return true;
    }
    return false;
}

/// Describes a sampler object to create via IRenderDevice::CreateSampler(). Filtering,
/// mip blending, wrapping, and anisotropy live here — NOT on the texture — so one texture can
/// be sampled differently from different slots (a shadow map point-sampled by the PCF pass
/// while a debug view linear-samples the same target). Defaults reproduce the pre-2.2
/// hardcoded behavior (linear/trilinear/clamp), so pipelines that never bind a sampler render
/// byte-identical pixels to before.
struct SamplerDesc {
    SamplerFilter magFilter = SamplerFilter::Linear;
    SamplerFilter minFilter = SamplerFilter::Linear;
    SamplerMipMode mipMode = SamplerMipMode::Linear;
    SamplerWrap wrapU = SamplerWrap::ClampToEdge;
    SamplerWrap wrapV = SamplerWrap::ClampToEdge;
    SamplerWrap wrapW = SamplerWrap::ClampToEdge;
    /// Maximum anisotropy; 1.0 disables. Must be finite and >= 1.0 — backends clamp values
    /// above their device limit (with a warn-once), but below-1.0/NaN is a malformed desc.
    float maxAnisotropy = 1.0F;
};

[[nodiscard]] inline bool IsSamplerDescValid(const SamplerDesc& desc) noexcept {
    return IsSamplerFilterValid(desc.magFilter) && IsSamplerFilterValid(desc.minFilter) &&
           IsSamplerMipModeValid(desc.mipMode) && IsSamplerWrapValid(desc.wrapU) &&
           IsSamplerWrapValid(desc.wrapV) && IsSamplerWrapValid(desc.wrapW) &&
           std::isfinite(desc.maxAnisotropy) && desc.maxAnisotropy >= 1.0F;
}

struct TextureMipExtent {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

[[nodiscard]] inline constexpr std::uint64_t TextureFormatBytesPerPixel(
    const TextureFormat format) noexcept {
    const TextureFormatBlockInfo block = GetTextureFormatBlockInfo(format);
    return !block.compressed && block.width == 1U && block.height == 1U ? block.bytes : 0U;
}

[[nodiscard]] inline bool CalculateTextureMipByteCount(const TextureFormat format,
                                                           const std::uint32_t width,
                                                           const std::uint32_t height,
                                                           std::uint64_t& outBytes) noexcept {
    const TextureFormatBlockInfo block = GetTextureFormatBlockInfo(format);
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

[[nodiscard]] inline constexpr TextureMipExtent GetTextureMipExtent(
    std::uint32_t width, std::uint32_t height, std::uint32_t level) noexcept {
    while (level > 0U && (width > 1U || height > 1U)) {
        width = width > 1U ? width / 2U : 1U;
        height = height > 1U ? height / 2U : 1U;
        --level;
    }
    return {width, height};
}

[[nodiscard]] inline constexpr std::uint32_t MaximumTextureMipLevelCount(
    std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1U) noexcept {
    if (width == 0U || height == 0U || depth == 0U) {
        return 0U;
    }
    std::uint32_t count = 1U;
    while (width > 1U || height > 1U || depth > 1U) {
        width = width > 1U ? width / 2U : 1U;
        height = height > 1U ? height / 2U : 1U;
        depth = depth > 1U ? depth / 2U : 1U;
        ++count;
    }
    return count;
}

/// Depth of mip `level` of a volume `depth` slices deep (1 for every non-volume texture).
[[nodiscard]] inline constexpr std::uint32_t GetTextureMipDepth(std::uint32_t depth, std::uint32_t level) noexcept {
    while (level > 0U && depth > 1U) {
        depth /= 2U;
        --level;
    }
    return depth == 0U ? 1U : depth;
}

/// Computes the byte count for a tightly packed level-0..N upload. The output is unchanged on
/// failure; mips beyond the complete dimension-derived chain and multi-level depth textures are
/// rejected because this slice does not expose depth-mip attachment selection.
[[nodiscard]] inline bool CalculateTextureUploadByteCount(const TextureDesc& desc,
                                                              std::uint64_t& outBytes) noexcept {
    const TextureFormatBlockInfo block = GetTextureFormatBlockInfo(desc.format);
    if (desc.width == 0U || desc.height == 0U || desc.depth == 0U || desc.mipLevels == 0U || block.bytes == 0U ||
        desc.mipLevels > MaximumTextureMipLevelCount(desc.width, desc.height, desc.depth) ||
        (IsDepthFormat(desc.format) && desc.mipLevels > 1U)) {
        return false;
    }
    // Tier 2.3 dimensionality rules. Layer-count CAPS are device properties, so backends
    // enforce them at creation; these shape rules hold on every device. (Cube arrays — 6N
    // layers with cube sampling — are a follow-up; v1 cubes are exactly one cube.) Only a
    // volume carries depth; a volume never carries layers.
    switch (desc.type) {
        case TextureType::Texture2D:
            if (desc.arrayLayers != 1U || desc.depth != 1U) {
                return false;
            }
            break;
        case TextureType::Texture2DArray:
            if (desc.arrayLayers == 0U || desc.depth != 1U) {
                return false;
            }
            break;
        case TextureType::Cubemap:
            if (desc.width != desc.height || desc.arrayLayers != 6U || desc.depth != 1U) {
                return false;
            }
            break;
        case TextureType::Texture3D:
            if (desc.arrayLayers != 1U || IsDepthFormat(desc.format)) {
                return false;
            }
            break;
        default:
            return false;
    }

    std::uint64_t totalBytes = 0U;
    TextureMipExtent extent{desc.width, desc.height};
    std::uint32_t levelDepth = desc.depth;
    for (std::uint32_t level = 0U; level < desc.mipLevels; ++level) {
        std::uint64_t levelBytes = 0U;
        if (!CalculateTextureMipByteCount(desc.format, extent.width, extent.height, levelBytes) ||
            levelBytes > std::numeric_limits<std::uint64_t>::max() / levelDepth) {
            return false;
        }
        levelBytes *= levelDepth; // slice-major: every slice of this level, then the next level
        if (levelBytes > std::numeric_limits<std::uint64_t>::max() - totalBytes) {
            return false;
        }
        totalBytes += levelBytes;
        extent.width = extent.width > 1U ? extent.width / 2U : 1U;
        extent.height = extent.height > 1U ? extent.height / 2U : 1U;
        levelDepth = levelDepth > 1U ? levelDepth / 2U : 1U;
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
[[nodiscard]] inline bool ValidateTextureUpload(const TextureDesc& desc,
                                                    const std::span<const std::byte> initialData) noexcept {
    if (desc.width == 0U || desc.height == 0U || desc.mipLevels == 0U) {
        return false;
    }
    switch (desc.colorSpace) {
        case TextureColorSpace::Linear:
            break;
        case TextureColorSpace::Srgb:
            if (!IsTextureFormatSrgbCapable(desc.format)) {
                return false;
            }
            break;
        default:
            return false;
    }
    if (!IsTextureUsageSupported(desc)) {
        return false;
    }
    std::uint64_t expectedBytes = 0U;
    if (!CalculateTextureUploadByteCount(desc, expectedBytes) ||
        (initialData.empty() &&
         (desc.mipLevels > 1U || IsTextureFormatCompressed(desc.format)))) {
        return false;
    }
    return initialData.empty() || initialData.size() == static_cast<std::size_t>(expectedBytes);
}

/// Which programmable stage a ShaderDesc belongs to. `Compute` is real since the M5a
/// compute slice: CreateShader accepts compute-stage shaders and CreateComputePipeline
/// links exactly one of them. `Geometry` can be attached through PipelineDesc's optional
/// geometryShader slot. Query RenderDeviceCapabilities::geometryShaders before using it:
/// Null models it, desktop GL executes it, and unsupported backends reject it explicitly.
/// Append-only: never renumber existing values, since ShaderStage crosses the RHI boundary.
enum class ShaderStage : std::uint8_t { Vertex, Fragment, Compute, Geometry };

[[nodiscard]] constexpr bool IsShaderStageValid(const ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:
        case ShaderStage::Fragment:
        case ShaderStage::Compute:
        case ShaderStage::Geometry:
            return true;
    }
    return false;
}

/// Describes a shader to create via IRenderDevice::CreateShader(). `sourceCode` is stored
/// and validated as-is; NullRenderDevice never compiles it — no glslang/shaderc/DXC is
/// available in this environment (see docs/CODING_STANDARDS.md).
struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::string sourceCode;
    std::string entryPointName = "main";
};

/// The shape of one vertex attribute inside a PipelineDesc's vertex layout. The three
/// `Float*` entries are 32-bit floats; the packed entries reach the shader as floats too
/// (`UByte4Norm` / `Short2Norm` normalised to [0,1] / [-1,1], `Half2` / `Half4` converted),
/// except the two integer entries `UShort4` / `UInt4`, which stay integers and must be declared
/// `uvec4` in the shader — joint indices, not weights. Each entry's byte width comes from
/// `GetVertexAttributeFormatByteSize`; backends fetch every one through their native
/// attribute-pointer / VkFormat path, so a layout never needs CPU repacking.
enum class VertexAttributeFormat : std::uint8_t {
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
[[nodiscard]] constexpr std::uint32_t GetVertexAttributeFormatByteSize(const VertexAttributeFormat format) noexcept {
    switch (format) {
        case VertexAttributeFormat::Float2:
            return 8U;
        case VertexAttributeFormat::Float3:
            return 12U;
        case VertexAttributeFormat::Float4:
            return 16U;
        case VertexAttributeFormat::UByte4Norm:
            return 4U;
        case VertexAttributeFormat::UShort4:
            return 8U;
        case VertexAttributeFormat::UInt4:
            return 16U;
        case VertexAttributeFormat::Short2Norm:
            return 4U;
        case VertexAttributeFormat::Half2:
            return 4U;
        case VertexAttributeFormat::Half4:
            return 8U;
    }
    return 0U;
}

/// Component count of `format` (2, 3 or 4); 0 for unknown values.
[[nodiscard]] constexpr std::uint32_t GetVertexAttributeFormatComponentCount(
    const VertexAttributeFormat format) noexcept {
    switch (format) {
        case VertexAttributeFormat::Float2:
        case VertexAttributeFormat::Short2Norm:
        case VertexAttributeFormat::Half2:
            return 2U;
        case VertexAttributeFormat::Float3:
            return 3U;
        case VertexAttributeFormat::Float4:
        case VertexAttributeFormat::UByte4Norm:
        case VertexAttributeFormat::UShort4:
        case VertexAttributeFormat::UInt4:
        case VertexAttributeFormat::Half4:
            return 4U;
    }
    return 0U;
}

/// True for the formats a shader must declare as an integer vector (`uvec4`): the fetch skips
/// float conversion entirely.
[[nodiscard]] constexpr bool IsVertexAttributeFormatInteger(const VertexAttributeFormat format) noexcept {
    return format == VertexAttributeFormat::UShort4 || format == VertexAttributeFormat::UInt4;
}

/// One vertex attribute (e.g. "position", "normal", "uv") within a pipeline's vertex layout.
struct VertexAttribute {
    std::string semanticName;
    VertexAttributeFormat format = VertexAttributeFormat::Float3;
    std::uint32_t offset = 0;
};

[[nodiscard]] constexpr bool IsVertexAttributeFormatValid(const VertexAttributeFormat format) noexcept {
    return GetVertexAttributeFormatByteSize(format) != 0U;
}

[[nodiscard]] inline bool IsVertexLayoutValid(const std::span<const VertexAttribute> vertexLayout) noexcept {
    for (const VertexAttribute& attribute : vertexLayout) {
        if (!IsVertexAttributeFormatValid(attribute.format)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool IsVertexLayoutWithinStride(const std::span<const VertexAttribute> vertexLayout,
                                                         const std::uint32_t vertexStride) noexcept {
    for (const VertexAttribute& attribute : vertexLayout) {
        const std::uint32_t attributeByteWidth = GetVertexAttributeFormatByteSize(attribute.format);
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
/// the driver — `GetPrimitiveCount` / `IsVertexCountCompleteForTopology` tell a caller
/// how a count will be consumed before it draws.
enum class PrimitiveTopology : std::uint8_t { Triangles, Lines, Points, LineStrip, TriangleStrip };

[[nodiscard]] constexpr bool IsPrimitiveTopologyValid(const PrimitiveTopology topology) noexcept {
    switch (topology) {
        case PrimitiveTopology::Triangles:
        case PrimitiveTopology::Lines:
        case PrimitiveTopology::Points:
        case PrimitiveTopology::LineStrip:
        case PrimitiveTopology::TriangleStrip:
            return true;
    }
    return false;
}

/// Number of primitives `vertexCount` vertices (or indices) assemble into under `topology`.
/// Unknown topologies yield 0.
[[nodiscard]] constexpr std::uint32_t GetPrimitiveCount(const PrimitiveTopology topology,
                                                          const std::uint32_t vertexCount) noexcept {
    switch (topology) {
        case PrimitiveTopology::Triangles:
            return vertexCount / 3U;
        case PrimitiveTopology::Lines:
            return vertexCount / 2U;
        case PrimitiveTopology::Points:
            return vertexCount;
        case PrimitiveTopology::LineStrip:
            return vertexCount >= 2U ? vertexCount - 1U : 0U;
        case PrimitiveTopology::TriangleStrip:
            return vertexCount >= 3U ? vertexCount - 2U : 0U;
    }
    return 0U;
}

/// True when every one of `vertexCount` vertices takes part in a primitive under `topology`: a
/// multiple of 3 / 2 for the list modes, anything for points, and 0 or at least 2 / 3 for the
/// strips. Zero is complete (an empty draw). Unknown topologies are never complete.
[[nodiscard]] constexpr bool IsVertexCountCompleteForTopology(const PrimitiveTopology topology,
                                                                const std::uint32_t vertexCount) noexcept {
    switch (topology) {
        case PrimitiveTopology::Triangles:
            return vertexCount % 3U == 0U;
        case PrimitiveTopology::Lines:
            return vertexCount % 2U == 0U;
        case PrimitiveTopology::Points:
            return true;
        case PrimitiveTopology::LineStrip:
            return vertexCount == 0U || vertexCount >= 2U;
        case PrimitiveTopology::TriangleStrip:
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
/// Each mode is a COMPLETE (source factor, destination factor, equation) triple, not a name the
/// backends interpret loosely: "SourceAlphaOver" means src*srcA + dst*(1-srcA) and nothing else.
/// That is why there is no separate equation field beside it - a factor pair and an equation chosen
/// independently can be combined into states neither author meant, and every one of those states
/// would look like a working pipeline that composites wrongly.
enum class PipelineBlendMode : std::uint8_t {
    /// No blending; the source replaces the destination.
    Opaque,
    /// src*srcA + dst*(1-srcA) - straight (non-premultiplied) alpha.
    SourceAlphaOver,
    /// src + dst.
    Additive,
    /// src*dst.
    Multiply,
    /// Already-premultiplied source over the destination: src + dst*(1-srcA). The fragment shader
    /// is expected to have multiplied rgb by alpha itself, which is why this cannot be expressed
    /// as SourceAlphaOver with a different factor.
    PremultipliedAlpha,
    /// dst - src*srcA (reverse subtract), the opposite of Additive. Rounds to zero rather than
    /// below it, which is what every 8-bit destination does anyway.
    Subtractive,
};

[[nodiscard]] constexpr bool IsPipelineBlendModeValid(const PipelineBlendMode blendMode) noexcept {
    switch (blendMode) {
        case PipelineBlendMode::Opaque:
        case PipelineBlendMode::SourceAlphaOver:
        case PipelineBlendMode::Additive:
        case PipelineBlendMode::Multiply:
        case PipelineBlendMode::PremultipliedAlpha:
        case PipelineBlendMode::Subtractive:
            return true;
    }
    return false;
}

/// Which triangle faces the rasterizer discards. `None` (no culling) is the default both
/// backends already implement: GL never enables `GL_CULL_FACE` for RHI pipelines, and Vulkan
/// hardcodes `VK_CULL_MODE_NONE`.
enum class CullMode : std::uint8_t { None, Front, Back, FrontAndBack };

[[nodiscard]] constexpr bool IsCullModeValid(const CullMode mode) noexcept {
    switch (mode) {
        case CullMode::None:
        case CullMode::Front:
        case CullMode::Back:
        case CullMode::FrontAndBack:
            return true;
    }
    return false;
}

/// Which vertex winding counts as front-facing for culling. Counter-clockwise is the default
/// both backends already implement (and the winding glTF importers produce).
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };

[[nodiscard]] constexpr bool IsFrontFaceValid(const FrontFace face) noexcept {
    switch (face) {
        case FrontFace::CounterClockwise:
        case FrontFace::Clockwise:
            return true;
    }
    return false;
}

/// How triangles fill their pixels. `Fill` is the default both backends already implement.
/// There is no point mode: points need a point-size state neither backend was given.
enum class FillMode : std::uint8_t { Fill, Wireframe };

[[nodiscard]] constexpr bool IsFillModeValid(const FillMode mode) noexcept {
    switch (mode) {
        case FillMode::Fill:
        case FillMode::Wireframe:
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
enum class DepthCompare : std::uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

[[nodiscard]] constexpr bool IsDepthCompareValid(const DepthCompare compare) noexcept {
    switch (compare) {
        case DepthCompare::Never:
        case DepthCompare::Less:
        case DepthCompare::Equal:
        case DepthCompare::LessOrEqual:
        case DepthCompare::Greater:
        case DepthCompare::NotEqual:
        case DepthCompare::GreaterOrEqual:
        case DepthCompare::Always:
            return true;
    }
    return false;
}

/// What a stencil operation does to the stored stencil value.
enum class StencilOp : std::uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

[[nodiscard]] constexpr bool IsStencilOpValid(const StencilOp op) noexcept {
    switch (op) {
        case StencilOp::Keep:
        case StencilOp::Zero:
        case StencilOp::Replace:
        case StencilOp::IncrementClamp:
        case StencilOp::DecrementClamp:
        case StencilOp::Invert:
        case StencilOp::IncrementWrap:
        case StencilOp::DecrementWrap:
            return true;
    }
    return false;
}

/// One face's stencil test. The comparison is `(reference & compareMask) OP (stored & compareMask)`;
/// masks and reference are 8-bit because Depth24Stencil8 is the stencil-carrying format.
struct StencilFaceState {
    DepthCompare compare = DepthCompare::Always;
    StencilOp stencilFail = StencilOp::Keep;
    StencilOp depthFail = StencilOp::Keep;
    StencilOp depthPass = StencilOp::Keep;
    std::uint32_t compareMask = 0xFFU;
    std::uint32_t writeMask = 0xFFU;
    std::uint32_t reference = 0U;
};

[[nodiscard]] constexpr bool IsStencilFaceStateValid(const StencilFaceState& state) noexcept {
    return IsDepthCompareValid(state.compare) && IsStencilOpValid(state.stencilFail) &&
           IsStencilOpValid(state.depthFail) && IsStencilOpValid(state.depthPass) &&
           state.compareMask <= 0xFFU && state.writeMask <= 0xFFU && state.reference <= 0xFFU;
}

/// Front/back stencil state. Disabled is the legacy behavior and ignores both faces. Enabled
/// state is meaningful only for a pass whose depth attachment uses Depth24Stencil8 (or a native
/// default surface that actually has stencil bits); backends reject malformed enum/mask values.
struct StencilState {
    bool enabled = false;
    StencilFaceState front;
    StencilFaceState back;
};

[[nodiscard]] constexpr bool IsStencilStateValid(const StencilState& state) noexcept {
    return IsStencilFaceStateValid(state.front) && IsStencilFaceStateValid(state.back);
}

/// Describes a pipeline state object to create via IRenderDevice::CreatePipeline().
/// Fixed-function state remains deliberately small; blending is explicit because editor visual
/// composition is the first proven consumer rather than an implicit global OpenGL side effect.
struct PipelineDesc {
    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    std::vector<VertexAttribute> vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendMode blendMode = PipelineBlendMode::Opaque;

    /// Byte distance between consecutive vertices in the bound vertex buffer — required by a real
    /// backend to interleave `vertexLayout`'s attributes correctly (e.g. GL's
    /// `glVertexAttribPointer` stride parameter). `0` (the default) is only ever valid for
    /// `NullRenderDevice`, which ignores this field like every other one it merely bookkeeps.
    std::uint32_t vertexStride = 0;

    // Appended after existing members to preserve aggregate initialization of legacy descriptors.
    // Defaults reproduce what both backends already did — except depthCompare, where Less
    // matches GL and moves Vulkan off its hardcoded LessOrEqual (see DepthCompare).
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    FillMode fillMode = FillMode::Fill;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0F;
    float depthBiasSlopeFactor = 0.0F;
    DepthCompare depthCompare = DepthCompare::Less;
    StencilState stencil;

    /// Optional live Geometry-stage shader. Invalid means the legacy vertex/fragment pair.
    /// Appended for aggregate/source compatibility. Unsupported backends must reject a supplied
    /// handle, never silently omit it. Shader objects need only live through pipeline creation;
    /// shader-specific input/output topology and limits are validated by the native linker.
    ShaderHandle geometryShader = kInvalidShaderHandle;
};

/// Describes a COMPUTE pipeline to create via IRenderDevice::CreateComputePipeline()
/// (M5a compute slice). Deliberately its own struct: a compute pipeline has no vertex layout,
/// no fixed-function state, and exactly one shader stage, so folding it into PipelineDesc
/// would make every graphics field a lie. The resulting PipelineHandle lives in the SAME
/// handle domain as graphics pipelines — BindPipeline binds either kind, and the backend
/// selects the correct bind point internally (Vulkan GRAPHICS vs COMPUTE).
struct ComputePipelineDesc {
    /// Must reference a live shader created with ShaderStage::Compute. An invalid handle,
    /// or a shader of any other stage, is a loud creation failure on every real backend.
    ShaderHandle computeShader;
};

/// Fixed-function state accompanying a pre-compiled GL program binary passed to
/// IRenderDevice::CreatePipelineFromBinary() (Increment 21's program-binary cache). Deliberately
/// has no shader-handle fields — loading from binary skips shader-object attachment entirely, so
/// there is nothing analogous to PipelineDesc::vertexShader/fragmentShader here.
struct PipelineBinaryDesc {
    std::vector<VertexAttribute> vertexLayout;
    std::uint32_t vertexStride = 0;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendMode blendMode = PipelineBlendMode::Opaque;

    // Same appended-rasterizer-state tail as PipelineDesc: a cache-loaded pipeline must be
    // able to express everything a compiled one can, or cache hits would silently drop state.
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    FillMode fillMode = FillMode::Fill;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0F;
    float depthBiasSlopeFactor = 0.0F;
    DepthCompare depthCompare = DepthCompare::Less;
    StencilState stencil;
};

/// What happens to a render pass attachment's existing contents at the start of the pass.
enum class LoadOp : std::uint8_t { Clear, Load, DontCare };

[[nodiscard]] constexpr bool IsLoadOpValid(const LoadOp loadOp) noexcept {
    switch (loadOp) {
        case LoadOp::Clear:
        case LoadOp::Load:
        case LoadOp::DontCare:
            return true;
    }
    return false;
}

/// What happens to a render pass attachment's contents at the END of the pass (Tier 2.5).
/// `Store` keeps the rendered bits for later sampling — the only pre-2.5 behavior, since every
/// backend stored unconditionally. `DontCare` lets the driver discard them (tiler efficiency);
/// the contents become undefined. There is deliberately no Resolve option: multisampled
/// textures do not exist in the RHI yet.
enum class StoreOp : std::uint8_t { Store, DontCare };

[[nodiscard]] constexpr bool IsStoreOpValid(const StoreOp storeOp) noexcept {
    switch (storeOp) {
        case StoreOp::Store:
        case StoreOp::DontCare:
            return true;
    }
    return false;
}

/// Describes one BeginRenderPass() call: which color/depth textures are rendered into and how
/// they're cleared. `depthAttachment` may be `kInvalidTextureHandle` for a color-only pass.
/// `colorAttachment` itself may also be `kInvalidTextureHandle`, meaning "render into the
/// backend's default framebuffer" (the window's back buffer) rather than an offscreen texture —
/// a backend-specific meaning `GlRenderDevice` acts on (binding GL framebuffer object `0`);
/// `NullRenderDevice` ignores it like every other field it bookkeeps without interpreting.
/// A pixel-space sub-rectangle of a render pass's target (attachment or, for the default
/// framebuffer, the window). Introduced for Phase 3's ViewportManager, where multiple panes
/// render into different regions of the same window in a single frame - but it's a general
/// RenderPassDesc capability, not split-view-specific, since any caller may want to render into
/// less than the full attachment.
/// Implemented as Math::RectInt under an RHI name: the old four-uint32 struct retired in
/// favour of the one shared integer-rect type. Position/size are signed now; backends reject
/// a negative or zero-size rect through the same viewport fit-check as an out-of-bounds one.
using ViewportRect = Math::RectInt;

/// How many color locations past location 0 a pass may target (Tier 2.4): locations 1..3, so
/// four total — inside every GL (MAX_DRAW_BUFFERS >= 8) and Vulkan (maxColorAttachments, queried
/// at init) device, and no G-buffer needs more.
inline constexpr std::uint32_t kExtraColorAttachmentCount = 3U;

/// One of locations 1..3 of a multi-target pass (Tier 2.4). Location 0 keeps the legacy
/// top-level fields below (colorAttachment/colorLoadOp/clearColor/...), so single-target passes
/// — every pass that exists today — compile and behave exactly as before. An extra slot whose
/// target is invalid is simply unused, and gaps are NOT allowed: location 2 set while location 1
/// is invalid is malformed, and backends reject it like any other malformed pass descriptor.
struct ColorAttachment {
    TextureHandle target;
    std::uint32_t layer = 0;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
};

struct RenderPassDesc {
    TextureHandle colorAttachment;
    TextureHandle depthAttachment;
    LoadOp colorLoadOp = LoadOp::Clear;
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    LoadOp depthLoadOp = LoadOp::Clear;
    float clearDepth = 1.0F;
    /// When set, the pass's GL viewport is this pixel rect instead of the full attachment/window
    /// size. Must fit entirely within the target: a zero-size, negative, or out-of-bounds rect is
    /// rejected the same way as any other malformed pass descriptor, rather than silently
    /// clamped. See Math::Contains for the exact fit-check the backends apply.
    std::optional<ViewportRect> viewportOverride;
    // Tier 2.3: which layer of an array/cube attachment this pass renders into (cube faces are
    // layers 0-5 in CubemapFace order). Must be 0 for Texture2D attachments and below
    // `arrayLayers` otherwise; backends reject out-of-range layers like any other malformed
    // pass descriptor. Defaults preserve the only pre-2.3 behavior (layer 0 of a 2D target).
    std::uint32_t colorLayer = 0;
    std::uint32_t depthLayer = 0;
    // Tier 2.5: end-of-pass behavior for location 0 and depth. Defaults preserve the only
    // pre-2.5 behavior (everything stored).
    StoreOp colorStoreOp = StoreOp::Store;
    StoreOp depthStoreOp = StoreOp::Store;
    // Tier 2.4: locations 1..3 (see ColorAttachment). Empty by default: a single target.
    std::array<ColorAttachment, kExtraColorAttachmentCount> extraColorAttachments{};
    /// Used only when depthLoadOp is Clear and the depth attachment carries stencil bits.
    /// The existing depth load/store operations govern both aspects of a combined attachment.
    std::uint32_t clearStencil = 0U;
};

} // namespace UVE::Render
