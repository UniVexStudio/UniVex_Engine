// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
struct BufferDescUVE {
    std::uint64_t sizeBytes = 0;
    BufferUsageUVE usage = BufferUsageUVE::Vertex;
};

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
/// loadable asset ever uses it — see the deliberately separate Asset::TextureFormatUVE) because
/// depth render targets are created directly through this RHI, never loaded from disk. Compressed
/// formats are sampled resources only: they cannot be framebuffer attachments or storage images.
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
        case TextureFormatUVE::RGBA8Unorm:
        case TextureFormatUVE::Depth32Float:
            return {1U, 1U, 4U, false};
        case TextureFormatUVE::RGBA16Float:
            return {1U, 1U, 8U, false};
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
            return false;
    }
    return false;
}

/// Describes a GPU texture to create via IRenderDeviceUVE::CreateTextureUVE(). `mipLevels` is
/// the total number of levels including level 0; it must not exceed the complete chain for the
/// base dimensions. Color assets may provide tightly concatenated texel or block bytes for every level.
struct TextureDescUVE {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormatUVE format = TextureFormatUVE::RGBA8Unorm;
    std::uint32_t mipLevels = 1;
    // Appended after existing members to preserve aggregate initialization of legacy descriptors.
    TextureColorSpaceUVE colorSpace = TextureColorSpaceUVE::Linear;
};

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
        (desc.format == TextureFormatUVE::Depth32Float && desc.mipLevels > 1U)) {
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
/// links exactly one of them. `Geometry` (Increment 21) is compilable standalone via
/// Shader::ShaderSourceUVE but still has no pipeline slot — PipelineDescUVE only links a
/// vertex+fragment pair; growing it with a geometry slot is deferred future work, not built
/// this increment. Append-only: never renumber existing values, since ShaderStageUVE crosses
/// the RHI boundary.
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

/// The shape of one vertex attribute inside a PipelineDescUVE's vertex layout.
enum class VertexAttributeFormatUVE : std::uint8_t { Float2, Float3, Float4 };

/// One vertex attribute (e.g. "position", "normal", "uv") within a pipeline's vertex layout.
struct VertexAttributeUVE {
    std::string semanticName;
    VertexAttributeFormatUVE format = VertexAttributeFormatUVE::Float3;
    std::uint32_t offset = 0;
};

[[nodiscard]] constexpr bool IsVertexAttributeFormatValidUVE(const VertexAttributeFormatUVE format) noexcept {
    switch (format) {
        case VertexAttributeFormatUVE::Float2:
        case VertexAttributeFormatUVE::Float3:
        case VertexAttributeFormatUVE::Float4:
            return true;
    }
    return false;
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
        std::uint32_t componentCount = 0;
        switch (attribute.format) {
            case VertexAttributeFormatUVE::Float2:
                componentCount = 2U;
                break;
            case VertexAttributeFormatUVE::Float3:
                componentCount = 3U;
                break;
            case VertexAttributeFormatUVE::Float4:
                componentCount = 4U;
                break;
        }
        const std::uint32_t attributeByteWidth = componentCount * static_cast<std::uint32_t>(sizeof(float));
        if (attribute.offset > vertexStride || attributeByteWidth > vertexStride - attribute.offset) {
            return false;
        }
    }
    return true;
}

/// How a pipeline's bound vertex/index buffers are assembled into primitives. Only `Triangles`
/// exists today — lines/points aren't needed by anything built so far.
enum class PrimitiveTopologyUVE : std::uint8_t { Triangles };

[[nodiscard]] constexpr bool IsPrimitiveTopologyValidUVE(const PrimitiveTopologyUVE topology) noexcept {
    switch (topology) {
        case PrimitiveTopologyUVE::Triangles:
            return true;
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
struct ViewportRectUVE {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct RenderPassDescUVE {
    TextureHandleUVE colorAttachment;
    TextureHandleUVE depthAttachment;
    LoadOpUVE colorLoadOp = LoadOpUVE::Clear;
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    LoadOpUVE depthLoadOp = LoadOpUVE::Clear;
    float clearDepth = 1.0F;
    /// When set, the pass's GL viewport is this pixel rect instead of the full attachment/window
    /// size. Must fit entirely within the target: `x + width` and `y + height` must not exceed the
    /// attachment's (or, for the default framebuffer, the window's) dimensions. A backend rejects
    /// an out-of-bounds rect the same way it rejects any other malformed pass descriptor, rather
    /// than silently clamping it.
    std::optional<ViewportRectUVE> viewportOverride;
};

} // namespace UVE::Render
