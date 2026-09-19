// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/renderer_3d_uve.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "uve/asset/asset_reloaded_event_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/render_systems/i_light_system_uve.h"
#include "uve/render_systems/render_graph_uve.h"
#include "uve/render_systems/primitive_geometry_uve.h"
#include "uve/render_systems/particle_render_bridge_uve.h"
#include "uve/render_systems/particle_draw_command_uve.h"
#include "uve/render_systems/render_batch_uve.h"
#include "uve/render_systems/render_queue_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"
#include "uve/rhi_shader/shader_program_desc_uve.h"
#include "uve/rhi_shader/shader_program_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFiniteMatrixUVE(const Math::Matrix4x4UVE& matrix) noexcept {
    for (const auto& row : matrix.m) {
        for (const float value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool IsOrderedFiniteAabbUVE(const Math::AabbUVE& bounds) noexcept {
    return IsFiniteVectorUVE(bounds.min) && IsFiniteVectorUVE(bounds.max) && bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

[[nodiscard]] Math::AabbUVE ComputeLightSpaceCameraBoundsUVE(const CameraFrustumCornersUVE& cameraCorners,
                                                              const Math::Matrix4x4UVE& lightView) noexcept {
    Math::Vector3UVE minimum = Math::TransformPointUVE(lightView, cameraCorners[0]);
    Math::Vector3UVE maximum = minimum;
    for (std::size_t cornerIndex = 1; cornerIndex < cameraCorners.size(); ++cornerIndex) {
        const Math::Vector3UVE corner = Math::TransformPointUVE(lightView, cameraCorners[cornerIndex]);
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    return Math::AabbUVE{minimum, maximum};
}

/// Expands and snaps the XY center of a light-space orthographic box to the shadow-map texel grid.
/// The half extents are widened by one texel on each side before snapping, so the resulting box
/// remains conservative even when the snapped center moves by up to half a texel.
/// Z remains fitted exactly: directional-shadow resolution is only two-dimensional, while the
/// existing padded near/far bounds already protect depth coverage.
[[nodiscard]] Math::AabbUVE StabilizeLightSpaceShadowBoundsUVE(const Math::AabbUVE& fittedBounds, float padding,
                                                               std::uint32_t shadowMapResolution) noexcept {
    const float clampedPadding = std::max(padding, 0.0F);
    const float minimumX = fittedBounds.min.x - clampedPadding;
    const float maximumX = fittedBounds.max.x + clampedPadding;
    const float minimumY = fittedBounds.min.y - clampedPadding;
    const float maximumY = fittedBounds.max.y + clampedPadding;

    const auto stabilizeAxis = [shadowMapResolution](float minimum, float maximum) noexcept {
        const float center = (minimum + maximum) * 0.5F;
        const float halfExtent = (maximum - minimum) * 0.5F;
        if (shadowMapResolution <= 2U || halfExtent <= 0.0F) {
            return std::array<float, 2>{minimum, maximum};
        }

        const float resolution = static_cast<float>(shadowMapResolution);
        const float stabilizedHalfExtent = halfExtent * resolution / (resolution - 2.0F);
        const float texelExtent = (stabilizedHalfExtent * 2.0F) / resolution;
        const float snappedCenter = std::floor(center / texelExtent) * texelExtent;
        return std::array<float, 2>{snappedCenter - stabilizedHalfExtent, snappedCenter + stabilizedHalfExtent};
    };

    const std::array<float, 2> stabilizedX = stabilizeAxis(minimumX, maximumX);
    const std::array<float, 2> stabilizedY = stabilizeAxis(minimumY, maximumY);
    return Math::AabbUVE{{stabilizedX[0], stabilizedY[0], fittedBounds.min.z - clampedPadding},
                         {stabilizedX[1], stabilizedY[1], fittedBounds.max.z + clampedPadding}};
}

/// A mesh's uploaded GPU buffers, cached by MeshAssetUVE's AssetGuidUVE.
struct MeshGpuResourcesUVE {
    BufferHandleUVE vertexBuffer;
    BufferHandleUVE indexBuffer;
    std::uint32_t indexCount = 0;
};

[[nodiscard]] bool IsValidMeshGpuResourcesUVE(const MeshGpuResourcesUVE& resources) noexcept {
    return resources.vertexBuffer != kInvalidBufferHandleUVE && resources.indexBuffer != kInvalidBufferHandleUVE &&
           resources.indexCount > 0U;
}

/// Renderer-owned primitive draw data. It deliberately contains no AssetHandleUVE: primitive
/// geometry is immutable renderer cache data, while authored kind/color remain ECS component state.
struct PrimitiveRenderItemUVE {
    Math::Matrix4x4UVE worldMatrix;
    Scene::PrimitiveMeshKindUVE kind = Scene::PrimitiveMeshKindUVE::Cube;
    Math::Vector3UVE baseColor{};
    float sortDepth = 0.0F;
};

/// Identity of a primitive's placement inputs. Two placements with equal keys must produce equal
/// world matrices and bounds, so every input the extraction reads appears here.
///
/// Exact float equality, matching MeshPlacementKeyUVE deliberately rather than using a tolerance:
/// a tolerance would let an object drift arbitrarily far in steps below it, and NaN comparing
/// unequal is the outcome we want twice over - recomputation is what rejects it, and a key that
/// "matched" two NaNs would be claiming a broken transform is unchanged.
struct PrimitivePlacementKeyUVE {
    Math::Vector3UVE worldPosition{};
    Math::QuaternionUVE worldRotation{};
    Math::Vector3UVE worldScale{};
    Scene::PrimitiveMeshKindUVE kind = Scene::PrimitiveMeshKindUVE::Cube;

    [[nodiscard]] bool MatchesUVE(const PrimitivePlacementKeyUVE& other) const noexcept {
        // Kind first: a single enum compare, and the field whose change invalidates the geometry
        // wholesale, so it rejects earliest for least work.
        return kind == other.kind && worldPosition.x == other.worldPosition.x &&
               worldPosition.y == other.worldPosition.y && worldPosition.z == other.worldPosition.z &&
               worldRotation.x == other.worldRotation.x && worldRotation.y == other.worldRotation.y &&
               worldRotation.z == other.worldRotation.z && worldRotation.w == other.worldRotation.w &&
               worldScale.x == other.worldScale.x && worldScale.y == other.worldScale.y &&
               worldScale.z == other.worldScale.z;
    }
};

/// A cached primitive placement. `placed` false records a REJECTION - a non-finite transform, an
/// unnormalizable rotation, degenerate bounds - which is worth caching exactly as much as a
/// success: it costs the same recompute to rediscover, every frame, forever.
struct PrimitivePlacementCacheEntryUVE {
    PrimitivePlacementKeyUVE key{};
    Math::Matrix4x4UVE worldMatrix{};
    Math::AabbUVE worldBounds{};
    bool placed = false;
    std::uint64_t lastSeenFrame = 0U;
};

/// CPU-expanded particle vertex consumed by the minimal built-in particle pipeline. The four
/// color floats carry a stable warm tint plus lifetime-derived alpha; keeping this as a private
/// renderer DTO prevents particle authoring data from crossing the RHI boundary.
struct ParticleVertexUVE {
    Math::Vector3UVE position{};
    float red = 1.0F;
    float green = 0.45F;
    float blue = 0.08F;
    float alpha = 1.0F;
};

/// CPU-expanded vertex consumed by the built-in UI overlay pipeline (ui_overlay.glsl) - one
/// screen-space position/texcoord pair plus a per-vertex tint, matching UI::UIQuadUVE's own field
/// shape directly (kept as a private renderer DTO for the same reason ParticleVertexUVE is).
struct UIVertexUVE {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

/// SSBO slots the instanced lit variant reads its per-instance transforms from. These mirror the
/// `layout(std430, binding = N)` lines in lit_shadowed_3d.glsl's UVE_INSTANCED block; the two must
/// agree, and a source-level test in the shader suite pins the shader half.
inline constexpr std::uint32_t kInstanceTransformSlotUVE = 0U;
inline constexpr std::uint32_t kInstanceNormalTransformSlotUVE = 1U;
inline constexpr std::uint32_t kInstanceBaseSlotUVE = 2U;

/// The most instances one frame may upload. Bounds the per-frame upload the same way
/// kMaximumParticleGpuDrawCommandsUVE bounds particles; a batch that would exceed it is recorded
/// per-object instead, which is slower but never wrong.
inline constexpr std::size_t kMaximumInstancesPerFrameUVE = 65'536U;

/// Whether a material's vertex source actually implements the instancing contract.
///
/// Deliberately a source-text check rather than a flag on MaterialAssetUVE: a flag would let a
/// material CLAIM instancing support that its shader does not implement, and the failure mode for
/// that lie is silent (every instance drawn at the first one's transform). The source either reads
/// the instance buffer or it does not, and that is the only thing worth trusting here.
[[nodiscard]] bool VertexSourceSupportsInstancingUVE(const std::string_view vertexSource) noexcept {
    return vertexSource.find("uInstanceBaseIndex") != std::string_view::npos &&
           vertexSource.find("gl_InstanceID") != std::string_view::npos;
}

inline constexpr std::size_t kMaximumParticleGpuDrawCommandsUVE = 16'384U;
inline constexpr std::size_t kParticleVerticesPerCommandUVE = 6U;
inline constexpr float kParticleHalfExtentUVE = 0.05F;

inline constexpr std::size_t kMaximumUIQuadsUVE = 8'192U;
inline constexpr std::size_t kUIVerticesPerQuadUVE = 6U;

/// A material's manager-owned linked program plus its resolved texture handles, cached by
/// MaterialAssetUVE's AssetGuidUVE. `program` owns its linked pipeline through ShaderManagerUVE;
/// Renderer3DUVE only retains a shared reference and must never destroy that pipeline directly.
/// The source GUIDs let AssetReloaded events invalidate exactly the materials that reference a
/// changed vertex or fragment shader. `albedoTexture`/`normalTexture`/`aoTexture` are never
/// kInvalidTextureHandleUVE once cached — an unset MaterialAssetUVE texture GUID resolves to one
/// of Renderer3DUVE's two fallback textures (see ResolveTextureGpuHandleUVE()'s doc comment).
struct MaterialGpuResourcesUVE {
    std::shared_ptr<Shader::ShaderProgramUVE> program;
    /// True only when this material's own vertex source actually declares the instancing
    /// contract. Instancing is OPT-IN per material and DETECTED, never assumed: material shaders
    /// come from `.uveshader` assets a project authors, so most of them know nothing about
    /// gl_InstanceID. Drawing such a material with instanceCount > 1 would not fail - it would
    /// silently stack every instance on top of the first one's uModel, which looks like missing
    /// objects rather than like a bug in the renderer.
    bool supportsInstancing = false;
    Asset::AssetGuidUVE vertexShaderGuid;
    Asset::AssetGuidUVE fragmentShaderGuid;
    Asset::AssetGuidUVE albedoTextureGuid;
    Asset::AssetGuidUVE normalTextureGuid;
    Asset::AssetGuidUVE aoTextureGuid;
    TextureHandleUVE albedoTexture;
    TextureHandleUVE normalTexture;
    TextureHandleUVE aoTexture;
};

/// Fixed texture-unit slots RecordItemsUVE() binds every material's three textures to, and the
/// matching sampler uniform names a material's fragment shader is expected to declare (a
/// sampler2D uniform is just an int uniform holding a texture unit index in GL — SetUniformIntUVE
/// is reused for this, no new RHI needed). A material shader that doesn't declare one of these
/// samplers simply never reads the corresponding bind (SetUniformIntUVE's own documented
/// safe-no-op contract for an unknown/optimized-out uniform name).
constexpr std::uint32_t kAlbedoTextureSlotUVE = 0;
constexpr std::uint32_t kNormalTextureSlotUVE = 1;
constexpr std::uint32_t kAoTextureSlotUVE = 2;

/// Slot the directional-light shadow map is bound to for the main color pass (Increment 26) —
/// the next slot after the three material texture slots above, following the same fixed-constant
/// convention.
constexpr std::uint32_t kShadowMapTextureSlotUVE = 3U;

// The renderer always clears its scene target. Desktop uses HDR RGBA16F while Android uses the
// GLES3-safe RGBA8 variant below. This neutral charcoal is the intentional empty-scene environment
// baseline; it is not an editor overlay and never counts as primitive presentation evidence in the
// real-GL fixture tests.
constexpr std::array<float, 4> kDefaultSceneClearColorUVE{0.050F, 0.050F, 0.050F, 1.0F};
// GLES3 devices do not universally expose float color-buffer renderability as a core guarantee.
// Keep desktop's HDR scene target, but use the core RGBA8 color-renderable format on Android so
// the real NativeActivity viewport remains valid without requiring an optional extension.
#if defined(__ANDROID__)
constexpr TextureFormatUVE kSceneColorTargetFormatUVE = TextureFormatUVE::RGBA8Unorm;
#else
constexpr TextureFormatUVE kSceneColorTargetFormatUVE = TextureFormatUVE::RGBA16Float;
#endif
constexpr std::size_t kShadowCascadeCountUVE = 3;
constexpr std::uint32_t kShadowCascadeFirstTextureSlotUVE = kShadowMapTextureSlotUVE;

// Phase 2b post-process tuning. Not currently exposed as public API - PostProcessSettingsUVE only
// asks for an enabled/disabled toggle per effect, not per-parameter tuning - but kept as named
// constants rather than inline magic numbers so a future increment can promote them without
// hunting through the render-graph pass callbacks below.
constexpr float kBloomThresholdUVE = 1.0F;
constexpr float kSsaoRadiusUVE = 0.5F;
constexpr float kSsaoBiasUVE = 0.025F;
constexpr float kSsaoIntensityUVE = 1.0F;

[[nodiscard]] constexpr std::uint32_t HalfExtentUVE(const std::uint32_t extent) noexcept {
    return std::max(1U, extent / 2U);
}

using ShadowCascadeMatricesUVE = std::array<Math::Matrix4x4UVE, kShadowCascadeCountUVE>;
using ShadowCascadeSplitsUVE = std::array<float, kShadowCascadeCountUVE>;

struct LightUniformNamesUVE {
    std::string type;
    std::string position;
    std::string direction;
    std::string color;
    std::string intensity;
    std::string range;
    std::string spotAngleDegrees;
};

struct RendererUniformNamesUVE {
    std::array<LightUniformNamesUVE, kMaxLightsUVE> lights{};
    std::string legacyLightSpaceMatrix;
    std::array<std::string, kShadowCascadeCountUVE> lightSpaceMatrices{};
    std::array<std::string, kShadowCascadeCountUVE> shadowCascadeSplits{};
    std::array<std::string, kShadowCascadeCountUVE> shadowMapTextures{};
    std::array<std::string, kShadowCascadeCountUVE> shadowPasses{};

    RendererUniformNamesUVE() : legacyLightSpaceMatrix("uLightSpaceMatrix") {
        for (std::size_t lightIndex = 0; lightIndex < kMaxLightsUVE; ++lightIndex) {
            const std::string prefix = "uLights[" + std::to_string(lightIndex) + "].";
            lights[lightIndex] = LightUniformNamesUVE{
                prefix + "type", prefix + "position", prefix + "direction", prefix + "color",
                prefix + "intensity", prefix + "range", prefix + "spotAngleDegrees"};
        }
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
            const std::string index = std::to_string(cascadeIndex);
            lightSpaceMatrices[cascadeIndex] = "uLightSpaceMatrices[" + index + "]";
            shadowCascadeSplits[cascadeIndex] = "uShadowCascadeSplits[" + index + "]";
            shadowMapTextures[cascadeIndex] = "uShadowMapTextures[" + index + "]";
            shadowPasses[cascadeIndex] = "DirectionalShadowCascade" + index;
        }
    }
};

[[nodiscard]] const RendererUniformNamesUVE& GetRendererUniformNamesUVE() {
    static const RendererUniformNamesUVE names;
    return names;
}

[[nodiscard]] float SanitizeFiniteNonNegativeUVE(float value, float fallback, std::string_view name) noexcept {
    const bool finite = std::isfinite(value);
    UVE_ASSERT(finite);
    if (!finite) {
        UVE_ERROR("Renderer3DUVE: {} must be finite; using {}", name, fallback);
        return fallback;
    }
    return std::max(value, 0.0F);
}

[[nodiscard]] float SanitizeFiniteClampedUVE(float value, float fallback, float minimum, float maximum,
                                              std::string_view name) noexcept {
    const bool finite = std::isfinite(value);
    UVE_ASSERT(finite);
    if (!finite) {
        UVE_ERROR("Renderer3DUVE: {} must be finite; using {}", name, fallback);
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] ShadowCascadeSplitsUVE ComputeCascadeSplitsUVE(float nearPlane, float farPlane,
                                                              float splitLambda) noexcept {
    const bool validPlanes = std::isfinite(nearPlane) && nearPlane > 0.0F && std::isfinite(farPlane) &&
                              farPlane > nearPlane;
    const bool validLambda = std::isfinite(splitLambda);
    UVE_ASSERT(validPlanes && validLambda);
    if (!validPlanes) {
        UVE_ERROR("Renderer3DUVE: ComputeCascadeSplitsUVE received invalid shadow clip planes");
        return ShadowCascadeSplitsUVE{};
    }
    if (!validLambda) {
        UVE_ERROR("Renderer3DUVE: ComputeCascadeSplitsUVE received a non-finite split lambda; using 0.5");
    }
    ShadowCascadeSplitsUVE splits{};
    const float clampedLambda = std::clamp(validLambda ? splitLambda : 0.5F, 0.0F, 1.0F);
    for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
        const float progress = static_cast<float>(cascadeIndex + 1U) / static_cast<float>(kShadowCascadeCountUVE);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * progress;
        const float logarithmicSplit = nearPlane * std::pow(farPlane / nearPlane, progress);
        splits[cascadeIndex] = uniformSplit * (1.0F - clampedLambda) + logarithmicSplit * clampedLambda;
    }
    return splits;
}

[[nodiscard]] bool AreCascadeSplitsValidUVE(const ShadowCascadeSplitsUVE& splits, float nearPlane,
                                             float farPlane) noexcept {
    float previousSplit = nearPlane;
    for (const float split : splits) {
        if (!std::isfinite(split) || split <= previousSplit || split > farPlane) {
            return false;
        }
        previousSplit = split;
    }
    return true;
}

[[nodiscard]] CameraFrustumCornersUVE ComputeCascadeFrustumCornersUVE(
    const CameraFrustumCornersUVE& fullCameraCorners, float nearRatio, float farRatio) noexcept {
    CameraFrustumCornersUVE cascadeCorners{};
    for (std::size_t cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
        const Math::Vector3UVE nearCorner = fullCameraCorners[cornerIndex];
        const Math::Vector3UVE farCorner = fullCameraCorners[cornerIndex + 4U];
        cascadeCorners[cornerIndex] = nearCorner + (farCorner - nearCorner) * nearRatio;
        cascadeCorners[cornerIndex + 4U] = nearCorner + (farCorner - nearCorner) * farRatio;
    }
    return cascadeCorners;
}

/// 1x1 RGBA8Unorm pixel data for the two fallback textures created once per Renderer3DUVE
/// instance (see ImplUVE::fallbackWhiteTexture/fallbackNormalTexture's own doc comments).
constexpr std::array<std::uint8_t, 4> kWhitePixelUVE{0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 4> kFlatNormalPixelUVE{0x80, 0x80, 0xFF, 0xFF};

/// Translates a loaded TextureAssetUVE's format into the RHI's own TextureFormatUVE (a
/// deliberately separate enum — see Asset::TextureFormatUVE's own doc comment for why). Asset
/// textures never use Depth32Float (that's only ever created directly as a GPU render target), so
/// this mapping is exhaustive over Asset::TextureFormatUVE's two enumerators.
[[nodiscard]] TextureFormatUVE ToRenderTextureFormatUVE(Asset::TextureFormatUVE format) noexcept {
    switch (format) {
        case Asset::TextureFormatUVE::RGBA8Unorm:
            return TextureFormatUVE::RGBA8Unorm;
        case Asset::TextureFormatUVE::RGBA16Float:
            return TextureFormatUVE::RGBA16Float;
    }
    UVE_ASSERT(false && "Unhandled Asset::TextureFormatUVE");
    return TextureFormatUVE::RGBA8Unorm;
}

/// Finds the first active Directional light in `lights` for the shadow depth pre-pass (Increment
/// 26) — Point/Spot shadows are out of scope this increment (see docs/CODING_STANDARDS.md). A
/// simple linear scan, no sorting: the same first-N-encountered spirit as
/// ILightSystemUVE::ExtractActiveLightsUVE() itself, not a distance- or importance-based
/// selection. Returns nullptr if no active (intensity > 0) Directional light exists this frame.
[[nodiscard]] const LightDataUVE* FindShadowCasterUVE(const LightListUVE& lights) noexcept {
    for (const LightDataUVE& light : lights) {
        if (light.type == Scene::LightTypeUVE::Directional && light.intensity > 0.0F) {
            return &light;
        }
    }
    return nullptr;
}

[[nodiscard]] Math::Vector3UVE ResolveWorldEnvironmentAmbientUVE(
    Scene::IEntityManagerUVE& entityManager, const Math::Vector3UVE fallbackAmbient) noexcept {
    Math::Vector3UVE ambient = fallbackAmbient;
    bool environmentFound = false;
    entityManager.ForEachUVE<Scene::WorldEnvironment3DNodeComponentUVE>(
        [&ambient, &environmentFound](Scene::EntityUVE,
                                      const Scene::WorldEnvironment3DNodeComponentUVE& environment) {
            if (environmentFound || !Scene::IsWorldEnvironment3DNodeComponentValidUVE(environment)) {
                return;
            }
            const Math::Vector3UVE resolved{environment.ambientColor.x * environment.ambientEnergy,
                                            environment.ambientColor.y * environment.ambientEnergy,
                                            environment.ambientColor.z * environment.ambientEnergy};
            if (!IsFiniteVectorUVE(resolved)) {
                return;
            }
            ambient = resolved;
            environmentFound = true;
        });
    return IsFiniteVectorUVE(ambient) ? ambient : Math::Vector3UVE{};
}

[[nodiscard]] bool AreShadowMapTargetsValidUVE(
    const std::array<TextureHandleUVE, kShadowCascadeCountUVE>& shadowMapTargets) noexcept {
    return std::all_of(shadowMapTargets.cbegin(), shadowMapTargets.cend(),
                       [](const TextureHandleUVE target) { return target != kInvalidTextureHandleUVE; });
}

void DestroyTextureIfValidUVE(IRenderDeviceUVE& renderDevice, const TextureHandleUVE texture) {
    if (texture != kInvalidTextureHandleUVE) {
        renderDevice.DestroyTextureUVE(texture);
    }
}

void DestroyBufferIfValidUVE(IRenderDeviceUVE& renderDevice, const BufferHandleUVE buffer) {
    if (buffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(buffer);
    }
}

/// MeshVertexUVE's binary layout (position, normal, UV, tangent, handedness — see
/// mesh_asset_uve.h), described once here for CreatePipelineUVE(). MeshVertexUVE is a
/// standard-layout aggregate of Math::Vector3UVE (itself standard-layout) and floats, so offsetof()
/// is well-defined.
const std::vector<VertexAttributeUVE>& MeshVertexLayoutUVE() {
    static const std::vector<VertexAttributeUVE> layout = {
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, offsetof(Asset::MeshVertexUVE, position)},
        VertexAttributeUVE{"NORMAL", VertexAttributeFormatUVE::Float3, offsetof(Asset::MeshVertexUVE, normal)},
        VertexAttributeUVE{"TEXCOORD0", VertexAttributeFormatUVE::Float2, offsetof(Asset::MeshVertexUVE, u)},
        VertexAttributeUVE{"TANGENT", VertexAttributeFormatUVE::Float4, offsetof(Asset::MeshVertexUVE, tangent)},
    };
    return layout;
}

/// Frame-constant uniform data threaded into RecordItemsUVE() for every item this frame: the
/// view-projection matrix, the rendering camera's world position (Increment 24 — the view vector
/// a specular term needs), up to kMaxLightsUVE active lights (Increment 25 — Point/Spot +
/// multi-light; trailing unused slots hold the "no light" LightDataUVE{} sentinel from
/// ILightSystemUVE::ExtractActiveLightsUVE()), and the global ambient term. Bundled into one
/// struct — mirroring PipelineDescUVE's/RenderPassDescUVE's own precedent for grouping related
/// descriptor data — rather than growing RecordItemsUVE's parameter list to six positional
/// parameters. Module-private: never crosses the RHI boundary, unlike PipelineDescUVE.
struct FrameUniformsUVE {
    Math::Matrix4x4UVE viewProjection;
    Math::Vector3UVE viewPosition;
    LightListUVE lights;
    Math::Vector3UVE ambientColor;

    /// Fixed three-cascade directional-shadow contract. A zero cascadeCount is the no-directional
    /// light sentinel; all maps remain cleared to 1.0 and material shaders naturally evaluate lit.
    ShadowCascadeMatricesUVE lightSpaceMatrices{};
    ShadowCascadeSplitsUVE cascadeSplits{};
    std::int32_t cascadeCount = 0;
    float cascadeBlendRatio = 0.0F;
};

} // namespace

struct Renderer3DUVE::ImplUVE {
    IRenderDeviceUVE& renderDevice;
    IRenderSystemUVE& renderSystem;
    IMeshRendererUVE& meshRenderer;
    ICameraSystemUVE& cameraSystem;
    ILightSystemUVE& lightSystem;
    Shader::IShaderManagerUVE& shaderManager;
    Asset::IAssetManagerUVE& assetManager;
    Asset::IAssetDatabaseUVE& assetDatabase;
    Events::IEventSystemUVE& eventSystem;
    const RendererUniformNamesUVE& uniformNames;
    std::uint32_t targetWidth;
    std::uint32_t targetHeight;

    /// One complete set of size-dependent offscreen targets, kept so a renderer that alternates
    /// between a few sizes can switch between them instead of reallocating.
    ///
    /// This exists because of ViewportManagerUVE::RenderAllPanesUVE(): it drives ONE shared
    /// renderer across every pane, resizing it to each pane's pixel size in turn. Without a cache,
    /// a split view of differently-sized panes destroyed and recreated all SIX of these textures
    /// per pane per frame, forever - not a warm-up cost, a permanent one. That churn is what the
    /// ViewportManagerUVE header documents as "a per-pane cached target pool would avoid".
    ///
    /// Keyed by size rather than by pane, deliberately: the renderer has no pane concept and
    /// should not acquire one, and two panes that happen to share a size should share a set.
    struct SizedTargetSetUVE final {
        TextureHandleUVE colorTarget = kInvalidTextureHandleUVE;
        TextureHandleUVE depthTarget = kInvalidTextureHandleUVE;
        TextureHandleUVE bloomBrightTarget = kInvalidTextureHandleUVE;
        TextureHandleUVE bloomBlurTargetA = kInvalidTextureHandleUVE;
        TextureHandleUVE bloomBlurTargetB = kInvalidTextureHandleUVE;
        TextureHandleUVE ssaoTarget = kInvalidTextureHandleUVE;
    };

    /// Bounded on purpose. A caller that resizes to a genuinely new size every frame - a window
    /// being dragged - must not accumulate texture sets without limit, so the cache is cleared
    /// once it exceeds this and rebuilt from the sizes actually in use. Small, because the case
    /// this serves is a handful of panes, not an arbitrary set of resolutions.
    static constexpr std::size_t kMaximumCachedTargetSetsUVE = 8U;

    /// Size -> its target set. The ACTIVE set's handles are also mirrored into the colorTarget/
    /// depthTarget/... members below, so every pass that reads them is untouched by this cache.
    std::map<std::pair<std::uint32_t, std::uint32_t>, SizedTargetSetUVE> targetSetCache;

    /// Copied only through IRenderer3DUVE::GetLastFrameDiagnosticsUVE(). Recorded counts are
    /// CPU-side renderer facts; the OpenGL-issued count never asserts completed presentation.
    Renderer3DFrameDiagnosticsUVE lastFrameDiagnostics;

    /// Flat ambient term added to every rendered item every frame, regardless of whether an
    /// active light exists this frame (see EngineConfigUVE::ambientColor, Increment 23).
    Math::Vector3UVE ambientColor;

    /// Shadow depth pre-pass tuning (see EngineConfigUVE::shadowMapResolution/shadowMapHalfExtent/
    /// shadowMapNearPlane/shadowMapFarPlane, Increment 26).
    std::uint32_t shadowMapResolution;
    float shadowMapHalfExtent;
    float shadowMapNearPlane;
    float shadowMapFarPlane;
    float shadowFrustumPadding;
    float shadowCascadeSplitLambda;

    /// Fraction of each non-final cascade range that cross-fades into the following cascade.
    /// The constructor keeps it bounded so canonical shader sampling has a predictable cost.
    float shadowCascadeBlendRatio;

    /// Bounded per-fragment PCF radius supplied to the canonical directional-shadow material
    /// shader. Zero keeps a hard comparison; the constructor clamps larger requested values to 2.
    std::int32_t shadowPcfKernelRadius;

    TextureHandleUVE colorTarget;
    TextureHandleUVE depthTarget;

    /// Persistent depth-only render target the shadow depth pre-pass renders into every frame
    /// (Increment 26) — unlike depthTarget above (the main pass's own depth buffer, written and
    /// never sampled), this is later bound as a sampled texture input during the main color pass.
    std::array<TextureHandleUVE, kShadowCascadeCountUVE> shadowMapTargets{};

    /// The built-in shadow-depth vertex+fragment program (engine/render/shader/built_in/
    /// shadow_depth.glsl), compiled once via shaderManager at construction — not tied to any
    /// MaterialAssetUVE, matching EngineCoreUVE's demo-triangle precedent for a built-in
    /// (non-material) shader. May still be compiling (IsReadyUVE() == false) or have failed
    /// (IsValidUVE() == false) on any given frame; RecordShadowPassUVE() checks IsValidUVE()
    /// before every use, exactly like RenderDemoTriangleUVE() does.
    std::shared_ptr<Shader::ShaderProgramUVE> shadowProgram;
    /// The UVE_INSTANCED build of the same shadow source. Null-checked at use; the non-instanced
    /// program is the fallback, so a link failure costs speed rather than shadows.
    std::shared_ptr<Shader::ShaderProgramUVE> instancedShadowProgram;
    /// Batch scratch for the shadow cascades, one per cascade so consecutive cascades do not
    /// thrash a single set's capacity.
    std::array<RenderBatchSetUVE, kShadowCascadeCountUVE> shadowBatches;
    /// This frame's frustum-independent candidate set, reused across frames so a scene-sized
    /// vector is not reallocated every frame.
    MeshVisibilitySetUVE visibilitySet;
    std::shared_ptr<Shader::ShaderProgramUVE> toneMappingProgram;

    /// Phase 2b post-process toggles, consulted while building each frame's render graph (see
    /// RenderFrameUVE()) - disabling either skips that group of passes entirely, not just their
    /// visual contribution.
    PostProcessSettingsUVE postProcessSettings{};

    /// Bloom intermediate targets, at half the main color target's resolution (a standard
    /// perf/quality tradeoff for a blurred, low-frequency effect) and the same HDR-capable format
    /// as colorTarget, since bright-pass extraction happens before tone mapping. bloomBrightTarget
    /// holds the thresholded bright pixels; bloomBlurTargetA/B ping-pong the separable Gaussian
    /// blur's horizontal then vertical pass.
    TextureHandleUVE bloomBrightTarget;
    TextureHandleUVE bloomBlurTargetA;
    TextureHandleUVE bloomBlurTargetB;
    std::shared_ptr<Shader::ShaderProgramUVE> bloomBrightPassProgram;
    std::shared_ptr<Shader::ShaderProgramUVE> bloomBlurProgram;
    /// fullscreen_copy.glsl compiled with PipelineBlendModeUVE::Additive - composites the blurred
    /// bloom result onto colorTarget.
    std::shared_ptr<Shader::ShaderProgramUVE> bloomCompositeProgram;

    /// SSAO occlusion target, also at half resolution (screen-space AO tolerates the softer detail
    /// well, and it more than halves the per-pixel hemisphere-kernel sampling cost).
    TextureHandleUVE ssaoTarget;
    std::shared_ptr<Shader::ShaderProgramUVE> ssaoProgram;
    /// fullscreen_copy.glsl compiled with PipelineBlendModeUVE::Multiply - composites the SSAO
    /// occlusion term onto colorTarget.
    std::shared_ptr<Shader::ShaderProgramUVE> ssaoCompositeProgram;

    /// Minimal built-in particle program and reusable CPU-expanded vertex buffer. ShaderManagerUVE
    /// owns the linked pipeline lifetime; Renderer3DUVE owns only the buffer and releases it in its
    /// destructor. The fixed capacity bounds both per-frame upload bytes and draw vertices.
    std::shared_ptr<Shader::ShaderProgramUVE> particleProgram;
    BufferHandleUVE particleVertexBuffer;
    std::vector<ParticleVertexUVE> particleVertexStaging;

    /// GPU instancing state, rebuilt every frame. Two matrix buffers rather than one because the
    /// shader needs both the model matrix and its inverse-transpose, and computing the latter in
    /// the vertex shader would mean inverting a matrix once per VERTEX instead of once per object.
    /// The base-index buffer is a one-int SSBO for the same reason every compute kernel's params
    /// are: it is the portable way to get a scalar to a shader.
    BufferHandleUVE instanceTransformBuffer;
    BufferHandleUVE instanceNormalTransformBuffer;
    BufferHandleUVE instanceBaseBuffer;
    std::size_t instanceBufferCapacity = 0U;
    RenderBatchSetUVE opaqueBatches;
    RenderBatchSetUVE transparentBatches;
    std::vector<Math::Matrix4x4UVE> instanceNormalMatrixStaging;
    std::vector<Math::Matrix4x4UVE> instanceTransposeStaging;
    /// Reset at the top of every frame and copied into the frame diagnostics at the end. These
    /// name what instancing actually did this frame rather than what it was asked to do: a scene
    /// of legacy materials reports zero, which is the honest answer.
    std::size_t instancedDrawCallsThisFrame = 0U;
    std::size_t instancedObjectsThisFrame = 0U;
    /// Shadow-pass instanced draws across all cascades this frame. Counted separately from the
    /// main pass because the shadow passes are where the draw-call count was actually worst - one
    /// per caster per cascade - and a single combined number would hide which half improved.
    std::size_t shadowInstancedDrawCallsThisFrame = 0U;

    /// Built-in primitive visualization program. Its Basic3D contract contains only model,
    /// view-projection, and authored base-color uniforms; primitives intentionally do not bind
    /// material, texture, light, or shadow state.
    std::shared_ptr<Shader::ShaderProgramUVE> primitiveProgram;

    /// A 1x1 opaque-white texture, used whenever a material leaves albedoTexture/aoTexture unset
    /// (kInvalidAssetGuidUVE) — sampling it always yields {1,1,1,1}, so
    /// `texture(uAlbedoTexture, uv) * uAlbedoColor == uAlbedoColor` and
    /// `texture(uAOTexture, uv).r == 1.0` (no occlusion), with no shader-side branching needed.
    TextureHandleUVE fallbackWhiteTexture;

    /// A 1x1 flat tangent-space "up" normal texture ({0.5,0.5,1.0} encoded as {128,128,255}),
    /// used whenever a material leaves normalTexture unset. Bound/sampled for
    /// forward-compatibility with a future lighting increment; unused in this increment's unlit
    /// color output.
    TextureHandleUVE fallbackNormalTexture;

    std::unordered_map<Asset::AssetGuidUVE, MeshGpuResourcesUVE> meshCache;
    std::unordered_map<std::uint8_t, MeshGpuResourcesUVE> primitiveMeshCache;
    std::unordered_map<Asset::AssetGuidUVE, MaterialGpuResourcesUVE> materialCache;

    /// GPU textures uploaded from a loaded TextureAssetUVE, cached by that texture's own
    /// AssetGuidUVE — independent of materialCache, so two materials sharing an albedo texture
    /// GUID upload it only once.
    std::unordered_map<Asset::AssetGuidUVE, TextureHandleUVE> textureCache;
    /// Permanent texture-resolution failures (asset load or GPU upload) remain on the fallback
    /// path until an explicit texture asset reload event. This prevents repeated failed-handle
    /// diagnostics and repeated invalid GPU uploads after a material cache rebuild.
    std::unordered_set<Asset::AssetGuidUVE> failedTextureGuids;
    RenderGraphUVE renderGraph;
    RenderQueueUVE frameQueue;
    std::array<RenderQueueUVE, kShadowCascadeCountUVE> shadowQueues;
    std::vector<PrimitiveRenderItemUVE> primitiveItems;

    /// Last frame's primitive placement per entity, reused when nothing about that entity changed.
    ///
    /// WHY THIS EXISTS. The mesh path already caches placement; the primitive path was left
    /// recomputing the same TRS compose and bounds transform every frame for objects that had not
    /// moved. Measured on this engine's own maths: recomputing a primitive placement costs about
    /// 123 us per 1000, comparing the key and reusing the answer about 1.8 us - roughly 67x.
    /// Primitives are overwhelmingly static scene dressing, so nearly all of that work was
    /// recomputing the previous frame's answer.
    ///
    /// Keyed by EntityUVE, which is generational, so a destroyed entity whose index is reused gets
    /// a different key and cannot inherit the dead entity's bounds. Bounded by the same
    /// seen-this-frame prune the mesh cache uses - without it a long session retains an entry for
    /// every primitive the scene ever had.
    std::unordered_map<Scene::EntityUVE, PrimitivePlacementCacheEntryUVE> primitivePlacementCache;

    /// Monotonic stamp for the prune above. Starts at 0 and is pre-incremented, so a live entry
    /// always has a non-zero lastSeenFrame and "never populated" stays distinguishable.
    std::uint64_t primitiveFrameIndex = 0U;
    ParticleDrawRecordingUVE particleDrawRecording;
    Events::EventSubscriptionUVE reloadSubscription;
    const Scene::ParticleRuntimeUVE* particleRuntimeForFrame = nullptr;

    /// Set only for the duration of a RenderFrameToRegionUVE() call (Phase 3's ViewportManagerUVE
    /// split-view support), mirroring particleRuntimeForFrame's own scoped-field pattern just
    /// above. The ToneMapping pass reads this when building its RenderPassDescUVE, so a full-frame
    /// RenderFrameUVE() call (this field left nullopt) is unaffected.
    std::optional<ViewportRectUVE> destinationViewportOverride;

    /// Set only for the duration of a RenderFrameToTargetUVE() call, mirroring
    /// destinationViewportOverride's own scoped-field pattern directly above. The ToneMapping pass
    /// reads this when building its RenderPassDescUVE's colorAttachment/depthAttachment, so a
    /// full-frame RenderFrameUVE() call (this field left nullopt) still targets the presentation
    /// surface exactly as before.
    std::optional<std::pair<TextureHandleUVE, TextureHandleUVE>> destinationTextureOverride;

    /// Set only alongside destinationTextureOverride, from RenderFrameToTargetUVE()'s own
    /// width/height parameters - TextureHandleUVE has no queryable size on IRenderDeviceUVE, so the
    /// UIOverlay pass's orthographic projection needs this supplied directly rather than read from
    /// targetWidth/targetHeight (which describe this renderer's own internal color/depth targets,
    /// not the caller-supplied destination texture pair). A zero width/height in either slot means
    /// "not usable for UI" (see the UIOverlay pass's own gating), matching the interface's
    /// documented default of skipping UI when the size is unknown.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> destinationTextureSizeOverride;

    /// Set via SetUIRuntimeUVE(); read fresh by the "UIOverlay" pass every RenderFrame* call.
    const UI::UIRuntimeUVE* uiRuntimeForFrame = nullptr;
    /// Built-in UI overlay program (engine/render/shader/built_in/ui_overlay.glsl) - position +
    /// texcoord + vertex color, alpha-blended, samples whichever texture is bound (the font atlas
    /// for glyph quads, fallbackWhiteTexture for solid/image quads - see RecordUIOverlayItemsUVE).
    std::shared_ptr<Shader::ShaderProgramUVE> uiOverlayProgram;
    /// The font atlas's baked RGBA8 bitmap, uploaded to the GPU lazily on first use (the bitmap
    /// never changes after baking, so one upload for the renderer's whole lifetime suffices).
    TextureHandleUVE uiFontAtlasTexture = kInvalidTextureHandleUVE;
    BufferHandleUVE uiVertexBuffer = kInvalidBufferHandleUVE;
    std::vector<UIVertexUVE> uiVertexStaging;

    ImplUVE(IRenderDeviceUVE& renderDeviceIn, IRenderSystemUVE& renderSystemIn, IMeshRendererUVE& meshRendererIn,
            ICameraSystemUVE& cameraSystemIn, ILightSystemUVE& lightSystemIn,
            Shader::IShaderManagerUVE& shaderManagerIn, Asset::IAssetManagerUVE& assetManagerIn,
            Asset::IAssetDatabaseUVE& assetDatabaseIn, Events::IEventSystemUVE& eventSystemIn,
            std::uint32_t targetWidthIn, std::uint32_t targetHeightIn, Math::Vector3UVE ambientColorIn,
            std::uint32_t shadowMapResolutionIn, float shadowMapHalfExtentIn, float shadowMapNearPlaneIn,
            float shadowMapFarPlaneIn, float shadowFrustumPaddingIn, float shadowCascadeSplitLambdaIn,
            float shadowCascadeBlendRatioIn, std::uint32_t shadowPcfKernelRadiusIn)
        : renderDevice(renderDeviceIn), renderSystem(renderSystemIn), meshRenderer(meshRendererIn),
          cameraSystem(cameraSystemIn), lightSystem(lightSystemIn), shaderManager(shaderManagerIn),
          assetManager(assetManagerIn), assetDatabase(assetDatabaseIn), eventSystem(eventSystemIn),
          uniformNames(GetRendererUniformNamesUVE()), targetWidth(targetWidthIn), targetHeight(targetHeightIn), ambientColor(ambientColorIn),
          shadowMapResolution(shadowMapResolutionIn), shadowMapHalfExtent(shadowMapHalfExtentIn),
          shadowMapNearPlane(shadowMapNearPlaneIn), shadowMapFarPlane(shadowMapFarPlaneIn),
          shadowFrustumPadding(SanitizeFiniteNonNegativeUVE(shadowFrustumPaddingIn, 0.0F,
                                                             "shadowFrustumPadding")),
          shadowCascadeSplitLambda(SanitizeFiniteClampedUVE(shadowCascadeSplitLambdaIn, 0.5F, 0.0F, 1.0F,
                                                            "shadowCascadeSplitLambda")),
          shadowCascadeBlendRatio(SanitizeFiniteClampedUVE(shadowCascadeBlendRatioIn, 0.0F, 0.0F, 0.25F,
                                                            "shadowCascadeBlendRatio")),
          shadowPcfKernelRadius(static_cast<std::int32_t>(std::min(shadowPcfKernelRadiusIn, 2U))) {}

    /// Creates a complete target set for `width`x`height`, or returns nullopt having destroyed any
    /// partial allocation. All six are created together because a half-built set is not usable and
    /// would only defer the failure into a pass.
    [[nodiscard]] std::optional<SizedTargetSetUVE> CreateTargetSetUVE(const std::uint32_t width,
                                                                      const std::uint32_t height) {
        SizedTargetSetUVE set;
        set.colorTarget =
            renderDevice.CreateTextureUVE(TextureDescUVE{width, height, kSceneColorTargetFormatUVE, 1});
        set.depthTarget =
            renderDevice.CreateTextureUVE(TextureDescUVE{width, height, TextureFormatUVE::Depth32Float, 1});
        const std::uint32_t halfWidth = HalfExtentUVE(width);
        const std::uint32_t halfHeight = HalfExtentUVE(height);
        set.bloomBrightTarget =
            renderDevice.CreateTextureUVE(TextureDescUVE{halfWidth, halfHeight, kSceneColorTargetFormatUVE, 1});
        set.bloomBlurTargetA =
            renderDevice.CreateTextureUVE(TextureDescUVE{halfWidth, halfHeight, kSceneColorTargetFormatUVE, 1});
        set.bloomBlurTargetB =
            renderDevice.CreateTextureUVE(TextureDescUVE{halfWidth, halfHeight, kSceneColorTargetFormatUVE, 1});
        set.ssaoTarget =
            renderDevice.CreateTextureUVE(TextureDescUVE{halfWidth, halfHeight, TextureFormatUVE::RGBA8Unorm, 1});

        // Color and depth are load-bearing: without them there is no frame at all. The
        // post-process four are not - RenderFrameUVE() guards on their validity and skips those
        // passes - so a set missing only those is still returned and still usable, preserving the
        // pre-cache behavior exactly.
        if (set.colorTarget == kInvalidTextureHandleUVE || set.depthTarget == kInvalidTextureHandleUVE) {
            DestroyTargetSetUVE(set);
            return std::nullopt;
        }
        if (set.bloomBrightTarget == kInvalidTextureHandleUVE ||
            set.bloomBlurTargetA == kInvalidTextureHandleUVE ||
            set.bloomBlurTargetB == kInvalidTextureHandleUVE || set.ssaoTarget == kInvalidTextureHandleUVE) {
            DestroyTextureIfValidUVE(renderDevice, set.bloomBrightTarget);
            DestroyTextureIfValidUVE(renderDevice, set.bloomBlurTargetA);
            DestroyTextureIfValidUVE(renderDevice, set.bloomBlurTargetB);
            DestroyTextureIfValidUVE(renderDevice, set.ssaoTarget);
            set.bloomBrightTarget = kInvalidTextureHandleUVE;
            set.bloomBlurTargetA = kInvalidTextureHandleUVE;
            set.bloomBlurTargetB = kInvalidTextureHandleUVE;
            set.ssaoTarget = kInvalidTextureHandleUVE;
            UVE_WARNING("Renderer3DUVE: post-process target creation failed at {}x{}; bloom/SSAO "
                        "passes will be skipped at this size",
                        halfWidth, halfHeight);
        }
        return set;
    }

    void DestroyTargetSetUVE(const SizedTargetSetUVE& set) {
        DestroyTextureIfValidUVE(renderDevice, set.colorTarget);
        DestroyTextureIfValidUVE(renderDevice, set.depthTarget);
        DestroyTextureIfValidUVE(renderDevice, set.bloomBrightTarget);
        DestroyTextureIfValidUVE(renderDevice, set.bloomBlurTargetA);
        DestroyTextureIfValidUVE(renderDevice, set.bloomBlurTargetB);
        DestroyTextureIfValidUVE(renderDevice, set.ssaoTarget);
    }

    /// Points the active target members at `set`. Every render pass reads these members, so
    /// switching sets is exactly this and nothing more - the cache is invisible to the passes.
    void ActivateTargetSetUVE(const SizedTargetSetUVE& set, const std::uint32_t width,
                              const std::uint32_t height) noexcept {
        colorTarget = set.colorTarget;
        depthTarget = set.depthTarget;
        bloomBrightTarget = set.bloomBrightTarget;
        bloomBlurTargetA = set.bloomBlurTargetA;
        bloomBlurTargetB = set.bloomBlurTargetB;
        ssaoTarget = set.ssaoTarget;
        targetWidth = width;
        targetHeight = height;
    }

    [[nodiscard]] bool ResizeTargetsUVE(const std::uint32_t newWidth, const std::uint32_t newHeight) {
        if (newWidth == 0U || newHeight == 0U) {
            return false;
        }
        if (targetWidth == newWidth && targetHeight == newHeight &&
            colorTarget != kInvalidTextureHandleUVE) {
            return true;
        }

        const std::pair<std::uint32_t, std::uint32_t> key{newWidth, newHeight};
        const auto cachedIt = targetSetCache.find(key);
        if (cachedIt != targetSetCache.end()) {
            // The whole point: a size seen before costs a handful of pointer assignments, not six
            // texture allocations. This is the path a steady-state split view takes every frame.
            ActivateTargetSetUVE(cachedIt->second, newWidth, newHeight);
            return true;
        }

        // A genuinely new size. Evict first if the cache has grown past its bound - a window being
        // dragged produces a new size every frame, and those sets are dead the moment they are
        // made. Clearing wholesale rather than evicting one entry keeps this simple and cannot
        // free a set that is about to be reactivated, because the active set is re-created
        // immediately below.
        if (targetSetCache.size() >= kMaximumCachedTargetSetsUVE) {
            for (const auto& [cachedSize, cachedSet] : targetSetCache) {
                static_cast<void>(cachedSize);
                DestroyTargetSetUVE(cachedSet);
            }
            targetSetCache.clear();
            colorTarget = kInvalidTextureHandleUVE;
            depthTarget = kInvalidTextureHandleUVE;
            bloomBrightTarget = kInvalidTextureHandleUVE;
            bloomBlurTargetA = kInvalidTextureHandleUVE;
            bloomBlurTargetB = kInvalidTextureHandleUVE;
            ssaoTarget = kInvalidTextureHandleUVE;
        }

        std::optional<SizedTargetSetUVE> created = CreateTargetSetUVE(newWidth, newHeight);
        if (!created.has_value()) {
            UVE_WARNING("Renderer3DUVE: adaptive target resize rejected ({}x{}); retaining {}x{}",
                        newWidth, newHeight, targetWidth, targetHeight);
            return false;
        }

        const auto inserted = targetSetCache.emplace(key, *created);
        ActivateTargetSetUVE(inserted.first->second, newWidth, newHeight);
        UVE_INFO("Renderer3DUVE: adaptive targets resized to {}x{}", targetWidth, targetHeight);
        return true;
    }

    void EvictUnreferencedTextureCacheEntriesUVE() {
        std::unordered_set<Asset::AssetGuidUVE> referencedTextureGuids;
        for (const auto& [materialGuid, resources] : materialCache) {
            static_cast<void>(materialGuid);
            if (resources.albedoTextureGuid != Asset::kInvalidAssetGuidUVE) {
                referencedTextureGuids.insert(resources.albedoTextureGuid);
            }
            if (resources.normalTextureGuid != Asset::kInvalidAssetGuidUVE) {
                referencedTextureGuids.insert(resources.normalTextureGuid);
            }
            if (resources.aoTextureGuid != Asset::kInvalidAssetGuidUVE) {
                referencedTextureGuids.insert(resources.aoTextureGuid);
            }
        }
        for (auto textureIt = textureCache.begin(); textureIt != textureCache.end();) {
            if (!referencedTextureGuids.contains(textureIt->first)) {
                DestroyTextureIfValidUVE(renderDevice, textureIt->second);
                textureIt = textureCache.erase(textureIt);
            } else {
                ++textureIt;
            }
        }
    }

    void OnAssetReloadedUVE(const Asset::AssetReloadedEventUVE& event) {
        const auto meshIt = meshCache.find(event.guid);
        if (meshIt != meshCache.end()) {
            DestroyBufferIfValidUVE(renderDevice, meshIt->second.vertexBuffer);
            DestroyBufferIfValidUVE(renderDevice, meshIt->second.indexBuffer);
            meshCache.erase(meshIt);
        }
        // A material asset reload, or a reload of either of its separate shader assets, drops the
        // renderer cache entry. The shared managed program then releases naturally; ShaderManagerUVE
        // remains the sole owner of the linked pipeline lifecycle. Texture source GUIDs are retained
        // in each record so entries no longer referenced by a reloaded material can be retired.
        bool materialAssetReloaded = false;
        for (auto materialIt = materialCache.begin(); materialIt != materialCache.end();) {
            const MaterialGpuResourcesUVE& resources = materialIt->second;
            if (materialIt->first == event.guid || resources.vertexShaderGuid == event.guid ||
                resources.fragmentShaderGuid == event.guid) {
                materialAssetReloaded = materialAssetReloaded || materialIt->first == event.guid;
                materialIt = materialCache.erase(materialIt);
            } else {
                ++materialIt;
            }
        }

        const bool textureFailureMemoErased = failedTextureGuids.erase(event.guid) > 0U;
        const auto textureIt = textureCache.find(event.guid);
        if (textureIt != textureCache.end()) {
            DestroyTextureIfValidUVE(renderDevice, textureIt->second);
            textureCache.erase(textureIt);

            // MaterialGpuResourcesUVE doesn't track which texture GUIDs it resolved from, so
            // there's no cheap way to know which cached materials referenced this one — clear the
            // whole cache instead. Every material's pipeline and resolved texture handles get
            // recomputed lazily next frame they're drawn (mostly cache hits against textureCache
            // for anything unaffected). Coarse but simple and obviously correct, matching this
            // codebase's existing preference for whole-unit invalidation over fine-grained
            // dependency tracking (compare ShaderManagerUVE's own hot-reload).
            materialCache.clear();
        } else if (materialAssetReloaded) {
            // Shader reloads intentionally retain valid texture uploads; only a material payload
            // swap can make a previously cached texture definitively unreferenced here.
            EvictUnreferencedTextureCacheEntriesUVE();
        } else if (textureFailureMemoErased) {
            // A failed upload has no textureCache entry, but a material rebuilt after that failure
            // can still cache fallback handles. Rebuild it after the explicit texture reload so the
            // asset gets one deliberate retry instead of remaining on a stale fallback forever.
            materialCache.clear();
        }
    }

    /// Returns the cached (creating-if-needed) GPU buffers for `item`'s mesh. `item.meshHandle`
    /// is always ready by construction (MeshRendererUVE::ExtractRenderQueueUVE only includes
    /// asset-ready items), so this never fails.
    [[nodiscard]] const MeshGpuResourcesUVE& ResolveMeshGpuResourcesUVE(const RenderItemUVE& item) {
        const Asset::AssetGuidUVE guid = item.meshHandle.GetGuidUVE();
        const auto existingIt = meshCache.find(guid);
        if (existingIt != meshCache.end()) {
            return existingIt->second;
        }

        const Asset::MeshAssetUVE* const mesh = item.meshHandle.TryGetUVE();
        // Asset loaders derive tangents for legacy `.uvemodel` payloads, but runtime/custom mesh
        // loaders may construct MeshAssetUVE directly. Regenerate into this one-time GPU-upload copy
        // so every material draw has the canonical TBN input without mutating shared asset data.
        std::vector<Asset::MeshVertexUVE> vertices = mesh->vertices;
        Asset::GenerateMeshTangentsUVE(vertices, mesh->indices);
        const std::span<const Asset::MeshVertexUVE> vertexSpan(vertices);
        const std::span<const std::uint32_t> indexSpan(mesh->indices);
        const std::span<const std::byte> vertexBytes = std::as_bytes(vertexSpan);
        const std::span<const std::byte> indexBytes = std::as_bytes(indexSpan);

        const BufferHandleUVE vertexBuffer =
            renderDevice.CreateBufferUVE(BufferDescUVE{vertexBytes.size(), BufferUsageUVE::Vertex}, vertexBytes);
        const BufferHandleUVE indexBuffer =
            renderDevice.CreateBufferUVE(BufferDescUVE{indexBytes.size(), BufferUsageUVE::Index}, indexBytes);
        MeshGpuResourcesUVE resources{vertexBuffer, indexBuffer, static_cast<std::uint32_t>(mesh->indices.size())};
        if (!IsValidMeshGpuResourcesUVE(resources)) {
            DestroyBufferIfValidUVE(renderDevice, vertexBuffer);
            DestroyBufferIfValidUVE(renderDevice, indexBuffer);
            UVE_ERROR("Renderer3DUVE: mesh GPU buffer allocation failed; caching a no-draw result");
            resources = MeshGpuResourcesUVE{};
        }

        const auto insertResult = meshCache.emplace(guid, resources);
        return insertResult.first->second;
    }

    /// Returns the cached immutable GPU buffers for one built-in primitive kind. Geometry is copied
    /// only for one-time tangent generation; the canonical catalog stays immutable and shared.
    [[nodiscard]] const MeshGpuResourcesUVE& ResolvePrimitiveMeshGpuResourcesUVE(
        const Scene::PrimitiveMeshKindUVE kind) {
        const std::uint8_t key = static_cast<std::uint8_t>(kind);
        const auto existingIt = primitiveMeshCache.find(key);
        if (existingIt != primitiveMeshCache.end()) {
            return existingIt->second;
        }

        const PrimitiveGeometryUVE& geometry = GetPrimitiveGeometryUVE(kind);
        std::vector<Asset::MeshVertexUVE> vertices = geometry.vertices;
        Asset::GenerateMeshTangentsUVE(vertices, geometry.indices);
        const std::span<const Asset::MeshVertexUVE> vertexSpan(vertices);
        const std::span<const std::uint32_t> indexSpan(geometry.indices);
        const BufferHandleUVE vertexBuffer = renderDevice.CreateBufferUVE(
            BufferDescUVE{std::as_bytes(vertexSpan).size(), BufferUsageUVE::Vertex}, std::as_bytes(vertexSpan));
        const BufferHandleUVE indexBuffer = renderDevice.CreateBufferUVE(
            BufferDescUVE{std::as_bytes(indexSpan).size(), BufferUsageUVE::Index}, std::as_bytes(indexSpan));
        MeshGpuResourcesUVE resources{vertexBuffer, indexBuffer, static_cast<std::uint32_t>(geometry.indices.size())};
        if (!IsValidMeshGpuResourcesUVE(resources)) {
            DestroyBufferIfValidUVE(renderDevice, vertexBuffer);
            DestroyBufferIfValidUVE(renderDevice, indexBuffer);
            UVE_ERROR("Renderer3DUVE: primitive GPU buffer allocation failed; caching a no-draw result");
            resources = MeshGpuResourcesUVE{};
        }
        const auto insertResult = primitiveMeshCache.emplace(key, resources);
        return insertResult.first->second;
    }

    /// Returns the GPU handle for `textureGuid`, or std::nullopt if it's still loading (caller
    /// should abort this frame's resolution without caching anything, and retry next frame — the
    /// same async-non-blocking convention as the shader/mesh/material readiness checks
    /// elsewhere in this file). `kInvalidAssetGuidUVE` (unset in the source MaterialAssetUVE)
    /// returns `fallbackHandle` immediately, no load ever attempted. A texture whose load has
    /// permanently failed (HasFailedUVE()) also resolves to `fallbackHandle` — logged once — since
    /// retrying a failed load forever would never succeed.
    [[nodiscard]] std::optional<TextureHandleUVE> ResolveTextureGpuHandleUVE(Asset::AssetGuidUVE textureGuid,
                                                                              TextureHandleUVE fallbackHandle) {
        if (textureGuid == Asset::kInvalidAssetGuidUVE) {
            return fallbackHandle;
        }
        const auto existingIt = textureCache.find(textureGuid);
        if (existingIt != textureCache.end()) {
            return existingIt->second;
        }
        if (failedTextureGuids.contains(textureGuid)) {
            ++lastFrameDiagnostics.textureFallbacks;
            return fallbackHandle;
        }

        Asset::AssetHandleUVE<Asset::TextureAssetUVE> textureHandle =
            assetManager.LoadUVE<Asset::TextureAssetUVE>(textureGuid, assetDatabase);
        if (textureHandle.HasFailedUVE()) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_WARNING("Renderer3DUVE: texture asset load failed - falling back to the default texture");
            return fallbackHandle;
        }
        if (!textureHandle.IsReadyUVE()) {
            return std::nullopt;
        }

        const Asset::TextureAssetUVE* const textureAsset = textureHandle.TryGetUVE();
        const bool textureAssetPresent = textureAsset != nullptr;
        UVE_ASSERT(textureAssetPresent);
        if (!textureAssetPresent) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3DUVE: ready texture handle has no payload - falling back to the default texture");
            return fallbackHandle;
        }
        const bool textureFormatValid = textureAsset->format == Asset::TextureFormatUVE::RGBA8Unorm ||
                                        textureAsset->format == Asset::TextureFormatUVE::RGBA16Float;
        UVE_ASSERT(textureFormatValid);
        if (!textureFormatValid) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3DUVE: ready texture payload has an unknown format - falling back to the default texture");
            return fallbackHandle;
        }
        const TextureDescUVE desc{textureAsset->width, textureAsset->height,
                                   ToRenderTextureFormatUVE(textureAsset->format), 1};
        const TextureHandleUVE handle =
            renderDevice.CreateTextureUVE(desc, std::as_bytes(std::span(textureAsset->pixels)));
        if (handle == kInvalidTextureHandleUVE) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3DUVE: texture asset upload failed - falling back to the default texture");
            return fallbackHandle;
        }
        textureCache.emplace(textureGuid, handle);
        return handle;
    }

    /// Returns the cached (creating-if-needed) managed program + resolved textures for `item`'s
    /// material, or nullptr if the material's source assets/textures have not finished loading yet.
    /// The cached program may still be preprocessing or linking; RecordItemsUVE checks IsValidUVE()
    /// and skips that frame without blocking, then renders automatically once ShaderManagerUVE
    /// completes the request.

    [[nodiscard]] const MaterialGpuResourcesUVE* ResolveMaterialGpuResourcesUVE(const RenderItemUVE& item) {
        const Asset::AssetGuidUVE guid = item.materialHandle.GetGuidUVE();
        const auto existingIt = materialCache.find(guid);
        if (existingIt != materialCache.end()) {
            return &existingIt->second;
        }

        const Asset::MaterialAssetUVE* const material = item.materialHandle.TryGetUVE();
        const bool validMaterial = material != nullptr && Asset::IsMaterialAssetValidUVE(*material);
        UVE_ASSERT(validMaterial);
        if (!validMaterial) {
            UVE_ERROR("Renderer3DUVE: invalid material payload skipped before GPU uniform preparation");
            return nullptr;
        }
        Asset::AssetHandleUVE<Asset::ShaderAssetUVE> vertexShaderHandle =
            assetManager.LoadUVE<Asset::ShaderAssetUVE>(material->vertexShader, assetDatabase);
        Asset::AssetHandleUVE<Asset::ShaderAssetUVE> fragmentShaderHandle =
            assetManager.LoadUVE<Asset::ShaderAssetUVE>(material->fragmentShader, assetDatabase);
        if (!vertexShaderHandle.IsReadyUVE() || !fragmentShaderHandle.IsReadyUVE()) {
            return nullptr;
        }

        const std::optional<TextureHandleUVE> albedoTexture =
            ResolveTextureGpuHandleUVE(material->albedoTexture, fallbackWhiteTexture);
        const std::optional<TextureHandleUVE> normalTexture =
            ResolveTextureGpuHandleUVE(material->normalTexture, fallbackNormalTexture);
        const std::optional<TextureHandleUVE> aoTexture =
            ResolveTextureGpuHandleUVE(material->aoTexture, fallbackWhiteTexture);
        if (!albedoTexture.has_value() || !normalTexture.has_value() || !aoTexture.has_value()) {
            return nullptr;
        }

        const Asset::ShaderAssetUVE* const vertexShaderAsset = vertexShaderHandle.TryGetUVE();
        const Asset::ShaderAssetUVE* const fragmentShaderAsset = fragmentShaderHandle.TryGetUVE();

        // `.uveshader` assets are envelope files rather than raw GLSL files, so their already
        // decoded source is supplied as the manager fallback and the root virtual path stays empty.
        // Includes inside that source still use the normal virtual include paths and participate in
        // program-level dependency tracking; AssetReloaded events invalidate root shader assets.
        Shader::ShaderProgramStagesDescUVE programDesc;
        programDesc.vertexSource.stage = ShaderStageUVE::Vertex;
        programDesc.vertexSource.embeddedFallbackSourceCode = vertexShaderAsset->sourceCode;
        programDesc.vertexSource.entryPointName = vertexShaderAsset->entryPointName;
        programDesc.vertexSource.debugNameUVE =
            "Material vertex " + assetDatabase.ResolveUVE(material->vertexShader).string();
        programDesc.fragmentSource.stage = ShaderStageUVE::Fragment;
        programDesc.fragmentSource.embeddedFallbackSourceCode = fragmentShaderAsset->sourceCode;
        programDesc.fragmentSource.entryPointName = fragmentShaderAsset->entryPointName;
        programDesc.fragmentSource.debugNameUVE =
            "Material fragment " + assetDatabase.ResolveUVE(material->fragmentShader).string();
        programDesc.vertexLayout = MeshVertexLayoutUVE();
        programDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertexUVE));
        programDesc.depthTestEnabled = true;
        programDesc.depthWriteEnabled = !material->isTransparent;
        programDesc.debugNameUVE = "Material " + assetDatabase.ResolveUVE(guid).string();
        const std::shared_ptr<Shader::ShaderProgramUVE> program = shaderManager.CreateProgramFromStagesUVE(programDesc);

        const auto insertResult = materialCache.emplace(
            guid, MaterialGpuResourcesUVE{program,
                                          VertexSourceSupportsInstancingUVE(vertexShaderAsset->sourceCode),
                                          material->vertexShader, material->fragmentShader,
                                          material->albedoTexture, material->normalTexture, material->aoTexture,
                                          *albedoTexture, *normalTexture, *aoTexture});
        return &insertResult.first->second;
    }

    /// Renders one directional-light shadow depth pass. The caller records it only when a valid
    /// directional caster and linked shadow program exist; otherwise the main pass receives the
    /// zero-cascade sentinel and no shadow-map work is submitted. Draws every opaque item in the
    /// cascade queue (no additional light-frustum culling beyond queue extraction).
    /// Uploads shadow instance transforms. Only the model matrix, transposed - a depth-only pass
    /// has no normals, so the normal-matrix buffer the main pass fills is left untouched here.
    [[nodiscard]] bool UploadShadowInstanceTransformsUVE(
        const std::vector<Math::Matrix4x4UVE>& modelMatrices) {
        instanceTransposeStaging.clear();
        instanceTransposeStaging.reserve(modelMatrices.size());
        for (const Math::Matrix4x4UVE& model : modelMatrices) {
            instanceTransposeStaging.push_back(Math::TransposeUVE(model));
        }
        return renderDevice.UpdateBufferUVE(instanceTransformBuffer,
                                            std::as_bytes(std::span(instanceTransposeStaging)));
    }

    void RecordShadowPassUVE(const std::vector<RenderItemUVE>& items, const Math::Matrix4x4UVE& lightSpaceMatrix,
                              TextureHandleUVE shadowMapTarget, bool hasCaster,
                              RenderBatchSetUVE& batchSet, ICommandBufferUVE& commandBuffer) {
        RenderPassDescUVE passDesc;
        passDesc.colorAttachment = kInvalidTextureHandleUVE;
        passDesc.depthAttachment = shadowMapTarget;
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer.BeginRenderPassUVE(passDesc);

        if (hasCaster && shadowProgram->IsValidUVE()) {
            // Instanced where possible. This pass matters more than its low profile suggests: it
            // runs ONCE PER CASCADE, so an uninstanced 200-object scene issued 600 shadow draws
            // against 200 main-pass ones - the shadow passes cost more than the frame they
            // shadow. It also batches better than the main pass does, because a depth-only draw
            // does not care about the material: two objects sharing a mesh batch together even
            // with completely different materials.
            const bool instancedShadows =
                instancedShadowProgram != nullptr && instancedShadowProgram->IsValidUVE();
            if (instancedShadows) {
                BuildShadowBatchesUVE(items, batchSet);
                if (!batchSet.batches.empty() &&
                    batchSet.instanceMatrices.size() <= kMaximumInstancesPerFrameUVE &&
                    EnsureInstanceBufferCapacityUVE(batchSet.instanceMatrices.size()) &&
                    UploadShadowInstanceTransformsUVE(batchSet.instanceMatrices)) {
                    instancedShadowProgram->SetMatrix4x4UVE("uLightSpaceMatrix", lightSpaceMatrix);
                    for (const RenderBatchUVE& batch : batchSet.batches) {
                        const MeshGpuResourcesUVE& meshResources =
                            ResolveMeshGpuResourcesUVE(items[batch.firstItem]);
                        if (!IsValidMeshGpuResourcesUVE(meshResources)) {
                            continue;
                        }
                        const auto baseIndex = static_cast<std::int32_t>(batch.firstItem);
                        const std::span<const std::byte> baseBytes{
                            reinterpret_cast<const std::byte*>(&baseIndex), sizeof(baseIndex)};
                        if (!renderDevice.UpdateBufferUVE(instanceBaseBuffer, baseBytes)) {
                            continue;
                        }
                        instancedShadowProgram->ApplyToUVE(commandBuffer);
                        commandBuffer.BindStorageBufferUVE(instanceTransformBuffer, kInstanceTransformSlotUVE);
                        commandBuffer.BindStorageBufferUVE(instanceBaseBuffer, kInstanceBaseSlotUVE);
                        commandBuffer.BindVertexBufferUVE(meshResources.vertexBuffer);
                        commandBuffer.BindIndexBufferUVE(meshResources.indexBuffer);
                        commandBuffer.DrawIndexedUVE(meshResources.indexCount,
                                                     static_cast<std::uint32_t>(batch.itemCount));
                        ++shadowInstancedDrawCallsThisFrame;
                    }
                    commandBuffer.EndRenderPassUVE();
                    return;
                }
            }

            // Fallback: correct, just one draw per caster. Reached when the instanced program did
            // not link, or a buffer step failed.
            shadowProgram->SetMatrix4x4UVE("uLightSpaceMatrix", lightSpaceMatrix);
            for (const RenderItemUVE& item : items) {
                const MeshGpuResourcesUVE& meshResources = ResolveMeshGpuResourcesUVE(item);
                if (!IsValidMeshGpuResourcesUVE(meshResources)) {
                    continue;
                }
                shadowProgram->SetMatrix4x4UVE("uModel", item.worldMatrix);
                shadowProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindVertexBufferUVE(meshResources.vertexBuffer);
                commandBuffer.BindIndexBufferUVE(meshResources.indexBuffer);
                commandBuffer.DrawIndexedUVE(meshResources.indexCount);
            }
        }

        commandBuffer.EndRenderPassUVE();
    }

    /// Builds this frame's visible primitive list.
    ///
    /// Split deliberately into two halves. Everything that depends only on the ENTITY - normalized
    /// rotation, world matrix, world bounds - is cached across frames and keyed on the transform
    /// that produced it. Everything that depends on the VIEW - the frustum test and the sort depth
    /// - is recomputed every frame, because the camera moves even when nothing in the scene does.
    /// Caching the view-dependent half would be a correctness bug, not an optimization.
    void ExtractPrimitiveItemsUVE(Scene::IEntityManagerUVE& entityManager, const Math::FrustumUVE& frustum,
                                   std::vector<PrimitiveRenderItemUVE>& outItems) {
        outItems.clear();
        ++primitiveFrameIndex;
        entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::PrimitiveMeshComponentUVE>(
            [&](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
                const Scene::PrimitiveMeshComponentUVE& primitive) {
                if (worldTransform.dirty || !Scene::IsPrimitiveMeshComponentValidUVE(primitive)) {
                    // Returning before the cache is touched leaves any existing entry unstamped,
                    // so an entity that stays dirty or invalid is pruned rather than kept alive by
                    // a placement nobody can use.
                    return;
                }
                ++lastFrameDiagnostics.primitiveCandidates;

                const PrimitivePlacementKeyUVE key{worldTransform.worldPosition, worldTransform.worldRotation,
                                                   worldTransform.worldScale, primitive.kind};
                PrimitivePlacementCacheEntryUVE& cacheEntry = primitivePlacementCache[entity];
                if (cacheEntry.lastSeenFrame != 0U && cacheEntry.key.MatchesUVE(key)) {
                    ++lastFrameDiagnostics.primitivePlacementCacheHits;
                } else {
                    ++lastFrameDiagnostics.primitivePlacementCacheMisses;
                    cacheEntry.key = key;
                    cacheEntry.placed = TryComputePrimitivePlacementUVE(worldTransform, primitive,
                                                                        cacheEntry.worldMatrix,
                                                                        cacheEntry.worldBounds);
                }
                // Stamped on hit as well as miss: the stamp records "seen this frame", which is
                // what the prune reads. Only stamping misses would evict every stationary object -
                // precisely the objects the cache exists to serve.
                cacheEntry.lastSeenFrame = primitiveFrameIndex;

                if (!cacheEntry.placed) {
                    return;
                }

                // View-dependent from here down. Never cached.
                if (!frustum.IntersectsUVE(cacheEntry.worldBounds)) {
                    return;
                }
                const float sortDepth = frustum.planes[4U].GetSignedDistanceUVE(cacheEntry.worldBounds.GetCenterUVE());
                if (!std::isfinite(sortDepth)) {
                    return;
                }
                outItems.push_back(
                    PrimitiveRenderItemUVE{cacheEntry.worldMatrix, primitive.kind, primitive.baseColor, sortDepth});
            });

        // Erase-while-iterating over an unordered_map, safe for the erased element only, so the
        // iterator is advanced before the erase and never after. Mirrors
        // MeshVisibilitySetUVE::PruneUnseenPlacementsUVE.
        for (auto it = primitivePlacementCache.begin(); it != primitivePlacementCache.end();) {
            if (it->second.lastSeenFrame != primitiveFrameIndex) {
                it = primitivePlacementCache.erase(it);
            } else {
                ++it;
            }
        }

        std::sort(outItems.begin(), outItems.end(),
                  [](const PrimitiveRenderItemUVE& lhs, const PrimitiveRenderItemUVE& rhs) {
                      return lhs.sortDepth < rhs.sortDepth;
                  });
    }

    /// The entity-dependent half of primitive extraction, factored out so the cache stores the
    /// result of exactly one function and the miss path cannot drift from what the key promises.
    /// Returns false for any input that cannot produce finite geometry; the caller caches that
    /// rejection rather than rediscovering it every frame.
    [[nodiscard]] static bool TryComputePrimitivePlacementUVE(const Scene::WorldTransformComponentUVE& worldTransform,
                                                              const Scene::PrimitiveMeshComponentUVE& primitive,
                                                              Math::Matrix4x4UVE& outWorldMatrix,
                                                              Math::AabbUVE& outWorldBounds) {
        if (!IsFiniteVectorUVE(worldTransform.worldPosition) || !IsFiniteVectorUVE(worldTransform.worldScale) ||
            !Math::IsFiniteUVE(worldTransform.worldRotation)) {
            return false;
        }
        Math::QuaternionUVE normalizedRotation;
        if (!Math::TryNormalizeUVE(worldTransform.worldRotation, normalizedRotation)) {
            return false;
        }
        const PrimitiveGeometryUVE& geometry = GetPrimitiveGeometryUVE(primitive.kind);
        if (!IsOrderedFiniteAabbUVE(geometry.localBounds)) {
            return false;
        }
        outWorldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(worldTransform.worldPosition, normalizedRotation,
                                                           worldTransform.worldScale);
        if (!IsFiniteMatrixUVE(outWorldMatrix)) {
            return false;
        }
        outWorldBounds = geometry.localBounds.TransformUVE(outWorldMatrix);
        return IsOrderedFiniteAabbUVE(outWorldBounds);
    }

    /// transpose(inverse(model)), falling back to the model matrix itself when it is singular -
    /// the same rule the per-object path has always used, factored out so the instanced path
    /// cannot quietly adopt a different one. A singular world matrix means the item would not
    /// project to visible geometry anyway.
    [[nodiscard]] static Math::Matrix4x4UVE ComputeNormalMatrixUVE(const Math::Matrix4x4UVE& worldMatrix) {
        Math::Matrix4x4UVE inverseWorldMatrix{};
        if (Math::TryInverseUVE(worldMatrix, inverseWorldMatrix)) {
            return Math::TransposeUVE(inverseWorldMatrix);
        }
        return worldMatrix;
    }

    /// Grows the three instancing buffers to hold `instanceCount` transforms. Returns false if any
    /// allocation fails, in which case the caller records per-object instead of instanced.
    [[nodiscard]] bool EnsureInstanceBufferCapacityUVE(const std::size_t instanceCount) {
        if (instanceBaseBuffer == kInvalidBufferHandleUVE) {
            instanceBaseBuffer = renderDevice.CreateBufferUVE(
                BufferDescUVE{sizeof(std::int32_t), BufferUsageUVE::Storage});
            if (instanceBaseBuffer == kInvalidBufferHandleUVE) {
                return false;
            }
        }
        if (instanceTransformBuffer != kInvalidBufferHandleUVE && instanceBufferCapacity >= instanceCount) {
            return true;
        }
        for (BufferHandleUVE* const buffer : {&instanceTransformBuffer, &instanceNormalTransformBuffer}) {
            if (*buffer != kInvalidBufferHandleUVE) {
                renderDevice.DestroyBufferUVE(*buffer);
                *buffer = kInvalidBufferHandleUVE;
            }
        }
        instanceBufferCapacity = 0U;
        const BufferDescUVE desc{instanceCount * sizeof(Math::Matrix4x4UVE), BufferUsageUVE::Storage};
        instanceTransformBuffer = renderDevice.CreateBufferUVE(desc);
        instanceNormalTransformBuffer = renderDevice.CreateBufferUVE(desc);
        if (instanceTransformBuffer == kInvalidBufferHandleUVE ||
            instanceNormalTransformBuffer == kInvalidBufferHandleUVE) {
            return false;
        }
        instanceBufferCapacity = instanceCount;
        return true;
    }

    /// Uploads one frame's instance transforms, TRANSPOSED - Matrix4x4UVE is row-major and std430
    /// mat4 is column-major. Same convention as mesh_skin.glsl, deliberately: one rule, not two.
    [[nodiscard]] bool UploadInstanceTransformsUVE(const std::vector<Math::Matrix4x4UVE>& modelMatrices) {
        instanceNormalMatrixStaging.clear();
        instanceNormalMatrixStaging.reserve(modelMatrices.size());
        instanceTransposeStaging.clear();
        instanceTransposeStaging.reserve(modelMatrices.size());
        for (const Math::Matrix4x4UVE& model : modelMatrices) {
            instanceTransposeStaging.push_back(Math::TransposeUVE(model));
            instanceNormalMatrixStaging.push_back(Math::TransposeUVE(ComputeNormalMatrixUVE(model)));
        }
        return renderDevice.UpdateBufferUVE(instanceTransformBuffer,
                                            std::as_bytes(std::span(instanceTransposeStaging))) &&
               renderDevice.UpdateBufferUVE(instanceNormalTransformBuffer,
                                            std::as_bytes(std::span(instanceNormalMatrixStaging)));
    }

    /// Sets every uniform that does not vary per object. Shared verbatim by the per-object and
    /// instanced paths so the two cannot drift in how they light a surface - the same reason the
    /// shader keeps both variants in one file.
    void ApplyFrameAndMaterialUniformsUVE(Shader::ShaderProgramUVE& program,
                                          const Asset::MaterialAssetUVE& material,
                                          const FrameUniformsUVE& frameUniforms) {
        program.SetMatrix4x4UVE("uViewProjection", frameUniforms.viewProjection);
        program.SetVector3UVE("uAmbientColor", frameUniforms.ambientColor);
        program.SetVector3UVE("uViewPosition", frameUniforms.viewPosition);
        for (std::size_t lightIndex = 0; lightIndex < kMaxLightsUVE; ++lightIndex) {
            const LightDataUVE& light = frameUniforms.lights[lightIndex];
            const LightUniformNamesUVE& names = uniformNames.lights[lightIndex];
            program.SetIntUVE(names.type, static_cast<std::int32_t>(light.type));
            program.SetVector3UVE(names.position, light.position);
            program.SetVector3UVE(names.direction, light.direction);
            program.SetVector3UVE(names.color, light.color);
            program.SetFloatUVE(names.intensity, light.intensity);
            program.SetFloatUVE(names.range, light.range);
            program.SetFloatUVE(names.spotAngleDegrees, light.spotAngleDegrees);
        }
        program.SetMatrix4x4UVE(uniformNames.legacyLightSpaceMatrix, frameUniforms.lightSpaceMatrices[0]);
        program.SetIntUVE("uShadowMapTexture", static_cast<std::int32_t>(kShadowMapTextureSlotUVE));
        program.SetIntUVE("uShadowCascadeCount", frameUniforms.cascadeCount);
        program.SetFloatUVE("uShadowCascadeBlendRatio", frameUniforms.cascadeBlendRatio);
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
            const std::uint32_t textureSlot =
                kShadowCascadeFirstTextureSlotUVE + static_cast<std::uint32_t>(cascadeIndex);
            program.SetMatrix4x4UVE(uniformNames.lightSpaceMatrices[cascadeIndex],
                                    frameUniforms.lightSpaceMatrices[cascadeIndex]);
            program.SetFloatUVE(uniformNames.shadowCascadeSplits[cascadeIndex],
                                frameUniforms.cascadeSplits[cascadeIndex]);
            program.SetIntUVE(uniformNames.shadowMapTextures[cascadeIndex],
                              static_cast<std::int32_t>(textureSlot));
        }
        program.SetIntUVE("uShadowPcfKernelRadius", shadowPcfKernelRadius);
        program.SetVector3UVE("uAlbedoColor", material.albedoColor);
        program.SetFloatUVE("uMetallic", material.metallic);
        program.SetFloatUVE("uRoughness", material.roughness);
        program.SetVector3UVE("uEmissiveColor", material.emissiveColor);
        program.SetIntUVE("uAlbedoTexture", static_cast<std::int32_t>(kAlbedoTextureSlotUVE));
        program.SetIntUVE("uNormalTexture", static_cast<std::int32_t>(kNormalTextureSlotUVE));
        program.SetIntUVE("uAOTexture", static_cast<std::int32_t>(kAoTextureSlotUVE));
    }

    /// Binds the shadow cascades and the material's three textures - also shared by both paths.
    void BindMaterialTexturesUVE(const MaterialGpuResourcesUVE& materialResources,
                                 const FrameUniformsUVE& frameUniforms,
                                 ICommandBufferUVE& commandBuffer) {
        if (frameUniforms.cascadeCount > 0) {
            commandBuffer.BindTextureUVE(shadowMapTargets[0], kShadowMapTextureSlotUVE);
            for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
                commandBuffer.BindTextureUVE(
                    shadowMapTargets[cascadeIndex],
                    kShadowCascadeFirstTextureSlotUVE + static_cast<std::uint32_t>(cascadeIndex));
            }
        }
        commandBuffer.BindTextureUVE(materialResources.albedoTexture, kAlbedoTextureSlotUVE);
        commandBuffer.BindTextureUVE(materialResources.normalTexture, kNormalTextureSlotUVE);
        commandBuffer.BindTextureUVE(materialResources.aoTexture, kAoTextureSlotUVE);
    }

    /// Records `items` as instanced draws where the material supports it, falling back to the
    /// per-object path for everything else. Returns the number of draw calls recorded.
    ///
    /// The fallback is per BATCH, not per frame: a scene mixing instancing-aware and legacy
    /// materials draws each with whichever path is correct for it, rather than giving up on
    /// instancing entirely because one material is old.
    [[nodiscard]] std::size_t RecordItemsInstancedUVE(const std::vector<RenderItemUVE>& items,
                                                      RenderBatchSetUVE& batchSet,
                                                      const FrameUniformsUVE& frameUniforms,
                                                      ICommandBufferUVE& commandBuffer) {
        BuildRenderBatchesUVE(items, batchSet);
        if (batchSet.batches.empty()) {
            return 0U;
        }

        // One upload for the whole bucket; each batch then reads its own window of it via
        // uInstanceBaseIndex. Uploading per batch would be the obvious shape and the wrong one -
        // it trades one buffer update per frame for one per batch.
        const bool instancingUsable =
            batchSet.instanceMatrices.size() <= kMaximumInstancesPerFrameUVE &&
            EnsureInstanceBufferCapacityUVE(batchSet.instanceMatrices.size()) &&
            UploadInstanceTransformsUVE(batchSet.instanceMatrices);

        std::size_t drawCalls = 0U;
        for (const RenderBatchUVE& batch : batchSet.batches) {
            const RenderItemUVE& representative = items[batch.firstItem];
            const MaterialGpuResourcesUVE* const materialResources =
                ResolveMaterialGpuResourcesUVE(representative);
            if (materialResources == nullptr) {
                continue;
            }
            const MeshGpuResourcesUVE& meshResources = ResolveMeshGpuResourcesUVE(representative);
            if (!IsValidMeshGpuResourcesUVE(meshResources)) {
                continue;
            }
            const Asset::MaterialAssetUVE* const material = representative.materialHandle.TryGetUVE();
            const std::shared_ptr<Shader::ShaderProgramUVE>& program = materialResources->program;
            if (material == nullptr || !program->IsValidUVE()) {
                continue; // Still compiling or invalid: never bind a stale raw material pipeline.
            }

            if (!instancingUsable || !materialResources->supportsInstancing) {
                // Correct, just not batched. Every item in the run still gets its own uModel.
                drawCalls += RecordItemRangeUnbatchedUVE(items, batch.firstItem, batch.itemCount,
                                                         frameUniforms, commandBuffer);
                continue;
            }

            const auto baseIndex = static_cast<std::int32_t>(batch.firstItem);
            const std::span<const std::byte> baseBytes{reinterpret_cast<const std::byte*>(&baseIndex),
                                                       sizeof(baseIndex)};
            if (!renderDevice.UpdateBufferUVE(instanceBaseBuffer, baseBytes)) {
                drawCalls += RecordItemRangeUnbatchedUVE(items, batch.firstItem, batch.itemCount,
                                                         frameUniforms, commandBuffer);
                continue;
            }

            ApplyFrameAndMaterialUniformsUVE(*program, *material, frameUniforms);
            program->ApplyToUVE(commandBuffer);
            BindMaterialTexturesUVE(*materialResources, frameUniforms, commandBuffer);
            commandBuffer.BindStorageBufferUVE(instanceTransformBuffer, kInstanceTransformSlotUVE);
            commandBuffer.BindStorageBufferUVE(instanceNormalTransformBuffer, kInstanceNormalTransformSlotUVE);
            commandBuffer.BindStorageBufferUVE(instanceBaseBuffer, kInstanceBaseSlotUVE);
            commandBuffer.BindVertexBufferUVE(meshResources.vertexBuffer);
            commandBuffer.BindIndexBufferUVE(meshResources.indexBuffer);
            commandBuffer.DrawIndexedUVE(meshResources.indexCount,
                                         static_cast<std::uint32_t>(batch.itemCount));
            ++drawCalls;
            instancedDrawCallsThisFrame += 1U;
            instancedObjectsThisFrame += batch.itemCount;
        }
        return drawCalls;
    }

    [[nodiscard]] std::size_t RecordItemsUVE(const std::vector<RenderItemUVE>& items,
                                              const FrameUniformsUVE& frameUniforms,
                                              ICommandBufferUVE& commandBuffer) {
        return RecordItemRangeUnbatchedUVE(items, 0U, items.size(), frameUniforms, commandBuffer);
    }

    /// The original one-draw-per-object path, now expressed over a sub-range so the instanced path
    /// can delegate a single batch to it when that batch's material cannot be instanced. Behavior
    /// for the whole-bucket case is unchanged.
    [[nodiscard]] std::size_t RecordItemRangeUnbatchedUVE(const std::vector<RenderItemUVE>& items,
                                                          const std::size_t firstItem,
                                                          const std::size_t itemCount,
                                                          const FrameUniformsUVE& frameUniforms,
                                                          ICommandBufferUVE& commandBuffer) {
        std::size_t drawCalls = 0U;
        for (std::size_t itemIndex = firstItem; itemIndex < firstItem + itemCount; ++itemIndex) {
            const RenderItemUVE& item = items[itemIndex];
            const MaterialGpuResourcesUVE* const materialResources = ResolveMaterialGpuResourcesUVE(item);
            if (materialResources == nullptr) {
                continue;
            }
            const MeshGpuResourcesUVE& meshResources = ResolveMeshGpuResourcesUVE(item);
            if (!IsValidMeshGpuResourcesUVE(meshResources)) {
                continue;
            }
            const Asset::MaterialAssetUVE* const material = item.materialHandle.TryGetUVE();
            const std::shared_ptr<Shader::ShaderProgramUVE>& program = materialResources->program;
            if (!program->IsValidUVE()) {
                continue; // Still compiling or invalid: never bind a stale raw material pipeline.
            }

            program->SetMatrix4x4UVE("uModel", item.worldMatrix);
            // Normal matrix = transpose(inverse(model)); see ComputeNormalMatrixUVE, which the
            // instanced path shares so the two cannot disagree about how a normal is transformed.
            program->SetMatrix4x4UVE("uNormalMatrix", ComputeNormalMatrixUVE(item.worldMatrix));
            ApplyFrameAndMaterialUniformsUVE(*program, *material, frameUniforms);
            program->ApplyToUVE(commandBuffer);
            BindMaterialTexturesUVE(*materialResources, frameUniforms, commandBuffer);
            commandBuffer.BindVertexBufferUVE(meshResources.vertexBuffer);
            commandBuffer.BindIndexBufferUVE(meshResources.indexBuffer);
            commandBuffer.DrawIndexedUVE(meshResources.indexCount);
            ++drawCalls;
        }
        return drawCalls;
    }

    [[nodiscard]] std::size_t RecordParticleItemsUVE(const ParticleDrawRecordingUVE& recording,
                                                       const FrameUniformsUVE& frameUniforms,
                                                       ICommandBufferUVE& commandBuffer) {
        if (!particleProgram->IsValidUVE() || recording.commands.empty() ||
            particleVertexBuffer == kInvalidBufferHandleUVE) {
            return 0U;
        }

        const std::size_t commandCount =
            std::min(recording.commands.size(), kMaximumParticleGpuDrawCommandsUVE);
        particleVertexStaging.clear();
        particleVertexStaging.reserve(commandCount * kParticleVerticesPerCommandUVE);
        const auto appendVertex = [this](const ParticleDrawCommandUVE& command, float xOffset, float yOffset) {
            const float alpha = std::clamp(command.remainingLifetimeSeconds, 0.15F, 1.0F);
            particleVertexStaging.push_back(ParticleVertexUVE{
                Math::Vector3UVE{command.position.x + xOffset, command.position.y + yOffset, command.position.z},
                1.0F, 0.45F, 0.08F, alpha});
        };
        for (std::size_t commandIndex = 0U; commandIndex < commandCount; ++commandIndex) {
            const ParticleDrawCommandUVE& command = recording.commands[commandIndex];
            appendVertex(command, -kParticleHalfExtentUVE, -kParticleHalfExtentUVE);
            appendVertex(command, kParticleHalfExtentUVE, -kParticleHalfExtentUVE);
            appendVertex(command, kParticleHalfExtentUVE, kParticleHalfExtentUVE);
            appendVertex(command, -kParticleHalfExtentUVE, -kParticleHalfExtentUVE);
            appendVertex(command, kParticleHalfExtentUVE, kParticleHalfExtentUVE);
            appendVertex(command, -kParticleHalfExtentUVE, kParticleHalfExtentUVE);
        }

        if (!renderDevice.UpdateBufferUVE(particleVertexBuffer, std::as_bytes(std::span(particleVertexStaging)))) {
            return 0U;
        }
        particleProgram->SetMatrix4x4UVE("uViewProjection", frameUniforms.viewProjection);
        particleProgram->ApplyToUVE(commandBuffer);
        commandBuffer.BindVertexBufferUVE(particleVertexBuffer);
        commandBuffer.DrawUVE(static_cast<std::uint32_t>(particleVertexStaging.size()));
        return commandCount;
    }

    [[nodiscard]] std::size_t RecordPrimitiveItemsUVE(const std::vector<PrimitiveRenderItemUVE>& items,
                                                       const FrameUniformsUVE& frameUniforms,
                                                       ICommandBufferUVE& commandBuffer) {
        if (!primitiveProgram->IsValidUVE()) {
            return 0U;
        }
        std::size_t drawCalls = 0U;
        for (const PrimitiveRenderItemUVE& item : items) {
            const MeshGpuResourcesUVE& meshResources = ResolvePrimitiveMeshGpuResourcesUVE(item.kind);
            if (!IsValidMeshGpuResourcesUVE(meshResources)) {
                continue;
            }
            primitiveProgram->SetMatrix4x4UVE("uModel", item.worldMatrix);
            primitiveProgram->SetMatrix4x4UVE("uViewProjection", frameUniforms.viewProjection);
            primitiveProgram->SetVector3UVE("uColor", item.baseColor);
            primitiveProgram->ApplyToUVE(commandBuffer);
            commandBuffer.BindVertexBufferUVE(meshResources.vertexBuffer);
            commandBuffer.BindIndexBufferUVE(meshResources.indexBuffer);
            commandBuffer.DrawIndexedUVE(meshResources.indexCount);
            ++drawCalls;
        }
        return drawCalls;
    }

    /// Records the UI overlay batch as up to 2 draw calls: non-glyph quads (solid-fill buttons/
    /// canvases, and images - which, until a real asset-texture resolution path exists, also
    /// render as a flat tint - see UIRuntimeUVE's own kind classification) bound to
    /// fallbackWhiteTexture, then glyph quads bound to the font atlas. This 2-pass split preserves
    /// the exact same visual order as one interleaved draw would, since UIRuntimeUVE always
    /// appends images/buttons before text (UIDrawBatchUVE's own doc comment) - no non-glyph quad
    /// ever needs to render on top of a glyph that precedes it in the array, because none ever
    /// does. Each pass is independently skippable: a missing font atlas texture (bake/upload
    /// failure) only drops the glyph pass, not the solid-quad one.
    [[nodiscard]] std::size_t RecordUIOverlayItemsUVE(const UI::UIDrawBatchUVE& batch,
                                                       const Math::Matrix4x4UVE& projection,
                                                       ICommandBufferUVE& commandBuffer) {
        if (!uiOverlayProgram->IsValidUVE() || batch.quads.empty() || uiVertexBuffer == kInvalidBufferHandleUVE) {
            return 0U;
        }
        const std::size_t maxVertices = kMaximumUIQuadsUVE * kUIVerticesPerQuadUVE;
        const auto appendQuad = [this](const UI::UIQuadUVE& quad) {
            const float x0 = quad.positionPixels.x;
            const float y0 = quad.positionPixels.y;
            const float x1 = quad.positionPixels.x + quad.sizePixels.x;
            const float y1 = quad.positionPixels.y + quad.sizePixels.y;
            const auto makeVertex = [&quad](const float x, const float y, const float u, const float v) {
                return UIVertexUVE{x, y, u, v, quad.color.x, quad.color.y, quad.color.z, quad.alpha};
            };
            uiVertexStaging.push_back(makeVertex(x0, y0, quad.u0, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y0, quad.u1, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y1, quad.u1, quad.v1));
            uiVertexStaging.push_back(makeVertex(x0, y0, quad.u0, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y1, quad.u1, quad.v1));
            uiVertexStaging.push_back(makeVertex(x0, y1, quad.u0, quad.v1));
        };
        const auto drawPass = [this, &batch, &appendQuad, &projection, &commandBuffer,
                               maxVertices](const TextureHandleUVE texture,
                                            const auto& matchesPass) -> bool {
            uiVertexStaging.clear();
            for (const UI::UIQuadUVE& quad : batch.quads) {
                if (!matchesPass(quad) || uiVertexStaging.size() + kUIVerticesPerQuadUVE > maxVertices) {
                    continue;
                }
                appendQuad(quad);
            }
            if (uiVertexStaging.empty() ||
                !renderDevice.UpdateBufferUVE(uiVertexBuffer, std::as_bytes(std::span(uiVertexStaging)))) {
                return false;
            }
            uiOverlayProgram->SetMatrix4x4UVE("uProjection", projection);
            uiOverlayProgram->SetIntUVE("uSourceTexture", 0);
            uiOverlayProgram->ApplyToUVE(commandBuffer);
            commandBuffer.BindTextureUVE(texture, 0U);
            commandBuffer.BindVertexBufferUVE(uiVertexBuffer);
            commandBuffer.DrawUVE(static_cast<std::uint32_t>(uiVertexStaging.size()));
            return true;
        };

        std::size_t drawCalls = 0U;
        if (drawPass(fallbackWhiteTexture,
                     [](const UI::UIQuadUVE& quad) { return quad.kind != UI::UIDrawItemKindUVE::Glyph; })) {
            ++drawCalls;
        }
        if (uiFontAtlasTexture != kInvalidTextureHandleUVE &&
            drawPass(uiFontAtlasTexture,
                     [](const UI::UIQuadUVE& quad) { return quad.kind == UI::UIDrawItemKindUVE::Glyph; })) {
            ++drawCalls;
        }
        return drawCalls;
    }

};

Renderer3DUVE::Renderer3DUVE(IRenderDeviceUVE& renderDevice, IRenderSystemUVE& renderSystem,
                              IMeshRendererUVE& meshRenderer, ICameraSystemUVE& cameraSystem,
                              ILightSystemUVE& lightSystem, Shader::IShaderManagerUVE& shaderManager,
                              Asset::IAssetManagerUVE& assetManager, Asset::IAssetDatabaseUVE& assetDatabase,
                              Events::IEventSystemUVE& eventSystem, std::uint32_t targetWidth,
                              std::uint32_t targetHeight, Math::Vector3UVE ambientColor,
                              std::uint32_t shadowMapResolution, float shadowMapHalfExtent,
                              float shadowMapNearPlane, float shadowMapFarPlane, float shadowFrustumPadding,
                              float shadowCascadeSplitLambda, float shadowCascadeBlendRatio,
                              std::uint32_t shadowPcfKernelRadius)
    : m_impl(std::make_unique<ImplUVE>(renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
                                        shaderManager, assetManager, assetDatabase, eventSystem, targetWidth,
                                        targetHeight, ambientColor, shadowMapResolution, shadowMapHalfExtent,
                                        shadowMapNearPlane, shadowMapFarPlane, shadowFrustumPadding,
                                        shadowCascadeSplitLambda, shadowCascadeBlendRatio, shadowPcfKernelRadius)) {
    // The scene is rendered to the offscreen color target first; desktop keeps HDR RGBA16F while
    // Android uses the GLES3-safe RGBA8 target, and the final fullscreen graph pass tone-maps it
    // to the default framebuffer's LDR presentation surface.
    // Created through the same cache path a later resize uses, rather than allocated directly:
    // the destructor frees size-dependent targets as CACHE ENTRIES, so a set created outside the
    // cache would simply leak. One owner for these textures, not two.
    if (std::optional<ImplUVE::SizedTargetSetUVE> initialSet =
            m_impl->CreateTargetSetUVE(targetWidth, targetHeight);
        initialSet.has_value()) {
        const auto inserted =
            m_impl->targetSetCache.emplace(std::pair{targetWidth, targetHeight}, *initialSet);
        m_impl->ActivateTargetSetUVE(inserted.first->second, targetWidth, targetHeight);
    } else {
        UVE_ERROR("Renderer3DUVE: main render target creation failed; frame rendering will be skipped");
    }
    m_impl->fallbackWhiteTexture = renderDevice.CreateTextureUVE(
        TextureDescUVE{1, 1, TextureFormatUVE::RGBA8Unorm, 1}, std::as_bytes(std::span(kWhitePixelUVE)));
    m_impl->fallbackNormalTexture = renderDevice.CreateTextureUVE(
        TextureDescUVE{1, 1, TextureFormatUVE::RGBA8Unorm, 1}, std::as_bytes(std::span(kFlatNormalPixelUVE)));
    for (TextureHandleUVE& shadowMapTarget : m_impl->shadowMapTargets) {
        shadowMapTarget = renderDevice.CreateTextureUVE(
            TextureDescUVE{shadowMapResolution, shadowMapResolution, TextureFormatUVE::Depth32Float, 1});
    }

    Shader::ShaderProgramDescUVE shadowProgramDesc;
    shadowProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kShadowDepthVirtualPath);
    shadowProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kShadowDepthSource);
    shadowProgramDesc.vertexLayout = MeshVertexLayoutUVE();
    shadowProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertexUVE));
    shadowProgramDesc.depthTestEnabled = true;
    shadowProgramDesc.depthWriteEnabled = true;
    shadowProgramDesc.debugNameUVE = "ShadowDepth";
    m_impl->shadowProgram = shaderManager.CreateProgramUVE(shadowProgramDesc);

    // The instanced twin of the shadow program. Same source file, same layout, one define - so the
    // depth a shadow map records cannot drift between the two paths. Compiled unconditionally
    // rather than lazily: a shadow pass that stalls mid-frame waiting for a program is worse than
    // one extra compile at startup, and the non-instanced program remains the fallback if this one
    // fails to link.
    Shader::ShaderProgramDescUVE instancedShadowProgramDesc = shadowProgramDesc;
    instancedShadowProgramDesc.extraDefines.emplace_back("UVE_INSTANCED", "1");
    instancedShadowProgramDesc.debugNameUVE = "ShadowDepthInstanced";
    m_impl->instancedShadowProgram = shaderManager.CreateProgramUVE(instancedShadowProgramDesc);

    Shader::ShaderProgramDescUVE toneMappingProgramDesc;
    toneMappingProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kFullscreenQuadVirtualPath);
    toneMappingProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kFullscreenQuadSource);
    toneMappingProgramDesc.depthTestEnabled = false;
    toneMappingProgramDesc.depthWriteEnabled = false;
    toneMappingProgramDesc.debugNameUVE = "ToneMapping";
    m_impl->toneMappingProgram = shaderManager.CreateProgramUVE(toneMappingProgramDesc);

    Shader::ShaderProgramDescUVE bloomBrightPassProgramDesc;
    bloomBrightPassProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBloomBrightPassVirtualPath);
    bloomBrightPassProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBloomBrightPassSource);
    bloomBrightPassProgramDesc.depthTestEnabled = false;
    bloomBrightPassProgramDesc.depthWriteEnabled = false;
    bloomBrightPassProgramDesc.debugNameUVE = "BloomBrightPass";
    m_impl->bloomBrightPassProgram = shaderManager.CreateProgramUVE(bloomBrightPassProgramDesc);

    Shader::ShaderProgramDescUVE bloomBlurProgramDesc;
    bloomBlurProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBloomBlurVirtualPath);
    bloomBlurProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBloomBlurSource);
    bloomBlurProgramDesc.depthTestEnabled = false;
    bloomBlurProgramDesc.depthWriteEnabled = false;
    bloomBlurProgramDesc.debugNameUVE = "BloomBlur";
    m_impl->bloomBlurProgram = shaderManager.CreateProgramUVE(bloomBlurProgramDesc);

    Shader::ShaderProgramDescUVE bloomCompositeProgramDesc;
    bloomCompositeProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kFullscreenCopyVirtualPath);
    bloomCompositeProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kFullscreenCopySource);
    bloomCompositeProgramDesc.depthTestEnabled = false;
    bloomCompositeProgramDesc.depthWriteEnabled = false;
    bloomCompositeProgramDesc.blendMode = PipelineBlendModeUVE::Additive;
    bloomCompositeProgramDesc.debugNameUVE = "BloomComposite";
    m_impl->bloomCompositeProgram = shaderManager.CreateProgramUVE(bloomCompositeProgramDesc);

    Shader::ShaderProgramDescUVE ssaoProgramDesc;
    ssaoProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kSsaoVirtualPath);
    ssaoProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kSsaoSource);
    ssaoProgramDesc.depthTestEnabled = false;
    ssaoProgramDesc.depthWriteEnabled = false;
    ssaoProgramDesc.debugNameUVE = "SSAO";
    m_impl->ssaoProgram = shaderManager.CreateProgramUVE(ssaoProgramDesc);

    Shader::ShaderProgramDescUVE ssaoCompositeProgramDesc;
    ssaoCompositeProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kFullscreenCopyVirtualPath);
    ssaoCompositeProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kFullscreenCopySource);
    ssaoCompositeProgramDesc.depthTestEnabled = false;
    ssaoCompositeProgramDesc.depthWriteEnabled = false;
    ssaoCompositeProgramDesc.blendMode = PipelineBlendModeUVE::Multiply;
    ssaoCompositeProgramDesc.debugNameUVE = "SSAOComposite";
    m_impl->ssaoCompositeProgram = shaderManager.CreateProgramUVE(ssaoCompositeProgramDesc);

    Shader::ShaderProgramDescUVE particleProgramDesc;
    particleProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kParticleVirtualPath);
    particleProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kParticleSource);
    particleProgramDesc.vertexLayout = {
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, offsetof(ParticleVertexUVE, position)},
        VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float4, offsetof(ParticleVertexUVE, red)},
    };
    particleProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(ParticleVertexUVE));
    particleProgramDesc.depthTestEnabled = true;
    particleProgramDesc.depthWriteEnabled = false;
    particleProgramDesc.blendMode = PipelineBlendModeUVE::SourceAlphaOver;
    particleProgramDesc.debugNameUVE = "Particle";
    m_impl->particleProgram = shaderManager.CreateProgramUVE(particleProgramDesc);
    m_impl->particleVertexStaging.reserve(kMaximumParticleGpuDrawCommandsUVE * kParticleVerticesPerCommandUVE);
    m_impl->particleVertexBuffer = renderDevice.CreateBufferUVE(
        BufferDescUVE{sizeof(ParticleVertexUVE) * kMaximumParticleGpuDrawCommandsUVE * kParticleVerticesPerCommandUVE,
                      BufferUsageUVE::Vertex});

    Shader::ShaderProgramDescUVE primitiveProgramDesc;
    primitiveProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBasic3DVirtualPath);
    primitiveProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBasic3DSource);
    primitiveProgramDesc.vertexLayout = MeshVertexLayoutUVE();
    primitiveProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertexUVE));
    primitiveProgramDesc.depthTestEnabled = true;
    primitiveProgramDesc.depthWriteEnabled = true;
    primitiveProgramDesc.debugNameUVE = "BuiltInPrimitiveVisual";
    m_impl->primitiveProgram = shaderManager.CreateProgramUVE(primitiveProgramDesc);

    Shader::ShaderProgramDescUVE uiOverlayProgramDesc;
    uiOverlayProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kUIOverlayVirtualPath);
    uiOverlayProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kUIOverlaySource);
    uiOverlayProgramDesc.vertexLayout = {
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float2, offsetof(UIVertexUVE, x)},
        VertexAttributeUVE{"TEXCOORD", VertexAttributeFormatUVE::Float2, offsetof(UIVertexUVE, u)},
        VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float4, offsetof(UIVertexUVE, red)},
    };
    uiOverlayProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(UIVertexUVE));
    uiOverlayProgramDesc.depthTestEnabled = false;
    uiOverlayProgramDesc.depthWriteEnabled = false;
    uiOverlayProgramDesc.blendMode = PipelineBlendModeUVE::SourceAlphaOver;
    uiOverlayProgramDesc.debugNameUVE = "UIOverlay";
    m_impl->uiOverlayProgram = shaderManager.CreateProgramUVE(uiOverlayProgramDesc);
    m_impl->uiVertexStaging.reserve(kMaximumUIQuadsUVE * kUIVerticesPerQuadUVE);
    m_impl->uiVertexBuffer = renderDevice.CreateBufferUVE(
        BufferDescUVE{sizeof(UIVertexUVE) * kMaximumUIQuadsUVE * kUIVerticesPerQuadUVE, BufferUsageUVE::Vertex});

    ImplUVE* const implPtr = m_impl.get();
    m_impl->reloadSubscription = eventSystem.Subscribe<Asset::AssetReloadedEventUVE>(
        [implPtr](const Asset::AssetReloadedEventUVE& event) { implPtr->OnAssetReloadedUVE(event); });
}

Renderer3DUVE::~Renderer3DUVE() {
    m_impl->eventSystem.Unsubscribe(m_impl->reloadSubscription);
    for (const auto& [guid, meshResources] : m_impl->meshCache) {
        DestroyBufferIfValidUVE(m_impl->renderDevice, meshResources.vertexBuffer);
        DestroyBufferIfValidUVE(m_impl->renderDevice, meshResources.indexBuffer);
    }
    for (const auto& [kind, meshResources] : m_impl->primitiveMeshCache) {
        DestroyBufferIfValidUVE(m_impl->renderDevice, meshResources.vertexBuffer);
        DestroyBufferIfValidUVE(m_impl->renderDevice, meshResources.indexBuffer);
    }
    // MaterialGpuResourcesUVE holds shared ShaderProgramUVE references only. Releasing the cache
    // lets ShaderManagerUVE-owned program deleters retire their pipelines exactly once.
    m_impl->materialCache.clear();
    for (const auto& [guid, textureHandle] : m_impl->textureCache) {
        DestroyTextureIfValidUVE(m_impl->renderDevice, textureHandle);
    }
    DestroyBufferIfValidUVE(m_impl->renderDevice, m_impl->instanceTransformBuffer);
    DestroyBufferIfValidUVE(m_impl->renderDevice, m_impl->instanceNormalTransformBuffer);
    DestroyBufferIfValidUVE(m_impl->renderDevice, m_impl->instanceBaseBuffer);
    // The cache owns every size-dependent target, INCLUDING the active one - colorTarget and its
    // siblings are mirrors of a cached set's handles, not separate allocations. Destroying both
    // would be a double free, so the mirrors are only cleared, and are destroyed exactly once here
    // as cache entries.
    for (const auto& [cachedSize, cachedSet] : m_impl->targetSetCache) {
        static_cast<void>(cachedSize);
        m_impl->DestroyTargetSetUVE(cachedSet);
    }
    m_impl->targetSetCache.clear();
    m_impl->colorTarget = kInvalidTextureHandleUVE;
    m_impl->depthTarget = kInvalidTextureHandleUVE;
    m_impl->bloomBrightTarget = kInvalidTextureHandleUVE;
    m_impl->bloomBlurTargetA = kInvalidTextureHandleUVE;
    m_impl->bloomBlurTargetB = kInvalidTextureHandleUVE;
    m_impl->ssaoTarget = kInvalidTextureHandleUVE;
    DestroyTextureIfValidUVE(m_impl->renderDevice, m_impl->fallbackWhiteTexture);
    DestroyTextureIfValidUVE(m_impl->renderDevice, m_impl->fallbackNormalTexture);
    for (const TextureHandleUVE shadowMapTarget : m_impl->shadowMapTargets) {
        DestroyTextureIfValidUVE(m_impl->renderDevice, shadowMapTarget);
    }
    DestroyBufferIfValidUVE(m_impl->renderDevice, m_impl->particleVertexBuffer);
    DestroyBufferIfValidUVE(m_impl->renderDevice, m_impl->uiVertexBuffer);
    DestroyTextureIfValidUVE(m_impl->renderDevice, m_impl->uiFontAtlasTexture);
}

bool Renderer3DUVE::ResizeTargetsUVE(const std::uint32_t width, const std::uint32_t height) {
    return m_impl->ResizeTargetsUVE(width, height);
}

void Renderer3DUVE::RenderFrameUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity) {
    m_impl->lastFrameDiagnostics = Renderer3DFrameDiagnosticsUVE{};
    // Reset here, not beside the main pass: the shadow cascades are recorded BEFORE it, so a reset
    // at the main pass would discard the count this counter exists to report.
    m_impl->shadowInstancedDrawCallsThisFrame = 0U;
    m_impl->lastFrameDiagnostics.renderTargetWidth = m_impl->targetWidth;
    m_impl->lastFrameDiagnostics.renderTargetHeight = m_impl->targetHeight;
    if (m_impl->colorTarget == kInvalidTextureHandleUVE || m_impl->depthTarget == kInvalidTextureHandleUVE) {
        return;
    }
    const Scene::CameraComponentUVE& camera =
        entityManager.GetComponentUVE<Scene::CameraComponentUVE>(cameraEntity);
    const bool cameraValid = Scene::IsCameraComponentValidUVE(camera);
    UVE_ASSERT(cameraValid);
    if (!cameraValid) {
        UVE_ERROR("Renderer3DUVE: RenderFrameUVE received invalid camera parameters");
        return;
    }
    const Scene::WorldTransformComponentUVE& cameraWorldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(cameraEntity);
    Math::QuaternionUVE normalizedCameraRotation;
    const bool cameraTransformValid =
        IsFiniteVectorUVE(cameraWorldTransform.worldPosition) &&
        Math::TryNormalizeUVE(cameraWorldTransform.worldRotation, normalizedCameraRotation);
    UVE_ASSERT(cameraTransformValid);
    if (!cameraTransformValid) {
        UVE_ERROR("Renderer3DUVE: RenderFrameUVE received an invalid camera world transform");
        return;
    }
    if (m_impl->targetWidth == 0U || m_impl->targetHeight == 0U) {
        UVE_ERROR("Renderer3DUVE: RenderFrameUVE cannot render to a zero-sized target");
        return;
    }
    const float aspectRatio = static_cast<float>(m_impl->targetWidth) / static_cast<float>(m_impl->targetHeight);
    const bool aspectRatioValid = std::isfinite(aspectRatio) && aspectRatio > 0.0F;
    UVE_ASSERT(aspectRatioValid);
    if (!aspectRatioValid) {
        UVE_ERROR("Renderer3DUVE: RenderFrameUVE computed an invalid target aspect ratio");
        return;
    }
    m_impl->lastFrameDiagnostics.primitiveProgramReady = m_impl->primitiveProgram->IsValidUVE();
    m_impl->lastFrameDiagnostics.particleProgramReady = m_impl->particleProgram->IsValidUVE();
    m_impl->lastFrameDiagnostics.toneMappingProgramReady = m_impl->toneMappingProgram->IsValidUVE();

    const Math::Matrix4x4UVE viewProjection =
        m_impl->cameraSystem.ComputeViewProjectionUVE(entityManager, cameraEntity, aspectRatio);
    const Math::FrustumUVE frustum = m_impl->cameraSystem.ExtractFrustumUVE(viewProjection);
    // SSAO reconstructs view-space position from depth using only the projection step (see
    // ssao.glsl's doc comment) - a singular projection (never expected in practice for a valid
    // perspective camera) just means SSAO is skipped this frame, not a fatal error.
    const Math::Matrix4x4UVE projection =
        m_impl->cameraSystem.ComputeProjectionMatrixUVE(entityManager, cameraEntity, aspectRatio);
    Math::Matrix4x4UVE inverseProjection{};
    const bool projectionInvertible = Math::TryInverseUVE(projection, inverseProjection);
    const Math::Vector3UVE viewPosition = m_impl->cameraSystem.GetWorldPositionUVE(entityManager, cameraEntity);
    // Selected by contribution at the camera, not by whichever four the ECS happened to visit
    // first. The old order was not merely arbitrary - it could change when an unrelated entity was
    // created or destroyed, so a light could vanish from the player's face for no visible reason.
    const LightListUVE lights =
        m_impl->lightSystem.ExtractActiveLightsForViewUVE(entityManager, viewPosition);
    const Math::Vector3UVE ambientColor = ResolveWorldEnvironmentAmbientUVE(entityManager, m_impl->ambientColor);

    // Built ONCE for the whole frame, then culled against each of the four frusta below. Before
    // this, the full extraction walk ran per frustum - three shadow cascades plus the main view -
    // re-resolving the same asset handles and recomputing the same world matrices and bounds four
    // times over to reach four different plane tests. Only the plane test ever differed.
    m_impl->meshRenderer.BuildVisibilitySetUVE(entityManager, m_impl->assetManager, m_impl->assetDatabase,
                                               m_impl->visibilitySet);
    m_impl->lastFrameDiagnostics.placementCacheHits = m_impl->visibilitySet.placementCacheHits;
    m_impl->lastFrameDiagnostics.placementCacheMisses = m_impl->visibilitySet.placementCacheMisses;

    const LightDataUVE* const shadowCaster = FindShadowCasterUVE(lights);
    bool shadowsReady = shadowCaster != nullptr && m_impl->shadowProgram->IsValidUVE() &&
                        AreShadowMapTargetsValidUVE(m_impl->shadowMapTargets);
    ShadowCascadeMatricesUVE lightSpaceMatrices{};
    ShadowCascadeSplitsUVE cascadeSplits{};
    std::int32_t cascadeCount = 0;
    if (shadowsReady) {
        cascadeSplits = ComputeCascadeSplitsUVE(camera.nearPlane, camera.farPlane, m_impl->shadowCascadeSplitLambda);
        const bool cascadeSplitsValid = AreCascadeSplitsValidUVE(cascadeSplits, camera.nearPlane, camera.farPlane);
        UVE_ASSERT(cascadeSplitsValid);
        if (!cascadeSplitsValid) {
            UVE_ERROR("Renderer3DUVE: shadow cascade split math produced invalid output; skipping shadow cascades");
            shadowsReady = false;
        } else {
            const Math::Matrix4x4UVE lightView =
                Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(shadowCaster->position, shadowCaster->rotation);
            const CameraFrustumCornersUVE cameraCorners =
                m_impl->cameraSystem.ComputeFrustumCornersUVE(entityManager, cameraEntity, aspectRatio);
            const bool shadowViewInputsValid = IsFiniteMatrixUVE(lightView) &&
                                               std::all_of(cameraCorners.cbegin(), cameraCorners.cend(),
                                                           [](const Math::Vector3UVE& corner) {
                                                               return IsFiniteVectorUVE(corner);
                                                           });
            UVE_ASSERT(shadowViewInputsValid);
            if (!shadowViewInputsValid) {
                UVE_ERROR("Renderer3DUVE: shadow view inputs are non-finite; skipping shadow cascades");
                shadowsReady = false;
            }
            float cascadeNearPlane = camera.nearPlane;
            for (std::size_t cascadeIndex = 0;
                 cascadeIndex < kShadowCascadeCountUVE && shadowsReady; ++cascadeIndex) {
            const float cascadeFarPlane = cascadeSplits[cascadeIndex];
            const float nearRatio = (cascadeNearPlane - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            const float farRatio = (cascadeFarPlane - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            const CameraFrustumCornersUVE cascadeCorners =
                ComputeCascadeFrustumCornersUVE(cameraCorners, nearRatio, farRatio);
            const Math::AabbUVE fittedBounds = ComputeLightSpaceCameraBoundsUVE(cascadeCorners, lightView);
            const Math::AabbUVE stabilizedBounds = StabilizeLightSpaceShadowBoundsUVE(
                fittedBounds, m_impl->shadowFrustumPadding, m_impl->shadowMapResolution);
            const Math::Matrix4x4UVE lightProjection = Math::Matrix4x4UVE::OrthographicUVE(
                stabilizedBounds.min.x, stabilizedBounds.max.x, stabilizedBounds.min.y, stabilizedBounds.max.y,
                -stabilizedBounds.max.z, -stabilizedBounds.min.z);
            lightSpaceMatrices[cascadeIndex] = lightProjection * lightView;
            const bool cascadeBoundsValid = IsOrderedFiniteAabbUVE(fittedBounds) &&
                                             IsOrderedFiniteAabbUVE(stabilizedBounds) &&
                                             IsFiniteMatrixUVE(lightProjection) &&
                                             IsFiniteMatrixUVE(lightSpaceMatrices[cascadeIndex]);
            UVE_ASSERT(cascadeBoundsValid);
            if (!cascadeBoundsValid) {
                UVE_ERROR("Renderer3DUVE: shadow cascade bounds produced non-finite output; skipping cascades");
                shadowsReady = false;
                break;
            }
            const Math::FrustumUVE lightFrustum =
                m_impl->cameraSystem.ExtractFrustumUVE(lightSpaceMatrices[cascadeIndex]);
            m_impl->meshRenderer.CullVisibilitySetIntoUVE(m_impl->visibilitySet, lightFrustum,
                                                          m_impl->shadowQueues[cascadeIndex]);
            // Mesh order, not depth order: this cascade renders depth only, and the shadow
            // batcher merges adjacent same-mesh items. See SortForDepthOnlyPassUVE.
            m_impl->shadowQueues[cascadeIndex].SortForDepthOnlyPassUVE();
                cascadeNearPlane = cascadeFarPlane;
            }
            if (shadowsReady) {
                cascadeCount = static_cast<std::int32_t>(kShadowCascadeCountUVE);
            } else {
                cascadeSplits = ShadowCascadeSplitsUVE{};
                lightSpaceMatrices = ShadowCascadeMatricesUVE{};
            }
        }
    }

    const FrameUniformsUVE frameUniforms{viewProjection, viewPosition, lights, ambientColor,
                                          lightSpaceMatrices, cascadeSplits, cascadeCount,
                                          m_impl->shadowCascadeBlendRatio};

    RenderQueueUVE& queue = m_impl->frameQueue;
    m_impl->meshRenderer.CullVisibilitySetIntoUVE(m_impl->visibilitySet, frustum, queue);
    if (m_impl->particleRuntimeForFrame != nullptr) {
        const ParticleRenderSnapshotUVE particleSnapshot =
            ParticleRenderBridgeUVE::ExtractUVE(*m_impl->particleRuntimeForFrame);
        queue.AppendParticleSnapshotUVE(particleSnapshot);
        m_impl->lastFrameDiagnostics.particleItemsExtracted = particleSnapshot.items.size();
        m_impl->lastFrameDiagnostics.particleItemsTruncated = particleSnapshot.truncated;
    }
    queue.SortUVE();
    ParticleDrawRecorderUVE::RecordIntoUVE(queue, kMaximumParticleGpuDrawCommandsUVE,
                                            m_impl->particleDrawRecording);
    m_impl->lastFrameDiagnostics.particleDrawCommandsRecorded = m_impl->particleDrawRecording.commands.size();
    m_impl->lastFrameDiagnostics.particleDrawCommandsSubmissionTruncated =
        m_impl->particleDrawRecording.truncated;
    m_impl->lastFrameDiagnostics.meshItemsExtracted = queue.opaqueItems.size() + queue.transparentItems.size();
    m_impl->lastFrameDiagnostics.invalidAssetReferences = queue.invalidAssetReferences;
    m_impl->lastFrameDiagnostics.pendingAssetLoads = queue.pendingAssetLoads;
    m_impl->lastFrameDiagnostics.failedAssetLoads = queue.failedAssetLoads;
    m_impl->ExtractPrimitiveItemsUVE(entityManager, frustum, m_impl->primitiveItems);
    m_impl->lastFrameDiagnostics.primitiveItemsExtracted = m_impl->primitiveItems.size();

    RenderGraphUVE& renderGraph = m_impl->renderGraph;
    renderGraph.ClearUVE();
    // +6 resources beyond the shadow cascades: color, depth, SSAO, bloom bright-pass, and the two
    // bloom blur ping-pong targets. +8 passes: MainColor, SSAO, SSAOComposite, BloomBrightPass,
    // BloomBlurH, BloomBlurV, BloomComposite, ToneMapping (a reserve() hint, not a hard capacity).
    renderGraph.ReserveUVE(kShadowCascadeCountUVE + 6U, kShadowCascadeCountUVE + 8U);
    std::array<RenderGraphResourceHandleUVE, kShadowCascadeCountUVE> shadowResources{};
    if (shadowsReady) {
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
            shadowResources[cascadeIndex] = renderGraph.ImportTextureUVE(
                m_impl->shadowMapTargets[cascadeIndex], GetRendererUniformNamesUVE().shadowPasses[cascadeIndex]);
        }
    }
    const RenderGraphResourceHandleUVE colorResource = renderGraph.ImportTextureUVE(m_impl->colorTarget, "MainColor");
    const RenderGraphResourceHandleUVE depthResource = renderGraph.ImportTextureUVE(m_impl->depthTarget, "MainDepth");

    if (shadowsReady) {
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCountUVE; ++cascadeIndex) {
            const std::string_view passName = GetRendererUniformNamesUVE().shadowPasses[cascadeIndex];
            const std::array<RenderGraphResourceUseUVE, 1U> shadowPassResources{
                RenderGraphResourceUseUVE{shadowResources[cascadeIndex], RenderGraphResourceAccessUVE::Write}};
            renderGraph.AddPassUVE(
                passName, shadowPassResources,
                [this, &lightSpaceMatrices, shadowCaster, cascadeIndex](ICommandBufferUVE& commandBuffer) {
                    m_impl->RecordShadowPassUVE(m_impl->shadowQueues[cascadeIndex].opaqueItems,
                                                lightSpaceMatrices[cascadeIndex],
                                                m_impl->shadowMapTargets[cascadeIndex], shadowCaster != nullptr,
                                                m_impl->shadowBatches[cascadeIndex], commandBuffer);
                });
        }
    }

    std::array<RenderGraphResourceUseUVE, kShadowCascadeCountUVE + 2U> mainResources{};
    mainResources[0] = RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Write};
    mainResources[1] = RenderGraphResourceUseUVE{depthResource, RenderGraphResourceAccessUVE::Write};
    std::size_t mainResourceCount = 2U;
    if (shadowsReady) {
        for (const RenderGraphResourceHandleUVE shadowResource : shadowResources) {
            mainResources[mainResourceCount++] =
                RenderGraphResourceUseUVE{shadowResource, RenderGraphResourceAccessUVE::Read};
        }
    }
    renderGraph.AddPassUVE(
        "MainColor", std::span<const RenderGraphResourceUseUVE>{mainResources.data(), mainResourceCount},
        [this, &queue, &frameUniforms](ICommandBufferUVE& commandBuffer) {
            RenderPassDescUVE passDesc;
            passDesc.colorAttachment = m_impl->colorTarget;
            passDesc.depthAttachment = m_impl->depthTarget;
            passDesc.colorLoadOp = LoadOpUVE::Clear;
            passDesc.clearColor = kDefaultSceneClearColorUVE;
            m_impl->lastFrameDiagnostics.mainPassRecorded = true;
            commandBuffer.BeginRenderPassUVE(passDesc);
            m_impl->instancedDrawCallsThisFrame = 0U;
            m_impl->instancedObjectsThisFrame = 0U;
            m_impl->lastFrameDiagnostics.meshDrawCallsRecorded += m_impl->RecordItemsInstancedUVE(
                queue.opaqueItems, m_impl->opaqueBatches, frameUniforms, commandBuffer);
            m_impl->lastFrameDiagnostics.meshDrawCallsRecorded += m_impl->RecordItemsInstancedUVE(
                queue.transparentItems, m_impl->transparentBatches, frameUniforms, commandBuffer);
            m_impl->lastFrameDiagnostics.instancedDrawCallsRecorded = m_impl->instancedDrawCallsThisFrame;
            m_impl->lastFrameDiagnostics.instancedObjectsRecorded = m_impl->instancedObjectsThisFrame;
            m_impl->lastFrameDiagnostics.shadowInstancedDrawCallsRecorded =
                m_impl->shadowInstancedDrawCallsThisFrame;
            m_impl->lastFrameDiagnostics.particleDrawCommandsSubmitted =
                m_impl->RecordParticleItemsUVE(m_impl->particleDrawRecording, frameUniforms, commandBuffer);
            m_impl->lastFrameDiagnostics.particleDrawCallsRecorded =
                m_impl->lastFrameDiagnostics.particleDrawCommandsSubmitted > 0U ? 1U : 0U;
            m_impl->lastFrameDiagnostics.primitiveDrawCallsRecorded +=
                m_impl->RecordPrimitiveItemsUVE(m_impl->primitiveItems, frameUniforms, commandBuffer);
            if (m_impl->renderDevice.GetBackendNameUVE() == "OpenGL") {
                m_impl->lastFrameDiagnostics.glDrawCallsIssued =
                    m_impl->lastFrameDiagnostics.meshDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.primitiveDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.particleDrawCallsRecorded;
            }
            commandBuffer.EndRenderPassUVE();
        });

    // Phase 2b post-process: SSAO first (darkens colorTarget before bloom's bright-pass threshold
    // reads it, so occluded creases correctly don't bloom), then bloom. Both are pure additions to
    // the existing MainColor -> ToneMapping flow - ToneMapping still just reads colorTarget, now
    // possibly modulated by SSAO and/or bloom before it runs.
    const bool bloomTargetsValid = m_impl->bloomBrightTarget != kInvalidTextureHandleUVE &&
                                    m_impl->bloomBlurTargetA != kInvalidTextureHandleUVE &&
                                    m_impl->bloomBlurTargetB != kInvalidTextureHandleUVE;
    const bool ssaoActive = m_impl->postProcessSettings.ssaoEnabledUVE &&
                             m_impl->ssaoTarget != kInvalidTextureHandleUVE && projectionInvertible &&
                             m_impl->ssaoProgram->IsValidUVE() && m_impl->ssaoCompositeProgram->IsValidUVE();
    const bool bloomActive = m_impl->postProcessSettings.bloomEnabledUVE && bloomTargetsValid &&
                              m_impl->bloomBrightPassProgram->IsValidUVE() && m_impl->bloomBlurProgram->IsValidUVE() &&
                              m_impl->bloomCompositeProgram->IsValidUVE();

    if (ssaoActive) {
        m_impl->lastFrameDiagnostics.ssaoPassRecorded = true;
        const RenderGraphResourceHandleUVE ssaoResource = renderGraph.ImportTextureUVE(m_impl->ssaoTarget, "SSAO");
        const std::array<RenderGraphResourceUseUVE, 2U> ssaoPassResources{
            RenderGraphResourceUseUVE{depthResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{ssaoResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "SSAO", ssaoPassResources,
            [this, &projection, &inverseProjection](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->ssaoTarget;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::DontCare;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->ssaoProgram->SetIntUVE("uDepthTexture", 0);
                m_impl->ssaoProgram->SetMatrix4x4UVE("uInverseProjection", inverseProjection);
                m_impl->ssaoProgram->SetMatrix4x4UVE("uProjection", projection);
                m_impl->ssaoProgram->SetFloatUVE("uRadius", kSsaoRadiusUVE);
                m_impl->ssaoProgram->SetFloatUVE("uBias", kSsaoBiasUVE);
                m_impl->ssaoProgram->SetFloatUVE("uIntensity", kSsaoIntensityUVE);
                m_impl->ssaoProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->depthTarget, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });
        const std::array<RenderGraphResourceUseUVE, 2U> ssaoCompositeResources{
            RenderGraphResourceUseUVE{ssaoResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "SSAOComposite", ssaoCompositeResources,
            [this](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->colorTarget;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::Load;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->ssaoCompositeProgram->SetIntUVE("uSourceTexture", 0);
                m_impl->ssaoCompositeProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->ssaoTarget, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });
    }

    if (bloomActive) {
        m_impl->lastFrameDiagnostics.bloomPassRecorded = true;
        const RenderGraphResourceHandleUVE bloomBrightResource =
            renderGraph.ImportTextureUVE(m_impl->bloomBrightTarget, "BloomBright");
        const RenderGraphResourceHandleUVE bloomBlurAResource =
            renderGraph.ImportTextureUVE(m_impl->bloomBlurTargetA, "BloomBlurA");
        const RenderGraphResourceHandleUVE bloomBlurBResource =
            renderGraph.ImportTextureUVE(m_impl->bloomBlurTargetB, "BloomBlurB");
        const std::uint32_t halfWidth = HalfExtentUVE(m_impl->targetWidth);
        const std::uint32_t halfHeight = HalfExtentUVE(m_impl->targetHeight);
        const float texelSizeX = 1.0F / static_cast<float>(halfWidth);
        const float texelSizeY = 1.0F / static_cast<float>(halfHeight);

        const std::array<RenderGraphResourceUseUVE, 2U> brightPassResources{
            RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{bloomBrightResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "BloomBrightPass", brightPassResources,
            [this](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->bloomBrightTarget;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::DontCare;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->bloomBrightPassProgram->SetIntUVE("uSourceTexture", 0);
                m_impl->bloomBrightPassProgram->SetFloatUVE("uBloomThreshold", kBloomThresholdUVE);
                m_impl->bloomBrightPassProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->colorTarget, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });

        const std::array<RenderGraphResourceUseUVE, 2U> blurHResources{
            RenderGraphResourceUseUVE{bloomBrightResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{bloomBlurAResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "BloomBlurH", blurHResources,
            [this, texelSizeX, texelSizeY](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->bloomBlurTargetA;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::DontCare;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->bloomBlurProgram->SetIntUVE("uSourceTexture", 0);
                m_impl->bloomBlurProgram->SetFloatUVE("uBlurDirectionX", 1.0F);
                m_impl->bloomBlurProgram->SetFloatUVE("uBlurDirectionY", 0.0F);
                m_impl->bloomBlurProgram->SetFloatUVE("uTexelSizeX", texelSizeX);
                m_impl->bloomBlurProgram->SetFloatUVE("uTexelSizeY", texelSizeY);
                m_impl->bloomBlurProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->bloomBrightTarget, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });

        const std::array<RenderGraphResourceUseUVE, 2U> blurVResources{
            RenderGraphResourceUseUVE{bloomBlurAResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{bloomBlurBResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "BloomBlurV", blurVResources,
            [this, texelSizeX, texelSizeY](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->bloomBlurTargetB;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::DontCare;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->bloomBlurProgram->SetIntUVE("uSourceTexture", 0);
                m_impl->bloomBlurProgram->SetFloatUVE("uBlurDirectionX", 0.0F);
                m_impl->bloomBlurProgram->SetFloatUVE("uBlurDirectionY", 1.0F);
                m_impl->bloomBlurProgram->SetFloatUVE("uTexelSizeX", texelSizeX);
                m_impl->bloomBlurProgram->SetFloatUVE("uTexelSizeY", texelSizeY);
                m_impl->bloomBlurProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->bloomBlurTargetA, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });

        const std::array<RenderGraphResourceUseUVE, 2U> bloomCompositeResources{
            RenderGraphResourceUseUVE{bloomBlurBResource, RenderGraphResourceAccessUVE::Read},
            RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Write}};
        renderGraph.AddPassUVE(
            "BloomComposite", bloomCompositeResources,
            [this](ICommandBufferUVE& commandBuffer) {
                RenderPassDescUVE passDesc;
                passDesc.colorAttachment = m_impl->colorTarget;
                passDesc.depthAttachment = kInvalidTextureHandleUVE;
                passDesc.colorLoadOp = LoadOpUVE::Load;
                commandBuffer.BeginRenderPassUVE(passDesc);
                m_impl->bloomCompositeProgram->SetIntUVE("uSourceTexture", 0);
                m_impl->bloomCompositeProgram->ApplyToUVE(commandBuffer);
                commandBuffer.BindTextureUVE(m_impl->bloomBlurTargetB, 0U);
                commandBuffer.DrawUVE(3);
                commandBuffer.EndRenderPassUVE();
            });
    }

    // The default framebuffer is an external presentation surface, not a TextureHandleUVE; the
    // scene color input remains explicit in the graph while this pass writes that external output.
    const std::array<RenderGraphResourceUseUVE, 1U> toneMappingResources{
        RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Read}};
    renderGraph.AddPassUVE(
        "ToneMapping", toneMappingResources,
        [this](ICommandBufferUVE& commandBuffer) {
            if (!m_impl->toneMappingProgram->IsValidUVE()) {
                return;
            }
            m_impl->lastFrameDiagnostics.toneMappingPassRecorded = true;
            RenderPassDescUVE passDesc;
            passDesc.colorAttachment = m_impl->destinationTextureOverride.has_value()
                                           ? m_impl->destinationTextureOverride->first
                                           : kInvalidTextureHandleUVE;
            passDesc.depthAttachment = m_impl->destinationTextureOverride.has_value()
                                           ? m_impl->destinationTextureOverride->second
                                           : kInvalidTextureHandleUVE;
            passDesc.viewportOverride = m_impl->destinationViewportOverride;
            commandBuffer.BeginRenderPassUVE(passDesc);
            m_impl->toneMappingProgram->SetIntUVE("uSourceTexture", 0);
            m_impl->toneMappingProgram->ApplyToUVE(commandBuffer);
            commandBuffer.BindTextureUVE(m_impl->colorTarget, 0U);
            commandBuffer.DrawUVE(3);
            commandBuffer.EndRenderPassUVE();
        });

    // UI overlay renders whenever the frame's destination size is known - the plain presentation
    // surface (targetWidth/targetHeight), an explicit sub-region (destinationViewportOverride), or
    // (Phase U3b) a RenderFrameToTargetUVE() caller-supplied texture whose size it passed directly
    // (destinationTextureSizeOverride) - TextureHandleUVE itself has no queryable size on
    // IRenderDeviceUVE, which is why that call must supply it explicitly.
    if (m_impl->uiRuntimeForFrame != nullptr) {
        const std::uint32_t uiWidth = m_impl->destinationTextureSizeOverride.has_value()
                                           ? m_impl->destinationTextureSizeOverride->first
                                       : m_impl->destinationViewportOverride.has_value()
                                           ? m_impl->destinationViewportOverride->width
                                           : m_impl->targetWidth;
        const std::uint32_t uiHeight = m_impl->destinationTextureSizeOverride.has_value()
                                            ? m_impl->destinationTextureSizeOverride->second
                                        : m_impl->destinationViewportOverride.has_value()
                                            ? m_impl->destinationViewportOverride->height
                                            : m_impl->targetHeight;
        if (uiWidth > 0U && uiHeight > 0U) {
            const std::array<RenderGraphResourceUseUVE, 1U> uiOverlayResources{
                RenderGraphResourceUseUVE{colorResource, RenderGraphResourceAccessUVE::Read}};
            renderGraph.AddPassUVE(
                "UIOverlay", uiOverlayResources,
                [this, uiWidth, uiHeight](ICommandBufferUVE& commandBuffer) {
                    const UI::UIFontAtlasUVE& fontAtlas = m_impl->uiRuntimeForFrame->GetFontAtlasUVE();
                    if (m_impl->uiFontAtlasTexture == kInvalidTextureHandleUVE && fontAtlas.IsValidUVE()) {
                        m_impl->uiFontAtlasTexture = m_impl->renderDevice.CreateTextureUVE(
                            TextureDescUVE{static_cast<std::uint32_t>(UI::UIFontAtlasUVE::kAtlasWidthUVE),
                                           static_cast<std::uint32_t>(UI::UIFontAtlasUVE::kAtlasHeightUVE),
                                           TextureFormatUVE::RGBA8Unorm, 1},
                            std::as_bytes(std::span(fontAtlas.GetBitmapUVE())));
                    }

                    RenderPassDescUVE passDesc;
                    // Mirrors the ToneMapping pass's own destinationTextureOverride handling just
                    // above: when RenderFrameToTargetUVE() is the caller, ToneMapping already wrote
                    // into that caller-owned texture pair rather than the presentation surface, so
                    // this pass must draw into the same place - kInvalidTextureHandleUVE here would
                    // otherwise bind FBO 0 (the real window) and silently misdirect the UI overlay.
                    passDesc.colorAttachment = m_impl->destinationTextureOverride.has_value()
                                                    ? m_impl->destinationTextureOverride->first
                                                    : kInvalidTextureHandleUVE;
                    passDesc.depthAttachment = m_impl->destinationTextureOverride.has_value()
                                                    ? m_impl->destinationTextureOverride->second
                                                    : kInvalidTextureHandleUVE;
                    passDesc.colorLoadOp = LoadOpUVE::Load;
                    // Must preserve MainColor's real depth values (RenderPassDescUVE's own default
                    // is Clear->1.0) - this pass now writes its own 0.0 depth for visible UI pixels
                    // (see uiOverlayProgramDesc's own comment), and clearing first would wipe every
                    // 3D mesh's real depth back to "nothing drawn here" for any host compositing
                    // against this depth buffer, exactly as colorLoadOp = Load already preserves the
                    // color buffer's own prior content.
                    passDesc.depthLoadOp = LoadOpUVE::Load;
                    passDesc.viewportOverride = m_impl->destinationViewportOverride;
                    commandBuffer.BeginRenderPassUVE(passDesc);
                    const Math::Matrix4x4UVE uiProjection = Math::Matrix4x4UVE::OrthographicUVE(
                        0.0F, static_cast<float>(uiWidth), static_cast<float>(uiHeight), 0.0F, -1.0F, 1.0F);
                    static_cast<void>(m_impl->RecordUIOverlayItemsUVE(m_impl->uiRuntimeForFrame->GetDrawBatchUVE(),
                                                                       uiProjection, commandBuffer));
                    commandBuffer.EndRenderPassUVE();
                });
        }
    }

    m_impl->renderSystem.BeginFrameUVE();
    ICommandBufferUVE& commandBuffer = m_impl->renderSystem.GetFrameCommandBufferUVE();
    const bool graphExecuted = renderGraph.ExecuteUVE(commandBuffer);
    UVE_ASSERT(graphExecuted && "Renderer3DUVE must build a valid render graph");
    m_impl->renderSystem.EndFrameUVE();
}

void Renderer3DUVE::RenderFrameWithParticleRuntimeUVE(Scene::IEntityManagerUVE& entityManager,
                                                         Scene::EntityUVE cameraEntity,
                                                         const Scene::ParticleRuntimeUVE& particleRuntime) {
    const Scene::ParticleRuntimeUVE* const previousRuntime = m_impl->particleRuntimeForFrame;
    m_impl->particleRuntimeForFrame = &particleRuntime;
    struct RuntimeFrameScopeUVE final {
        const Scene::ParticleRuntimeUVE*& slot;
        const Scene::ParticleRuntimeUVE* previous;
        ~RuntimeFrameScopeUVE() { slot = previous; }
    } scope{m_impl->particleRuntimeForFrame, previousRuntime};
    RenderFrameUVE(entityManager, cameraEntity);
}

void Renderer3DUVE::RenderFrameToRegionUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                            const ViewportRectUVE& region,
                                            const Scene::ParticleRuntimeUVE* const particleRuntime) {
    const std::optional<ViewportRectUVE> previousOverride = m_impl->destinationViewportOverride;
    m_impl->destinationViewportOverride = region;
    struct RegionScopeUVE final {
        std::optional<ViewportRectUVE>& slot;
        std::optional<ViewportRectUVE> previous;
        ~RegionScopeUVE() { slot = previous; }
    } regionScope{m_impl->destinationViewportOverride, previousOverride};

    const Scene::ParticleRuntimeUVE* const previousRuntime = m_impl->particleRuntimeForFrame;
    m_impl->particleRuntimeForFrame = particleRuntime;
    struct RuntimeFrameScopeUVE final {
        const Scene::ParticleRuntimeUVE*& slot;
        const Scene::ParticleRuntimeUVE* previous;
        ~RuntimeFrameScopeUVE() { slot = previous; }
    } runtimeScope{m_impl->particleRuntimeForFrame, previousRuntime};

    RenderFrameUVE(entityManager, cameraEntity);
}

void Renderer3DUVE::RenderFrameToTargetUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                            const TextureHandleUVE colorTarget, const TextureHandleUVE depthTarget,
                                            const std::uint32_t width, const std::uint32_t height) {
    const std::optional<std::pair<TextureHandleUVE, TextureHandleUVE>> previousOverride =
        m_impl->destinationTextureOverride;
    m_impl->destinationTextureOverride = std::make_pair(colorTarget, depthTarget);
    struct TargetScopeUVE final {
        std::optional<std::pair<TextureHandleUVE, TextureHandleUVE>>& slot;
        std::optional<std::pair<TextureHandleUVE, TextureHandleUVE>> previous;
        ~TargetScopeUVE() { slot = previous; }
    } targetScope{m_impl->destinationTextureOverride, previousOverride};

    const std::optional<std::pair<std::uint32_t, std::uint32_t>> previousSizeOverride =
        m_impl->destinationTextureSizeOverride;
    m_impl->destinationTextureSizeOverride =
        (width > 0U && height > 0U) ? std::make_optional(std::make_pair(width, height)) : std::nullopt;
    struct TargetSizeScopeUVE final {
        std::optional<std::pair<std::uint32_t, std::uint32_t>>& slot;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> previous;
        ~TargetSizeScopeUVE() { slot = previous; }
    } targetSizeScope{m_impl->destinationTextureSizeOverride, previousSizeOverride};

    RenderFrameUVE(entityManager, cameraEntity);
}

void Renderer3DUVE::SetPostProcessSettingsUVE(const PostProcessSettingsUVE& settings) {
    m_impl->postProcessSettings = settings;
}

void Renderer3DUVE::SetUIRuntimeUVE(const UI::UIRuntimeUVE* const uiRuntime) noexcept {
    m_impl->uiRuntimeForFrame = uiRuntime;
}

Renderer3DFrameDiagnosticsUVE Renderer3DUVE::GetLastFrameDiagnosticsUVE() const noexcept {
    return m_impl->lastFrameDiagnostics;
}

} // namespace UVE::Render
