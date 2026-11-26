// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "renderer_3d.h"
#include "bind_group.h"

#include <algorithm>
#include <cstdio>
#include <array>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset_load_completed_event.h"
#include "asset_reloaded_event.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "mesh_skinning.h"
#include "shader_asset.h"
#include "texture_asset.h"
#include "texture_compression.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "ui_view_3d_component.h"
#include "hierarchy_component.h"
#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "engine_assert.h"
#include "logging_macros.h"
#include "aabb.h"
#include "color.h"
#include "frustum.h"
#include "matrix3x3.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "scalar.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "all_objects_3d.h"
#include "compute_system.h"
#include "contact_shadows.h"
#include "decal_draw_command.h"
#include "decal_renderer.h"
#include "light_cluster_grid.h"
#include "taa_math.h"
#include "frustum_cull_indirect.h"
#include "i_light_system.h"
#include "light_shadow_planner.h"
#include "particle_draw_command.h"
#include "particle_render_bridge.h"
#include "material_pipeline_state.h"
#include "primitive_geometry.h"
#include "render_batch.h"
#include "render_graph.h"
#include "render_queue.h"
#include "renderer_3d_frame_diagnostics.h"
#include "shadow_atlas_manager.h"
#include "built_in_shaders.h"
#include "shader_program_desc.h"
#include "shader_program.h"
#include "ui_runtime.h"
#include "vfx_sprite_renderer.h"

namespace UVE::Render {

namespace {

/// Hidden - by its own switch or an ancestor's - as the scene graph resolved it last update. An
/// entity without a Visibility component is always drawn, the same rule the mesh renderer uses.
[[nodiscard]] bool IsHiddenInHierarchy(Scene::IEntityManager& entityManager, const Scene::Entity entity) {
    return entityManager.HasComponent<Scene::VisibilityComponent>(entity) &&
           !entityManager.GetComponent<Scene::VisibilityComponent>(entity).visibleInHierarchy;
}

[[nodiscard]] bool IsFiniteMatrix(const Math::Matrix4x4& matrix) noexcept {
    for (const auto& row : matrix.m) {
        for (const float value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool IsOrderedFiniteAabb(const Math::Aabb& bounds) noexcept {
    return Math::IsFinite(bounds.min) && Math::IsFinite(bounds.max) && bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

/// True when `skinned` - the bounds of a freshly skinned mesh - can be trusted as a description of
/// that mesh, judged against the bind-pose bounds `rest` of the same mesh. A pose whose bone
/// translations or scale disagree with the mesh's inverse-bind matrices (an FBX in centimetres on a
/// metre rig, 100x or 0.01x) skins every vertex to a finite but absurd place: the box is
/// ordered and finite, yet hundreds of times the model's size or collapsed to a point, so the mesh
/// is drawn off screen and the frustum test then rejects it. Bounds that are not ordered and finite,
/// or that differ from the bind pose by more than animation plausibly explains, are not trusted.
/// `travel` is how far the skeleton's root joint has moved from its bind position, and
/// `characterDiagonal` the size of the whole skeleton: a small part (a head, a hat) of a character
/// that walks or jumps far is judged by where it is on the character, not by how far it went.
[[nodiscard]] bool IsPlausibleSkinnedBounds(const Math::Aabb& skinned, const Math::Aabb& rest,
                                               const Math::Vector3 travel = Math::Vector3{},
                                               const float characterDiagonal = 0.0F) noexcept {
    if (!IsOrderedFiniteAabb(skinned)) {
        return false;
    }
    if (!IsOrderedFiniteAabb(rest)) {
        return true; // Nothing sound to compare against; the caller falls back on its own checks.
    }
    const float restDiagonal = Math::Length(rest.max - rest.min);
    if (!std::isfinite(restDiagonal) || restDiagonal <= 1.0e-4F) {
        return true; // A point-sized bind pose has no scale to compare the pose's against.
    }
    const float skinnedDiagonal = Math::Length(skinned.max - skinned.min);
    constexpr float kMinimumSizeRatio = 0.05F; // 20x smaller than the bind pose
    constexpr float kMaximumSizeRatio = 20.0F; // 20x larger than the bind pose
    constexpr float kMaximumCenterDriftInDiagonals = 10.0F;
    if (!std::isfinite(skinnedDiagonal) || skinnedDiagonal < restDiagonal * kMinimumSizeRatio ||
        skinnedDiagonal > restDiagonal * kMaximumSizeRatio) {
        return false;
    }
    const float scale = std::isfinite(characterDiagonal) ? std::max(restDiagonal, characterDiagonal) : restDiagonal;
    const Math::Vector3 shift = Math::IsFinite(travel) ? travel : Math::Vector3{};
    return Math::Length(skinned.GetCenter() - shift - rest.GetCenter()) <= scale * kMaximumCenterDriftInDiagonals;
}

[[nodiscard]] Math::Aabb ComputeLightSpaceCameraBounds(const CameraFrustumCorners& cameraCorners,
                                                              const Math::Matrix4x4& lightView) noexcept {
    Math::Vector3 minimum = Math::TransformPoint(lightView, cameraCorners[0]);
    Math::Vector3 maximum = minimum;
    for (std::size_t cornerIndex = 1; cornerIndex < cameraCorners.size(); ++cornerIndex) {
        const Math::Vector3 corner = Math::TransformPoint(lightView, cameraCorners[cornerIndex]);
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    return Math::Aabb{minimum, maximum};
}

/// Expands and snaps the XY center of a light-space orthographic box to the shadow-map texel grid.
/// The half extents are widened by one texel on each side before snapping, so the resulting box
/// remains conservative even when the snapped center moves by up to half a texel.
/// Z remains fitted exactly: directional-shadow resolution is only two-dimensional, while the
/// existing padded near/far bounds already protect depth coverage.
[[nodiscard]] Math::Aabb StabilizeLightSpaceShadowBounds(const Math::Aabb& fittedBounds, float padding,
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
    return Math::Aabb{{stabilizedX[0], stabilizedY[0], fittedBounds.min.z - clampedPadding},
                         {stabilizedX[1], stabilizedY[1], fittedBounds.max.z + clampedPadding}};
}

/// A mesh's uploaded GPU buffers, cached by MeshAsset's AssetGuid.
struct MeshGpuResources {
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    std::uint32_t indexCount = 0;
};

[[nodiscard]] bool IsValidMeshGpuResources(const MeshGpuResources& resources) noexcept {
    return resources.vertexBuffer != kInvalidBufferHandle && resources.indexBuffer != kInvalidBufferHandle &&
           resources.indexCount > 0U;
}

/// How many indices an item draws. `indexCount == 0` on the item means the mesh's whole index
/// buffer - the shape every item had before meshes could be split into surfaces - so the answer is
/// the uploaded buffer's own count.
[[nodiscard]] std::uint32_t ResolveItemIndexCount(const RenderItem& item,
                                                     const MeshGpuResources& meshResources) noexcept {
    return item.indexCount == 0U ? meshResources.indexCount : item.indexCount;
}

/// Where an item's index range begins. Only meaningful together with ResolveItemIndexCount: an
/// item that draws the whole buffer starts at zero by definition.
[[nodiscard]] std::uint32_t ResolveItemFirstIndex(const RenderItem& item) noexcept {
    return item.indexCount == 0U ? 0U : item.firstIndex;
}

/// Renderer-owned primitive draw data. It deliberately contains no AssetHandle: primitive
/// geometry is immutable renderer cache data, while authored kind/color remain ECS component state.
/// A primitive's MATERIAL reference is a plain GUID here for the same reason - the handle that
/// keeps that material loaded for the frame lives in `primitiveMaterialHandles`, keyed by this
/// GUID, because AssetHandle has no default constructor and a draw item must be aggregate-
/// initialisable without an asset manager.
struct PrimitiveRenderItem {
    Math::Matrix4x4 worldMatrix;
    Scene::PrimitiveMeshKind kind = Scene::PrimitiveMeshKind::Cube;
    Math::Vector3 baseColor{};
    float sortDepth = 0.0F;
    /// 1 solid, 0 invisible. SurfaceInstance transparency inverted, times a Self fade.
    float opacity = 1.0F;
    /// Set for an imported mesh drawn without a material (see ExtractUnmaterialedMeshItems):
    /// the geometry then comes from that mesh asset instead of the built-in `kind`. The mesh stays
    /// loaded through `unmaterialedMeshHandles` for as long as an item can name it.
    Asset::AssetGuid meshGuid = Asset::kInvalidAssetGuid;
    const Asset::MeshAsset* mesh = nullptr;
    /// Set for a skinned mesh posed by a skeleton this frame: that entity's own vertex buffer.
    const MeshGpuResources* skinned = nullptr;
    /// The material this primitive is shaded with, or kInvalidAssetGuid for the built-in lit
    /// material (kDefaultLitMaterial). Only authored primitives set this; the unmaterialed-mesh
    /// items above leave it unset, so they draw through the same default material a primitive with
    /// no material assigned does.
    Asset::AssetGuid materialGuid = Asset::kInvalidAssetGuid;
    /// RenderInstance layers this item is on; 1 when the entity has no RenderInstance. Same value,
    /// same meaning and same default as RenderItem::renderLayers - the lit shader reads it for
    /// reflection-probe layer filtering.
    std::uint32_t renderLayers = 1U;
    /// First index in the index buffer for this surface draw.
    std::uint32_t firstIndex = 0U;
    /// Number of indices for this surface draw, or 0U to draw the whole index buffer.
    std::uint32_t indexCount = 0U;
};

/// The material a primitive with no authored material is shaded with, and the same one an imported
/// mesh with no material of its own draws through. It is a plain MaterialAsset so the PBR path
/// has exactly one input type: neutral white albedo, fully dielectric, mid roughness. `baseColor`
/// on the primitive replaces the albedo, which is why this stays white rather than grey - a
/// tinted default would silently darken every primitive whose author never touched the colour.
inline const Asset::MaterialAsset kDefaultLitMaterial{};

/// Identity of a primitive's placement inputs. Two placements with equal keys must produce equal
/// world matrices and bounds, so every input the extraction reads appears here.
///
/// Exact float equality, matching MeshPlacementKey deliberately rather than using a tolerance:
/// a tolerance would let an object drift arbitrarily far in steps below it, and NaN comparing
/// unequal is the outcome we want twice over - recomputation is what rejects it, and a key that
/// "matched" two NaNs would be claiming a broken transform is unchanged.
struct PrimitivePlacementKey {
    Math::Vector3 worldPosition{};
    Math::Quaternion worldRotation{};
    Math::Vector3 worldScale{};
    Scene::PrimitiveMeshKind kind = Scene::PrimitiveMeshKind::Cube;

    [[nodiscard]] bool Matches(const PrimitivePlacementKey& other) const noexcept {
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
struct PrimitivePlacementCacheEntry {
    PrimitivePlacementKey key{};
    Math::Matrix4x4 worldMatrix{};
    Math::Aabb worldBounds{};
    bool placed = false;
    std::uint64_t lastSeenFrame = 0U;
};

/// CPU-expanded particle vertex consumed by the minimal built-in particle pipeline. The four
/// color floats carry a stable warm tint plus lifetime-derived alpha; keeping this as a private
/// renderer DTO prevents particle authoring data from crossing the RHI boundary.
struct ParticleVertex {
    Math::Vector3 position{};
    float red = 1.0F;
    float green = 0.45F;
    float blue = 0.08F;
    float alpha = 1.0F;
};

/// CPU-expanded vertex consumed by the built-in UI overlay pipeline (ui_overlay.glsl) - one
/// screen-space position/texcoord pair plus a per-vertex tint, matching UI::UIQuad's own field
/// shape directly (kept as a private renderer DTO for the same reason ParticleVertex is).
struct UIVertex {
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
inline constexpr std::uint32_t kInstanceTransformSlot = 0U;
inline constexpr std::uint32_t kInstanceNormalTransformSlot = 1U;
inline constexpr std::uint32_t kInstanceBaseSlot = 2U;
inline constexpr std::uint32_t kInstanceVisibleIndexSlot = 3U;
inline constexpr std::uint32_t kClusteredLightSlot = 4U;
inline constexpr std::uint32_t kClusteredCellSlot = 5U;
inline constexpr std::uint32_t kClusteredIndexSlot = 6U;

/// The most instances one frame may upload. Bounds the per-frame upload the same way
/// kMaximumParticleGpuDrawCommands bounds particles; a batch that would exceed it is recorded
/// per-object instead, which is slower but never wrong.
inline constexpr std::size_t kMaximumInstancesPerFrame = 262'144U;

/// Whether a material's vertex source actually implements the instancing contract.
///
/// Deliberately a source-text check rather than a flag on MaterialAsset: a flag would let a
/// material CLAIM instancing support that its shader does not implement, and the failure mode for
/// that lie is silent (every instance drawn at the first one's transform). The source either reads
/// the instance buffer or it does not, and that is the only thing worth trusting here.
[[nodiscard]] bool VertexSourceSupportsInstancing(const std::string_view vertexSource) noexcept {
    return vertexSource.contains("uInstanceBaseIndex") &&
           vertexSource.contains("gl_InstanceID");
}

inline constexpr std::size_t kMaximumParticleGpuDrawCommands = 32'768U;
inline constexpr std::size_t kParticleVerticesPerCommand = 6U;
inline constexpr float kParticleHalfExtent = 0.05F;

inline constexpr std::size_t kMaximumUIQuads = 32'768U;
inline constexpr std::size_t kUIVerticesPerQuad = 6U;

/// A material's manager-owned linked program plus its resolved texture handles, cached by
/// MaterialAsset's AssetGuid. `program` owns its linked pipeline through ShaderManager;
/// Renderer3D only retains a shared reference and must never destroy that pipeline directly.
/// The source GUIDs let AssetReloaded events invalidate exactly the materials that reference a
/// changed vertex or fragment shader. Texture handles are never kInvalidTextureHandle once
/// cached — an unset MaterialAsset texture GUID resolves to one of Renderer3D's two
/// fallback textures (see ResolveTextureGpuHandle()'s doc comment).
struct MaterialGpuResources {
    std::shared_ptr<Shader::ShaderProgram> program;
    /// True only when this material's own vertex source actually declares the instancing
    /// contract. Instancing is OPT-IN per material and DETECTED, never assumed: material shaders
    /// come from `.uvshader` assets a project authors, so most of them know nothing about
    /// gl_InstanceID. Drawing such a material with instanceCount > 1 would not fail - it would
    /// silently stack every instance on top of the first one's uModel, which looks like missing
    /// objects rather than like a bug in the renderer.
    bool supportsInstancing = false;
    Asset::AssetGuid vertexShaderGuid;
    Asset::AssetGuid fragmentShaderGuid;
    Asset::AssetGuid albedoTextureGuid;
    Asset::AssetGuid normalTextureGuid;
    Asset::AssetGuid aoTextureGuid;
    Asset::AssetGuid metallicRoughnessTextureGuid;
    Asset::AssetGuid emissiveTextureGuid;
    TextureHandle albedoTexture;
    TextureHandle normalTexture;
    TextureHandle aoTexture;
    TextureHandle metallicRoughnessTexture;
    TextureHandle emissiveTexture;
    /// Depth-write off, source-alpha blend. Used when SurfaceInstance3D fades the mesh or draws
    /// an overlay; the ordinary program stays opaque so solid draws keep early-z.
    std::shared_ptr<Shader::ShaderProgram> blendedProgram;
};

/// Picks the program a material draws with. `blended` is true for anything that must not write
/// depth - a fading SurfaceInstance, a SurfaceInstance overlay, or a primitive drawn at partial
/// opacity - and is a plain flag rather than a RenderItem so the primitive path (which has no
/// RenderItem) resolves it through the same rule the mesh path does.
[[nodiscard]] Shader::ShaderProgram* MeshColorProgram(const MaterialGpuResources& resources,
                                                           bool blended) noexcept {
    if (blended) {
        if (resources.blendedProgram == nullptr || !resources.blendedProgram->IsValid()) {
            return nullptr;
        }
        return resources.blendedProgram.get();
    }
    if (resources.program == nullptr || !resources.program->IsValid()) {
        return nullptr;
    }
    return resources.program.get();
}

/// Fixed texture-unit slots used by the built-in lit path: three core material maps, shadow cascades
/// (3-5), reflection-probe faces (6-11), two remaining material maps (12-13), and the world ambient
/// environment map (14). Slot 14 stays within GLES3's minimum 16 fragment texture units (0-15).
constexpr std::uint32_t kAlbedoTextureSlot = 0;
constexpr std::uint32_t kNormalTextureSlot = 1;
constexpr std::uint32_t kAoTextureSlot = 2;
constexpr std::uint32_t kMetallicRoughnessTextureSlot = 12;
constexpr std::uint32_t kEmissiveTextureSlot = 13;
constexpr std::uint32_t kAmbientEnvironmentTextureSlot = 14U;

/// Slot the directional-light shadow map is bound to for the main color pass (Increment 26) —
/// the next slot after the three material texture slots above, following the same fixed-constant
/// convention.
constexpr std::uint32_t kShadowMapTextureSlot = 3U;

/// The render layers the main view draws. Every layer, because CameraComponent carries no layer
/// filter yet - when it grows one, this becomes the camera's mask and the decal pass already tests
/// its decals against it. Until then a decal whose cullMask omits EVERY layer is the only decal the
/// main view cannot see, which is exactly what an authored mask that matches nothing means.
constexpr std::uint32_t kMainViewReceiverLayerMask = 0xFFFFFFFFU;

// The renderer always clears its scene target. Desktop uses HDR RGBA16F while Android uses the
// GLES3-safe RGBA8 variant below. This neutral charcoal is the intentional empty-scene environment
// baseline; it is not an editor overlay and never counts as primitive presentation evidence in the
// real-GL fixture tests.
constexpr std::array<float, 4> kDefaultSceneClearColor{0.050F, 0.050F, 0.050F, 1.0F};
// Select HDR RGBA16Float whenever the active RHI backend advertises half-float color-attachment
// renderability (all desktop GL 3.3+, Vulkan, Null model, and modern Android GPUs exposing
// GL_EXT_color_buffer_float / GL_EXT_color_buffer_half_float), falling back to core RGBA8Unorm
// only on constrained ES 3.0 implementations without float render target extensions.
[[nodiscard]] TextureFormat SceneColorTargetFormat(
    const IRenderDevice& renderDevice,
    const Platform::RendererProfile effectiveProfile = Platform::RendererProfile::ForwardPlus) noexcept {
    if (effectiveProfile == Platform::RendererProfile::Compatibility) {
        return TextureFormat::RGBA8Unorm;
    }
    return renderDevice.GetCapabilities().halfFloatColorTargets != RenderFeatureSupport::Unsupported
               ? TextureFormat::RGBA16Float
               : TextureFormat::RGBA8Unorm;
}
constexpr std::size_t kShadowCascadeCount = 3;
constexpr std::uint32_t kShadowCascadeFirstTextureSlot = kShadowMapTextureSlot;
constexpr std::uint32_t kReflectionProbeFirstTextureSlot = 6U;
constexpr std::uint32_t kReflectionProbeSecondaryTextureSlot = 7U;
constexpr std::uint32_t kSkyTextureSlot = 1U;
constexpr std::uint32_t kToneMappingLutTextureSlot = 2U;
constexpr std::uint32_t kToneMappingSsaoTextureSlot = 14U;
constexpr std::uint32_t kToneMappingContactShadowTextureSlot = 11U;
constexpr std::uint32_t kToneMappingSsrTextureSlot = 12U;
constexpr std::uint32_t kToneMappingBloomTextureSlot = 13U;

// Phase 2b post-process defaults. User-facing SSAO radius, intensity, power, and sample quality live
// in PostProcessSettings; without a WorldEnvironment the renderer keeps bloom's legacy hard knee.
constexpr float kBloomThreshold = 1.0F;
constexpr float kBloomSoftKnee = 0.0F;
constexpr std::uint32_t kDefaultBloomMipCount = 1U;
constexpr std::uint32_t kMaximumBloomMipCount = Scene::kMaximumWorldEnvironmentBloomMipCount;
constexpr float kSsaoBias = 0.025F;
constexpr std::array<std::int32_t, 3U> kSsaoSamplesPerQuality{4, 8, 12};

[[nodiscard]] constexpr std::uint32_t HalfExtent(const std::uint32_t extent) noexcept {
    return std::max(1U, extent / 2U);
}

[[nodiscard]] constexpr std::uint32_t MaximumBloomMipCountForTarget(const std::uint32_t targetWidth,
                                                                         const std::uint32_t targetHeight) noexcept {
    std::uint32_t width = HalfExtent(targetWidth);
    std::uint32_t height = HalfExtent(targetHeight);
    std::uint32_t count = 1U;
    while (count < kMaximumBloomMipCount && (width > 1U || height > 1U)) {
        width = HalfExtent(width);
        height = HalfExtent(height);
        ++count;
    }
    return count;
}

using ShadowCascadeMatrices = std::array<Math::Matrix4x4, kShadowCascadeCount>;
using ShadowCascadeSplits = std::array<float, kShadowCascadeCount>;

struct LightUniformNames {
    std::string type;
    std::string position;
    std::string direction;
    std::string color;
    std::string intensity;
    std::string range;
    std::string spotAngleDegrees;
    std::string cullMask;
    std::string specular;
};

struct FogVolumeUniformNames {
    std::string position;
    std::string axisX;
    std::string axisY;
    std::string axisZ;
    std::string scale;
    std::string size;
    std::string albedo;
    std::string emission;
    std::string density;
    std::string heightFalloff;
    std::string edgeFade;
    std::string shape;
};

struct RendererUniformNames {
    std::array<LightUniformNames, kMaxLights> lights{};
    std::array<FogVolumeUniformNames, Scene::kMaximumFogVolumesPerFrame> fogVolumes{};
    std::string legacyLightSpaceMatrix;
    std::array<std::string, kShadowCascadeCount> lightSpaceMatrices{};
    std::array<std::string, kShadowCascadeCount> shadowCascadeSplits{};
    std::array<std::string, kShadowCascadeCount> shadowMapTextures{};
    std::array<std::string, kShadowCascadeCount> shadowPasses{};
    // Tier 2.3: one cube uniform (slots 7..11, once the other five faces, are now unused).
    std::string reflectionProbeCube;
    // Increment 32: the per-light atlas shadow contract, indexed by uLights[] slot exactly like
    // the shader's own fixed-size arrays. A spot light reads one matrix + one rect; a point light
    // reads six cube rects, at [lightIndex * 6 + faceIndex].
    std::array<std::string, kMaxShadowedLights> lightShadowMatrices{};
    std::array<std::string, kMaxShadowedLights> lightShadowTileRects{};
    std::array<std::string, kMaxShadowedLights> lightShadowEnabled{};
    std::array<std::string, kMaxShadowedLights> lightShadowDepthRanges{};
    std::array<std::string, kMaxShadowTiles> lightShadowCubeRects{};
    std::array<std::string, kMaxLights> lightShadowSlots{};
    std::string shadowAtlasTexelSize;

    RendererUniformNames() : legacyLightSpaceMatrix("uLightSpaceMatrix") {
        for (std::size_t lightIndex = 0; lightIndex < kMaxLights; ++lightIndex) {
            const std::string prefix = "uLights[" + std::to_string(lightIndex) + "].";
            lights[lightIndex] = LightUniformNames{
                prefix + "type", prefix + "position", prefix + "direction", prefix + "color",
                prefix + "intensity", prefix + "range", prefix + "spotAngleDegrees",
                prefix + "cullMask", prefix + "specular"};
        }
        for (std::size_t volumeIndex = 0; volumeIndex < Scene::kMaximumFogVolumesPerFrame; ++volumeIndex) {
            const std::string prefix = "uFogVolumes[" + std::to_string(volumeIndex) + "].";
            fogVolumes[volumeIndex] = FogVolumeUniformNames{
                prefix + "position", prefix + "axisX", prefix + "axisY", prefix + "axisZ", prefix + "scale",
                prefix + "size", prefix + "albedo", prefix + "emission", prefix + "density",
                prefix + "heightFalloff", prefix + "edgeFade", prefix + "shape"};
        }
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
            const std::string index = std::to_string(cascadeIndex);
            lightSpaceMatrices[cascadeIndex] = "uLightSpaceMatrices[" + index + "]";
            shadowCascadeSplits[cascadeIndex] = "uShadowCascadeSplits[" + index + "]";
            shadowMapTextures[cascadeIndex] = "uShadowMapTextures[" + index + "]";
            shadowPasses[cascadeIndex] = "DirectionalShadowCascade" + index;
        }
        reflectionProbeCube = "uReflectionProbeCube";
        for (std::size_t lightSlot = 0; lightSlot < kMaxShadowedLights; ++lightSlot) {
            const std::string index = std::to_string(lightSlot);
            lightShadowMatrices[lightSlot] = "uLightShadowMatrices[" + index + "]";
            lightShadowTileRects[lightSlot] = "uLightShadowTileRects[" + index + "]";
            lightShadowEnabled[lightSlot] = "uLightShadowEnabled[" + index + "]";
            lightShadowDepthRanges[lightSlot] = "uLightShadowDepthRange[" + index + "]";
        }
        for (std::size_t lightIndex = 0; lightIndex < kMaxLights; ++lightIndex) {
            lightShadowSlots[lightIndex] = "uLightShadowSlot[" + std::to_string(lightIndex) + "]";
        }
        for (std::size_t tileSlot = 0; tileSlot < kMaxShadowTiles; ++tileSlot) {
            lightShadowCubeRects[tileSlot] = "uLightShadowCubeRects[" + std::to_string(tileSlot) + "]";
        }
        shadowAtlasTexelSize = "uShadowAtlasTexelSize";
    }
};

[[nodiscard]] const RendererUniformNames& GetRendererUniformNames() {
    static const RendererUniformNames names;
    return names;
}

[[nodiscard]] float SanitizeFiniteNonNegative(float value, float fallback, std::string_view name) noexcept {
    const bool finite = std::isfinite(value);
    UVE_ASSERT(finite);
    if (!finite) {
        UVE_ERROR("Renderer3D: {} must be finite; using {}", name, fallback);
        return fallback;
    }
    return std::max(value, 0.0F);
}

[[nodiscard]] float SanitizeFiniteClamped(float value, float fallback, float minimum, float maximum,
                                              std::string_view name) noexcept {
    const bool finite = std::isfinite(value);
    UVE_ASSERT(finite);
    if (!finite) {
        UVE_ERROR("Renderer3D: {} must be finite; using {}", name, fallback);
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] ShadowCascadeSplits ComputeCascadeSplits(float nearPlane, float farPlane,
                                                              float splitLambda) noexcept {
    const bool validPlanes = std::isfinite(nearPlane) && nearPlane > 0.0F && std::isfinite(farPlane) &&
                              farPlane > nearPlane;
    const bool validLambda = std::isfinite(splitLambda);
    UVE_ASSERT(validPlanes && validLambda);
    if (!validPlanes) {
        UVE_ERROR("Renderer3D: ComputeCascadeSplits received invalid shadow clip planes");
        return ShadowCascadeSplits{};
    }
    if (!validLambda) {
        UVE_ERROR("Renderer3D: ComputeCascadeSplits received a non-finite split lambda; using 0.5");
    }
    ShadowCascadeSplits splits{};
    const float clampedLambda = std::clamp(validLambda ? splitLambda : 0.5F, 0.0F, 1.0F);
    float previousSplit = nearPlane;
    for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
        if (cascadeIndex + 1U == kShadowCascadeCount) {
            splits[cascadeIndex] = farPlane;
            break;
        }
        const float progress = static_cast<float>(cascadeIndex + 1U) / static_cast<float>(kShadowCascadeCount);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * progress;
        const float logarithmicSplit = nearPlane * std::pow(farPlane / nearPlane, progress);
        const float rawSplit = uniformSplit * (1.0F - clampedLambda) + logarithmicSplit * clampedLambda;
        splits[cascadeIndex] = std::clamp(rawSplit, std::nextafter(previousSplit, farPlane), farPlane);
        previousSplit = splits[cascadeIndex];
    }
    return splits;
}

[[nodiscard]] bool AreCascadeSplitsValid(const ShadowCascadeSplits& splits, float nearPlane,
                                             float farPlane) noexcept {
    const float maxAllowed = farPlane + std::max(1.0e-4F, std::abs(farPlane) * 1.0e-5F);
    float previousSplit = nearPlane;
    for (const float split : splits) {
        if (!std::isfinite(split) || split <= previousSplit || split > maxAllowed) {
            return false;
        }
        previousSplit = split;
    }
    return true;
}

[[nodiscard]] CameraFrustumCorners ComputeCascadeFrustumCorners(
    const CameraFrustumCorners& fullCameraCorners, float nearRatio, float farRatio) noexcept {
    CameraFrustumCorners cascadeCorners{};
    for (std::size_t cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
        const Math::Vector3 nearCorner = fullCameraCorners[cornerIndex];
        const Math::Vector3 farCorner = fullCameraCorners[cornerIndex + 4U];
        cascadeCorners[cornerIndex] = nearCorner + (farCorner - nearCorner) * nearRatio;
        cascadeCorners[cornerIndex + 4U] = nearCorner + (farCorner - nearCorner) * farRatio;
    }
    return cascadeCorners;
}

/// 1x1 RGBA8Unorm pixel data for the two fallback textures created once per Renderer3D
/// instance (see Impl::fallbackWhiteTexture/fallbackNormalTexture's own doc comments).
constexpr std::array<std::uint8_t, 4> kWhitePixel{0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 4> kFlatNormalPixel{0x80, 0x80, 0xFF, 0xFF};

/// Translates a loaded TextureAsset's format into the RHI's own TextureFormat (a
/// deliberately separate enum — see Asset::TextureAssetFormat's own doc comment for why). Asset
/// textures never use Depth32Float (that's only ever created directly as a GPU render target), so
/// this mapping is exhaustive over Asset::TextureAssetFormat's two enumerators.
[[nodiscard]] TextureFormat ToRenderTextureFormat(Asset::TextureAssetFormat format) noexcept {
    switch (format) {
        case Asset::TextureAssetFormat::RGBA8Unorm:
            return TextureFormat::RGBA8Unorm;
        case Asset::TextureAssetFormat::RGBA16Float:
            return TextureFormat::RGBA16Float;
    }
    UVE_ASSERT(false && "Unhandled Asset::TextureAssetFormat");
    return TextureFormat::RGBA8Unorm;
}

[[nodiscard]] TextureColorSpace ToRenderTextureColorSpace(
    Asset::TextureAssetColorSpace colorSpace) noexcept {
    switch (colorSpace) {
        case Asset::TextureAssetColorSpace::Linear:
            return TextureColorSpace::Linear;
        case Asset::TextureAssetColorSpace::Srgb:
            return TextureColorSpace::Srgb;
    }
    UVE_ASSERT(false && "Unhandled Asset::TextureAssetColorSpace");
    return TextureColorSpace::Linear;
}

[[nodiscard]] std::optional<TextureFormat> ToRenderTranscodeFormat(
    const Asset::TextureTranscodeTarget target) noexcept {
    switch (target) {
        case Asset::TextureTranscodeTarget::Rgba8Unorm:
            return TextureFormat::RGBA8Unorm;
        case Asset::TextureTranscodeTarget::Bc1Rgb:
            return TextureFormat::BC1RGB;
        case Asset::TextureTranscodeTarget::Bc3Rgba:
            return TextureFormat::BC3RGBA;
        case Asset::TextureTranscodeTarget::Bc7Rgba:
            return TextureFormat::BC7RGBA;
        case Asset::TextureTranscodeTarget::Etc2Rgb:
            return TextureFormat::ETC2RGB8;
        case Asset::TextureTranscodeTarget::Etc2Rgba:
            return TextureFormat::ETC2RGBA8;
        case Asset::TextureTranscodeTarget::Astc4x4Rgba:
            return TextureFormat::ASTC4x4RGBA;
    }
    return std::nullopt;
}

[[nodiscard]] bool BuildTextureAssetUpload(const Asset::TextureAsset& textureAsset,
                                               IRenderDevice& renderDevice,
                                               TextureDesc& outDesc,
                                               std::vector<std::byte>& outUploadData) {
    const TextureColorSpace colorSpace = ToRenderTextureColorSpace(textureAsset.colorSpace);
    if (textureAsset.payloadEncoding == Asset::TexturePayloadEncoding::RawPixels) {
        std::size_t uploadByteCount = textureAsset.pixels.size();
        const std::size_t maximumUploadBytes = std::vector<std::byte>{}.max_size();
        for (const Asset::TextureMipLevel& mipLevel : textureAsset.mipLevels) {
            if (mipLevel.pixels.size() > maximumUploadBytes - uploadByteCount) {
                return false;
            }
            uploadByteCount += mipLevel.pixels.size();
        }
        std::vector<std::byte> uploadData;
        uploadData.reserve(uploadByteCount);
        uploadData.insert(uploadData.end(), textureAsset.pixels.begin(), textureAsset.pixels.end());
        for (const Asset::TextureMipLevel& mipLevel : textureAsset.mipLevels) {
            uploadData.insert(uploadData.end(), mipLevel.pixels.begin(), mipLevel.pixels.end());
        }
        const TextureDesc desc{textureAsset.width, textureAsset.height,
                                  ToRenderTextureFormat(textureAsset.format),
                                  static_cast<std::uint32_t>(textureAsset.mipLevels.size() + 1U), colorSpace};
        if (!ValidateTextureUpload(desc, uploadData)) {
            return false;
        }
        outDesc = desc;
        outUploadData = std::move(uploadData);
        return true;
    }
    if (textureAsset.payloadEncoding != Asset::TexturePayloadEncoding::BasisUniversalKtx2) {
        return false;
    }

    Asset::TextureCompressionInfo compressionInfo;
    if (!Asset::GetTextureCompressionInfo(textureAsset, compressionInfo)) {
        return false;
    }
    const std::array<Asset::TextureTranscodeTarget, 5U> opaqueTargets{
        Asset::TextureTranscodeTarget::Bc7Rgba,
        Asset::TextureTranscodeTarget::Bc1Rgb,
        Asset::TextureTranscodeTarget::Bc3Rgba,
        Asset::TextureTranscodeTarget::Etc2Rgb,
        Asset::TextureTranscodeTarget::Astc4x4Rgba};
    const std::array<Asset::TextureTranscodeTarget, 4U> alphaTargets{
        Asset::TextureTranscodeTarget::Bc7Rgba,
        Asset::TextureTranscodeTarget::Bc3Rgba,
        Asset::TextureTranscodeTarget::Astc4x4Rgba,
        Asset::TextureTranscodeTarget::Etc2Rgba};
    const auto tryTarget = [&](const Asset::TextureTranscodeTarget target) {
        const std::optional<TextureFormat> format = ToRenderTranscodeFormat(target);
        if (!format.has_value() || !renderDevice.SupportsTextureFormat(*format, colorSpace)) {
            return false;
        }
        Asset::TextureTranscodedMipChain transcoded;
        if (!Asset::TranscodeTextureAsset(textureAsset, target, transcoded)) {
            return false;
        }
        const TextureDesc desc{transcoded.width, transcoded.height, *format, transcoded.mipLevels, colorSpace};
        if (!ValidateTextureUpload(desc, transcoded.pixels)) {
            return false;
        }
        outDesc = desc;
        outUploadData = std::move(transcoded.pixels);
        return true;
    };
    if (compressionInfo.hasAlpha) {
        for (const Asset::TextureTranscodeTarget target : alphaTargets) {
            if (tryTarget(target)) {
                return true;
            }
        }
    } else {
        for (const Asset::TextureTranscodeTarget target : opaqueTargets) {
            if (tryTarget(target)) {
                return true;
            }
        }
    }

    // Portable data is useful on every backend, even when that backend has no block-compressed
    // target. Keep all KTX2 mip levels and let the regular RGBA upload path provide the fallback.
    return tryTarget(Asset::TextureTranscodeTarget::Rgba8Unorm);
}

/// Finds the first active Directional light in `lights` for the shadow depth pre-pass (Increment
/// 26) — Point/Spot shadows are out of scope this increment (see docs/CODING_STANDARDS.md). A
/// simple linear scan, no sorting: the same first-N-encountered spirit as
/// ILightSystem::ExtractActiveLights() itself, not a distance- or importance-based
/// selection. Returns nullptr if no active (intensity > 0) Directional light exists this frame.
[[nodiscard]] const LightData* FindShadowCaster(const LightList& lights) noexcept {
    for (const LightData& light : lights) {
        if (light.type == Scene::LightType::Directional && light.intensity > 0.0F && light.castsShadows) {
            return &light;
        }
    }
    return nullptr;
}

[[nodiscard]] bool AreShadowMapTargetsValid(
    const std::array<TextureHandle, kShadowCascadeCount>& shadowMapTargets) noexcept {
    return std::all_of(shadowMapTargets.cbegin(), shadowMapTargets.cend(),
                       [](const TextureHandle target) { return target != kInvalidTextureHandle; });
}

void DestroyTextureIfValid(IRenderDevice& renderDevice, const TextureHandle texture) {
    if (texture != kInvalidTextureHandle) {
        renderDevice.DestroyTexture(texture);
    }
}

void DestroySamplerIfValid(IRenderDevice& renderDevice, const SamplerHandle sampler) {
    if (sampler != kInvalidSamplerHandle) {
        renderDevice.DestroySampler(sampler);
    }
}

void DestroyBufferIfValid(IRenderDevice& renderDevice, const BufferHandle buffer) {
    if (buffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(buffer);
    }
}

/// MeshVertex's binary layout (position, normal, UV, tangent, handedness — see
/// mesh_asset_uve.h), described once here for CreatePipeline(). MeshVertex is a
/// standard-layout aggregate of Math::Vector3 (itself standard-layout) and floats, so offsetof()
/// is well-defined.
const std::vector<VertexAttribute>& MeshVertexLayout() {
    static const std::vector<VertexAttribute> layout = {
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, offsetof(Asset::MeshVertex, position)},
        VertexAttribute{"NORMAL", VertexAttributeFormat::Float3, offsetof(Asset::MeshVertex, normal)},
        VertexAttribute{"TEXCOORD0", VertexAttributeFormat::Float2, offsetof(Asset::MeshVertex, u)},
        VertexAttribute{"TANGENT", VertexAttributeFormat::Float4, offsetof(Asset::MeshVertex, tangent)},
    };
    return layout;
}

/// The three depth-pass instrumentation slots a shadow pass writes to. Directional cascades and
/// atlas tiles report separately: "the cascade batcher merged 200 items into 12 draws" and "one
/// spot light drew 3 objects" are different facts, and averaging them into one number describes
/// neither.
struct ShadowPassCounters {
    std::size_t* batchesRecorded;
    std::size_t* batchedItems;
    std::size_t* instancedDrawCalls;
};

/// Frame-constant uniform data threaded into RecordItems() for every item this frame: the
/// view-projection matrix, the rendering camera's world position (Increment 24 — the view vector
/// a specular term needs), up to kMaxLights active lights (Increment 25 — Point/Spot +
/// multi-light; trailing unused slots hold the "no light" LightData{} sentinel from
/// ILightSystem::ExtractActiveLights()), and the global ambient term. Bundled into one
/// struct — mirroring PipelineDesc's/RenderPassDesc's own precedent for grouping related
/// descriptor data — rather than growing RecordItems's parameter list to six positional
/// parameters. Module-private: never crosses the RHI boundary, unlike PipelineDesc.
struct FrameUniforms {
    Math::Matrix4x4 viewProjection;
    Math::Vector3 viewPosition;
    LightList lights;
    Math::Vector3 ambientColor;
    Math::Vector3 skyAmbient;
    Math::Vector3 groundAmbient;
    Scene::WorldEnvironmentAmbientSource ambientSource = Scene::WorldEnvironmentAmbientSource::Sky;
    TextureHandle ambientEnvironmentTexture = kInvalidTextureHandle;
    bool ambientEnvironmentMapEnabled = false;

    /// Fixed three-cascade directional-shadow contract. A zero cascadeCount is the no-directional
    /// light sentinel; all maps remain cleared to 1.0 and material shaders naturally evaluate lit.
    ShadowCascadeMatrices lightSpaceMatrices{};
    ShadowCascadeSplits cascadeSplits{};
    std::int32_t cascadeCount = 0;
    float cascadeBlendRatio = 0.0F;
    float shadowDistanceFadeRange = 0.0F;
    float shadowBias = 0.0025F;
    float shadowNormalBias = 1.0F;
    float shadowOpacity = 1.0F;
    std::int32_t shadowPcfKernelRadius = 0;

    /// Atlas shadows for this frame, already indexed by `uLights[]` slot. A zero tile rect and a
    /// zero enable flag are the "this slot has no atlas shadow" defaults the shader tests first, so
    /// a light that stopped casting last frame cannot leave a stale tile bound. The cube rects are
    /// indexed [lightIndex * 6 + face] and stay all-zero for spot lights, which never read them.
    std::array<Math::Matrix4x4, kMaxShadowedLights> lightShadowMatrices{};
    std::array<Math::Vector4, kMaxShadowedLights> lightShadowTileRects{};
    std::array<std::int32_t, kMaxShadowedLights> lightShadowEnabled{};
    std::array<Math::Vector2, kMaxShadowedLights> lightShadowDepthRanges{};
    std::array<Math::Vector4, kMaxShadowTiles> lightShadowCubeRects{};
    /// Quality-tier ceiling on lights the shader loops over (Renderer3D::SetScalabilityMode).
    std::size_t activeLightCap = kMaxLights;
    bool clusteredLightingEnabled = false;
    float clusterNear = 0.1F;
    float clusterFar = 1000.0F;
    float viewportWidth = 1.0F;
    float viewportHeight = 1.0F;
    Math::Vector3 giIndirectIrradiance{0.0F, 0.0F, 0.0F};
    /// Per light: 0-based shadow slot + 1, or -1 when the light owns no slot (see lit_shadowed_3d.glsl).
    std::array<std::int32_t, kMaxLights> lightShadowSlots = [] {
        std::array<std::int32_t, kMaxLights> slots{};
        slots.fill(-1);
        return slots;
    }();
    /// True when at least one point light owns a tile. The 24 cube-rect uploads are skipped when it
    /// is false - a spot-only or directional-only frame should not pay 96 float uploads per draw for
    /// an array no light reads. Skipping is safe precisely because `uLightShadowEnabled[]` is still
    /// uploaded every frame and gates every read of those rects.
    bool pointShadowTilesActive = false;
    /// Valid only while at least one light owns a tile this frame. BindMaterialTextures binds
    /// the atlas only when this is valid; every consumer of the slot is gated by
    /// uLightShadowEnabled[] first, so an unbound unit is never sampled.
    TextureHandle shadowAtlasTexture = kInvalidTextureHandle;
    /// 1 / atlas resolution, so the PCF tap spacing has one source of truth instead of a literal in
    /// the shader that silently disagrees with the atlas the renderer actually allocated.
    Math::Vector2 shadowAtlasTexelSize{1.0F / static_cast<float>(kShadowAtlasResolution),
                                          1.0F / static_cast<float>(kShadowAtlasResolution)};

    /// Sky reflections, appended rather than placed beside the other ambient fields so the long
    /// positional aggregate initialiser at the build site keeps meaning what it says. Set right
    /// after that initialiser, exactly like the shadow arrays above.
    bool skyReflectionsLinked = false;
    Math::Vector3 skyReflectionSky{};
    Math::Vector3 skyReflectionHorizon{};
    Math::Vector3 skyReflectionGround{};
    bool isOrthographic = false;
    Math::Vector3 cameraForward{0.0F, 0.0F, -1.0F};
};

} // namespace

struct Renderer3D::Impl {
    IRenderDevice& renderDevice;
    IRenderSystem& renderSystem;
    IMeshRenderer& meshRenderer;
    ICameraSystem& cameraSystem;
    ILightSystem& lightSystem;
    Shader::IShaderManager& shaderManager;
    Asset::IAssetManager& assetManager;
    Asset::IAssetDatabase& assetDatabase;
    Events::IEventSystem& eventSystem;
    const RendererUniformNames& uniformNames;
    std::uint32_t targetWidth;
    std::uint32_t targetHeight;

    /// One complete set of size-dependent offscreen targets, kept so a renderer that alternates
    /// between a few sizes can switch between them instead of reallocating. Each set has six base
    /// targets; optional extra bloom scales are allocated lazily only for sizes that request them.
    ///
    /// This exists because of ViewportManager::RenderAllPanes(): it drives ONE shared
    /// renderer across every pane, resizing it to each pane's pixel size in turn. Without a cache,
    /// a split view of differently-sized panes destroyed and recreated all SIX base textures per
    /// pane per frame, forever - not a warm-up cost, a permanent one. That churn is what the
    /// ViewportManager header documents as "a per-pane cached target pool would avoid".
    ///
    /// Keyed by size rather than by pane, deliberately: the renderer has no pane concept and
    /// should not acquire one, and two panes that happen to share a size should share a set.
    struct BloomMipTargets final {
        TextureHandle bright = kInvalidTextureHandle;
        TextureHandle blurA = kInvalidTextureHandle;
        TextureHandle blurB = kInvalidTextureHandle;
    };

    struct SizedTargetSet final {
        TextureHandle colorTarget = kInvalidTextureHandle;
        TextureHandle depthTarget = kInvalidTextureHandle;
        std::array<BloomMipTargets, kMaximumBloomMipCount> bloomMipTargets{};
        std::uint32_t bloomMipTargetCount = 0U;
        TextureHandle ssaoTarget = kInvalidTextureHandle;
        TextureHandle ssaoBlurTarget = kInvalidTextureHandle;
        TextureHandle contactShadowTarget = kInvalidTextureHandle;
        TextureHandle ssrTarget = kInvalidTextureHandle;
        std::array<TextureHandle, 2> taaHistoryTargets{kInvalidTextureHandle, kInvalidTextureHandle};
        bool taaHistoryValid = false;
        std::uint32_t taaHistoryWriteIndex = 0U;
        TextureHandle wboitAccumTarget = kInvalidTextureHandle;
        TextureHandle wboitRevealTarget = kInvalidTextureHandle;
    };

    /// Bounded on purpose. A caller that resizes to a genuinely new size every frame - a window
    /// being dragged - must not accumulate texture sets without limit, so the cache is cleared
    /// once it exceeds this and rebuilt from the sizes actually in use. Small, because the case
    /// this serves is a handful of panes, not an arbitrary set of resolutions.
    static constexpr std::size_t kMaximumCachedTargetSets = 8U;

    /// Size -> its target set. The ACTIVE set's handles are also mirrored into the colorTarget/
    /// depthTarget/... members below, so every pass that reads them is untouched by this cache.
    std::map<std::pair<std::uint32_t, std::uint32_t>, SizedTargetSet> targetSetCache;

    /// Copied only through IRenderer3D::GetLastFrameDiagnostics(). Recorded counts are
    /// CPU-side renderer facts; the OpenGL-issued count never asserts completed presentation.
    Renderer3DFrameDiagnostics lastFrameDiagnostics;
    /// New meshes a frame may upload (0: no limit), and this frame's deferred ones (each counted once).
    std::size_t meshUploadBudget = 0U;
    std::vector<std::uint64_t> deferredMeshesThisFrame;

    /// Flat ambient term added to every rendered item every frame, regardless of whether an
    /// active light exists this frame (see EngineConfig::ambientColor, Increment 23).
    Math::Vector3 ambientColor;

    /// Shadow depth pre-pass tuning (see EngineConfig::shadowMapResolution/shadowMapHalfExtent/
    /// shadowMapNearPlane/shadowMapFarPlane, Increment 26).
    std::uint32_t shadowMapResolution;
    float shadowMapHalfExtent;
    float shadowMapNearPlane;
    float shadowMapFarPlane;
    float shadowFrustumPadding;
    float shadowCascadeSplitLambda;
    float shadowBiasDefault = 0.1F;
    float shadowNormalBiasDefault = 1.0F;

    /// Fraction of each non-final cascade range that cross-fades into the following cascade.
    /// The constructor keeps it bounded so canonical shader sampling has a predictable cost.
    float shadowCascadeBlendRatio;

    /// Bounded per-fragment PCF radius supplied to the canonical directional-shadow material
    /// shader. Zero keeps a hard comparison; the constructor clamps larger requested values to 2.
    std::int32_t shadowPcfKernelRadius;

    TextureHandle colorTarget;
    TextureHandle depthTarget;

    /// Persistent depth-only render target the shadow depth pre-pass renders into every frame
    /// (Increment 26) — unlike depthTarget above (the main pass's own depth buffer, written and
    /// never sampled), this is later bound as a sampled texture input during the main color pass.
    std::array<TextureHandle, kShadowCascadeCount> shadowMapTargets{};

    /// Tier 2.2 point sampler bound alongside every shadow cascade and depth texture read:
    /// manual PCF and depth reconstruction tap texel centers, so point sampling is required.
    SamplerHandle shadowPointSampler{};
    /// Trilinear sampler for material albedo/normal/AO/ORM/emissive maps (repeat + anisotropy).
    SamplerHandle materialRepeatSampler{};
    /// Linear clamp-to-edge sampler for decals, UI, bloom, and post-process color reads.
    SamplerHandle clampLinearSampler{};
    /// Equirectangular environment/sky sampler (repeat horizontally across longitude, clamp poles).
    SamplerHandle environmentEquirectSampler{};
    bool materialTextureRepeatEnabled = true;
    float materialMaxAnisotropy = 8.0F;

    void RebuildMaterialRepeatSampler() {
        DestroySamplerIfValid(renderDevice, materialRepeatSampler);
        materialRepeatSampler = kInvalidSamplerHandle;
        SamplerDesc desc;
        desc.magFilter = SamplerFilter::Linear;
        desc.minFilter = SamplerFilter::Linear;
        desc.mipMode = SamplerMipMode::Linear;
        const SamplerWrap wrap =
            materialTextureRepeatEnabled ? SamplerWrap::Repeat : SamplerWrap::ClampToEdge;
        desc.wrapU = wrap;
        desc.wrapV = wrap;
        desc.wrapW = wrap;
        desc.maxAnisotropy = std::clamp(materialMaxAnisotropy, 1.0F, 16.0F);
        materialRepeatSampler = renderDevice.CreateSampler(desc);
    }

    /// The built-in shadow-depth vertex+fragment program (engine/render/shader/built_in/
    /// shadow_depth.glsl), compiled once via shaderManager at construction — not tied to any
    /// MaterialAsset, matching EngineCore's demo-triangle precedent for a built-in
    /// (non-material) shader. May still be compiling (IsReady() == false) or have failed
    /// (IsValid() == false) on any given frame; RecordShadowPass() checks IsValid()
    /// before every use, exactly like RenderDemoTriangle() does.
    std::shared_ptr<Shader::ShaderProgram> shadowProgram;
    /// The UVE_INSTANCED build of the same shadow source. Null-checked at use; the non-instanced
    /// program is the fallback, so a link failure costs speed rather than shadows.
    std::shared_ptr<Shader::ShaderProgram> instancedShadowProgram;
    /// Batch scratch for the shadow cascades, one per cascade so consecutive cascades do not
    /// thrash a single set's capacity.
    std::array<RenderBatchSet, kShadowCascadeCount> shadowBatches;
    /// This frame's frustum-independent candidate set, reused across frames so a scene-sized
    /// vector is not reallocated every frame.
    MeshVisibilitySet visibilitySet;

    /// The unified spot/point shadow atlas. Held by std::optional and created LAZILY: a 4096x4096
    /// Depth32F attachment is 64 MiB, which is not a cost an empty scene - or a scene with only a
    /// directional light - should pay for a feature it never uses. Created on the first frame that
    /// has a spot-light candidate, and kept afterwards so the allocation is not per frame.
    std::optional<ShadowAtlasManager> shadowAtlas;
    /// Tile edge requested by EngineConfig::pointSpotShadowResolution (the project-settings
    /// "rendering.shadows.pointSpotCubeResolution" key). Sanitized on assignment.
    std::uint32_t shadowAtlasTileResolution = 1024U;
    /// One cull queue per atlas tile - up to 24, since a point light spends six. Same
    /// reuse-per-frame rationale as shadowQueues: the vectors keep their capacity across frames.
    std::array<RenderQueue, kMaxShadowTiles> shadowAtlasQueues;
    /// Per-tile batch scratch, matching shadowBatches.
    std::array<RenderBatchSet, kMaxShadowTiles> shadowAtlasBatches;
    /// Decides whether the atlas is worth creating this frame, before one exists to allocate from.
    std::size_t spotShadowCandidatesThisFrame = 0U;
    std::size_t pointShadowCandidatesThisFrame = 0U;
    std::shared_ptr<Shader::ShaderProgram> toneMappingProgram;
    std::shared_ptr<Shader::ShaderProgram> proceduralSkyProgram;

    /// Project-selected presentation clear color used when no world environment supplies a sky.
    std::array<float, 4U> sceneClearColor = kDefaultSceneClearColor;

    /// Phase 2b post-process toggles, consulted while building each frame's render graph (see
    /// RenderFrame()) - disabling either skips that group of passes entirely, not just their
    /// visual contribution.
    PostProcessSettings postProcessSettings{};

    /// Frustum-culling controls for the main colour view (see CullingSettings). Consulted every
    /// frame rather than baked into the render graph, so a host can freeze culling between frames.
    CullingSettings cullingSettings{};
    std::int32_t shadingMode = 0;
    std::int32_t scalabilityMode = 2;
    std::size_t activeMaxInstancesPerFrame = 131'072U;
    std::size_t activeMaxLightsPerFrame = kMaxLights;
    std::size_t activeMaxClusteredLights = kMaxClusteredLights;
    std::uint32_t taaFrameIndex = 0U;
    std::size_t activeMaxParticleGpuDrawCommands = kMaximumParticleGpuDrawCommands;
    bool humanEyeEnabled = false;
    float humanEyeCenterScale = 1.0F;
    float humanEyeTexelX = 0.0F;
    float humanEyeTexelY = 0.0F;
    Scene::WorldEnvironmentFrame environmentFrame{};
    /// Editor preview environment applied only when the scene resolves to no WorldEnvironment of
    /// its own, so authored lighting is never overridden - see SetPreviewEnvironment.
    std::optional<Scene::WorldEnvironmentFrame> previewEnvironment{};
    Asset::AssetGuid skyTextureGuid{};
    TextureHandle skyTextureHandle = kInvalidTextureHandle;
    bool skyTextureEnabled = false;
    Asset::AssetGuid lutTextureGuid{};
    TextureHandle lutTextureHandle = kInvalidTextureHandle;
    bool lutTextureEnabled = false;
    /// See IRenderer3D::GetLastSceneDepth().
    SceneDepthExport lastSceneDepth{};
    float environmentCameraNear = 0.1F;
    float environmentCameraFar = 250.0F;
    float environmentAspect = 1.0F;
    Math::Matrix4x4 previousMotionBlurViewProjection{};
    Scene::Entity previousMotionBlurCameraEntity = Scene::kInvalidEntity;
    float previousMotionBlurAspect = 0.0F;
    bool motionBlurHistoryValid = false;
    Math::Vector3 environmentCameraPosition{};
    float skyTanHalfFov = 0.57735026919F;
    Math::Vector3 skyCameraRight{1.0F, 0.0F, 0.0F};
    Math::Vector3 skyCameraUp{0.0F, 1.0F, 0.0F};
    Math::Vector3 skyCameraForward{0.0F, 0.0F, -1.0F};
    Math::Vector3 sunDirection{0.0F, 1.0F, 0.0F};
    Math::Vector3 sunColor{1.0F, 1.0F, 1.0F};
    float sunEnergy = 0.0F;
    /// Elapsed wall-clock seconds, advanced once per frame and clamped per step. The sky needs it
    /// to move its cloud deck; nothing in the renderer otherwise keeps a clock, and a fixed step
    /// per frame would make the wind speed depend on the frame rate.
    float skyTimeSeconds = 0.0F;
    std::chrono::steady_clock::time_point skyClockLast{};
    /// The disc the sky draws where `sunDirection` points. Owned by the active directional light
    /// (DirectionalLight3D or Preview Sun), so a scene with only an environment and no sun draws no disc.
    float sunDiscAngularDiameterDegrees = 0.0F;
    float sunDiscIntensity = 0.0F;
    float sunDiscLimbDarkening = 0.0F;
    float sunVolumetricFogEnergy = 0.0F;
    std::array<Scene::FogVolume3DFrame, Scene::kMaximumFogVolumesPerFrame> fogVolumes{};
    std::size_t fogVolumeCount = 0;

    struct ReflectionProbeGpuCache {
        // Tier 2.3: one cubemap — capture renders each face into its layer (face == layer).
        TextureHandle cube = kInvalidTextureHandle;
        std::uint32_t captureGeneration = 0;
        std::uint32_t captureResolution = 0U;
        bool ready = false;
    };
    std::unordered_map<Scene::Entity, ReflectionProbeGpuCache> reflectionProbeCaptures;
    // Keep one shared depth texture per tier alive for the renderer's lifetime. Captures submit
    // recorded command buffers before Present replays them, so destroying a depth texture while
    // moving between probe tiers in the same frame could invalidate an earlier capture's handle.
    std::unordered_map<std::uint32_t, TextureHandle> reflectionProbeCaptureDepths;
    bool probeCaptureViewActive = false;
    std::uint32_t probeCaptureVisibilityLayers = 0xFFFFFFFFU;
    Math::Vector3 probeCapturePosition{};
    Math::Quaternion probeCaptureRotation{};
    float probeCaptureNear = 0.05F;
    float probeCaptureFar = 50.0F;
    /// A capture's vertical lens (a probe face is 90 degrees; a 3D View's is its own).
    float probeCaptureFovY = Math::kPi * 0.5F;
    /// A 3D View capture: no sky, and alpha 0 wherever nothing was drawn.
    bool captureTransparentBackground = false;
    /// A 3D View is being captured (not a reflection probe).
    bool uiViewCaptureActive = false;
    /// Each 3D View's own picture (UIView3DComponent), by its widget.
    struct UIViewCapture final {
        TextureHandle color = kInvalidTextureHandle;
        TextureHandle depth = kInvalidTextureHandle;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        bool ready = false;
    };
    std::unordered_map<std::uint64_t, UIViewCapture> uiViewCaptures;
    void DestroyUIViewCapture(UIViewCapture& capture) {
        if (capture.color != kInvalidTextureHandle) {
            renderDevice.DestroyTexture(capture.color);
        }
        if (capture.depth != kInvalidTextureHandle) {
            renderDevice.DestroyTexture(capture.depth);
        }
        capture = UIViewCapture{};
    }
    int reflectionProbeEnabled = 0;
    Math::Vector3 reflectionProbePosition{};
    Math::Vector3 reflectionProbeAxisX{1.0F, 0.0F, 0.0F};
    Math::Vector3 reflectionProbeAxisY{0.0F, 1.0F, 0.0F};
    Math::Vector3 reflectionProbeAxisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3 reflectionProbeHalfExtents{};
    TextureHandle reflectionProbeCube = kInvalidTextureHandle;
    TextureHandle reflectionProbeSecondaryCube = kInvalidTextureHandle;
    float reflectionProbeBlendWeight = 0.0F;

    std::optional<StudioFloorSettings> studioFloorSettings{};
    std::shared_ptr<Shader::ShaderProgram> studioFloorProgram;
    BufferHandle studioFloorVertexBuffer = kInvalidBufferHandle;
    BufferHandle studioFloorIndexBuffer = kInvalidBufferHandle;
    TextureHandle studioFloorAlbedoTexture = kInvalidTextureHandle;
    TextureHandle studioFloorNormalTexture = kInvalidTextureHandle;
    TextureHandle studioFloorOrmTexture = kInvalidTextureHandle;

    void DestroyStudioFloorResources() {
        DestroyBufferIfValid(renderDevice, studioFloorVertexBuffer);
        DestroyBufferIfValid(renderDevice, studioFloorIndexBuffer);
        DestroyTextureIfValid(renderDevice, studioFloorAlbedoTexture);
        DestroyTextureIfValid(renderDevice, studioFloorNormalTexture);
        DestroyTextureIfValid(renderDevice, studioFloorOrmTexture);
        studioFloorVertexBuffer = kInvalidBufferHandle;
        studioFloorIndexBuffer = kInvalidBufferHandle;
        studioFloorAlbedoTexture = kInvalidTextureHandle;
        studioFloorNormalTexture = kInvalidTextureHandle;
        studioFloorOrmTexture = kInvalidTextureHandle;
        studioFloorProgram.reset();
    }

    [[nodiscard]] bool EnsureStudioFloorResources() {
        if (!studioFloorProgram) {
            Shader::ShaderProgramDesc floorDesc;
            floorDesc.virtualFilePath = std::string(Shader::BuiltIn::kStudioFloorPbrVirtualPath);
            floorDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kStudioFloorPbrSource);
            floorDesc.vertexLayout = MeshVertexLayout();
            floorDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
            floorDesc.depthTestEnabled = true;
            floorDesc.depthWriteEnabled = true;
            floorDesc.depthCompare = DepthCompare::LessOrEqual;
            floorDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
            floorDesc.cullMode = CullMode::None;
            floorDesc.debugName = "StudioFloorPBR";
            studioFloorProgram = shaderManager.CreateProgram(floorDesc);
        }
        if (studioFloorVertexBuffer == kInvalidBufferHandle ||
            studioFloorIndexBuffer == kInvalidBufferHandle) {
            constexpr float kExtent = 48.0F;
            const std::array<Asset::MeshVertex, 4U> vertices{{
                Asset::MeshVertex{{-kExtent, 0.0F, -kExtent}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F, {1.0F, 0.0F, 0.0F}, 1.0F},
                Asset::MeshVertex{{-kExtent, 0.0F,  kExtent}, {0.0F, 1.0F, 0.0F}, 0.0F, 1.0F, {1.0F, 0.0F, 0.0F}, 1.0F},
                Asset::MeshVertex{{ kExtent, 0.0F,  kExtent}, {0.0F, 1.0F, 0.0F}, 1.0F, 1.0F, {1.0F, 0.0F, 0.0F}, 1.0F},
                Asset::MeshVertex{{ kExtent, 0.0F, -kExtent}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F, {1.0F, 0.0F, 0.0F}, 1.0F},
            }};
            const std::array<std::uint32_t, 6U> indices{{0U, 1U, 2U, 0U, 2U, 3U}};
            DestroyBufferIfValid(renderDevice, studioFloorVertexBuffer);
            DestroyBufferIfValid(renderDevice, studioFloorIndexBuffer);
            studioFloorVertexBuffer = renderDevice.CreateBuffer(
                BufferDesc{sizeof(vertices), BufferUsage::Vertex},
                std::as_bytes(std::span<const Asset::MeshVertex>{vertices}));
            studioFloorIndexBuffer = renderDevice.CreateBuffer(
                BufferDesc{sizeof(indices), BufferUsage::Index},
                std::as_bytes(std::span<const std::uint32_t>{indices}));
        }
        if (studioFloorAlbedoTexture == kInvalidTextureHandle ||
            studioFloorNormalTexture == kInvalidTextureHandle ||
            studioFloorOrmTexture == kInvalidTextureHandle) {
            constexpr std::uint32_t kTexSize = 256U;
            constexpr std::size_t kTexPixels = static_cast<std::size_t>(kTexSize) * kTexSize;
            std::vector<float> heightMap(kTexPixels, 0.0F);
            std::vector<std::uint8_t> albedoBytes(kTexPixels * 4U, 0U);
            std::vector<std::uint8_t> normalBytes(kTexPixels * 4U, 0U);
            std::vector<std::uint8_t> ormBytes(kTexPixels * 4U, 0U);

            const auto hashGrid = [](const int x, const int y, const int period, const std::uint32_t seed) noexcept -> float {
                const int wx = ((x % period) + period) % period;
                const int wy = ((y % period) + period) % period;
                std::uint32_t h = static_cast<std::uint32_t>(wx) * 374761393U +
                                  static_cast<std::uint32_t>(wy) * 668265263U + seed * 2246822519U;
                h = (h ^ (h >> 13U)) * 1274126177U;
                h ^= (h >> 16U);
                return static_cast<float>(h & 0xFFFFU) / 65535.0F;
            };
            const auto sampleTileNoise = [&](const float u, const float v, const int period, const std::uint32_t seed) noexcept -> float {
                const float gx = u * static_cast<float>(period);
                const float gy = v * static_cast<float>(period);
                const int x0 = static_cast<int>(std::floor(gx));
                const int y0 = static_cast<int>(std::floor(gy));
                const float fx = gx - static_cast<float>(x0);
                const float fy = gy - static_cast<float>(y0);
                const float sx = fx * fx * (3.0F - 2.0F * fx);
                const float sy = fy * fy * (3.0F - 2.0F * fy);
                const float n00 = hashGrid(x0, y0, period, seed);
                const float n10 = hashGrid(x0 + 1, y0, period, seed);
                const float n01 = hashGrid(x0, y0 + 1, period, seed);
                const float n11 = hashGrid(x0 + 1, y0 + 1, period, seed);
                return (n00 * (1.0F - sx) + n10 * sx) * (1.0F - sy) +
                       (n01 * (1.0F - sx) + n11 * sx) * sy;
            };

            for (std::uint32_t y = 0U; y < kTexSize; ++y) {
                const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(kTexSize);
                for (std::uint32_t x = 0U; x < kTexSize; ++x) {
                    const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(kTexSize);
                    const float nMacro = sampleTileNoise(u, v, 8, 11U);
                    const float nMid = sampleTileNoise(u, v, 24, 37U);
                    const float nFine = sampleTileNoise(u, v, 64, 73U);
                    const float nMicro = sampleTileNoise(u, v, 128, 131U);
                    // Subtle brushed concrete/basalt micro-grain along X.
                    const float brushed = sampleTileNoise(u * 0.25F, v, 96, 197U);
                    const float fbm = 0.36F * nMacro + 0.28F * nMid + 0.20F * nFine + 0.16F * nMicro;

                    // Subtle recessed 2x2 architectural slab seam (no bright grid lines).
                    const float tileU = std::fmod(u * 2.0F, 1.0F);
                    const float tileV = std::fmod(v * 2.0F, 1.0F);
                    const float edgeDist = std::min({tileU, 1.0F - tileU, tileV, 1.0F - tileV});
                    const float seamMask = std::clamp(edgeDist / 0.014F, 0.0F, 1.0F);
                    const float seamSmooth = seamMask * seamMask * (3.0F - 2.0F * seamMask);

                    const float height = (fbm * 0.78F + brushed * 0.22F) * (0.65F + 0.35F * seamSmooth);
                    const std::size_t idx = static_cast<std::size_t>(y) * kTexSize + x;
                    heightMap[idx] = height;

                    // Dark studio slate-basalt albedo (sRGB ~29..41 out of 255) with subtle mineral variation.
                    const float baseTone = (0.118F + 0.038F * (fbm - 0.5F) + 0.012F * (brushed - 0.5F)) *
                                           (0.86F + 0.14F * seamSmooth);
                    const auto toByte = [](const float c) noexcept -> std::uint8_t {
                        return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0F, 1.0F) * 255.0F));
                    };
                    albedoBytes[idx * 4U + 0U] = toByte(baseTone * 0.96F);
                    albedoBytes[idx * 4U + 1U] = toByte(baseTone * 0.99F);
                    albedoBytes[idx * 4U + 2U] = toByte(baseTone * 1.06F);
                    albedoBytes[idx * 4U + 3U] = 255U;

                    const float ao = std::clamp((0.88F + 0.12F * nFine) * (0.82F + 0.18F * seamSmooth), 0.0F, 1.0F);
                    const float roughness = std::clamp(0.74F - 0.10F * (fbm - 0.5F) + 0.06F * (1.0F - seamSmooth), 0.45F, 0.92F);
                    ormBytes[idx * 4U + 0U] = toByte(ao);
                    ormBytes[idx * 4U + 1U] = toByte(roughness);
                    ormBytes[idx * 4U + 2U] = 0U;
                    ormBytes[idx * 4U + 3U] = 255U;
                }
            }

            for (std::uint32_t y = 0U; y < kTexSize; ++y) {
                const std::uint32_t yPrev = (y + kTexSize - 1U) % kTexSize;
                const std::uint32_t yNext = (y + 1U) % kTexSize;
                for (std::uint32_t x = 0U; x < kTexSize; ++x) {
                    const std::uint32_t xPrev = (x + kTexSize - 1U) % kTexSize;
                    const std::uint32_t xNext = (x + 1U) % kTexSize;
                    const float hL = heightMap[static_cast<std::size_t>(y) * kTexSize + xPrev];
                    const float hR = heightMap[static_cast<std::size_t>(y) * kTexSize + xNext];
                    const float hD = heightMap[static_cast<std::size_t>(yPrev) * kTexSize + x];
                    const float hU = heightMap[static_cast<std::size_t>(yNext) * kTexSize + x];
                    constexpr float kBumpStrength = 1.65F;
                    const Math::Vector3 n = Math::Normalize(
                        Math::Vector3{(hL - hR) * kBumpStrength, (hD - hU) * kBumpStrength, 1.0F});
                    const auto toByte = [](const float c) noexcept -> std::uint8_t {
                        return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0F, 1.0F) * 255.0F));
                    };
                    const std::size_t idx = static_cast<std::size_t>(y) * kTexSize + x;
                    normalBytes[idx * 4U + 0U] = toByte(n.x * 0.5F + 0.5F);
                    normalBytes[idx * 4U + 1U] = toByte(n.y * 0.5F + 0.5F);
                    normalBytes[idx * 4U + 2U] = toByte(n.z * 0.5F + 0.5F);
                    normalBytes[idx * 4U + 3U] = 255U;
                }
            }

            DestroyTextureIfValid(renderDevice, studioFloorAlbedoTexture);
            DestroyTextureIfValid(renderDevice, studioFloorNormalTexture);
            DestroyTextureIfValid(renderDevice, studioFloorOrmTexture);
            studioFloorAlbedoTexture = renderDevice.CreateTexture(
                TextureDesc{kTexSize, kTexSize, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Srgb},
                std::as_bytes(std::span<const std::uint8_t>{albedoBytes}));
            studioFloorNormalTexture = renderDevice.CreateTexture(
                TextureDesc{kTexSize, kTexSize, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear},
                std::as_bytes(std::span<const std::uint8_t>{normalBytes}));
            studioFloorOrmTexture = renderDevice.CreateTexture(
                TextureDesc{kTexSize, kTexSize, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear},
                std::as_bytes(std::span<const std::uint8_t>{ormBytes}));
        }
        return studioFloorProgram != nullptr && studioFloorProgram->IsValid() &&
               studioFloorVertexBuffer != kInvalidBufferHandle &&
               studioFloorIndexBuffer != kInvalidBufferHandle &&
               studioFloorAlbedoTexture != kInvalidTextureHandle &&
               studioFloorNormalTexture != kInvalidTextureHandle &&
               studioFloorOrmTexture != kInvalidTextureHandle;
    }

    void DestroyReflectionProbeGpuCache(ReflectionProbeGpuCache& cache) {
        DestroyTextureIfValid(renderDevice, cache.cube);
        cache.cube = kInvalidTextureHandle;
        cache.captureGeneration = 0U;
        cache.captureResolution = 0U;
        cache.ready = false;
    }

    [[nodiscard]] bool EnsureReflectionProbeCube(ReflectionProbeGpuCache& cache,
                                                    const std::uint32_t resolution) {
        if (cache.captureResolution != resolution) {
            DestroyReflectionProbeGpuCache(cache);
            cache.captureResolution = resolution;
        }
        if (cache.cube == kInvalidTextureHandle) {
            const TextureFormat sceneColorFormat =
                SceneColorTargetFormat(renderDevice, rendererProfileResolution.effective);
            TextureDesc cubeDesc{resolution, resolution, sceneColorFormat, 1};
            cubeDesc.type = TextureType::Cubemap;
            cubeDesc.arrayLayers =
                static_cast<std::uint32_t>(Scene::kReflectionProbeCubemapFaceCount);
            cache.cube = renderDevice.CreateTexture(cubeDesc);
            if (cache.cube == kInvalidTextureHandle && sceneColorFormat != TextureFormat::RGBA8Unorm) {
                cubeDesc.format = TextureFormat::RGBA8Unorm;
                cache.cube = renderDevice.CreateTexture(cubeDesc);
            }
        }
        if (cache.cube == kInvalidTextureHandle) {
            DestroyReflectionProbeGpuCache(cache);
            return false;
        }
        return true;
    }

    [[nodiscard]] TextureHandle EnsureReflectionProbeCaptureDepth(const std::uint32_t resolution) {
        const auto existing = reflectionProbeCaptureDepths.find(resolution);
        if (existing != reflectionProbeCaptureDepths.end()) {
            return existing->second;
        }
        const TextureHandle depth = renderDevice.CreateTexture(
            TextureDesc{resolution, resolution, TextureFormat::Depth32Float, 1});
        if (depth != kInvalidTextureHandle) {
            reflectionProbeCaptureDepths.emplace(resolution, depth);
        }
        return depth;
    }

    void ClearBoundReflectionProbe() {
        reflectionProbeEnabled = 0;
        reflectionProbePosition = {};
        reflectionProbeAxisX = Math::Vector3{1.0F, 0.0F, 0.0F};
        reflectionProbeAxisY = Math::Vector3{0.0F, 1.0F, 0.0F};
        reflectionProbeAxisZ = Math::Vector3{0.0F, 0.0F, 1.0F};
        reflectionProbeHalfExtents = {};
        reflectionProbeCube = kInvalidTextureHandle;
        reflectionProbeSecondaryCube = kInvalidTextureHandle;
        reflectionProbeBlendWeight = 0.0F;
    }

    void EvictDeadReflectionProbeCaptures(Scene::IEntityManager& entityManager) {
        for (auto it = reflectionProbeCaptures.begin(); it != reflectionProbeCaptures.end();) {
            if (!entityManager.IsAlive(it->first) ||
                !entityManager.HasComponent<Scene::ReflectionProbe3DComponent>(it->first)) {
                DestroyReflectionProbeGpuCache(it->second);
                it = reflectionProbeCaptures.erase(it);
            } else {
                ++it;
            }
        }
    }

    void BindReflectionProbeForView(Scene::IEntityManager& entityManager, const Math::Vector3& viewPosition) {
        ClearBoundReflectionProbe();
        std::array<Scene::ReflectionProbe3DFrame, Scene::kMaximumReflectionProbesPerFrame> frames{};
        const std::size_t count = Scene::CollectReflectionProbe3DFrames(entityManager, viewPosition, frames);
        float primaryWeight = 0.0F;
        for (std::size_t i = 0; i < count; ++i) {
            const Scene::ReflectionProbe3DFrame& frame = frames[i];
            if (!frame.capturedOnce || frame.entity == Scene::kInvalidEntity) {
                continue;
            }
            const auto cacheIt = reflectionProbeCaptures.find(frame.entity);
            if (cacheIt == reflectionProbeCaptures.end() || !cacheIt->second.ready ||
                cacheIt->second.captureResolution != frame.captureResolution) {
                continue;
            }
            if (reflectionProbeEnabled == 0) {
                reflectionProbeEnabled = 1;
                reflectionProbePosition = frame.worldPosition;
                reflectionProbeAxisX = frame.axisX;
                reflectionProbeAxisY = frame.axisY;
                reflectionProbeAxisZ = frame.axisZ;
                reflectionProbeHalfExtents = frame.halfExtents;
                reflectionProbeCube = cacheIt->second.cube;
                primaryWeight = std::max(frame.influenceWeight, 1.0e-4F);
            } else {
                reflectionProbeSecondaryCube = cacheIt->second.cube;
                const float secondaryWeight = std::max(frame.influenceWeight, 0.0F);
                reflectionProbeBlendWeight =
                    std::clamp(secondaryWeight / (primaryWeight + secondaryWeight), 0.0F, 0.5F);
                break;
            }
        }
    }

    /// Bloom targets start at half the main color resolution and form a lazily allocated chain of
    /// progressively smaller scales. Each level owns a bright/downsample image and two HDR blur
    /// ping-pong images; the active array mirrors the current entry in targetSetCache.
    std::array<BloomMipTargets, kMaximumBloomMipCount> bloomMipTargets{};
    std::uint32_t bloomMipTargetCount = 0U;
    std::shared_ptr<Shader::ShaderProgram> bloomBrightPassProgram;
    std::shared_ptr<Shader::ShaderProgram> bloomDownsampleProgram;
    std::shared_ptr<Shader::ShaderProgram> bloomBlurProgram;

    /// Half-resolution R8Unorm SSAO target and ping-pong bilateral blur target.
    TextureHandle ssaoTarget;
    TextureHandle ssaoBlurTarget;
    std::shared_ptr<Shader::ShaderProgram> ssaoProgram;
    /// Opaque fullscreen_copy.glsl used for the two-pass separable depth-aware bilateral SSAO blur.
    std::shared_ptr<Shader::ShaderProgram> ssaoBlurProgram;

    /// Full-resolution R8Unorm screen-space contact-shadow visibility term.
    TextureHandle contactShadowTarget;
    std::shared_ptr<Shader::ShaderProgram> contactShadowProgram;

    /// Half-resolution HDR screen-space reflections term (composited bilaterally in ToneMapping).
    TextureHandle ssrTarget;
    std::shared_ptr<Shader::ShaderProgram> ssrProgram;
    std::shared_ptr<Shader::ShaderProgram> taaResolveProgram;
    std::shared_ptr<Shader::ShaderProgram> wboitLitProgram;
    std::shared_ptr<Shader::ShaderProgram> wboitCompositeProgram;
    BufferHandle clusteredLightBuffer;
    BufferHandle clusteredCellBuffer;
    BufferHandle clusteredIndexBuffer;
    LightClusterGridData clusteredLightGrid;

    /// Minimal built-in particle program and reusable CPU-expanded vertex buffer. ShaderManager
    /// owns the linked pipeline lifetime; Renderer3D owns only the buffer and releases it in its
    /// destructor. The fixed capacity bounds both per-frame upload bytes and draw vertices.
    std::shared_ptr<Shader::ShaderProgram> particleProgram;
    BufferHandle particleVertexBuffer;
    std::vector<ParticleVertex> particleVertexStaging;

    /// GPU instancing state, rebuilt every frame. Two matrix buffers rather than one because the
    /// shader needs both the model matrix and its inverse-transpose, and computing the latter in
    /// the vertex shader would mean inverting a matrix once per VERTEX instead of once per object.
    /// The base-index buffer is a one-int SSBO for the same reason every compute kernel's params
    /// are: it is the portable way to get a scalar to a shader.
    BufferHandle instanceTransformBuffer;
    BufferHandle instanceNormalTransformBuffer;
    BufferHandle instanceBaseBuffer;
    std::size_t instanceBufferCapacity = 0U;
    RenderBatchSet opaqueBatches;
    RenderBatchSet transparentBatches;
    std::vector<Math::Matrix4x4> instanceNormalMatrixStaging;
    std::vector<Math::Matrix4x4> instanceTransposeStaging;
    /// Reset at the top of every frame and copied into the frame diagnostics at the end. These
    /// name what instancing actually did this frame rather than what it was asked to do: a scene
    /// of legacy materials reports zero, which is the honest answer.
    std::size_t instancedDrawCallsThisFrame = 0U;
    std::size_t instancedObjectsThisFrame = 0U;
    /// Shadow-pass instanced draws across every depth-only shadow pass this frame (cascades and
    /// atlas tiles alike). Counted separately from the
    /// main pass because the shadow passes are where the draw-call count was actually worst - one
    /// per caster per cascade - and a single combined number would hide which half improved.
    std::size_t shadowInstancedDrawCallsThisFrame = 0U;
    std::size_t indirectInstancedDrawCallsThisFrame = 0U;

    /// CS8 Compute indirect instancing systems.
    ComputeSystem computeSystem;
    FrustumCullIndirect indirectCull;
    std::vector<std::unique_ptr<FrustumCullIndirect>> indirectCullPool;
    std::size_t nextIndirectCullIndex = 0U;
    std::vector<Math::Aabb> instanceBoundsScratch;

    [[nodiscard]] FrustumCullIndirect* AcquireIndirectCull() {
        if (nextIndirectCullIndex == 0U) {
            ++nextIndirectCullIndex;
            if (!indirectCull.IsReady()) {
                static_cast<void>(indirectCull.Initialize());
            }
            return indirectCull.IsReady() ? &indirectCull : nullptr;
        }
        const std::size_t poolSlot = nextIndirectCullIndex - 1U;
        ++nextIndirectCullIndex;
        if (poolSlot < indirectCullPool.size()) {
            FrustumCullIndirect* const pooled = indirectCullPool[poolSlot].get();
            if (pooled != nullptr && !pooled->IsReady()) {
                static_cast<void>(pooled->Initialize());
            }
            return (pooled != nullptr && pooled->IsReady()) ? pooled : nullptr;
        }
        auto newCull = std::make_unique<FrustumCullIndirect>(renderDevice, computeSystem);
        if (!newCull->Initialize()) {
            return nullptr;
        }
        FrustumCullIndirect* const rawPtr = newCull.get();
        indirectCullPool.push_back(std::move(newCull));
        return rawPtr;
    }

    /// The built-in lit program plus white/normal fallback textures that stand in for a material
    /// nobody assigned. One instance serves every primitive with no material AND every material
    /// asset whose shader GUIDs are unset (the Standard preset), so those two cases cannot shade
    /// differently. Its `.program` is null only before program creation has run.
    MaterialGpuResources defaultLitMaterialResources;
    /// The stage description the shared built-in lit program is built from, kept so a material that
    /// asks for a different blend/cull/depth state can have the SAME source linked with that state.
    Shader::ShaderProgramStagesDesc defaultLitStagesDesc;
    /// Built-in lit programs by (effectiveProfile << 16) | MaterialPipelineState::Pack().
    std::unordered_map<std::uint32_t, std::shared_ptr<Shader::ShaderProgram>> standardLitPrograms;
    std::array<std::shared_ptr<Shader::ShaderProgram>, 3U> defaultLitProgramsByProfile{};
    std::array<std::shared_ptr<Shader::ShaderProgram>, 3U> defaultLitBlendedProgramsByProfile{};
    RendererProfileResolution rendererProfileResolution{};
    /// Programs that already received this render pass's frame-wide lighting & shadow uniforms.
    std::vector<const Shader::ShaderProgram*> frameUniformUploadedPrograms;
    void BeginFrameUniformScope() noexcept { frameUniformUploadedPrograms.clear(); }
    /// Material handles the primitive path is drawing this frame, keyed by GUID and pruned to the
    /// GUIDs extraction named. Mirrors `unmaterialedMeshHandles`: the handle owns the reference
    /// that keeps the payload loaded between extraction and the colour pass, which a
    /// PrimitiveRenderItem cannot do without an asset manager to construct it.
    std::unordered_map<Asset::AssetGuid, Asset::AssetHandle<Asset::MaterialAsset>>
        primitiveMaterialHandles;
    std::unordered_map<Asset::AssetGuid, bool> namedPrimitiveMaterials;
    std::size_t pendingPrimitiveMaterials = 0U;
    std::size_t failedPrimitiveMaterials = 0U;

    /// Foundation WP3: the GPU occlusion stage's proxy program (built_in/occlusion_proxy.glsl).
    /// Depth-tested LessOrEqual against everything the main pass has written, no depth write, and
    /// an additive all-zero colour, so the attachments are untouched and only the query bracket
    /// around each proxy draw observes it.
    std::shared_ptr<Shader::ShaderProgram> occlusionProxyProgram;
    /// One probe per opaque mesh item, keyed by mesh GUID + world matrix bits: a moved object is a
    /// new probe, so no stale verdict can hide it. Two query slots alternate between frames - the
    /// frame that records slot N reads slot N^1 - so a bracket is never re-recorded (which resets
    /// it in queue order) before its result was consumed.
    struct OcclusionProbe final {
        std::array<QueryHandle, 2U> queries{};
        std::array<bool, 2U> issued{{false, false}};
        std::array<Math::Matrix4x4, 2U> viewProjections{};
        std::uint64_t lastSeenFrame = 0U;
    };
    struct OcclusionProxyDraw final {
        std::uint64_t key = 0U;
        Math::Matrix4x4 worldMatrix;
        std::uint32_t slot = 0U;
    };
    std::unordered_map<std::uint64_t, OcclusionProbe> occlusionProbes;
    std::vector<OcclusionProxyDraw> occlusionProxyDraws;
    // Reuse filter capacity across frames. Clear elements after swapping so culled assets
    // are released immediately; retaining capacity trades peak storage for fewer allocations.
    std::vector<RenderItem> occlusionKeptItems;
    std::uint64_t occlusionFrame = 0U;
    bool occlusionQueryCreationFailed = false;

    /// A 1x1 opaque-white texture, used whenever a material leaves albedoTexture/aoTexture unset
    /// (kInvalidAssetGuid) — sampling it always yields {1,1,1,1}, so
    /// `texture(uAlbedoTexture, uv) * uAlbedoColor == uAlbedoColor` and
    /// `texture(uAOTexture, uv).r == 1.0` (no occlusion), with no shader-side branching needed.
    TextureHandle fallbackWhiteTexture;
    /// Tier 2.3: a 1x1-white CUBEMAP fallback — bound wherever a samplerCube uniform (the
    /// reflection probe) has no captured cube yet. Never destroyed explicitly, exactly like its
    /// 2D siblings above (device teardown reaps renderer-owned textures).
    TextureHandle fallbackWhiteCubeTexture;

    /// A 1x1 flat tangent-space "up" normal texture ({0.5,0.5,1.0} encoded as {128,128,255}),
    /// used whenever a material leaves normalTexture unset. Bound/sampled for
    /// forward-compatibility with a future lighting increment; unused in this increment's unlit
    /// color output.
    TextureHandle fallbackNormalTexture;

    std::unordered_map<Asset::AssetGuid, MeshGpuResources> meshCache;
    /// Textures still loading, held so their loads are not dropped (ResolveTextureGpuHandle).
    std::unordered_map<Asset::AssetGuid, Asset::AssetHandle<Asset::TextureAsset>> pendingTextureHandles;
    /// Per-entity vertex buffers of skinned meshes, re-skinned every frame. The index buffer is the
    /// mesh's shared one. Entries an extraction did not touch are released at the next one.
    struct SkinnedMeshGpu final {
        MeshGpuResources resources;
        std::size_t vertexBytes = 0U;
        std::uint64_t frame = 0U;
        Asset::AssetGuid meshGuid = Asset::kInvalidAssetGuid;
        std::vector<Math::Matrix4x4> lastLocals;
        Math::Aabb localBounds;
        std::vector<std::uint32_t> jointToBoneIndex;
        std::size_t cachedSkeletonBoneCount = 0U;
        std::uint64_t cachedSkeletonNameHash = 0U;
    };
    std::unordered_map<std::uint64_t, SkinnedMeshGpu> skinnedMeshCache;
    std::vector<Math::Matrix4x4> skinLocalsScratch;
    std::vector<Math::Matrix4x4> skinMatricesScratch;
    std::vector<Asset::MeshVertex> skinVerticesScratch;
    std::vector<Math::Matrix4x4> skinModelSpaceScratch;
    std::uint64_t skinnedFrame = 0U;
    std::size_t skinnedMeshesThisFrame = 0U;
    std::unordered_map<std::uint8_t, MeshGpuResources> primitiveMeshCache;
    /// Keeps each imported mesh that is drawn without a material loaded between frames. Pruned
    /// when no entity names the mesh any more.
    std::unordered_map<Asset::AssetGuid, Asset::AssetHandle<Asset::MeshAsset>> unmaterialedMeshHandles;
    std::unordered_map<Asset::AssetGuid, MaterialGpuResources> materialCache;

    /// GPU textures uploaded from a loaded TextureAsset, cached by that texture's own
    /// AssetGuid — independent of materialCache, so two materials sharing an albedo texture
    /// GUID upload it only once.
    std::unordered_map<Asset::AssetGuid, TextureHandle> textureCache;
    /// Permanent texture-resolution failures (asset load or GPU upload) remain on the fallback
    /// path until an explicit texture asset reload event. This prevents repeated failed-handle
    /// diagnostics and repeated invalid GPU uploads after a material cache rebuild.
    std::unordered_set<Asset::AssetGuid> failedTextureGuids;
    RenderGraph renderGraph;
    RenderQueue frameQueue;

    /// The decal pass and what it published this frame. Held rather than rebuilt per call for the
    /// same reason the queue is: the vectors keep their capacity, and the pass clears them at the
    /// top of every build rather than making the allocator do it.
    DecalRenderer decalRenderer;
    DecalDrawList decalDraws;
    /// This frame's decal geometry and the commands that draw it, built from decalDraws in the same
    /// frame step and consumed by the MainColor pass.
    DecalDrawPlan decalPlan;
    /// The program Decal3D's patches are painted with - unlit paint blended over the shaded surface,
    /// so a decal needs no material shader to be visible.
    std::shared_ptr<Shader::ShaderProgram> decalProgram;
    /// Per-decal vertex and index buffers, re-uploaded every frame because the patches are rebuilt
    /// every frame - a receiver can move, and so can the decal. Mirrors skinnedMeshCache below it:
    /// keyed by entity, sized to the geometry it currently holds, and entries no plan touched are
    /// released at the next one.
    struct DecalGpu final {
        BufferHandle vertexBuffer = kInvalidBufferHandle;
        BufferHandle indexBuffer = kInvalidBufferHandle;
        std::size_t vertexBytes = 0U;
        std::size_t indexBytes = 0U;
        std::uint64_t frame = 0U;
    };
    std::unordered_map<std::uint64_t, DecalGpu> decalBuffers;
    std::uint64_t decalFrame = 0U;
    struct ShadowPrimitiveDraw final {
        Math::Matrix4x4 worldMatrix = Math::Matrix4x4::Identity();
        Scene::PrimitiveMeshKind kind = Scene::PrimitiveMeshKind::Cube;
        Asset::AssetGuid meshGuid = Asset::kInvalidAssetGuid;
        const Asset::MeshAsset* mesh = nullptr;
        const MeshGpuResources* skinned = nullptr;
    };
    std::array<RenderQueue, kShadowCascadeCount> shadowQueues;
    std::array<std::vector<ShadowPrimitiveDraw>, kShadowCascadeCount> shadowPrimitiveQueues;
    std::array<std::vector<ShadowPrimitiveDraw>, kMaxShadowTiles> shadowAtlasPrimitiveQueues;
    std::vector<PrimitiveRenderItem> primitiveItems;
    struct ActiveSkeletonEntry final {
        Scene::Entity entity = Scene::kInvalidEntity;
        Scene::Entity parent = Scene::kInvalidEntity;
        const Scene::Skeleton3DComponent* component = nullptr;
    };
    std::vector<ActiveSkeletonEntry> frameSkeletons;

    /// Last frame's primitive placement per entity, reused when nothing about that entity changed.
    ///
    /// WHY THIS EXISTS. The mesh path already caches placement; the primitive path was left
    /// recomputing the same TRS compose and bounds transform every frame for objects that had not
    /// moved. Measured on this engine's own maths: recomputing a primitive placement costs about
    /// 123 us per 1000, comparing the key and reusing the answer about 1.8 us - roughly 67x.
    /// Primitives are overwhelmingly static scene dressing, so nearly all of that work was
    /// recomputing the previous frame's answer.
    ///
    /// Keyed by Entity, which is generational, so a destroyed entity whose index is reused gets
    /// a different key and cannot inherit the dead entity's bounds. Bounded by the same
    /// seen-this-frame prune the mesh cache uses - without it a long session retains an entry for
    /// every primitive the scene ever had.
    std::unordered_map<Scene::Entity, PrimitivePlacementCacheEntry> primitivePlacementCache;

    /// Monotonic stamp for the prune above. Starts at 0 and is pre-incremented, so a live entry
    /// always has a non-zero lastSeenFrame and "never populated" stays distinguishable.
    std::uint64_t primitiveFrameIndex = 0U;
    ParticleDrawRecording particleDrawRecording;
    Events::EventSubscription reloadSubscription;
    /// Reloads announced by AssetReloadedEvent whose AssetLoadCompletedEvent has not arrived
    /// yet. See OnAssetReloaded for why the announcement alone cannot invalidate correctly.
    Events::EventSubscription loadCompletedSubscription;
    std::unordered_set<Asset::AssetGuid> reloadsInFlight;
    const Scene::ParticleRuntime* particleRuntimeForFrame = nullptr;

    /// Set only for the duration of a RenderFrameToRegion() call (Phase 3's ViewportManager
    /// split-view support), mirroring particleRuntimeForFrame's own scoped-field pattern just
    /// above. The ToneMapping pass reads this when building its RenderPassDesc, so a full-frame
    /// RenderFrame() call (this field left nullopt) is unaffected.
    std::optional<ViewportRect> destinationViewportOverride;

    /// Set only for the duration of a RenderFrameToTarget() call, mirroring
    /// destinationViewportOverride's own scoped-field pattern directly above. The ToneMapping pass
    /// reads this when building its RenderPassDesc's colorAttachment/depthAttachment, so a
    /// full-frame RenderFrame() call (this field left nullopt) still targets the presentation
    /// surface exactly as before.
    std::optional<std::pair<TextureHandle, TextureHandle>> destinationTextureOverride;
    /// Tier 2.3: layers of the override pair the current RenderFrameToTarget() call renders
    /// (probe capture aims faces at cube layers). Read only when destinationTextureOverride is
    /// set; plain zeros (not optionals) so the pass builders below stay branch-symmetric.
    std::uint32_t destinationColorLayerOverride = 0U;
    std::uint32_t destinationDepthLayerOverride = 0U;

    /// Set only alongside destinationTextureOverride, from RenderFrameToTarget()'s own
    /// width/height parameters - TextureHandle has no queryable size on IRenderDevice, so the
    /// UIOverlay pass's orthographic projection needs this supplied directly rather than read from
    /// targetWidth/targetHeight (which describe this renderer's own internal color/depth targets,
    /// not the caller-supplied destination texture pair). A zero width/height in either slot means
    /// "not usable for UI" (see the UIOverlay pass's own gating), matching the interface's
    /// documented default of skipping UI when the size is unknown.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> destinationTextureSizeOverride;

    /// Set via SetUIRuntime(); read fresh by the "UIOverlay" pass every RenderFrame* call.
    const UI::UIRuntime* uiRuntimeForFrame = nullptr;
    /// Built-in UI overlay program (engine/render/shader/built_in/ui_overlay.glsl) - position +
    /// texcoord + vertex color, alpha-blended, samples whichever texture is bound (the font atlas
    /// for glyph quads, fallbackWhiteTexture for solid quads, and resolved asset textures for images -
    /// see RecordUIOverlayItems).
    std::shared_ptr<Shader::ShaderProgram> uiOverlayProgram;
    /// The font atlas's baked RGBA8 bitmap, uploaded to the GPU lazily on first use (the bitmap
    /// never changes after baking, so one upload for the renderer's whole lifetime suffices).
    TextureHandle uiFontAtlasTexture = kInvalidTextureHandle;
    BufferHandle uiVertexBuffer = kInvalidBufferHandle;
    std::vector<UIVertex> uiVertexStaging;

    const VfxDrawList* vfxDrawListForFrame = nullptr;
    VfxSpriteRenderer vfxSpriteRenderer;
    VfxSpriteFrameContext vfxFrameContext{};
    std::unordered_map<Asset::AssetGuid, Asset::AssetHandle<Asset::MaterialAsset>> vfxMaterialHandles;
    std::unordered_set<Asset::AssetGuid> vfxReferencedTextureGuids;

    Impl(IRenderDevice& renderDeviceIn, IRenderSystem& renderSystemIn, IMeshRenderer& meshRendererIn,
            ICameraSystem& cameraSystemIn, ILightSystem& lightSystemIn,
            Shader::IShaderManager& shaderManagerIn, Asset::IAssetManager& assetManagerIn,
            Asset::IAssetDatabase& assetDatabaseIn, Events::IEventSystem& eventSystemIn,
            std::uint32_t targetWidthIn, std::uint32_t targetHeightIn, Math::Vector3 ambientColorIn,
            std::uint32_t shadowMapResolutionIn, float shadowMapHalfExtentIn, float shadowMapNearPlaneIn,
            float shadowMapFarPlaneIn, float shadowFrustumPaddingIn, float shadowCascadeSplitLambdaIn,
            float shadowCascadeBlendRatioIn, std::uint32_t shadowPcfKernelRadiusIn)
        : renderDevice(renderDeviceIn), renderSystem(renderSystemIn), meshRenderer(meshRendererIn),
          cameraSystem(cameraSystemIn), lightSystem(lightSystemIn), shaderManager(shaderManagerIn),
          assetManager(assetManagerIn), assetDatabase(assetDatabaseIn), eventSystem(eventSystemIn),
          uniformNames(GetRendererUniformNames()), targetWidth(targetWidthIn), targetHeight(targetHeightIn), ambientColor(ambientColorIn),
          shadowMapResolution(shadowMapResolutionIn), shadowMapHalfExtent(shadowMapHalfExtentIn),
          shadowMapNearPlane(shadowMapNearPlaneIn), shadowMapFarPlane(shadowMapFarPlaneIn),
          shadowFrustumPadding(SanitizeFiniteNonNegative(shadowFrustumPaddingIn, 0.0F,
                                                             "shadowFrustumPadding")),
          shadowCascadeSplitLambda(SanitizeFiniteClamped(shadowCascadeSplitLambdaIn, 0.6F, 0.0F, 1.0F,
                                                            "shadowCascadeSplitLambda")),
          shadowCascadeBlendRatio(SanitizeFiniteClamped(shadowCascadeBlendRatioIn, 0.0F, 0.0F, 0.25F,
                                                            "shadowCascadeBlendRatio")),
          shadowPcfKernelRadius(static_cast<std::int32_t>(std::min(shadowPcfKernelRadiusIn, 2U))),
          computeSystem(renderDeviceIn),
          indirectCull(renderDeviceIn, computeSystem) {}

    /// Creates the base target set for `width`x`height`, or returns nullopt having destroyed any
    /// partial allocation. The six base targets are created together because a half-built set is
    /// not usable; higher bloom scales are added later, only when requested.
    [[nodiscard]] std::optional<SizedTargetSet> CreateTargetSet(const std::uint32_t width,
                                                                      const std::uint32_t height) {
        const TextureFormat sceneColorFormat =
            SceneColorTargetFormat(renderDevice, rendererProfileResolution.effective);
        SizedTargetSet set;
        set.colorTarget =
            renderDevice.CreateTexture(TextureDesc{width, height, sceneColorFormat, 1});
        set.depthTarget =
            renderDevice.CreateTexture(TextureDesc{width, height, TextureFormat::Depth32Float, 1});
        const std::uint32_t halfWidth = HalfExtent(width);
        const std::uint32_t halfHeight = HalfExtent(height);
        BloomMipTargets& baseBloomTargets = set.bloomMipTargets[0U];
        baseBloomTargets.bright =
            renderDevice.CreateTexture(TextureDesc{halfWidth, halfHeight, sceneColorFormat, 1});
        baseBloomTargets.blurA =
            renderDevice.CreateTexture(TextureDesc{halfWidth, halfHeight, sceneColorFormat, 1});
        baseBloomTargets.blurB =
            renderDevice.CreateTexture(TextureDesc{halfWidth, halfHeight, sceneColorFormat, 1});
        const auto createSingleChannelTarget = [this](const std::uint32_t targetW, const std::uint32_t targetH) {
            TextureHandle handle =
                renderDevice.CreateTexture(TextureDesc{targetW, targetH, TextureFormat::R8Unorm, 1});
            if (handle == kInvalidTextureHandle) {
                handle = renderDevice.CreateTexture(TextureDesc{targetW, targetH, TextureFormat::RGBA8Unorm, 1});
            }
            return handle;
        };
        set.ssaoTarget = createSingleChannelTarget(halfWidth, halfHeight);
        set.ssaoBlurTarget = createSingleChannelTarget(halfWidth, halfHeight);
        set.contactShadowTarget = createSingleChannelTarget(width, height);
        set.ssrTarget =
            renderDevice.CreateTexture(TextureDesc{halfWidth, halfHeight, sceneColorFormat, 1});
        if (set.ssrTarget == kInvalidTextureHandle && sceneColorFormat != TextureFormat::RGBA8Unorm) {
            set.ssrTarget =
                renderDevice.CreateTexture(TextureDesc{halfWidth, halfHeight, TextureFormat::RGBA8Unorm, 1});
        }

        // Color and depth are load-bearing: without them there is no frame at all. The
        // post-process targets are not - RenderFrame() guards on their validity and skips those
        // passes - so a set missing only those is still returned and still usable, preserving the
        // pre-cache behavior exactly.
        if (set.colorTarget == kInvalidTextureHandle || set.depthTarget == kInvalidTextureHandle) {
            DestroyTargetSet(set);
            return std::nullopt;
        }
        if (baseBloomTargets.bright == kInvalidTextureHandle ||
            baseBloomTargets.blurA == kInvalidTextureHandle ||
            baseBloomTargets.blurB == kInvalidTextureHandle || set.ssaoTarget == kInvalidTextureHandle ||
            set.contactShadowTarget == kInvalidTextureHandle) {
            for (BloomMipTargets& bloomTargets : set.bloomMipTargets) {
                DestroyTextureIfValid(renderDevice, bloomTargets.bright);
                DestroyTextureIfValid(renderDevice, bloomTargets.blurA);
                DestroyTextureIfValid(renderDevice, bloomTargets.blurB);
                bloomTargets = BloomMipTargets{};
            }
            DestroyTextureIfValid(renderDevice, set.ssaoTarget);
            DestroyTextureIfValid(renderDevice, set.ssaoBlurTarget);
            DestroyTextureIfValid(renderDevice, set.contactShadowTarget);
            DestroyTextureIfValid(renderDevice, set.ssrTarget);
            set.bloomMipTargetCount = 0U;
            set.ssaoTarget = kInvalidTextureHandle;
            set.ssaoBlurTarget = kInvalidTextureHandle;
            set.contactShadowTarget = kInvalidTextureHandle;
            set.ssrTarget = kInvalidTextureHandle;
            UVE_WARNING("Renderer3D: post-process target creation failed at {}x{}; bloom/SSAO/contact "
                        "shadow passes will be skipped at this size",
                        halfWidth, halfHeight);
        } else {
            set.bloomMipTargetCount = 1U;
        }
        return set;
    }

    void DestroyTargetSet(const SizedTargetSet& set) {
        DestroyTextureIfValid(renderDevice, set.colorTarget);
        DestroyTextureIfValid(renderDevice, set.depthTarget);
        for (const BloomMipTargets& bloomTargets : set.bloomMipTargets) {
            DestroyTextureIfValid(renderDevice, bloomTargets.bright);
            DestroyTextureIfValid(renderDevice, bloomTargets.blurA);
            DestroyTextureIfValid(renderDevice, bloomTargets.blurB);
        }
        DestroyTextureIfValid(renderDevice, set.ssaoTarget);
        DestroyTextureIfValid(renderDevice, set.ssaoBlurTarget);
        DestroyTextureIfValid(renderDevice, set.contactShadowTarget);
        DestroyTextureIfValid(renderDevice, set.ssrTarget);
        DestroyTextureIfValid(renderDevice, set.taaHistoryTargets[0]);
        DestroyTextureIfValid(renderDevice, set.taaHistoryTargets[1]);
        DestroyTextureIfValid(renderDevice, set.wboitAccumTarget);
        DestroyTextureIfValid(renderDevice, set.wboitRevealTarget);
    }

    void RecreateTargetSetsForProfileFormatChange() {
        const std::uint32_t activeW = targetWidth;
        const std::uint32_t activeH = targetHeight;
        for (const auto& [sizeKey, set] : targetSetCache) {
            DestroyTargetSet(set);
        }
        targetSetCache.clear();
        colorTarget = kInvalidTextureHandle;
        depthTarget = kInvalidTextureHandle;
        bloomMipTargets = {};
        bloomMipTargetCount = 0U;
        ssaoTarget = kInvalidTextureHandle;
        ssaoBlurTarget = kInvalidTextureHandle;
        contactShadowTarget = kInvalidTextureHandle;
        ssrTarget = kInvalidTextureHandle;
        if (activeW > 0U && activeH > 0U) {
            if (std::optional<SizedTargetSet> recreated = CreateTargetSet(activeW, activeH);
                recreated.has_value()) {
                const auto inserted = targetSetCache.emplace(std::pair{activeW, activeH}, *recreated);
                ActivateTargetSet(inserted.first->second, activeW, activeH);
            }
        }
    }

    void EnsureShadowMapResolution(const std::uint32_t requestedResolution) {
        const std::uint32_t clamped = std::clamp(requestedResolution, 256U, 4096U);
        if (clamped == shadowMapResolution && AreShadowMapTargetsValid(shadowMapTargets)) {
            return;
        }
        for (TextureHandle& target : shadowMapTargets) {
            DestroyTextureIfValid(renderDevice, target);
            target = renderDevice.CreateTexture(
                TextureDesc{clamped, clamped, TextureFormat::Depth32Float, 1});
        }
        shadowMapResolution = clamped;
    }

    [[nodiscard]] Shader::ShaderProgramStagesDesc MakeLitStagesDescForProfile(
        const Platform::RendererProfile profile) const {
        Shader::ShaderProgramStagesDesc desc = defaultLitStagesDesc;
        if (profile == Platform::RendererProfile::Compatibility) {
            desc.vertexSource.extraDefines.emplace_back("UVE_COMPATIBILITY", "1");
            desc.fragmentSource.extraDefines.emplace_back("UVE_COMPATIBILITY", "1");
        } else if (profile == Platform::RendererProfile::Mobile) {
            desc.vertexSource.extraDefines.emplace_back("UVE_MOBILE_PROFILE", "1");
            desc.fragmentSource.extraDefines.emplace_back("UVE_MOBILE_PROFILE", "1");
        }
        return desc;
    }

    void EnsureDefaultLitProgramsForProfile(const Platform::RendererProfile profile) {
        if (defaultLitStagesDesc.vertexLayout.empty()) {
            return;
        }
        const std::size_t idx = std::min<std::size_t>(static_cast<std::size_t>(profile), 2U);
        if (defaultLitProgramsByProfile[idx] == nullptr) {
            Shader::ShaderProgramStagesDesc desc = MakeLitStagesDescForProfile(profile);
            desc.debugName = "BuiltInLit default (" +
                                std::string(Platform::GetRendererProfileName(profile)) + ")";
            defaultLitProgramsByProfile[idx] = shaderManager.CreateProgramFromStages(desc);
            const std::uint32_t key =
                (static_cast<std::uint32_t>(idx) << 16U) |
                MaterialPipelineState{desc.blendMode, desc.cullMode, desc.depthTestEnabled, desc.depthWriteEnabled}
                    .Pack();
            standardLitPrograms.emplace(key, defaultLitProgramsByProfile[idx]);

            Shader::ShaderProgramStagesDesc blendedDesc = desc;
            blendedDesc.depthWriteEnabled = false;
            blendedDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
            blendedDesc.depthCompare = DepthCompare::LessOrEqual;
            blendedDesc.debugName = desc.debugName + " blended";
            defaultLitBlendedProgramsByProfile[idx] = shaderManager.CreateProgramFromStages(blendedDesc);
        }
        defaultLitMaterialResources.program = defaultLitProgramsByProfile[idx];
        defaultLitMaterialResources.blendedProgram = defaultLitBlendedProgramsByProfile[idx];
    }

    [[nodiscard]] SizedTargetSet* ActiveTargetSetMutable() noexcept {
        const auto it = targetSetCache.find({targetWidth, targetHeight});
        return it == targetSetCache.end() ? nullptr : &it->second;
    }

    [[nodiscard]] bool EnsureTaaTargets() {
        SizedTargetSet* const set = ActiveTargetSetMutable();
        if (set == nullptr || targetWidth == 0U || targetHeight == 0U) {
            return false;
        }
        if (set->taaHistoryTargets[0] != kInvalidTextureHandle &&
            set->taaHistoryTargets[1] != kInvalidTextureHandle) {
            return true;
        }
        const TextureFormat sceneColorFormat =
            SceneColorTargetFormat(renderDevice, rendererProfileResolution.effective);
        for (TextureHandle& handle : set->taaHistoryTargets) {
            if (handle == kInvalidTextureHandle) {
                handle = renderDevice.CreateTexture(
                    TextureDesc{targetWidth, targetHeight, sceneColorFormat, 1});
            }
        }
        if (set->taaHistoryTargets[0] == kInvalidTextureHandle ||
            set->taaHistoryTargets[1] == kInvalidTextureHandle) {
            DestroyTextureIfValid(renderDevice, set->taaHistoryTargets[0]);
            DestroyTextureIfValid(renderDevice, set->taaHistoryTargets[1]);
            set->taaHistoryTargets = {kInvalidTextureHandle, kInvalidTextureHandle};
            set->taaHistoryValid = false;
            return false;
        }
        set->taaHistoryValid = false;
        return true;
    }

    [[nodiscard]] bool EnsureWboitTargets() {
        SizedTargetSet* const set = ActiveTargetSetMutable();
        if (set == nullptr || targetWidth == 0U || targetHeight == 0U) {
            return false;
        }
        if (set->wboitAccumTarget != kInvalidTextureHandle &&
            set->wboitRevealTarget != kInvalidTextureHandle) {
            return true;
        }
        const TextureFormat sceneColorFormat =
            SceneColorTargetFormat(renderDevice, rendererProfileResolution.effective);
        if (set->wboitAccumTarget == kInvalidTextureHandle) {
            set->wboitAccumTarget = renderDevice.CreateTexture(
                TextureDesc{targetWidth, targetHeight, sceneColorFormat, 1});
        }
        if (set->wboitRevealTarget == kInvalidTextureHandle) {
            set->wboitRevealTarget = renderDevice.CreateTexture(
                TextureDesc{targetWidth, targetHeight, sceneColorFormat, 1});
        }
        if (set->wboitAccumTarget == kInvalidTextureHandle ||
            set->wboitRevealTarget == kInvalidTextureHandle) {
            DestroyTextureIfValid(renderDevice, set->wboitAccumTarget);
            DestroyTextureIfValid(renderDevice, set->wboitRevealTarget);
            set->wboitAccumTarget = kInvalidTextureHandle;
            set->wboitRevealTarget = kInvalidTextureHandle;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool EnsureClusteredLightBuffers() {
        if (rendererProfileResolution.effective != Platform::RendererProfile::ForwardPlus ||
            renderDevice.GetCapabilities().storageBuffers == RenderFeatureSupport::Unsupported) {
            return false;
        }
        if (clusteredLightBuffer != kInvalidBufferHandle &&
            clusteredCellBuffer != kInvalidBufferHandle &&
            clusteredIndexBuffer != kInvalidBufferHandle) {
            return true;
        }
        if (clusteredLightBuffer == kInvalidBufferHandle) {
            clusteredLightBuffer = renderDevice.CreateBuffer(BufferDesc{
                kMaxClusteredLights * sizeof(ClusteredGpuLight), BufferUsage::Storage});
        }
        if (clusteredCellBuffer == kInvalidBufferHandle) {
            clusteredCellBuffer = renderDevice.CreateBuffer(BufferDesc{
                kTotalLightClusters * sizeof(LightClusterCell), BufferUsage::Storage});
        }
        if (clusteredIndexBuffer == kInvalidBufferHandle) {
            clusteredIndexBuffer = renderDevice.CreateBuffer(BufferDesc{
                kMaxClusterLightIndices * sizeof(std::uint32_t), BufferUsage::Storage});
        }
        if (clusteredLightBuffer == kInvalidBufferHandle ||
            clusteredCellBuffer == kInvalidBufferHandle ||
            clusteredIndexBuffer == kInvalidBufferHandle) {
            DestroyBufferIfValid(renderDevice, clusteredLightBuffer);
            DestroyBufferIfValid(renderDevice, clusteredCellBuffer);
            DestroyBufferIfValid(renderDevice, clusteredIndexBuffer);
            clusteredLightBuffer = kInvalidBufferHandle;
            clusteredCellBuffer = kInvalidBufferHandle;
            clusteredIndexBuffer = kInvalidBufferHandle;
            return false;
        }
        return true;
    }

    /// Points the active target members at `set`. Every render pass reads these members, so
    /// switching sets is exactly this and nothing more - the cache is invisible to the passes.
    void ActivateTargetSet(const SizedTargetSet& set, const std::uint32_t width,
                              const std::uint32_t height) noexcept {
        colorTarget = set.colorTarget;
        depthTarget = set.depthTarget;
        bloomMipTargets = set.bloomMipTargets;
        bloomMipTargetCount = set.bloomMipTargetCount;
        ssaoTarget = set.ssaoTarget;
        ssaoBlurTarget = set.ssaoBlurTarget;
        contactShadowTarget = set.contactShadowTarget;
        ssrTarget = set.ssrTarget;
        targetWidth = width;
        targetHeight = height;
    }

    /// Adds any requested downsample levels to this size's cached target set. Additional levels
    /// are allocated lazily so the default one-scale bloom and other cached view sizes pay no cost.
    [[nodiscard]] std::uint32_t EnsureBloomMipTargets(const std::uint32_t requestedMipCount) {
        const std::pair<std::uint32_t, std::uint32_t> key{targetWidth, targetHeight};
        const auto cachedIt = targetSetCache.find(key);
        if (cachedIt == targetSetCache.end()) {
            return 0U;
        }
        SizedTargetSet& set = cachedIt->second;
        if (set.bloomMipTargetCount == 0U) {
            return 0U;
        }

        const std::uint32_t targetMipCount = std::min(
            requestedMipCount, MaximumBloomMipCountForTarget(targetWidth, targetHeight));
        std::uint32_t mipWidth = HalfExtent(targetWidth);
        std::uint32_t mipHeight = HalfExtent(targetHeight);
        const TextureFormat sceneColorFormat =
            SceneColorTargetFormat(renderDevice, rendererProfileResolution.effective);
        for (std::uint32_t mipIndex = 1U; mipIndex < targetMipCount; ++mipIndex) {
            mipWidth = HalfExtent(mipWidth);
            mipHeight = HalfExtent(mipHeight);
            if (set.bloomMipTargetCount > mipIndex) {
                continue;
            }

            BloomMipTargets targets;
            targets.bright = renderDevice.CreateTexture(
                TextureDesc{mipWidth, mipHeight, sceneColorFormat, 1});
            targets.blurA = renderDevice.CreateTexture(
                TextureDesc{mipWidth, mipHeight, sceneColorFormat, 1});
            targets.blurB = renderDevice.CreateTexture(
                TextureDesc{mipWidth, mipHeight, sceneColorFormat, 1});
            if (targets.bright == kInvalidTextureHandle || targets.blurA == kInvalidTextureHandle ||
                targets.blurB == kInvalidTextureHandle) {
                DestroyTextureIfValid(renderDevice, targets.bright);
                DestroyTextureIfValid(renderDevice, targets.blurA);
                DestroyTextureIfValid(renderDevice, targets.blurB);
                UVE_WARNING("Renderer3D: Bloom mip {} target allocation failed at {}x{}; using {} levels",
                            mipIndex, mipWidth, mipHeight, set.bloomMipTargetCount);
                break;
            }
            set.bloomMipTargets[mipIndex] = targets;
            set.bloomMipTargetCount = mipIndex + 1U;
        }
        ActivateTargetSet(set, targetWidth, targetHeight);
        return std::min(targetMipCount, set.bloomMipTargetCount);
    }

    [[nodiscard]] bool ResizeTargets(const std::uint32_t newWidth, const std::uint32_t newHeight) {
        if (newWidth == 0U || newHeight == 0U) {
            return false;
        }
        if (targetWidth == newWidth && targetHeight == newHeight &&
            colorTarget != kInvalidTextureHandle) {
            return true;
        }

        const std::pair<std::uint32_t, std::uint32_t> key{newWidth, newHeight};
        const auto cachedIt = targetSetCache.find(key);
        if (cachedIt != targetSetCache.end()) {
            // The whole point: a size seen before costs a handful of pointer assignments, not
            // texture allocations. This is the path a steady-state split view takes every frame.
            ActivateTargetSet(cachedIt->second, newWidth, newHeight);
            return true;
        }

        // A genuinely new size. Evict first if the cache has grown past its bound - a window being
        // dragged produces a new size every frame, and those sets are dead the moment they are
        // made. Clearing wholesale rather than evicting one entry keeps this simple and cannot
        // free a set that is about to be reactivated, because the active set is re-created
        // immediately below.
        if (targetSetCache.size() >= kMaximumCachedTargetSets) {
            for (const auto& [cachedSize, cachedSet] : targetSetCache) {
                static_cast<void>(cachedSize);
                DestroyTargetSet(cachedSet);
            }
            targetSetCache.clear();
            colorTarget = kInvalidTextureHandle;
            depthTarget = kInvalidTextureHandle;
            bloomMipTargets = {};
            bloomMipTargetCount = 0U;
            ssaoTarget = kInvalidTextureHandle;
            ssaoBlurTarget = kInvalidTextureHandle;
            contactShadowTarget = kInvalidTextureHandle;
            ssrTarget = kInvalidTextureHandle;
        }

        std::optional<SizedTargetSet> created = CreateTargetSet(newWidth, newHeight);
        if (!created.has_value()) {
            UVE_WARNING("Renderer3D: adaptive target resize rejected ({}x{}); retaining {}x{}",
                        newWidth, newHeight, targetWidth, targetHeight);
            return false;
        }

        const auto inserted = targetSetCache.emplace(key, *created);
        ActivateTargetSet(inserted.first->second, newWidth, newHeight);
        UVE_INFO("Renderer3D: adaptive targets resized to {}x{}", targetWidth, targetHeight);
        return true;
    }

    void EvictUnreferencedTextureCacheEntries() {
        std::unordered_set<Asset::AssetGuid> referencedTextureGuids;
        for (const auto& [materialGuid, resources] : materialCache) {
            static_cast<void>(materialGuid);
            if (resources.albedoTextureGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(resources.albedoTextureGuid);
            }
            if (resources.normalTextureGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(resources.normalTextureGuid);
            }
            if (resources.aoTextureGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(resources.aoTextureGuid);
            }
            if (resources.metallicRoughnessTextureGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(resources.metallicRoughnessTextureGuid);
            }
            if (resources.emissiveTextureGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(resources.emissiveTextureGuid);
            }
        }
        if (skyTextureGuid != Asset::kInvalidAssetGuid) {
            referencedTextureGuids.insert(skyTextureGuid);
        }
        if (lutTextureGuid != Asset::kInvalidAssetGuid) {
            referencedTextureGuids.insert(lutTextureGuid);
        }
        for (const Asset::AssetGuid vfxTexGuid : vfxReferencedTextureGuids) {
            if (vfxTexGuid != Asset::kInvalidAssetGuid) {
                referencedTextureGuids.insert(vfxTexGuid);
            }
        }
        for (auto textureIt = textureCache.begin(); textureIt != textureCache.end();) {
            if (!referencedTextureGuids.contains(textureIt->first)) {
                DestroyTextureIfValid(renderDevice, textureIt->second);
                textureIt = textureCache.erase(textureIt);
            } else {
                ++textureIt;
            }
        }
    }

    [[nodiscard]] VfxResolvedBatchAppearance ResolveVfxBatchAppearance(
        const std::string_view texturePath,
        const std::string_view materialPath) {
        VfxResolvedBatchAppearance result{};
        result.albedoTexture = fallbackWhiteTexture;
        result.materialTint = Math::Vector4{1.0F, 1.0F, 1.0F, 1.0F};
        result.materialEmissive = Math::Vector3{0.0F, 0.0F, 0.0F};
        result.ready = true;

        if (!materialPath.empty()) {
            const Asset::AssetGuid matGuid = assetDatabase.Register(materialPath);
            if (matGuid != Asset::kInvalidAssetGuid) {
                auto matIt = vfxMaterialHandles.find(matGuid);
                if (matIt == vfxMaterialHandles.end()) {
                    matIt = vfxMaterialHandles
                                .emplace(matGuid,
                                         assetManager.Load<Asset::MaterialAsset>(matGuid, assetDatabase))
                                .first;
                }
                const Asset::AssetHandle<Asset::MaterialAsset>& matHandle = matIt->second;
                if (!matHandle.IsReady() && !matHandle.HasFailed()) {
                    result.ready = false;
                    return result;
                }
                if (matHandle.IsReady()) {
                    const Asset::MaterialAsset* const mat = matHandle.TryGet();
                    if (mat != nullptr && Asset::IsMaterialAssetValid(*mat)) {
                        result.materialTint = Math::Vector4{
                            mat->albedoColor.r,
                            mat->albedoColor.g,
                            mat->albedoColor.b,
                            std::clamp(mat->albedoAlpha, 0.0F, 1.0F),
                        };
                        const float emissiveEnergy = std::max(0.0F, mat->emissiveEnergy);
                        result.materialEmissive = Math::Vector3{
                            mat->emissiveColor.r * emissiveEnergy,
                            mat->emissiveColor.g * emissiveEnergy,
                            mat->emissiveColor.b * emissiveEnergy,
                        };
                        if (mat->albedoTexture != Asset::kInvalidAssetGuid) {
                            vfxReferencedTextureGuids.insert(mat->albedoTexture);
                            const std::optional<TextureHandle> matTex =
                                ResolveTextureGpuHandle(mat->albedoTexture, fallbackWhiteTexture);
                            if (!matTex.has_value()) {
                                result.ready = false;
                                return result;
                            }
                            result.albedoTexture = *matTex;
                        }
                    }
                }
            }
        }

        if (!texturePath.empty()) {
            const Asset::AssetGuid texGuid = assetDatabase.Register(texturePath);
            if (texGuid != Asset::kInvalidAssetGuid) {
                vfxReferencedTextureGuids.insert(texGuid);
                const std::optional<TextureHandle> resolvedTex =
                    ResolveTextureGpuHandle(texGuid, fallbackWhiteTexture);
                if (!resolvedTex.has_value()) {
                    result.ready = false;
                    return result;
                }
                result.albedoTexture = *resolvedTex;
            }
        }

        return result;
    }

    /// True when anything this renderer has cached was derived from `guid`: the sky texture, a
    /// mesh's GPU buffers, a material's resolved program plus texture handles (or the shaders one
    /// of those materials is built from), a texture upload, or a failed-upload memo.
    [[nodiscard]] bool CachedStateDependsOn(const Asset::AssetGuid guid) const {
        if (guid == skyTextureGuid || meshCache.contains(guid) || textureCache.contains(guid) ||
            failedTextureGuids.contains(guid) || materialCache.contains(guid) ||
            vfxMaterialHandles.contains(guid) || vfxReferencedTextureGuids.contains(guid)) {
            return true;
        }
        // A shader asset has no cache entry of its own, but every material built from it does.
        for (const auto& entry : materialCache) {
            if (entry.second.vertexShaderGuid == guid || entry.second.fragmentShaderGuid == guid) {
                return true;
            }
        }
        return false;
    }

    /// Drops every piece of cached GPU state derived from `guid`. Split out from the event
    /// handlers because it is deliberately run twice per reload - see OnAssetReloaded.
    void InvalidateReloadedAsset(const Asset::AssetGuid guid) {
        if (guid == skyTextureGuid) {
            skyTextureHandle = kInvalidTextureHandle;
            skyTextureEnabled = false;
        }
        vfxMaterialHandles.erase(guid);
        const auto meshIt = meshCache.find(guid);
        if (meshIt != meshCache.end()) {
            DestroyBufferIfValid(renderDevice, meshIt->second.vertexBuffer);
            DestroyBufferIfValid(renderDevice, meshIt->second.indexBuffer);
            meshCache.erase(meshIt);
        }
        for (auto& [entityKey, skinned] : skinnedMeshCache) {
            static_cast<void>(entityKey);
            if (skinned.meshGuid == guid) {
                skinned.lastLocals.clear();
            }
        }
        // A material asset reload, or a reload of either of its separate shader assets, drops the
        // renderer cache entry. The shared managed program then releases naturally; ShaderManager
        // remains the sole owner of the linked pipeline lifecycle. Texture source GUIDs are retained
        // in each record so entries no longer referenced by a reloaded material can be retired.
        bool materialAssetReloaded = false;
        for (auto materialIt = materialCache.begin(); materialIt != materialCache.end();) {
            const MaterialGpuResources& resources = materialIt->second;
            if (materialIt->first == guid || resources.vertexShaderGuid == guid ||
                resources.fragmentShaderGuid == guid) {
                materialAssetReloaded = materialAssetReloaded || materialIt->first == guid;
                materialIt = materialCache.erase(materialIt);
            } else {
                ++materialIt;
            }
        }

        const bool textureFailureMemoErased = failedTextureGuids.erase(guid) > 0U;
        const auto textureIt = textureCache.find(guid);
        if (textureIt != textureCache.end()) {
            DestroyTextureIfValid(renderDevice, textureIt->second);
            textureCache.erase(textureIt);

            // MaterialGpuResources doesn't track which texture GUIDs it resolved from, so
            // there's no cheap way to know which cached materials referenced this one — clear the
            // whole cache instead. Every material's pipeline and resolved texture handles get
            // recomputed lazily next frame they're drawn (mostly cache hits against textureCache
            // for anything unaffected). Coarse but simple and obviously correct, matching this
            // codebase's existing preference for whole-unit invalidation over fine-grained
            // dependency tracking (compare ShaderManager's own hot-reload).
            materialCache.clear();
        } else if (materialAssetReloaded) {
            // Shader reloads intentionally retain valid texture uploads; only a material payload
            // swap can make a previously cached texture definitively unreferenced here.
            EvictUnreferencedTextureCacheEntries();
        } else if (textureFailureMemoErased) {
            // A failed upload has no textureCache entry, but a material rebuilt after that failure
            // can still cache fallback handles. Rebuild it after the explicit texture reload so the
            // asset gets one deliberate retry instead of remaining on a stale fallback forever.
            materialCache.clear();
        }
    }

    /// AssetReloadedEvent says a reload was *started*, and the reload runs on a worker thread:
    /// the payload swap may still be minutes-of-code away when this arrives. Invalidating only here
    /// therefore had a real failure mode - the caches were dropped, the very next frame rebuilt
    /// them from the payload that was about to be replaced, and nothing ever told the renderer
    /// again, so an edited material (a swapped albedo map, a shader GUID, a new `isTransparent`)
    /// kept drawing the old files for the rest of the session. Invalidate now for responsiveness -
    /// if the swap already landed this is the only invalidation needed - and register the GUID so
    /// OnAssetLoadCompleted can do it again once the new payload is actually installed.
    void OnAssetReloaded(const Asset::AssetReloadedEvent& event) {
        // Only assets this renderer actually derived state from need to be remembered; that keeps
        // the set bounded by the caches below instead of by every reload in the project.
        if (!CachedStateDependsOn(event.guid)) {
            return;
        }
        reloadsInFlight.insert(event.guid);
        InvalidateReloadedAsset(event.guid);
    }

    /// The reload's real news: the manager has installed the new payload (or, failure-atomic, kept
    /// the last-known-good one and reported the diagnostic). Re-invalidating here is what makes the
    /// caches observe the bytes that were just loaded.
    void OnAssetLoadCompleted(const Asset::AssetLoadCompletedEvent& event) {
        if (reloadsInFlight.erase(event.guid) == 0U) {
            return; // A first load, not a reload: nothing cached under this GUID could be stale.
        }
        if (!event.success) {
            // The manager only reaches this branch with the last-known-good payload still
            // installed, so everything the caches were built from is still current. Dropping them
            // would just rebuild identical entries from identical data.
            return;
        }
        InvalidateReloadedAsset(event.guid);
    }

    /// Returns the cached (creating-if-needed) GPU buffers for `item`'s mesh. `item.meshHandle`
    /// is always ready by construction (MeshRenderer::ExtractRenderQueue only includes
    /// asset-ready items), so this never fails.
    [[nodiscard]] const MeshGpuResources& ResolveMeshGpuResources(const RenderItem& item) {
        return ResolveMeshGpuResources(item.meshHandle.GetGuid(), item.meshHandle.TryGet());
    }

    /// The same cache, for a caller that already holds the ready mesh. Both paths draw the same
    /// vertex layout, so an imported mesh is uploaded once whichever path draws it.
    [[nodiscard]] const MeshGpuResources& ResolveMeshGpuResources(const Asset::AssetGuid guid,
                                                                       const Asset::MeshAsset* const mesh) {
        const auto existingIt = meshCache.find(guid);
        if (existingIt != meshCache.end()) {
            return existingIt->second;
        }
        // Over this frame's upload budget: drawn from a later frame, when its turn comes.
        if (meshUploadBudget > 0U && lastFrameDiagnostics.meshUploads >= meshUploadBudget) {
            if (std::find(deferredMeshesThisFrame.begin(), deferredMeshesThisFrame.end(), guid.value) == deferredMeshesThisFrame.end()) {
                deferredMeshesThisFrame.push_back(guid.value);
                ++lastFrameDiagnostics.meshUploadsDeferred;
            }
            static const MeshGpuResources kNotYet{};
            return kNotYet;
        }
        if (lastFrameDiagnostics.meshUploads < lastFrameDiagnostics.uploadedMeshes.size()) {
            lastFrameDiagnostics.uploadedMeshes[lastFrameDiagnostics.meshUploads] = guid;
        }
        ++lastFrameDiagnostics.meshUploads;

        // Asset loaders derive tangents for legacy `.uvmodel` payloads, but runtime/custom mesh
        // loaders may construct MeshAsset directly. Regenerate into this one-time GPU-upload copy
        // so every material draw has the canonical TBN input without mutating shared asset data.
        std::vector<Asset::MeshVertex> vertices = mesh->vertices;
        Asset::GenerateMeshTangents(vertices, mesh->indices);
        const std::span<const Asset::MeshVertex> vertexSpan(vertices);
        const std::span<const std::uint32_t> indexSpan(mesh->indices);
        const std::span<const std::byte> vertexBytes = std::as_bytes(vertexSpan);
        const std::span<const std::byte> indexBytes = std::as_bytes(indexSpan);

        const BufferHandle vertexBuffer =
            renderDevice.CreateBuffer(BufferDesc{vertexBytes.size(), BufferUsage::Vertex}, vertexBytes);
        const BufferHandle indexBuffer =
            renderDevice.CreateBuffer(BufferDesc{indexBytes.size(), BufferUsage::Index}, indexBytes);
        MeshGpuResources resources{vertexBuffer, indexBuffer, static_cast<std::uint32_t>(mesh->indices.size())};
        if (!IsValidMeshGpuResources(resources)) {
            DestroyBufferIfValid(renderDevice, vertexBuffer);
            DestroyBufferIfValid(renderDevice, indexBuffer);
            UVE_ERROR("Renderer3D: mesh GPU buffer allocation failed; caching a no-draw result");
            resources = MeshGpuResources{};
        }

        const auto insertResult = meshCache.emplace(guid, resources);
        return insertResult.first->second;
    }

    /// Returns the cached immutable GPU buffers for one built-in primitive kind. Geometry is copied
    /// only for one-time tangent generation; the canonical catalog stays immutable and shared.
    [[nodiscard]] const MeshGpuResources& ResolvePrimitiveMeshGpuResources(
        const Scene::PrimitiveMeshKind kind) {
        const std::uint8_t key = static_cast<std::uint8_t>(kind);
        const auto existingIt = primitiveMeshCache.find(key);
        if (existingIt != primitiveMeshCache.end()) {
            return existingIt->second;
        }

        // Tangent-complete geometry: the material path samples normal maps through the TBN basis,
        // and the geometry tables themselves carry only a placeholder tangent. The derivation and
        // its cache live in the geometry module, so every consumer of a primitive sees the same basis.
        const PrimitiveGeometry& geometry = GetPrimitiveLitGeometry(kind);
        const std::span<const Asset::MeshVertex> vertexSpan(geometry.vertices);
        const std::span<const std::uint32_t> indexSpan(geometry.indices);
        const BufferHandle vertexBuffer = renderDevice.CreateBuffer(
            BufferDesc{std::as_bytes(vertexSpan).size(), BufferUsage::Vertex}, std::as_bytes(vertexSpan));
        const BufferHandle indexBuffer = renderDevice.CreateBuffer(
            BufferDesc{std::as_bytes(indexSpan).size(), BufferUsage::Index}, std::as_bytes(indexSpan));
        MeshGpuResources resources{vertexBuffer, indexBuffer, static_cast<std::uint32_t>(geometry.indices.size())};
        if (!IsValidMeshGpuResources(resources)) {
            DestroyBufferIfValid(renderDevice, vertexBuffer);
            DestroyBufferIfValid(renderDevice, indexBuffer);
            UVE_ERROR("Renderer3D: primitive GPU buffer allocation failed; caching a no-draw result");
            resources = MeshGpuResources{};
        }
        const auto insertResult = primitiveMeshCache.emplace(key, resources);
        return insertResult.first->second;
    }

    /// Returns the GPU handle for `textureGuid`, or std::nullopt if it's still loading (caller
    /// should abort this frame's resolution without caching anything, and retry next frame — the
    /// same async-non-blocking convention as the shader/mesh/material readiness checks
    /// elsewhere in this file). `kInvalidAssetGuid` (unset in the source MaterialAsset)
    /// returns `fallbackHandle` immediately, no load ever attempted. A texture whose load has
    /// permanently failed (HasFailed()) also resolves to `fallbackHandle` — logged once — since
    /// retrying a failed load forever would never succeed.
    [[nodiscard]] std::optional<TextureHandle> ResolveTextureGpuHandle(Asset::AssetGuid textureGuid,
                                                                              TextureHandle fallbackHandle) {
        if (textureGuid == Asset::kInvalidAssetGuid) {
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

        // The handle is kept while the texture loads: a load nobody holds is dropped when it
        // finishes, so asking again every frame would start it over and over and a large texture
        // would never arrive.
        auto pendingIt = pendingTextureHandles.find(textureGuid);
        if (pendingIt == pendingTextureHandles.end()) {
            pendingIt = pendingTextureHandles.emplace(textureGuid, assetManager.Load<Asset::TextureAsset>(textureGuid, assetDatabase)).first;
        }
        Asset::AssetHandle<Asset::TextureAsset> textureHandle = pendingIt->second;
        if (textureHandle.HasFailed() || textureHandle.IsReady()) {
            pendingTextureHandles.erase(pendingIt);
        }
        if (textureHandle.HasFailed()) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_WARNING("Renderer3D: texture asset load failed - falling back to the default texture");
            return fallbackHandle;
        }
        if (!textureHandle.IsReady()) {
            return std::nullopt;
        }

        const Asset::TextureAsset* const textureAsset = textureHandle.TryGet();
        const bool textureAssetPresent = textureAsset != nullptr;
        UVE_ASSERT(textureAssetPresent);
        if (!textureAssetPresent) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3D: ready texture handle has no payload - falling back to the default texture");
            return fallbackHandle;
        }
        // Asset payloads can come from disk, custom loaders, or hot reload. Treat malformed texture
        // contents as a recoverable load/upload failure rather than an assertion on a render thread.
        const bool textureAssetValid = Asset::IsTextureAssetValid(*textureAsset);
        if (!textureAssetValid) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3D: ready texture payload has an invalid format, sampling metadata, "
                      "base image, or mip chain - falling back to the default texture");
            return fallbackHandle;
        }

        TextureDesc desc;
        std::vector<std::byte> uploadData;
        if (!BuildTextureAssetUpload(*textureAsset, renderDevice, desc, uploadData)) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3D: texture could not be staged or transcoded for this device - "
                      "falling back to the default texture");
            return fallbackHandle;
        }
        const TextureHandle handle =
            renderDevice.CreateTexture(desc, std::span<const std::byte>(uploadData));
        if (handle == kInvalidTextureHandle) {
            failedTextureGuids.insert(textureGuid);
            ++lastFrameDiagnostics.textureFallbacks;
            UVE_ERROR("Renderer3D: texture asset upload failed - falling back to the default texture");
            return fallbackHandle;
        }
        textureCache.emplace(textureGuid, handle);
        return handle;
    }

    /// Returns the cached (creating-if-needed) managed program + resolved textures for the
    /// material behind `materialHandle`, or nullptr if that material's payload or textures have not
    /// finished loading yet. The cached program may still be preprocessing or linking;
    /// RecordItems checks IsValid() and skips that frame without blocking, then renders
    /// automatically once ShaderManager completes the request.
    ///
    /// A material that names no shader assets is NOT a broken reference: MaterialAsset's own
    /// contract treats an unset shader GUID as "intentionally unset", which is exactly what a
    /// Standard material created from the Inspector looks like. Those materials shade through the
    /// engine's built-in lit program - the same program a primitive with no material assigned uses -
    /// so assigning a fresh material changes a surface's look instead of making it disappear.
    ///
    /// The shared built-in lit program for `material`'s pipeline state, created on first use.
    ///
    /// Returns null only before the session's program creation has run, which is the same contract
    /// the single shared program had. The Opaque/disabled-cull combination is pre-registered to
    /// that shared program, so the ordinary case allocates nothing.
    [[nodiscard]] std::shared_ptr<Shader::ShaderProgram> StandardLitProgram(
        const Asset::MaterialAsset& material) {
        const MaterialPipelineState state = ResolveMaterialPipelineState(material);
        const std::uint32_t profileSlot =
            std::min<std::uint32_t>(static_cast<std::uint32_t>(rendererProfileResolution.effective), 2U);
        const std::uint32_t key = (profileSlot << 16U) | state.Pack();
        if (const auto cached = standardLitPrograms.find(key); cached != standardLitPrograms.end()) {
            return cached->second;
        }
        if (defaultLitStagesDesc.vertexLayout.empty()) {
            return nullptr; // Program creation has not run yet this session.
        }
        Shader::ShaderProgramStagesDesc desc = MakeLitStagesDescForProfile(rendererProfileResolution.effective);
        desc.blendMode = state.blendMode;
        desc.cullMode = state.cullMode;
        desc.depthTestEnabled = state.depthTestEnabled;
        desc.depthWriteEnabled = state.depthWriteEnabled;
        if (state.blendMode != PipelineBlendMode::Opaque) {
            // Coplanar translucent geometry at LESS loses one of the two surfaces; EQUAL keeps it,
            // matching what the per-material blended variant has always done.
            desc.depthCompare = DepthCompare::LessOrEqual;
        }
        desc.debugName = "BuiltInLit material state " + std::to_string(key);
        std::shared_ptr<Shader::ShaderProgram> program = shaderManager.CreateProgramFromStages(desc);
        standardLitPrograms.emplace(key, program);
        return program;
    }

    [[nodiscard]] const MaterialGpuResources* ResolveMaterialGpuResources(
        const Asset::AssetHandle<Asset::MaterialAsset>& materialHandle) {
        const Asset::AssetGuid guid = materialHandle.GetGuid();
        const auto existingIt = materialCache.find(guid);
        if (existingIt != materialCache.end()) {
            return &existingIt->second;
        }

        const Asset::MaterialAsset* const material = materialHandle.TryGet();
        const bool validMaterial = material != nullptr && Asset::IsMaterialAssetValid(*material);
        UVE_ASSERT(validMaterial);
        if (!validMaterial) {
            UVE_ERROR("Renderer3D: invalid material payload skipped before GPU uniform preparation");
            return nullptr;
        }

        const std::optional<TextureHandle> albedoTexture =
            ResolveTextureGpuHandle(material->albedoTexture, fallbackWhiteTexture);
        const std::optional<TextureHandle> normalTexture =
            ResolveTextureGpuHandle(material->normalTexture, fallbackNormalTexture);
        const std::optional<TextureHandle> aoTexture =
            ResolveTextureGpuHandle(material->aoTexture, fallbackWhiteTexture);
        const std::optional<TextureHandle> metallicRoughnessTexture =
            ResolveTextureGpuHandle(material->metallicRoughnessTexture, fallbackWhiteTexture);
        const std::optional<TextureHandle> emissiveTexture =
            ResolveTextureGpuHandle(material->emissiveTexture, fallbackWhiteTexture);
        if (!albedoTexture.has_value() || !normalTexture.has_value() || !aoTexture.has_value() ||
            !metallicRoughnessTexture.has_value() || !emissiveTexture.has_value()) {
            return nullptr;
        }

        // A Standard material carries no shader of its own: the built-in lit program IS its shader,
        // shared with the default material so the two cannot shade differently. It also cannot be
        // instanced through a project-authored instancing contract, hence the explicit false.
        //
        // The program is chosen by the material's PIPELINE STATE, not handed the one shared opaque
        // program: a material that asks for additive blending or two-sided culling needs the same
        // source linked with that state, and the shared program would draw it opaque and
        // back-face-culled - the authored control silently doing nothing.
        if (material->vertexShader == Asset::kInvalidAssetGuid ||
            material->fragmentShader == Asset::kInvalidAssetGuid) {
            const std::shared_ptr<Shader::ShaderProgram> standardProgram = StandardLitProgram(*material);
            if (standardProgram == nullptr) {
                return nullptr; // Program creation has not run yet this session.
            }
            const auto insertResult = materialCache.emplace(
                guid, MaterialGpuResources{standardProgram,
                                              false,
                                              Asset::kInvalidAssetGuid,
                                              Asset::kInvalidAssetGuid,
                                              material->albedoTexture,
                                              material->normalTexture,
                                              material->aoTexture,
                                              material->metallicRoughnessTexture,
                                              material->emissiveTexture,
                                              *albedoTexture,
                                              *normalTexture,
                                              *aoTexture,
                                              *metallicRoughnessTexture,
                                              *emissiveTexture,
                                              defaultLitMaterialResources.blendedProgram});
            return &insertResult.first->second;
        }

        Asset::AssetHandle<Asset::ShaderAsset> vertexShaderHandle =
            assetManager.Load<Asset::ShaderAsset>(material->vertexShader, assetDatabase);
        Asset::AssetHandle<Asset::ShaderAsset> fragmentShaderHandle =
            assetManager.Load<Asset::ShaderAsset>(material->fragmentShader, assetDatabase);
        if (!vertexShaderHandle.IsReady() || !fragmentShaderHandle.IsReady()) {
            return nullptr;
        }

        const Asset::ShaderAsset* const vertexShaderAsset = vertexShaderHandle.TryGet();
        const Asset::ShaderAsset* const fragmentShaderAsset = fragmentShaderHandle.TryGet();
        if (vertexShaderAsset == nullptr || fragmentShaderAsset == nullptr) {
            return nullptr;
        }

        // `.uvshader` assets are envelope files rather than raw GLSL files, so their already
        // decoded source is supplied as the manager fallback and the root virtual path stays empty.
        // Includes inside that source still use the normal virtual include paths and participate in
        // program-level dependency tracking; AssetReloaded events invalidate root shader assets.
        Shader::ShaderProgramStagesDesc programDesc;
        programDesc.vertexSource.stage = ShaderStage::Vertex;
        programDesc.vertexSource.embeddedFallbackSourceCode = vertexShaderAsset->sourceCode;
        programDesc.vertexSource.entryPointName = vertexShaderAsset->entryPointName;
        programDesc.vertexSource.debugName =
            "Material vertex " + assetDatabase.Resolve(material->vertexShader).string();
        programDesc.fragmentSource.stage = ShaderStage::Fragment;
        programDesc.fragmentSource.embeddedFallbackSourceCode = fragmentShaderAsset->sourceCode;
        programDesc.fragmentSource.entryPointName = fragmentShaderAsset->entryPointName;
        programDesc.fragmentSource.debugName =
            "Material fragment " + assetDatabase.Resolve(material->fragmentShader).string();
        programDesc.vertexLayout = MeshVertexLayout();
        programDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
        // Same state table as the built-in path: a project-authored shader is still a material, and
        // its blend/cull/depth fields meant nothing here either.
        const MaterialPipelineState pipelineState = ResolveMaterialPipelineState(*material);
        programDesc.depthTestEnabled = pipelineState.depthTestEnabled;
        programDesc.depthWriteEnabled = pipelineState.depthWriteEnabled;
        programDesc.blendMode = pipelineState.blendMode;
        programDesc.cullMode = pipelineState.cullMode;
        if (pipelineState.blendMode != PipelineBlendMode::Opaque) {
            programDesc.depthCompare = DepthCompare::LessOrEqual;
        }
        programDesc.debugName = "Material " + assetDatabase.Resolve(guid).string();
        const std::shared_ptr<Shader::ShaderProgram> program = shaderManager.CreateProgramFromStages(programDesc);
        // The fade/overlay variant stays what it always was: a straight alpha blend that writes no
        // depth, regardless of the material's own mode. An object fading out is translucent even if
        // the material composites additively at full opacity.
        Shader::ShaderProgramStagesDesc blendedDesc = programDesc;
        blendedDesc.depthWriteEnabled = false;
        blendedDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
        // Tier 2.1: overlays pass at EQUAL depth so coplanar blends survive the unified LESS
        // default (Vulkan's old LESS_OR_EQUAL behavior, now opt-in per pipeline).
        blendedDesc.depthCompare = DepthCompare::LessOrEqual;
        blendedDesc.debugName = programDesc.debugName + " blended";
        const std::shared_ptr<Shader::ShaderProgram> blendedProgram =
            shaderManager.CreateProgramFromStages(blendedDesc);

        const auto insertResult = materialCache.emplace(
            guid, MaterialGpuResources{program,
                                          VertexSourceSupportsInstancing(vertexShaderAsset->sourceCode),
                                          material->vertexShader, material->fragmentShader,
                                          material->albedoTexture, material->normalTexture, material->aoTexture,
                                          material->metallicRoughnessTexture, material->emissiveTexture,
                                          *albedoTexture, *normalTexture, *aoTexture,
                                          *metallicRoughnessTexture, *emissiveTexture, blendedProgram});
        return &insertResult.first->second;
    }

    /// Renders one directional-light shadow depth pass. The caller records it only when a valid
    /// directional caster and linked shadow program exist; otherwise the main pass receives the
    /// zero-cascade sentinel and no shadow-map work is submitted. Draws every opaque item in the
    /// cascade queue (no additional light-frustum culling beyond queue extraction).
    /// Uploads shadow instance transforms. Only the model matrix, transposed - a depth-only pass
    /// has no normals, so the normal-matrix buffer the main pass fills is left untouched here.
    [[nodiscard]] bool UploadShadowInstanceTransforms(
        const std::vector<Math::Matrix4x4>& modelMatrices) {
        instanceTransposeStaging.clear();
        instanceTransposeStaging.reserve(modelMatrices.size());
        for (const Math::Matrix4x4& model : modelMatrices) {
            instanceTransposeStaging.push_back(Math::Transpose(model));
        }
        return renderDevice.UpdateBuffer(instanceTransformBuffer,
                                            std::as_bytes(std::span(instanceTransposeStaging)));
    }

    /// Records one depth-only shadow pass.
    ///
    /// `viewportOverride` confines an atlas tile pass to its own rectangle; `depthLoadOp` defaults
    /// to Clear because a standalone cascade map is the common case, and is Load for everything
    /// that shares the atlas. That distinction is load-bearing and not a style choice: the atlas is
    /// cleared ONCE per frame by a dedicated full-attachment pass, because a Vulkan attachment
    /// clear ignores the scissor and a per-tile Clear would wipe every tile rendered before it.
    [[nodiscard]] std::size_t RecordShadowPass(
        const std::vector<RenderItem>& items,
        const std::vector<ShadowPrimitiveDraw>& primitiveDraws,
        const Math::Matrix4x4& lightSpaceMatrix,
        TextureHandle shadowMapTarget, bool hasCaster, RenderBatchSet& batchSet,
        ICommandBuffer& commandBuffer, const ShadowPassCounters& counters,
        std::optional<ViewportRect> viewportOverride = std::nullopt,
        LoadOp depthLoadOp = LoadOp::Clear) {
        std::size_t drawCalls = 0U;
        RenderPassDesc passDesc;
        passDesc.colorAttachment = kInvalidTextureHandle;
        passDesc.depthAttachment = shadowMapTarget;
        passDesc.depthLoadOp = depthLoadOp;
        passDesc.clearDepth = 1.0F;
        passDesc.viewportOverride = viewportOverride;
        commandBuffer.BeginRenderPass(passDesc);

        if (hasCaster && shadowProgram->IsValid()) {
            // Instanced where possible. This pass matters more than its low profile suggests: it
            // runs ONCE PER CASCADE, so an uninstanced 200-object scene issued 600 shadow draws
            // against 200 main-pass ones - the shadow passes cost more than the frame they
            // shadow. It also batches better than the main pass does, because a depth-only draw
            // does not care about the material: two objects sharing a mesh batch together even
            // with completely different materials.
            bool meshItemsDrawnInstanced = false;
            const bool instancedShadows =
                instancedShadowProgram != nullptr && instancedShadowProgram->IsValid();
            if (instancedShadows) {
                BuildShadowBatches(items, batchSet);
                // Counted here rather than at the draw loop below, because what is being reported
                // is how well the ORDER batched - a batch that is later skipped for an unresolved
                // mesh still tells the truth about the merge.
                *counters.batchesRecorded += batchSet.batches.size();
                *counters.batchedItems += items.size();
                if (!batchSet.batches.empty() &&
                    batchSet.instanceMatrices.size() <= activeMaxInstancesPerFrame &&
                    EnsureInstanceBufferCapacity(batchSet.instanceMatrices.size()) &&
                    UploadShadowInstanceTransforms(batchSet.instanceMatrices)) {
                    instancedShadowProgram->SetMatrix4x4("uLightSpaceMatrix", lightSpaceMatrix);
                    for (const RenderBatch& batch : batchSet.batches) {
                        const MeshGpuResources& meshResources =
                            ResolveMeshGpuResources(items[batch.firstItem]);
                        if (!IsValidMeshGpuResources(meshResources)) {
                            continue;
                        }
                        const auto baseIndex = static_cast<std::int32_t>(batch.firstItem);
                        const std::span<const std::byte> baseBytes{
                            reinterpret_cast<const std::byte*>(&baseIndex), sizeof(baseIndex)};
                        if (!renderDevice.UpdateBuffer(instanceBaseBuffer, baseBytes)) {
                            continue;
                        }
                        instancedShadowProgram->ApplyTo(commandBuffer);
                        commandBuffer.BindStorageBuffer(instanceTransformBuffer, kInstanceTransformSlot);
                        commandBuffer.BindStorageBuffer(instanceBaseBuffer, kInstanceBaseSlot);
                        commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                        commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                        commandBuffer.DrawIndexed(
                            ResolveItemIndexCount(items[batch.firstItem], meshResources),
                            static_cast<std::uint32_t>(batch.itemCount),
                            ResolveItemFirstIndex(items[batch.firstItem]));
                        ++*counters.instancedDrawCalls;
                        ++drawCalls;
                    }
                    meshItemsDrawnInstanced = true;
                }
            }

            if (!meshItemsDrawnInstanced) {
                // Fallback: correct, just one draw per caster. Reached when the instanced program did
                // not link, or a buffer step failed.
                shadowProgram->SetMatrix4x4("uLightSpaceMatrix", lightSpaceMatrix);
                for (const RenderItem& item : items) {
                    const MeshGpuResources& meshResources = ResolveMeshGpuResources(item);
                    if (!IsValidMeshGpuResources(meshResources)) {
                        continue;
                    }
                    shadowProgram->SetMatrix4x4("uModel", item.worldMatrix);
                    shadowProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                    commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                    commandBuffer.DrawIndexed(ResolveItemIndexCount(item, meshResources), 1U,
                                                 ResolveItemFirstIndex(item));
                    ++drawCalls;
                }
            }

            if (!primitiveDraws.empty()) {
                if (meshItemsDrawnInstanced) {
                    shadowProgram->SetMatrix4x4("uLightSpaceMatrix", lightSpaceMatrix);
                }
                for (const ShadowPrimitiveDraw& primDraw : primitiveDraws) {
                    const MeshGpuResources& meshResources =
                        primDraw.skinned != nullptr ? *primDraw.skinned
                        : primDraw.mesh != nullptr  ? ResolveMeshGpuResources(primDraw.meshGuid, primDraw.mesh)
                                                    : ResolvePrimitiveMeshGpuResources(primDraw.kind);
                    if (!IsValidMeshGpuResources(meshResources)) {
                        continue;
                    }
                    shadowProgram->SetMatrix4x4("uModel", primDraw.worldMatrix);
                    shadowProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                    commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                    commandBuffer.DrawIndexed(meshResources.indexCount, 1U, 0U);
                    ++drawCalls;
                }
            }
        }

        commandBuffer.EndRenderPass();
        return drawCalls;
    }

    /// Builds this frame's visible primitive list.
    ///
    /// Split deliberately into two halves. Everything that depends only on the ENTITY - normalized
    /// rotation, world matrix, world bounds - is cached across frames and keyed on the transform
    /// that produced it. Everything that depends on the VIEW - the frustum test and the sort depth
    /// - is recomputed every frame, because the camera moves even when nothing in the scene does.
    /// Caching the view-dependent half would be a correctness bug, not an optimization.
    /// An imported mesh whose MeshInstance3D has no material yet is still drawn, with the built-in
    /// lit shader in a neutral grey - so a model brought in from a DCC tool shows up the moment it
    /// is assigned, instead of staying invisible until a material and its shaders are authored.
    /// Appended to the primitive items; call after ExtractPrimitiveItems and before sorting.
    /// Poses `mesh` with the nearest Skeleton3D above `entity` and uploads the result into that
    /// entity's own vertex buffer. Null when there is no skeleton, a joint has no bone of its name,
    /// or skinning fails - the mesh is then drawn in its bind pose. `outBounds` receives the posed
    /// mesh's local bounds, which the rest pose's no longer contain.
    [[nodiscard]] const MeshGpuResources* SkinForEntity(Scene::IEntityManager& entityManager,
                                                              const Scene::Entity entity,
                                                              const Asset::AssetGuid guid,
                                                              const Asset::MeshAsset& mesh, Math::Aabb& outBounds) {
        // Every return below leaves outBounds describing what will actually be drawn: the bind
        // pose's own bounds unless a skinned result replaces them, so a caller that gets nullptr
        // never inherits a half-written box.
        outBounds = mesh.localBounds;
        const Scene::Skeleton3DComponent* skeleton = nullptr;
        // Search entity itself, child, sibling, or parent
        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(entity)) {
            skeleton = &entityManager.GetComponent<Scene::Skeleton3DComponent>(entity);
        }
        if (skeleton == nullptr && !frameSkeletons.empty()) {
            const Scene::Entity entityParent =
                entityManager.HasComponent<Scene::HierarchyComponent>(entity)
                    ? entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent
                    : Scene::kInvalidEntity;
            for (const ActiveSkeletonEntry& entry : frameSkeletons) {
                if (entry.entity == entity || entry.parent == entity) {
                    skeleton = entry.component;
                    break;
                }
                if (entityParent != Scene::kInvalidEntity &&
                    (entry.parent == entityParent || entry.entity == entityParent)) {
                    skeleton = entry.component;
                    break;
                }
            }
        }
        if (skeleton == nullptr && !frameSkeletons.empty() &&
            entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
            // A mesh inside a group (Geo > Soldier_head) with the skeleton beside the group: the nearest
            // ancestor that has a skeleton as a child. Nearest first, so two characters in one scene each bind to
            // their own rig, and the far ancestors are only a last resort.
            Scene::Entity ancestor = entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent;
            for (std::size_t depth = 0U; depth < 64U && skeleton == nullptr && ancestor != Scene::kInvalidEntity; ++depth) {
                for (const ActiveSkeletonEntry& entry : frameSkeletons) {
                    if (entry.parent == ancestor) {
                        skeleton = entry.component;
                        break;
                    }
                }
                ancestor = entityManager.HasComponent<Scene::HierarchyComponent>(ancestor)
                               ? entityManager.GetComponent<Scene::HierarchyComponent>(ancestor).parent
                               : Scene::kInvalidEntity;
            }
        }
        if (skeleton == nullptr) {
            Scene::Entity cursor = entity;
            for (std::size_t depth = 0U; depth < 256U && cursor != Scene::kInvalidEntity; ++depth) {
                if (entityManager.HasComponent<Scene::Skeleton3DComponent>(cursor)) {
                    skeleton = &entityManager.GetComponent<Scene::Skeleton3DComponent>(cursor);
                    break;
                }
                cursor = entityManager.HasComponent<Scene::HierarchyComponent>(cursor)
                             ? entityManager.GetComponent<Scene::HierarchyComponent>(cursor).parent
                             : Scene::kInvalidEntity;
            }
        }
        if (skeleton == nullptr || !skeleton->enabled || skeleton->bones.empty()) {
            return nullptr;
        }
        const std::vector<Scene::SkeletonBonePose> pose = Scene::GetSkeletonCurrentPose(*skeleton);
        const MeshGpuResources& shared = ResolveMeshGpuResources(guid, &mesh);
        if (!IsValidMeshGpuResources(shared)) {
            return nullptr;
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index;
        SkinnedMeshGpu& entry = skinnedMeshCache[key];

        // Hash bone names to detect rig edits without rebuilding SkeletonBoneLookup every frame.
        std::uint64_t nameHash = 1469598103934665603ULL;
        for (const Scene::SkeletonBone& bone : skeleton->bones) {
            for (const unsigned char ch : bone.name) {
                nameHash = (nameHash ^ ch) * 1099511628211ULL;
            }
            nameHash = (nameHash ^ 0xFFU) * 1099511628211ULL;
        }
        constexpr std::uint32_t kUnmappedJointBone = std::numeric_limits<std::uint32_t>::max();
        if (entry.meshGuid != guid || entry.jointToBoneIndex.size() != mesh.joints.size() ||
            entry.cachedSkeletonBoneCount != skeleton->bones.size() ||
            entry.cachedSkeletonNameHash != nameHash) {
            Scene::SkeletonBoneLookup boneLookup;
            boneLookup.Build(*skeleton);
            entry.jointToBoneIndex.assign(mesh.joints.size(), kUnmappedJointBone);
            for (std::size_t jointIndex = 0U; jointIndex < mesh.joints.size(); ++jointIndex) {
                std::uint32_t foundIndex = 0U;
                if (boneLookup.TryFind(mesh.joints[jointIndex].name, foundIndex)) {
                    entry.jointToBoneIndex[jointIndex] = foundIndex;
                }
            }
            entry.cachedSkeletonBoneCount = skeleton->bones.size();
            entry.cachedSkeletonNameHash = nameHash;
        }

        auto& locals = skinLocalsScratch;
        locals.clear();
        locals.reserve(mesh.joints.size());
        for (std::size_t jointIndex = 0U; jointIndex < mesh.joints.size(); ++jointIndex) {
            const Asset::MeshJoint& joint = mesh.joints[jointIndex];
            const std::uint32_t foundIndex = entry.jointToBoneIndex[jointIndex];
            if (foundIndex == kUnmappedJointBone || foundIndex >= pose.size()) {
                Math::Matrix4x4 bind;
                Math::Matrix4x4 rest = Math::Matrix4x4::Identity();
                if (Math::TryInverse(joint.inverseBindMatrix, bind)) {
                    rest = bind;
                    if (joint.parentIndex != Asset::kInvalidJointParent && joint.parentIndex < jointIndex) {
                        rest = mesh.joints[joint.parentIndex].inverseBindMatrix * bind;
                    }
                }
                locals.push_back(rest);
                continue;
            }
            const Scene::SkeletonBonePose& local = pose[foundIndex];
            locals.push_back(Math::Matrix4x4::ComposeTrs(local.position, local.rotation, local.scale));
        }
        if (entry.resources.vertexBuffer.value != 0U && entry.meshGuid == guid &&
            entry.lastLocals.size() == locals.size() &&
            std::memcmp(entry.lastLocals.data(), locals.data(), locals.size() * sizeof(Math::Matrix4x4)) == 0) {
            entry.resources.indexBuffer = shared.indexBuffer;
            entry.resources.indexCount = shared.indexCount;
            entry.frame = skinnedFrame;
            outBounds = entry.localBounds;
            ++skinnedMeshesThisFrame;
            return &entry.resources;
        }
        auto& skinning = skinMatricesScratch;
        auto& vertices = skinVerticesScratch;
        skinning.clear();
        vertices.clear();
        if (!Asset::TryResolvePose(mesh.joints, locals, skinning)) {
            return nullptr;
        }
        // Reconcile 100x / 0.01x FBX unit-scale mismatches between the skeleton pose and the mesh's
        // inverseBindMatrix (e.g. a unit-scale RigFromMesh skeleton on a cm-scaled FBX bind matrix,
        // or a cm-scaled FBX animation clip playing on a conformed unit-scale mesh).
        auto& modelSpace = skinModelSpaceScratch;
        modelSpace.clear();
        for (std::size_t index = 0U; index < skinning.size(); ++index) {
            const Math::Matrix4x4& sk = skinning[index];
            const float skinScale =
                std::sqrt(sk.m[0][0] * sk.m[0][0] + sk.m[1][0] * sk.m[1][0] + sk.m[2][0] * sk.m[2][0]);
            if (skinScale > 1e-6F && (skinScale < 0.1F || skinScale > 10.0F)) {
                if (modelSpace.empty()) {
                    modelSpace.reserve(mesh.joints.size());
                    for (std::size_t j = 0U; j < mesh.joints.size(); ++j) {
                        const std::uint32_t parent = mesh.joints[j].parentIndex;
                        // A parent at or after its child (or out of range) is a malformed table: treat the
                        // joint as a root rather than indexing past what has been built so far.
                        modelSpace.push_back(parent == Asset::kInvalidJointParent || parent >= j
                                                 ? locals[j]
                                                 : modelSpace[parent] * locals[j]);
                    }
                }
                Math::Matrix4x4 correctedModel = modelSpace[index];
                const float invScale = 1.0F / skinScale;
                for (std::size_t row = 0U; row < 3U; ++row) {
                    for (std::size_t col = 0U; col < 4U; ++col) {
                        correctedModel.m[row][col] *= invScale;
                    }
                }
                skinning[index] = correctedModel * mesh.joints[index].inverseBindMatrix;
            }
        }
        if (!Asset::TrySkinMesh(mesh, skinning, vertices) || vertices.empty()) {
            return nullptr;
        }
        Math::Aabb skinnedBounds{vertices.front().position, vertices.front().position};
        bool verticesFinite = true;
        for (const Asset::MeshVertex& vertex : vertices) {
            if (!Math::IsFinite(vertex.position)) {
                verticesFinite = false;
                break;
            }
            skinnedBounds.min = Math::Vector3{std::min(skinnedBounds.min.x, vertex.position.x),
                                                 std::min(skinnedBounds.min.y, vertex.position.y),
                                                 std::min(skinnedBounds.min.z, vertex.position.z)};
            skinnedBounds.max = Math::Vector3{std::max(skinnedBounds.max.x, vertex.position.x),
                                                 std::max(skinnedBounds.max.y, vertex.position.y),
                                                 std::max(skinnedBounds.max.z, vertex.position.z)};
        }
        // How far the character's root joint travelled, and how big the whole rig is (every joint's
        // bind position), so a small part is judged as part of the character.
        Math::Vector3 travel{};
        float characterDiagonal = 0.0F;
        {
            Math::Aabb rig{};
            bool any = false;
            for (std::size_t j = 0U; j < mesh.joints.size() && j < skinning.size(); ++j) {
                Math::Matrix4x4 bind;
                if (!Math::TryInverse(mesh.joints[j].inverseBindMatrix, bind)) {
                    continue;
                }
                const Math::Vector3 at = Math::TransformPoint(bind, Math::Vector3{});
                if (!any) {
                    rig = Math::Aabb{at, at};
                    travel = Math::TransformPoint(skinning[j], at) - at;   // the first placed joint: the root
                    any = true;
                } else {
                    rig.min = Math::Vector3{std::min(rig.min.x, at.x), std::min(rig.min.y, at.y), std::min(rig.min.z, at.z)};
                    rig.max = Math::Vector3{std::max(rig.max.x, at.x), std::max(rig.max.y, at.y), std::max(rig.max.z, at.z)};
                }
            }
            if (any) {
                characterDiagonal = Math::Length(rig.max - rig.min);
            }
        }
        if (!verticesFinite || !IsPlausibleSkinnedBounds(skinnedBounds, mesh.localBounds, travel, characterDiagonal)) {
            // NaN / infinite vertices, or a pose whose scale or translation disagrees with the mesh's
            // bind matrices and flings the skin far from (or collapses it onto) the model. Uploading
            // that buffer is what made a character vanish a frame after it first appeared, leaving only
            // its bones on screen. The bind pose is always a valid picture of the model, so draw that.
            outBounds = mesh.localBounds;
            return nullptr;
        }
        outBounds = skinnedBounds;

        const std::span<const std::byte> bytes = std::as_bytes(std::span<const Asset::MeshVertex>(vertices));
        if (entry.resources.vertexBuffer.value == 0U || entry.vertexBytes != bytes.size()) {
            DestroyBufferIfValid(renderDevice, entry.resources.vertexBuffer);
            entry.resources.vertexBuffer =
                renderDevice.CreateBuffer(BufferDesc{bytes.size(), BufferUsage::Vertex,
                                                     BufferMemory::Auto, BufferUpdateFrequency::Dynamic},
                                          bytes);
            entry.vertexBytes = bytes.size();
        } else if (!renderDevice.UpdateBuffer(entry.resources.vertexBuffer, bytes)) {
            return nullptr;
        }
        entry.resources.indexBuffer = shared.indexBuffer;
        entry.resources.indexCount = shared.indexCount;
        entry.frame = skinnedFrame;
        entry.meshGuid = guid;
        entry.lastLocals = locals;
        entry.localBounds = outBounds;
        if (!IsValidMeshGpuResources(entry.resources)) {
            return nullptr;
        }
        ++skinnedMeshesThisFrame;
        return &entry.resources;
    }

    /// `frustumCullingFrozen` is this frame's debug-freeze verdict from RenderFrame: when true the
    /// frustum rejection below is skipped so off-screen meshes are still submitted. Occlusion, layer,
    /// hierarchy-visibility and partition verdicts are separate systems and keep running.
    void ExtractUnmaterialedMeshItems(Scene::IEntityManager& entityManager, const Math::Frustum& frustum,
                                          const Math::Vector3& viewPosition, bool frustumCullingFrozen,
                                          std::vector<PrimitiveRenderItem>& outItems) {
        static constexpr Math::Vector3 kUnmaterialedColor{0.72F, 0.72F, 0.74F};
        std::unordered_map<Asset::AssetGuid, bool> named;
        if (!probeCaptureViewActive) {
            // Skinned buffers nobody drew last frame belong to entities that are gone or hidden.
            for (auto it = skinnedMeshCache.begin(); it != skinnedMeshCache.end();) {
                if (it->second.frame != skinnedFrame) {
                    DestroyBufferIfValid(renderDevice, it->second.resources.vertexBuffer);
                    it = skinnedMeshCache.erase(it);
                } else {
                    ++it;
                }
            }
            ++skinnedFrame;
            skinnedMeshesThisFrame = 0U;
        }
        frameSkeletons.clear();
        entityManager.ForEach<Scene::Skeleton3DComponent>(
            [&](const Scene::Entity skelEntity, const Scene::Skeleton3DComponent& skelComp) {
                if (!skelComp.enabled || skelComp.bones.empty()) {
                    return;
                }
                const Scene::Entity parent =
                    entityManager.HasComponent<Scene::HierarchyComponent>(skelEntity)
                        ? entityManager.GetComponent<Scene::HierarchyComponent>(skelEntity).parent
                        : Scene::kInvalidEntity;
                frameSkeletons.push_back(ActiveSkeletonEntry{skelEntity, parent, &skelComp});
            });
        std::vector<Scene::Occluder3DSnapshot> occluders;
        Scene::CollectOccluder3DSnapshots(entityManager, occluders);
        entityManager.ForEach<Scene::WorldTransformComponent, Scene::MeshComponent>(
            [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                const Scene::MeshComponent& meshComponent) {
                if (meshComponent.meshGuid == Asset::kInvalidAssetGuid || worldTransform.dirty ||
                    IsHiddenInHierarchy(entityManager, entity) ||
                    Scene::IsWorldPartition3DDrawHidden(entityManager, entity) ||
                    Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                    return;
                }
                if (probeCaptureViewActive) {
                    const std::uint32_t entityLayers =
                        entityManager.HasComponent<Scene::RenderInstanceComponent>(entity)
                            ? entityManager.GetComponent<Scene::RenderInstanceComponent>(entity).renderLayers
                            : meshComponent.visibilityLayers;
                    if (!Scene::IsRenderInstance3DOnViewLayers(entityLayers, probeCaptureVisibilityLayers)) {
                        return;
                    }
                }
                named[meshComponent.meshGuid] = true;
                auto handleIt = unmaterialedMeshHandles.find(meshComponent.meshGuid);
                if (handleIt == unmaterialedMeshHandles.end()) {
                    handleIt = unmaterialedMeshHandles
                                   .emplace(meshComponent.meshGuid, assetManager.Load<Asset::MeshAsset>(
                                                                        meshComponent.meshGuid, assetDatabase))
                                   .first;
                }
                const Asset::MeshAsset* const mesh = handleIt->second.TryGet();
                if (mesh == nullptr || mesh->indices.empty() || !IsOrderedFiniteAabb(mesh->localBounds) ||
                    !Math::IsFinite(worldTransform.worldPosition) || !Math::IsFinite(worldTransform.worldScale)) {
                    return;
                }
                const bool isSkinned = mesh->IsSkinned();
                if (!isSkinned) {
                    if (meshComponent.materialGuid != Asset::kInvalidAssetGuid) {
                        return;
                    }
                    const Asset::AssetGuid meshOwnMaterial = mesh->FirstSurfaceMaterialGuid();
                    if (meshOwnMaterial != Asset::kInvalidAssetGuid &&
                        !assetDatabase.Resolve(meshOwnMaterial).empty()) {
                        return;
                    }
                }
                Math::Quaternion rotation;
                if (!Math::TryNormalize(worldTransform.worldRotation, rotation)) {
                    return;
                }
                const Math::Matrix4x4 worldMatrix =
                    Math::Matrix4x4::ComposeTrs(worldTransform.worldPosition, rotation, worldTransform.worldScale);
                if (!IsFiniteMatrix(worldMatrix)) {
                    return;
                }
                Math::Aabb localBounds = mesh->localBounds;
                const MeshGpuResources* const skinned =
                    isSkinned
                        ? SkinForEntity(entityManager, entity, meshComponent.meshGuid, *mesh, localBounds)
                        : nullptr;
                if (skinned != nullptr && IsOrderedFiniteAabb(localBounds)) {
                    // The posed bounds, grown to keep the bind pose's: the culling box then never
                    // shrinks away from where the model was authored, whatever the pose does.
                    localBounds.min = Math::Vector3{std::min(localBounds.min.x, mesh->localBounds.min.x),
                                                       std::min(localBounds.min.y, mesh->localBounds.min.y),
                                                       std::min(localBounds.min.z, mesh->localBounds.min.z)};
                    localBounds.max = Math::Vector3{std::max(localBounds.max.x, mesh->localBounds.max.x),
                                                       std::max(localBounds.max.y, mesh->localBounds.max.y),
                                                       std::max(localBounds.max.z, mesh->localBounds.max.z)};
                } else {
                    // Drawn in bind pose (no skeleton yet, or the pose was rejected as implausible): the
                    // bind pose's own bounds are exactly the geometry on screen.
                    localBounds = mesh->localBounds;
                }
                Math::Aabb worldBounds = localBounds.Transform(worldMatrix);
                if (!IsOrderedFiniteAabb(worldBounds)) {
                    worldBounds = mesh->localBounds.Transform(worldMatrix);
                }

                const Scene::SurfaceInstanceComponent* surfaceInstance =
                    entityManager.HasComponent<Scene::SurfaceInstanceComponent>(entity)
                        ? &entityManager.GetComponent<Scene::SurfaceInstanceComponent>(entity)
                        : nullptr;

                float opacity = 1.0F;
                if (surfaceInstance != nullptr) {
                    if (surfaceInstance->extraCullMargin > 0.0F) {
                        worldBounds.min -= Math::Vector3{surfaceInstance->extraCullMargin, surfaceInstance->extraCullMargin, surfaceInstance->extraCullMargin};
                        worldBounds.max += Math::Vector3{surfaceInstance->extraCullMargin, surfaceInstance->extraCullMargin, surfaceInstance->extraCullMargin};
                    }
                    const float cameraDistance = Math::Length(worldTransform.worldPosition - viewPosition);
                    if (Scene::IsSurfaceInstance3DOutsideVisibilityRange(*surfaceInstance, cameraDistance) ||
                        !Scene::SurfaceInstance3DDrawsInView(*surfaceInstance)) {
                        return;
                    }
                    opacity = Scene::SurfaceInstance3DOpacity(*surfaceInstance) *
                              Scene::SurfaceInstance3DVisibilityFadeWeight(*surfaceInstance, cameraDistance);
                    if (opacity <= 0.0F) {
                        return;
                    }
                }

                if (!IsOrderedFiniteAabb(worldBounds) ||
                    (!frustumCullingFrozen && !frustum.Intersects(worldBounds)) ||
                    (!(surfaceInstance != nullptr && surfaceInstance->ignoreOcclusionCulling) &&
                     Scene::IsOccluder3DAabbDrawHidden(occluders, viewPosition, worldBounds))) {
                    return;
                }
                const float sortDepth = frustum.planes[4U].GetSignedDistance(worldBounds.GetCenter());
                if (!std::isfinite(sortDepth)) {
                    return;
                }

                const std::uint32_t renderLayers =
                    entityManager.HasComponent<Scene::RenderInstanceComponent>(entity)
                        ? entityManager.GetComponent<Scene::RenderInstanceComponent>(entity).renderLayers
                        : meshComponent.visibilityLayers;

                Asset::AssetGuid objectMaterialGuid = meshComponent.materialGuid;
                if (surfaceInstance != nullptr && !surfaceInstance->materialOverridePath.empty()) {
                    const Asset::AssetGuid overrideGuid = assetDatabase.Register(surfaceInstance->materialOverridePath);
                    if (overrideGuid != Asset::kInvalidAssetGuid) {
                        objectMaterialGuid = overrideGuid;
                    }
                }

                const std::size_t surfaceCount = mesh->SurfaceCount();
                for (std::size_t surfaceIndex = 0U; surfaceIndex < surfaceCount; ++surfaceIndex) {
                    const Asset::MeshSurface surface = mesh->GetSurface(surfaceIndex);
                    Asset::AssetGuid materialGuid = objectMaterialGuid;
                    if (materialGuid == Asset::kInvalidAssetGuid && surfaceInstance != nullptr &&
                        surfaceIndex < surfaceInstance->surfaceMaterialOverridePaths.size() &&
                        !surfaceInstance->surfaceMaterialOverridePaths[surfaceIndex].empty()) {
                        materialGuid = assetDatabase.Register(surfaceInstance->surfaceMaterialOverridePaths[surfaceIndex]);
                    }
                    if (materialGuid == Asset::kInvalidAssetGuid) {
                        materialGuid = mesh->SurfaceMaterialGuid(surfaceIndex);
                    }
                    if (materialGuid != Asset::kInvalidAssetGuid) {
                        if (assetDatabase.Resolve(materialGuid).empty()) {
                            materialGuid = Asset::kInvalidAssetGuid;
                        } else {
                            namedPrimitiveMaterials[materialGuid] = true;
                            auto materialIt = primitiveMaterialHandles.find(materialGuid);
                            if (materialIt == primitiveMaterialHandles.end()) {
                                materialIt = primitiveMaterialHandles
                                                 .emplace(materialGuid, assetManager.Load<Asset::MaterialAsset>(
                                                                            materialGuid, assetDatabase))
                                                 .first;
                            }
                            if (materialIt->second.TryGet() == nullptr) {
                                if (materialIt->second.HasFailed()) {
                                    ++failedPrimitiveMaterials;
                                } else {
                                    ++pendingPrimitiveMaterials;
                                }
                            }
                        }
                    }

                    PrimitiveRenderItem item{worldMatrix, Scene::PrimitiveMeshKind::Cube, kUnmaterialedColor,
                                                sortDepth, opacity};
                    item.meshGuid = meshComponent.meshGuid;
                    item.mesh = mesh;
                    item.skinned = skinned;
                    item.materialGuid = materialGuid;
                    item.renderLayers = renderLayers;
                    item.firstIndex = surface.firstIndex;
                    item.indexCount = surface.indexCount;
                    outItems.push_back(item);
                }
            });
        for (auto it = unmaterialedMeshHandles.begin(); it != unmaterialedMeshHandles.end();) {
            if (named.find(it->first) == named.end()) {
                it = unmaterialedMeshHandles.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = primitiveMaterialHandles.begin(); it != primitiveMaterialHandles.end();) {
            if (namedPrimitiveMaterials.find(it->first) == namedPrimitiveMaterials.end()) {
                it = primitiveMaterialHandles.erase(it);
            } else {
                ++it;
            }
        }
        lastFrameDiagnostics.primitiveMaterialPending = pendingPrimitiveMaterials;
        lastFrameDiagnostics.primitiveMaterialFailed = failedPrimitiveMaterials;
        std::sort(outItems.begin(), outItems.end(),
                  [](const PrimitiveRenderItem& lhs, const PrimitiveRenderItem& rhs) {
                      const bool lhsTransparent = lhs.opacity < 1.0F;
                      const bool rhsTransparent = rhs.opacity < 1.0F;
                      if (lhsTransparent != rhsTransparent) {
                          return !lhsTransparent;
                      }
                      return lhs.sortDepth < rhs.sortDepth;
                  });
    }

    /// `frustumCullingFrozen` is this frame's debug-freeze verdict from RenderFrame, honoured by
    /// the per-item frustum test below; see ExtractUnmaterialedMeshItems for what stays enabled.
    void ExtractPrimitiveItems(Scene::IEntityManager& entityManager, const Math::Frustum& frustum,
                                   const Math::Vector3& viewPosition, bool frustumCullingFrozen,
                                   std::vector<PrimitiveRenderItem>& outItems) {
        outItems.clear();
        namedPrimitiveMaterials.clear();
        pendingPrimitiveMaterials = 0U;
        failedPrimitiveMaterials = 0U;
        ++primitiveFrameIndex;
        std::vector<Scene::Occluder3DSnapshot> occluders;
        Scene::CollectOccluder3DSnapshots(entityManager, occluders);
        entityManager.ForEach<Scene::WorldTransformComponent, Scene::PrimitiveMeshComponent>(
            [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                const Scene::PrimitiveMeshComponent& primitive) {
                if (worldTransform.dirty || !Scene::IsPrimitiveMeshComponentValid(primitive) ||
                    IsHiddenInHierarchy(entityManager, entity) ||
                    Scene::IsWorldPartition3DDrawHidden(entityManager, entity) ||
                    Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                    // Returning before the cache is touched leaves any existing entry unstamped,
                    // so an entity that stays dirty or invalid is pruned rather than kept alive by
                    // a placement nobody can use.
                    return;
                }
                // Read once and reused: the probe view test and the shader's own reflection-probe
                // layer filter must see the same layers, and an entity without a RenderInstance
                // sits on layer 0 (value 1), exactly as RenderItem::renderLayers defaults.
                const std::uint32_t renderLayers =
                    entityManager.HasComponent<Scene::RenderInstanceComponent>(entity)
                        ? entityManager.GetComponent<Scene::RenderInstanceComponent>(entity).renderLayers
                        : 1U;
                if (probeCaptureViewActive) {
                    if (!Scene::IsRenderInstance3DOnViewLayers(renderLayers, probeCaptureVisibilityLayers)) {
                        return;
                    }
                }

                const Scene::SurfaceInstanceComponent* surface = nullptr;
                if (entityManager.HasComponent<Scene::SurfaceInstanceComponent>(entity)) {
                    const Scene::SurfaceInstanceComponent& surfaceComponent =
                        entityManager.GetComponent<Scene::SurfaceInstanceComponent>(entity);
                    if (!Scene::IsSurfaceInstanceComponentValid(surfaceComponent)) {
                        return;
                    }
                    surface = &surfaceComponent;
                }

                ++lastFrameDiagnostics.primitiveCandidates;

                const PrimitivePlacementKey key{worldTransform.worldPosition, worldTransform.worldRotation,
                                                   worldTransform.worldScale, primitive.kind};
                PrimitivePlacementCacheEntry& cacheEntry = primitivePlacementCache[entity];
                if (cacheEntry.lastSeenFrame != 0U && cacheEntry.key.Matches(key)) {
                    ++lastFrameDiagnostics.primitivePlacementCacheHits;
                } else {
                    ++lastFrameDiagnostics.primitivePlacementCacheMisses;
                    cacheEntry.key = key;
                    cacheEntry.placed = TryComputePrimitivePlacement(worldTransform, primitive,
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

                // Resolved here rather than at draw time for the same reason the mesh path resolves
                // at extraction: the handle owns the reference that keeps the payload alive until
                // the colour pass consumes it.
                Asset::AssetGuid materialGuid = Asset::kInvalidAssetGuid;
                // A MeshInstance3D drawing a built-in shape keeps its material on the mesh slot (the
                // one its Details show); the shape's own slot wins when both are set.
                Asset::AssetGuid chosenMaterial = primitive.materialGuid;
                if (chosenMaterial == Asset::kInvalidAssetGuid && entityManager.HasComponent<Scene::MeshComponent>(entity)) {
                    chosenMaterial = entityManager.GetComponent<Scene::MeshComponent>(entity).materialGuid;
                }
                if (chosenMaterial != Asset::kInvalidAssetGuid) {
                    materialGuid = chosenMaterial;
                    namedPrimitiveMaterials[materialGuid] = true;
                    auto materialIt = primitiveMaterialHandles.find(materialGuid);
                    if (materialIt == primitiveMaterialHandles.end()) {
                        materialIt =
                            primitiveMaterialHandles
                                .emplace(materialGuid, assetManager.Load<Asset::MaterialAsset>(
                                                           materialGuid, assetDatabase))
                                .first;
                    }
                    if (materialIt->second.TryGet() == nullptr) {
                        // No silent fallback to the default material: a primitive whose material is
                        // still loading would flash white and then pop into its real look. Skipping
                        // is what the mesh path does for the same reason.
                        if (materialIt->second.HasFailed()) {
                            ++failedPrimitiveMaterials;
                        } else {
                            ++pendingPrimitiveMaterials;
                        }
                        return;
                    }
                }

                float opacity = 1.0F;
                if (surface != nullptr) {
                    const float cameraDistance = Math::Length(worldTransform.worldPosition - viewPosition);
                    if (Scene::IsSurfaceInstance3DOutsideVisibilityRange(*surface, cameraDistance) ||
                        !Scene::SurfaceInstance3DDrawsInView(*surface)) {
                        return;
                    }
                    opacity = Scene::SurfaceInstance3DOpacity(*surface) *
                              Scene::SurfaceInstance3DVisibilityFadeWeight(*surface, cameraDistance);
                    if (opacity <= 0.0F) {
                        return;
                    }
                }

                // View-dependent from here down. Never cached. Extra cull grows the tests, not the
                // placement: a shader that moves vertices still sits at the same authored pose.
                Math::Aabb cullBounds = cacheEntry.worldBounds;
                if (surface != nullptr) {
                    Scene::ExpandSurfaceInstance3DCullBounds(*surface, cullBounds);
                }
                // Same freeze the mesh path honours, read from the same setting: primitives and
                // surface instances are drawn by the colour view too, so a frozen frame that kept
                // rejecting them would only be half frozen.
                if (!frustumCullingFrozen && !frustum.Intersects(cullBounds)) {
                    return;
                }
                if (!(surface != nullptr && surface->ignoreOcclusionCulling) &&
                    Scene::IsOccluder3DAabbDrawHidden(occluders, viewPosition, cullBounds)) {
                    return;
                }
                const float sortDepth = frustum.planes[4U].GetSignedDistance(cacheEntry.worldBounds.GetCenter());
                if (!std::isfinite(sortDepth)) {
                    return;
                }
                PrimitiveRenderItem item{cacheEntry.worldMatrix, primitive.kind,
                                            primitive.baseColor, sortDepth, opacity};
                item.materialGuid = materialGuid;
                item.renderLayers = renderLayers;
                outItems.push_back(item);
            });

        // Erase-while-iterating over an unordered_map, safe for the erased element only, so the
        // iterator is advanced before the erase and never after. Mirrors
        // MeshVisibilitySet::PruneUnseenPlacements.
        for (auto it = primitivePlacementCache.begin(); it != primitivePlacementCache.end();) {
            if (it->second.lastSeenFrame != primitiveFrameIndex) {
                it = primitivePlacementCache.erase(it);
            } else {
                ++it;
            }
        }

        std::sort(outItems.begin(), outItems.end(),
                  [](const PrimitiveRenderItem& lhs, const PrimitiveRenderItem& rhs) {
                      const bool lhsTransparent = lhs.opacity < 1.0F;
                      const bool rhsTransparent = rhs.opacity < 1.0F;
                      if (lhsTransparent != rhsTransparent) {
                          return !lhsTransparent;
                      }
                      return lhs.sortDepth < rhs.sortDepth;
                  });
    }

    /// The entity-dependent half of primitive extraction, factored out so the cache stores the
    /// result of exactly one function and the miss path cannot drift from what the key promises.
    /// Returns false for any input that cannot produce finite geometry; the caller caches that
    /// rejection rather than rediscovering it every frame.
    [[nodiscard]] static bool TryComputePrimitivePlacement(const Scene::WorldTransformComponent& worldTransform,
                                                              const Scene::PrimitiveMeshComponent& primitive,
                                                              Math::Matrix4x4& outWorldMatrix,
                                                              Math::Aabb& outWorldBounds) {
        if (!Math::IsFinite(worldTransform.worldPosition) || !Math::IsFinite(worldTransform.worldScale) ||
            !Math::IsFinite(worldTransform.worldRotation)) {
            return false;
        }
        Math::Quaternion normalizedRotation;
        if (!Math::TryNormalize(worldTransform.worldRotation, normalizedRotation)) {
            return false;
        }
        const PrimitiveGeometry& geometry = GetPrimitiveGeometry(primitive.kind);
        if (!IsOrderedFiniteAabb(geometry.localBounds)) {
            return false;
        }
        outWorldMatrix = Math::Matrix4x4::ComposeTrs(worldTransform.worldPosition, normalizedRotation,
                                                           worldTransform.worldScale);
        if (!IsFiniteMatrix(outWorldMatrix)) {
            return false;
        }
        outWorldBounds = geometry.localBounds.Transform(outWorldMatrix);
        return IsOrderedFiniteAabb(outWorldBounds);
    }

    /// Collects shadow-casting built-in primitives, unmaterialed MeshInstance3D objects, and skinned
    /// meshes (Character3D / Player3D) for a shadow cascade or shadow atlas tile frustum without
    /// perturbing main-view primitive placement cache or diagnostic counters.
    void CollectShadowPrimitiveDraws(Scene::IEntityManager& entityManager,
                                        const Math::Frustum& lightFrustum,
                                        std::vector<ShadowPrimitiveDraw>& outDraws) {
        outDraws.clear();
        entityManager.ForEach<Scene::WorldTransformComponent, Scene::PrimitiveMeshComponent>(
            [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                const Scene::PrimitiveMeshComponent& primitive) {
                if (worldTransform.dirty || !Scene::IsPrimitiveMeshComponentValid(primitive) ||
                    IsHiddenInHierarchy(entityManager, entity) ||
                    Scene::IsWorldPartition3DDrawHidden(entityManager, entity) ||
                    Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                    return;
                }
                const Scene::SurfaceInstanceComponent* surface = nullptr;
                if (entityManager.HasComponent<Scene::SurfaceInstanceComponent>(entity)) {
                    const Scene::SurfaceInstanceComponent& surfaceComponent =
                        entityManager.GetComponent<Scene::SurfaceInstanceComponent>(entity);
                    if (!Scene::IsSurfaceInstanceComponentValid(surfaceComponent) ||
                        !Scene::SurfaceInstance3DCastsShadow(surfaceComponent) ||
                        Scene::SurfaceInstance3DOpacity(surfaceComponent) <= 0.0F) {
                        return;
                    }
                    surface = &surfaceComponent;
                }
                Math::Matrix4x4 worldMatrix{};
                Math::Aabb worldBounds{};
                if (!TryComputePrimitivePlacement(worldTransform, primitive, worldMatrix, worldBounds)) {
                    return;
                }
                if (surface != nullptr) {
                    Scene::ExpandSurfaceInstance3DCullBounds(*surface, worldBounds);
                }
                if (!lightFrustum.Intersects(worldBounds)) {
                    return;
                }
                outDraws.push_back(ShadowPrimitiveDraw{
                    worldMatrix, primitive.kind, Asset::kInvalidAssetGuid, nullptr, nullptr});
            });

        entityManager.ForEach<Scene::WorldTransformComponent, Scene::MeshComponent>(
            [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                const Scene::MeshComponent& meshComponent) {
                if (meshComponent.meshGuid == Asset::kInvalidAssetGuid || worldTransform.dirty ||
                    IsHiddenInHierarchy(entityManager, entity) ||
                    Scene::IsWorldPartition3DDrawHidden(entityManager, entity) ||
                    Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                    return;
                }
                const auto handleIt = unmaterialedMeshHandles.find(meshComponent.meshGuid);
                if (handleIt == unmaterialedMeshHandles.end()) {
                    return;
                }
                const Asset::MeshAsset* const mesh = handleIt->second.TryGet();
                if (mesh == nullptr || mesh->indices.empty() || !IsOrderedFiniteAabb(mesh->localBounds) ||
                    !Math::IsFinite(worldTransform.worldPosition) || !Math::IsFinite(worldTransform.worldScale)) {
                    return;
                }
                const bool isSkinned = mesh->IsSkinned();
                if (!isSkinned) {
                    if (meshComponent.materialGuid != Asset::kInvalidAssetGuid) {
                        return;
                    }
                    const Asset::AssetGuid meshOwnMaterial = mesh->FirstSurfaceMaterialGuid();
                    if (meshOwnMaterial != Asset::kInvalidAssetGuid &&
                        !assetDatabase.Resolve(meshOwnMaterial).empty()) {
                        return;
                    }
                }
                if (entityManager.HasComponent<Scene::SurfaceInstanceComponent>(entity)) {
                    const Scene::SurfaceInstanceComponent& surfaceComponent =
                        entityManager.GetComponent<Scene::SurfaceInstanceComponent>(entity);
                    if (!Scene::IsSurfaceInstanceComponentValid(surfaceComponent) ||
                        !Scene::SurfaceInstance3DCastsShadow(surfaceComponent) ||
                        Scene::SurfaceInstance3DOpacity(surfaceComponent) <= 0.0F) {
                        return;
                    }
                }
                Math::Quaternion rotation;
                if (!Math::TryNormalize(worldTransform.worldRotation, rotation)) {
                    return;
                }
                const Math::Matrix4x4 worldMatrix =
                    Math::Matrix4x4::ComposeTrs(worldTransform.worldPosition, rotation, worldTransform.worldScale);
                if (!IsFiniteMatrix(worldMatrix)) {
                    return;
                }
                Math::Aabb localBounds = mesh->localBounds;
                const MeshGpuResources* skinned = nullptr;
                if (isSkinned) {
                    const std::uint64_t key =
                        (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index;
                    const auto skinnedIt = skinnedMeshCache.find(key);
                    if (skinnedIt != skinnedMeshCache.end() &&
                        skinnedIt->second.resources.vertexBuffer.value != 0U &&
                        skinnedIt->second.meshGuid == meshComponent.meshGuid) {
                        skinned = &skinnedIt->second.resources;
                        if (IsOrderedFiniteAabb(skinnedIt->second.localBounds)) {
                            localBounds.min = Math::Vector3{
                                std::min(skinnedIt->second.localBounds.min.x, mesh->localBounds.min.x),
                                std::min(skinnedIt->second.localBounds.min.y, mesh->localBounds.min.y),
                                std::min(skinnedIt->second.localBounds.min.z, mesh->localBounds.min.z)};
                            localBounds.max = Math::Vector3{
                                std::max(skinnedIt->second.localBounds.max.x, mesh->localBounds.max.x),
                                std::max(skinnedIt->second.localBounds.max.y, mesh->localBounds.max.y),
                                std::max(skinnedIt->second.localBounds.max.z, mesh->localBounds.max.z)};
                        }
                    }
                }
                Math::Aabb worldBounds = localBounds.Transform(worldMatrix);
                if (!IsOrderedFiniteAabb(worldBounds)) {
                    worldBounds = mesh->localBounds.Transform(worldMatrix);
                }
                if (!IsOrderedFiniteAabb(worldBounds) || !lightFrustum.Intersects(worldBounds)) {
                    return;
                }
                outDraws.push_back(ShadowPrimitiveDraw{
                    worldMatrix, Scene::PrimitiveMeshKind::Cube, meshComponent.meshGuid, mesh, skinned});
            });
    }

    /// transpose(inverse(model)), falling back to the model matrix itself when it is singular -
    /// the same rule the per-object path has always used, factored out so the instanced path
    /// cannot quietly adopt a different one. A singular world matrix means the item would not
    /// project to visible geometry anyway.
    /// The computation is honestly 3x3 (normals are directions, so translation must not reach
    /// them); callers embed the result in a 4x4 for the mat4-only uniform/buffer upload.
    [[nodiscard]] static Math::Matrix3x3 ComputeNormalMatrix(const Math::Matrix4x4& worldMatrix) {
        Math::Matrix3x3 inverseUpper{};
        if (Math::TryInverse(Math::ToMatrix3x3(worldMatrix), inverseUpper)) {
            return Math::Transpose(inverseUpper);
        }
        return Math::ToMatrix3x3(worldMatrix);
    }

    /// Grows the three instancing buffers to hold `instanceCount` transforms. Returns false if any
    /// allocation fails, in which case the caller records per-object instead of instanced.
    [[nodiscard]] bool EnsureInstanceBufferCapacity(const std::size_t instanceCount) {
        if (instanceBaseBuffer == kInvalidBufferHandle) {
            instanceBaseBuffer = renderDevice.CreateBuffer(
                BufferDesc{sizeof(std::int32_t), BufferUsage::Storage, BufferMemory::Auto,
                           BufferUpdateFrequency::Dynamic});
            if (instanceBaseBuffer == kInvalidBufferHandle) {
                return false;
            }
        }
        if (instanceTransformBuffer != kInvalidBufferHandle && instanceBufferCapacity >= instanceCount) {
            return true;
        }
        for (BufferHandle* const buffer : {&instanceTransformBuffer, &instanceNormalTransformBuffer}) {
            if (*buffer != kInvalidBufferHandle) {
                renderDevice.DestroyBuffer(*buffer);
                *buffer = kInvalidBufferHandle;
            }
        }
        instanceBufferCapacity = 0U;
        const BufferDesc desc{instanceCount * sizeof(Math::Matrix4x4), BufferUsage::Storage,
                              BufferMemory::Auto, BufferUpdateFrequency::Dynamic};
        instanceTransformBuffer = renderDevice.CreateBuffer(desc);
        instanceNormalTransformBuffer = renderDevice.CreateBuffer(desc);
        if (instanceTransformBuffer == kInvalidBufferHandle ||
            instanceNormalTransformBuffer == kInvalidBufferHandle) {
            return false;
        }
        instanceBufferCapacity = instanceCount;
        return true;
    }

    /// Uploads one frame's instance transforms, TRANSPOSED - Matrix4x4 is row-major and std430
    /// mat4 is column-major. Same convention as mesh_skin.glsl, deliberately: one rule, not two.
    [[nodiscard]] bool UploadInstanceTransforms(const std::vector<Math::Matrix4x4>& modelMatrices) {
        instanceNormalMatrixStaging.clear();
        instanceNormalMatrixStaging.reserve(modelMatrices.size());
        instanceTransposeStaging.clear();
        instanceTransposeStaging.reserve(modelMatrices.size());
        for (const Math::Matrix4x4& model : modelMatrices) {
            instanceTransposeStaging.push_back(Math::Transpose(model));
            instanceNormalMatrixStaging.push_back(Math::Transpose(Math::ToMatrix4x4(ComputeNormalMatrix(model))));
        }
        return renderDevice.UpdateBuffer(instanceTransformBuffer,
                                            std::as_bytes(std::span(instanceTransposeStaging))) &&
               renderDevice.UpdateBuffer(instanceNormalTransformBuffer,
                                            std::as_bytes(std::span(instanceNormalMatrixStaging)));
    }

    /// Sets every uniform that does not vary per object. Shared verbatim by the per-object and
    /// instanced paths so the two cannot drift in how they light a surface - the same reason the
    /// shader keeps both variants in one file.
    /// The frame-constant view and lighting state every lit program needs, and nothing else -
    /// deliberately not uViewPosition or any shadow/material uniform, so a program that lights
    /// without them (the primitive shader, which is Lambert-only) can share this without the
    /// backend warning about uniforms it does not declare.
    void ApplyLightingUniforms(Shader::ShaderProgram& program, const FrameUniforms& frameUniforms) {
        program.SetMatrix4x4("uViewProjection", frameUniforms.viewProjection);
        program.SetVector3("uAmbientColor", frameUniforms.ambientColor);
        program.SetVector3("uSkyAmbient", frameUniforms.skyAmbient);
        program.SetVector3("uGroundAmbient", frameUniforms.groundAmbient);
        program.SetInt("uAmbientSource", static_cast<std::int32_t>(frameUniforms.ambientSource));
        program.SetInt("uSkyReflectionsLinked", frameUniforms.skyReflectionsLinked ? 1 : 0);
        program.SetVector3("uSkyReflectionSky", frameUniforms.skyReflectionSky);
        program.SetVector3("uSkyReflectionHorizon", frameUniforms.skyReflectionHorizon);
        program.SetVector3("uSkyReflectionGround", frameUniforms.skyReflectionGround);
        program.SetInt("uAmbientEnvironmentMap",
                          static_cast<std::int32_t>(kAmbientEnvironmentTextureSlot));
        program.SetInt("uAmbientEnvironmentMapEnabled", frameUniforms.ambientEnvironmentMapEnabled ? 1 : 0);
        const std::size_t activeLightCount = std::min(frameUniforms.lights.Size(), frameUniforms.activeLightCap);
        program.SetInt("uActiveLightCount",
                          activeLightCount == 0U ? -1 : static_cast<std::int32_t>(activeLightCount));
        if (rendererProfileResolution.effective == Platform::RendererProfile::ForwardPlus &&
            renderDevice.GetCapabilities().storageBuffers != RenderFeatureSupport::Unsupported) {
            program.SetInt("uClusteredLightingEnabled", frameUniforms.clusteredLightingEnabled ? 1 : 0);
            if (frameUniforms.clusteredLightingEnabled) {
                program.SetVector3("uClusterGridDims",
                                      Math::Vector3{static_cast<float>(kLightClusterCountX),
                                                       static_cast<float>(kLightClusterCountY),
                                                       static_cast<float>(kLightClusterCountZ)});
                program.SetVector2("uClusterNearFar",
                                      Math::Vector2{frameUniforms.clusterNear, frameUniforms.clusterFar});
                program.SetVector2("uClusterViewportSize",
                                      Math::Vector2{frameUniforms.viewportWidth, frameUniforms.viewportHeight});
            }
        }
        for (std::size_t lightIndex = 0; lightIndex < kMaxLights; ++lightIndex) {
            program.SetInt(uniformNames.lightShadowSlots[lightIndex], frameUniforms.lightShadowSlots[lightIndex]);
            // The shader's light slots are fixed at kMaxLights, but the extracted list only
            // holds live lights — paste a default (intensity 0, contributes nothing) over the
            // unused slots so stale uniforms from an earlier frame can never leak through.
            const LightData light = lightIndex < frameUniforms.lights.Size()
                                           ? frameUniforms.lights[lightIndex]
                                           : LightData{};
            const LightUniformNames& names = uniformNames.lights[lightIndex];
            program.SetInt(names.type, static_cast<std::int32_t>(light.type));
            program.SetVector3(names.position, light.position);
            program.SetVector3(names.direction, light.direction);
            program.SetVector3(names.color, Math::ToVector3(light.color));
            program.SetFloat(names.intensity, light.intensity);
            program.SetFloat(names.range, light.range);
            program.SetFloat(names.spotAngleDegrees, light.spotAngleDegrees);
            program.SetInt(names.cullMask, static_cast<std::int32_t>(light.cullMask));
            program.SetFloat(names.specular, light.specular);
        }
    }

    /// `blendedVariant` is whether the FADE/OVERLAY program was chosen - not whether the material
    /// blends, which the material's own pipeline state already answers. The two together decide
    /// whether the material's alpha may reach the colour target:
    ///
    /// - An OPAQUE pipeline must write alpha 1. A cutout material with an alpha-bearing albedo map
    ///   has no blending to consume that alpha, and writing it anyway would leave holes in the
    ///   target's alpha channel for every later fullscreen pass (bloom's additive copy, SSAO's
    ///   multiply) to composite through.
    /// - The blended variant is a straight-alpha pipeline whatever the material asked for, so it
    ///   never asks the shader to premultiply, however the material composites at full opacity.
    void ApplyFrameAndMaterialUniforms(Shader::ShaderProgram& program,
                                          const Asset::MaterialAsset& material,
                                          const FrameUniforms& frameUniforms,
                                          const bool blendedVariant) {
        const bool frameAlreadyUploaded =
            std::find(frameUniformUploadedPrograms.begin(), frameUniformUploadedPrograms.end(), &program) !=
            frameUniformUploadedPrograms.end();
        if (!frameAlreadyUploaded) {
            ApplyLightingUniforms(program, frameUniforms);
            program.SetVector3("uViewPosition", frameUniforms.viewPosition);
            program.SetInt("uOrthographic", frameUniforms.isOrthographic ? 1 : 0);
            program.SetVector3("uCameraForward", frameUniforms.cameraForward);
            program.SetMatrix4x4(uniformNames.legacyLightSpaceMatrix, frameUniforms.lightSpaceMatrices[0]);
            program.SetInt("uShadowMapTexture", static_cast<std::int32_t>(kShadowMapTextureSlot));
            program.SetInt("uShadowCascadeCount", frameUniforms.cascadeCount);
            program.SetFloat("uShadowCascadeBlendRatio", frameUniforms.cascadeBlendRatio);
            program.SetFloat("uShadowMaxDistanceFadeRange", frameUniforms.shadowDistanceFadeRange);
            for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
                const std::uint32_t textureSlot =
                    kShadowCascadeFirstTextureSlot + static_cast<std::uint32_t>(cascadeIndex);
                program.SetMatrix4x4(uniformNames.lightSpaceMatrices[cascadeIndex],
                                        frameUniforms.lightSpaceMatrices[cascadeIndex]);
                program.SetFloat(uniformNames.shadowCascadeSplits[cascadeIndex],
                                    frameUniforms.cascadeSplits[cascadeIndex]);
                program.SetInt(uniformNames.shadowMapTextures[cascadeIndex],
                                  static_cast<std::int32_t>(textureSlot));
            }
            // The atlas contract. Uploaded for all four light slots once per program per pass so
            // cleared tiles never leak from an earlier frame.
            program.SetInt("uShadowAtlasTexture", static_cast<std::int32_t>(kShadowAtlasTextureSlot));
            program.SetVector2(uniformNames.shadowAtlasTexelSize, frameUniforms.shadowAtlasTexelSize);
            for (std::size_t lightSlot = 0; lightSlot < kMaxShadowedLights; ++lightSlot) {
                program.SetMatrix4x4(uniformNames.lightShadowMatrices[lightSlot],
                                        frameUniforms.lightShadowMatrices[lightSlot]);
                program.SetVector4(uniformNames.lightShadowTileRects[lightSlot],
                                      frameUniforms.lightShadowTileRects[lightSlot]);
                program.SetInt(uniformNames.lightShadowEnabled[lightSlot],
                                  frameUniforms.lightShadowEnabled[lightSlot]);
                program.SetVector2(uniformNames.lightShadowDepthRanges[lightSlot],
                                      frameUniforms.lightShadowDepthRanges[lightSlot]);
            }
            if (frameUniforms.pointShadowTilesActive) {
                for (std::size_t tileSlot = 0; tileSlot < kMaxShadowTiles; ++tileSlot) {
                    program.SetVector4(uniformNames.lightShadowCubeRects[tileSlot],
                                          frameUniforms.lightShadowCubeRects[tileSlot]);
                }
            }
            program.SetInt("uShadowPcfKernelRadius", frameUniforms.shadowPcfKernelRadius);
            program.SetFloat("uShadowMapResolution", static_cast<float>(shadowMapResolution));
            program.SetFloat("uShadowBias", frameUniforms.shadowBias);
            program.SetFloat("uShadowNormalBias", frameUniforms.shadowNormalBias);
            program.SetFloat("uShadowOpacity", frameUniforms.shadowOpacity);
            program.SetInt("uShadingMode", shadingMode);
            program.SetInt("uAlbedoTexture", static_cast<std::int32_t>(kAlbedoTextureSlot));
            program.SetInt("uNormalTexture", static_cast<std::int32_t>(kNormalTextureSlot));
            program.SetInt("uAOTexture", static_cast<std::int32_t>(kAoTextureSlot));
            program.SetInt("uMetallicRoughnessTexture",
                              static_cast<std::int32_t>(kMetallicRoughnessTextureSlot));
            program.SetInt("uEmissiveTexture", static_cast<std::int32_t>(kEmissiveTextureSlot));
            program.SetInt(uniformNames.reflectionProbeCube,
                              static_cast<std::int32_t>(kReflectionProbeFirstTextureSlot));
            program.SetInt("uReflectionProbeSecondaryCube",
                              static_cast<std::int32_t>(kReflectionProbeSecondaryTextureSlot));
            program.SetVector3("uGiIndirectIrradiance", frameUniforms.giIndirectIrradiance);
            frameUniformUploadedPrograms.push_back(&program);
        }
        program.SetInt("uDisableReceiveShadows", material.receiveShadows ? 0 : 1);
        program.SetVector3("uAlbedoColor", Math::ToVector3(material.albedoColor));
        program.SetFloat("uMetallic", material.metallic);
        program.SetFloat("uRoughness", material.roughness);
        program.SetVector3("uEmissiveColor", Math::ToVector3(material.emissiveColor));
        program.SetFloat("uEmissiveEnergy", material.emissiveEnergy);
        program.SetFloat("uNormalScale", material.normalScale);
        program.SetInt("uNormalFlipGreen", material.normalFlipGreen ? 1 : 0);
        program.SetFloat("uDielectricSpecular", std::clamp(material.dielectricSpecular, 0.0F, 1.0F));
        program.SetFloat("uOcclusionStrength", material.occlusionStrength);
        program.SetVector3("uUvScale", Math::Vector3{material.uvScale.x, material.uvScale.y, material.uvScale.x});
        program.SetVector3("uUvOffset", Math::Vector3{material.uvOffset.x, material.uvOffset.y, 0.0F});
        program.SetVector3("uUv2Scale", Math::Vector3{material.uv2Scale.x, material.uv2Scale.y, material.uv2Scale.x});
        program.SetVector3("uUv2Offset", Math::Vector3{material.uv2Offset.x, material.uv2Offset.y, 0.0F});
        program.SetInt("uUv1Triplanar", material.worldSpaceTriplanar ? 2 : (material.triplanarEnabled ? 1 : 0));
        program.SetFloat("uUv1TriplanarSharpness", material.triplanarBlendSharpness);
        program.SetInt("uUv2Triplanar", material.uv2WorldTriplanar ? 2 : (material.uv2Triplanar ? 1 : 0));
        program.SetFloat("uUv2TriplanarSharpness", material.uv2TriplanarSharpness);
        program.SetInt("uAoOnUv2", material.aoOnUv2 ? 1 : 0);
        program.SetInt("uEmissionOnUv2", material.emissionOnUv2 ? 1 : 0);
        // Each map's own reading (all zero: as authored). AO and Emission also follow the material's
        // older "on UV2" switches.
        const auto setMap = [&program, &material](const char* transformName, const char* controlName,
                                                  const Asset::MaterialMap map, const Asset::AssetGuid texGuid,
                                                  const bool alsoOnUv2) {
            const Asset::MaterialMapSettings& settings = material.Map(map);
            const float channel = settings.channel == Asset::MaterialTextureChannel::Rgb
                                      ? 0.0F
                                      : static_cast<float>(static_cast<int>(settings.channel) + 1);
            const bool unboundNeutral = texGuid == Asset::kInvalidAssetGuid && !settings.invert &&
                                        settings.channel == Asset::MaterialTextureChannel::Rgb;
            const float neutralWeight =
                unboundNeutral ? 1.0F : (1.0F - std::clamp(settings.strength, 0.0F, 1.0F));
            program.SetVector4(transformName, Math::Vector4{settings.tiling.x - 1.0F, settings.tiling.y - 1.0F,
                                                                  settings.offset.x, settings.offset.y});
            program.SetVector4(controlName, Math::Vector4{channel, settings.invert ? 1.0F : 0.0F,
                                                                neutralWeight,
                                                                (settings.onUv2 || alsoOnUv2) ? 1.0F : 0.0F});
        };
        setMap("uAlbedoMapTransform", "uAlbedoMapControl", Asset::MaterialMap::Albedo, material.albedoTexture, false);
        setMap("uNormalMapTransform", "uNormalMapControl", Asset::MaterialMap::Normal, material.normalTexture, false);
        setMap("uMetallicRoughnessMapTransform", "uMetallicRoughnessMapControl",
               Asset::MaterialMap::MetallicRoughness, material.metallicRoughnessTexture, false);
        setMap("uOcclusionMapTransform", "uOcclusionMapControl", Asset::MaterialMap::Occlusion, material.aoTexture,
               material.aoOnUv2);
        setMap("uEmissiveMapTransform", "uEmissiveMapControl", Asset::MaterialMap::Emissive, material.emissiveTexture,
               material.emissionOnUv2);
        program.SetInt("uUnshaded", material.unshaded ? 1 : 0);
        program.SetFloat("uAlphaCutoff", material.alphaCutoff);
        // The material's own alpha, which until now reached nothing: only the pipeline state and
        // the SurfaceInstance fade did. See the shader's ComposeFragment for what these feed.
        program.SetFloat("uAlbedoAlpha", material.albedoAlpha);
        const bool blends = blendedVariant ||
                            ResolveMaterialPipelineState(material).blendMode != PipelineBlendMode::Opaque;
        const Asset::MaterialAlphaSource alphaSource =
            blends ? material.alphaSource : Asset::MaterialAlphaSource::None;
        program.SetInt("uAlphaSource", static_cast<std::int32_t>(alphaSource));
        program.SetInt("uPremultiplyAlpha",
                          !blendedVariant && MaterialPremultipliesAlpha(material) ? 1 : 0);
        program.SetInt("uPackIndirectWeightAlpha", blends ? 0 : 1);
        program.SetInt("uReflectionProbeEnabled", reflectionProbeEnabled);
        program.SetVector3("uReflectionProbePosition", reflectionProbePosition);
        program.SetVector3("uReflectionProbeAxisX", reflectionProbeAxisX);
        program.SetVector3("uReflectionProbeAxisY", reflectionProbeAxisY);
        program.SetVector3("uReflectionProbeAxisZ", reflectionProbeAxisZ);
        program.SetVector3("uReflectionProbeHalfExtents", reflectionProbeHalfExtents);
        program.SetFloat("uReflectionProbeBlendWeight", reflectionProbeBlendWeight);
    }

    /// Binds shadow cascades, material/probe textures, and the world ambient map for every lit draw.
    void BindMaterialTextures(const MaterialGpuResources& materialResources,
                                 const FrameUniforms& frameUniforms,
                                 ICommandBuffer& commandBuffer) {
        if (frameUniforms.cascadeCount > 0) {
            // kShadowMapTextureSlot aliases cascade 0 (slot 3), so binding the 3 cascade slots
            // covers both legacy single-map and three-cascade lookups without duplicate binds.
            for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
                const std::uint32_t slot =
                    kShadowCascadeFirstTextureSlot + static_cast<std::uint32_t>(cascadeIndex);
                commandBuffer.BindTexture(shadowMapTargets[cascadeIndex], slot);
                if (shadowPointSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(shadowPointSampler, slot);
                }
            }
        }
        // The atlas is bound only on frames that own one, exactly like the cascades above: an
        // unbound slot is not a correctness hole here, because every shader path that would read
        // it is gated by uLightShadowEnabled[] first. Vulkan's device-wide 1x1 white fallback makes
        // that gate's "no shadow" answer (1.0) the value an unbound unit samples, and GL keeps
        // whatever the previous lit pass bound - which is either the atlas itself or nothing, and
        // in both cases uLightShadowEnabled is 0, so it is never read.
        if (frameUniforms.shadowAtlasTexture != kInvalidTextureHandle) {
            commandBuffer.BindTexture(frameUniforms.shadowAtlasTexture, kShadowAtlasTextureSlot);
            if (shadowPointSampler != kInvalidSamplerHandle) {
                commandBuffer.BindSampler(shadowPointSampler, kShadowAtlasTextureSlot);
            }
        }
        const std::array<ResourceBinding, 5> materialBindings{
            BindTextureCommand{materialResources.albedoTexture, kAlbedoTextureSlot},
            BindTextureCommand{materialResources.normalTexture, kNormalTextureSlot},
            BindTextureCommand{materialResources.aoTexture, kAoTextureSlot},
            BindTextureCommand{materialResources.metallicRoughnessTexture, kMetallicRoughnessTextureSlot},
            BindTextureCommand{materialResources.emissiveTexture, kEmissiveTextureSlot}};
        constexpr std::array<BindGroupSlot, 5> materialLayout{{
            {ResourceBindingKind::Texture, kAlbedoTextureSlot},
            {ResourceBindingKind::Texture, kNormalTextureSlot},
            {ResourceBindingKind::Texture, kAoTextureSlot},
            {ResourceBindingKind::Texture, kMetallicRoughnessTextureSlot},
            {ResourceBindingKind::Texture, kEmissiveTextureSlot}}};
        BindGroup(commandBuffer, {materialBindings, materialLayout});
        if (materialRepeatSampler != kInvalidSamplerHandle) {
            commandBuffer.BindSampler(materialRepeatSampler, kAlbedoTextureSlot);
            commandBuffer.BindSampler(materialRepeatSampler, kNormalTextureSlot);
            commandBuffer.BindSampler(materialRepeatSampler, kAoTextureSlot);
            commandBuffer.BindSampler(materialRepeatSampler, kMetallicRoughnessTextureSlot);
            commandBuffer.BindSampler(materialRepeatSampler, kEmissiveTextureSlot);
        }
        if (reflectionProbeEnabled != 0) {
            // The fallback must be a cube too (a 2D texture under a samplerCube uniform is
            // incomplete on GL and a view-type violation on Vulkan).
            const TextureHandle probeCube = reflectionProbeCube != kInvalidTextureHandle
                                                   ? reflectionProbeCube
                                                   : fallbackWhiteCubeTexture;
            commandBuffer.BindTexture(probeCube, kReflectionProbeFirstTextureSlot);
            const TextureHandle secondaryCube =
                reflectionProbeSecondaryCube != kInvalidTextureHandle ? reflectionProbeSecondaryCube
                                                                         : fallbackWhiteCubeTexture;
            commandBuffer.BindTexture(secondaryCube, kReflectionProbeSecondaryTextureSlot);
        }
        // The built-in lit shaders declare this sampler statically, so bind a valid fallback even
        // when the mode disables sampling; Vulkan still requires every active sampler descriptor.
        const TextureHandle ambientEnvironmentTexture =
            frameUniforms.ambientEnvironmentMapEnabled &&
                    frameUniforms.ambientEnvironmentTexture != kInvalidTextureHandle
                ? frameUniforms.ambientEnvironmentTexture
                : fallbackWhiteTexture;
        commandBuffer.BindTexture(ambientEnvironmentTexture, kAmbientEnvironmentTextureSlot);
        if (environmentEquirectSampler != kInvalidSamplerHandle) {
            commandBuffer.BindSampler(environmentEquirectSampler, kAmbientEnvironmentTextureSlot);
        }
        if (frameUniforms.clusteredLightingEnabled &&
            clusteredLightBuffer != kInvalidBufferHandle &&
            clusteredCellBuffer != kInvalidBufferHandle &&
            clusteredIndexBuffer != kInvalidBufferHandle) {
            commandBuffer.BindStorageBuffer(clusteredLightBuffer, kClusteredLightSlot);
            commandBuffer.BindStorageBuffer(clusteredCellBuffer, kClusteredCellSlot);
            commandBuffer.BindStorageBuffer(clusteredIndexBuffer, kClusteredIndexSlot);
        }
    }

    /// Records `items` as instanced draws where the material supports it, falling back to the
    /// per-object path for everything else. Returns the number of draw calls recorded.
    ///
    /// The fallback is per BATCH, not per frame: a scene mixing instancing-aware and legacy
    /// materials draws each with whichever path is correct for it, rather than giving up on
    /// instancing entirely because one material is old.
    [[nodiscard]] std::size_t RecordItemsInstanced(const std::vector<RenderItem>& items,
                                                      RenderBatchSet& batchSet,
                                                      const FrameUniforms& frameUniforms,
                                                      ICommandBuffer& commandBuffer) {
        BuildRenderBatches(items, batchSet);
        if (batchSet.batches.empty()) {
            return 0U;
        }

        // One upload for the whole bucket; each batch then reads its own window of it via
        // uInstanceBaseIndex. Uploading per batch would be the obvious shape and the wrong one -
        // it trades one buffer update per frame for one per batch.
        const bool instancingUsable =
            batchSet.instanceMatrices.size() <= activeMaxInstancesPerFrame &&
            EnsureInstanceBufferCapacity(batchSet.instanceMatrices.size()) &&
            UploadInstanceTransforms(batchSet.instanceMatrices);

        std::size_t drawCalls = 0U;
        for (const RenderBatch& batch : batchSet.batches) {
            const RenderItem& representative = items[batch.firstItem];
            const MaterialGpuResources* const materialResources =
                ResolveMaterialGpuResources(representative.materialHandle);
            if (materialResources == nullptr) {
                continue;
            }
            const MeshGpuResources& meshResources = ResolveMeshGpuResources(representative);
            if (!IsValidMeshGpuResources(meshResources)) {
                continue;
            }
            const Asset::MaterialAsset* const material = representative.materialHandle.TryGet();
            // Fade/overlay only: the material's own blend state is already baked into the program
            // MeshColorProgram returns, so folding it in here would swap an additive material for
            // the straight-alpha fade variant at full opacity.
            const bool blendedVariant = representative.opacity < 1.0F || representative.overlay;
            Shader::ShaderProgram* const program = MeshColorProgram(*materialResources, blendedVariant);
            if (material == nullptr || program == nullptr) {
                continue; // Still compiling or invalid: never bind a stale raw material pipeline.
            }

            if (!instancingUsable || !materialResources->supportsInstancing) {
                // Correct, just not batched. Every item in the run still gets its own uModel.
                drawCalls += RecordItemRangeUnbatched(items, batch.firstItem, batch.itemCount,
                                                         frameUniforms, commandBuffer);
                continue;
            }

            const auto baseIndex = static_cast<std::int32_t>(batch.firstItem);
            const std::span<const std::byte> baseBytes{reinterpret_cast<const std::byte*>(&baseIndex),
                                                       sizeof(baseIndex)};
            if (!renderDevice.UpdateBuffer(instanceBaseBuffer, baseBytes)) {
                drawCalls += RecordItemRangeUnbatched(items, batch.firstItem, batch.itemCount,
                                                         frameUniforms, commandBuffer);
                continue;
            }

            ApplyFrameAndMaterialUniforms(*program, *material, frameUniforms, blendedVariant);
            program->SetInt("uMeshRenderLayers", static_cast<std::int32_t>(representative.renderLayers));
            program->SetFloat("uSurfaceOpacity", representative.opacity);
            program->ApplyTo(commandBuffer);
            BindMaterialTextures(*materialResources, frameUniforms, commandBuffer);
            commandBuffer.BindStorageBuffer(instanceTransformBuffer, kInstanceTransformSlot);
            commandBuffer.BindStorageBuffer(instanceNormalTransformBuffer, kInstanceNormalTransformSlot);
            commandBuffer.BindStorageBuffer(instanceBaseBuffer, kInstanceBaseSlot);
            commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
            commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
            bool indirectRecorded = false;
            if (cullingSettings.frustumCullingEnabled) {
                FrustumCullIndirect* const cullIndirect = AcquireIndirectCull();
                if (cullIndirect != nullptr && cullIndirect->IsReady()) {
                    const Asset::MeshAsset* const meshAsset = representative.meshHandle.TryGet();
                    const Math::Aabb localBounds =
                        meshAsset != nullptr ? meshAsset->localBounds : Math::Aabb{};
                    instanceBoundsScratch.resize(batch.itemCount);
                    for (std::size_t i = 0U; i < batch.itemCount; ++i) {
                        instanceBoundsScratch[i] =
                            localBounds.Transform(items[batch.firstItem + i].worldMatrix);
                    }
                    const Math::Frustum cullFrustum =
                        Math::Frustum::FromViewProjection(frameUniforms.viewProjection);
                    DrawIndexedIndirectCommand meshParams{};
                    // The batch's range, not the buffer's: a surface is a window into the same
                    // index buffer, and the indirect path must draw that window.
                    meshParams.indexCount = ResolveItemIndexCount(representative, meshResources);
                    meshParams.instanceCount = 0U;
                    meshParams.firstIndex = ResolveItemFirstIndex(representative);
                    meshParams.vertexOffset = 0;
                    meshParams.firstInstance = 0U;
                    if (cullIndirect->Prepare(instanceBoundsScratch, cullFrustum, meshParams)) {
                        commandBuffer.BindStorageBuffer(cullIndirect->GetVisibleIndexBuffer(),
                                                           kInstanceVisibleIndexSlot);
                        commandBuffer.DrawIndexedIndirect(cullIndirect->GetDrawCommandBuffer(), 0U);
                        indirectRecorded = true;
                        ++drawCalls;
                        instancedDrawCallsThisFrame += 1U;
                        indirectInstancedDrawCallsThisFrame += 1U;
                        instancedObjectsThisFrame += batch.itemCount;
                    }
                }
            }

            if (!indirectRecorded) {
                commandBuffer.DrawIndexed(ResolveItemIndexCount(representative, meshResources),
                                             static_cast<std::uint32_t>(batch.itemCount),
                                             ResolveItemFirstIndex(representative));
                ++drawCalls;
                instancedDrawCallsThisFrame += 1U;
                instancedObjectsThisFrame += batch.itemCount;
            }
        }
        return drawCalls;
    }

    [[nodiscard]] std::size_t RecordItems(const std::vector<RenderItem>& items,
                                              const FrameUniforms& frameUniforms,
                                              ICommandBuffer& commandBuffer) {
        return RecordItemRangeUnbatched(items, 0U, items.size(), frameUniforms, commandBuffer);
    }

    /// The original one-draw-per-object path, now expressed over a sub-range so the instanced path
    /// can delegate a single batch to it when that batch's material cannot be instanced. Behavior
    /// for the whole-bucket case is unchanged.
    [[nodiscard]] std::size_t RecordItemRangeUnbatched(const std::vector<RenderItem>& items,
                                                          const std::size_t firstItem,
                                                          const std::size_t itemCount,
                                                          const FrameUniforms& frameUniforms,
                                                          ICommandBuffer& commandBuffer) {
        std::size_t drawCalls = 0U;
        const Shader::ShaderProgram* lastProgram = nullptr;
        const Asset::MaterialAsset* lastMaterial = nullptr;
        const MaterialGpuResources* lastMaterialResources = nullptr;
        bool lastBlendedVariant = false;
        BufferHandle lastVertexBuffer = kInvalidBufferHandle;
        BufferHandle lastIndexBuffer = kInvalidBufferHandle;
        std::uint32_t lastRenderLayers = 0U;
        float lastOpacity = -1.0F;
        for (std::size_t itemIndex = firstItem; itemIndex < firstItem + itemCount; ++itemIndex) {
            const RenderItem& item = items[itemIndex];
            const MaterialGpuResources* const materialResources = ResolveMaterialGpuResources(item.materialHandle);
            if (materialResources == nullptr) {
                continue;
            }
            const MeshGpuResources& meshResources = ResolveMeshGpuResources(item);
            if (!IsValidMeshGpuResources(meshResources)) {
                continue;
            }
            const Asset::MaterialAsset* const material = item.materialHandle.TryGet();
            const bool blendedVariant = item.opacity < 1.0F || item.overlay;
            Shader::ShaderProgram* const program = MeshColorProgram(*materialResources, blendedVariant);
            if (material == nullptr || program == nullptr) {
                continue; // Still compiling or invalid: never bind a stale raw material pipeline.
            }

            const Math::Matrix3x3 normalMatrix = ComputeNormalMatrix(item.worldMatrix);
            const bool stateChanged = program != lastProgram || material != lastMaterial ||
                                      materialResources != lastMaterialResources ||
                                      blendedVariant != lastBlendedVariant;
            if (stateChanged) {
                program->SetMatrix4x4("uModel", item.worldMatrix);
                // Normal matrix = transpose(inverse(model)); see ComputeNormalMatrix, which the
                // instanced path shares so the two cannot disagree about how a normal is transformed.
                program->SetMatrix3x3("uNormalMatrix", normalMatrix);
                ApplyFrameAndMaterialUniforms(*program, *material, frameUniforms, blendedVariant);
                program->SetInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                program->SetFloat("uSurfaceOpacity", item.opacity);
                program->ApplyTo(commandBuffer);
                BindMaterialTextures(*materialResources, frameUniforms, commandBuffer);
                lastProgram = program;
                lastMaterial = material;
                lastMaterialResources = materialResources;
                lastBlendedVariant = blendedVariant;
                lastRenderLayers = item.renderLayers;
                lastOpacity = item.opacity;
            } else if (commandBuffer.RetainsUniformStatePerPipeline()) {
                program->SetMatrix4x4("uModel", item.worldMatrix);
                program->SetMatrix3x3("uNormalMatrix", normalMatrix);
                if (item.renderLayers != lastRenderLayers) {
                    program->SetInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                    lastRenderLayers = item.renderLayers;
                }
                if (item.opacity != lastOpacity) {
                    program->SetFloat("uSurfaceOpacity", item.opacity);
                    lastOpacity = item.opacity;
                }
                program->ApplyTo(commandBuffer);
            } else {
                commandBuffer.SetUniformMatrix4x4("uModel", item.worldMatrix);
                commandBuffer.SetUniformMatrix4x4("uNormalMatrix", Math::ToMatrix4x4(normalMatrix));
                if (item.renderLayers != lastRenderLayers) {
                    commandBuffer.SetUniformInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                    lastRenderLayers = item.renderLayers;
                }
                if (item.opacity != lastOpacity) {
                    commandBuffer.SetUniformFloat("uSurfaceOpacity", item.opacity);
                    lastOpacity = item.opacity;
                }
            }
            if (meshResources.vertexBuffer != lastVertexBuffer) {
                commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                lastVertexBuffer = meshResources.vertexBuffer;
            }
            if (meshResources.indexBuffer != lastIndexBuffer) {
                commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                lastIndexBuffer = meshResources.indexBuffer;
            }
            commandBuffer.DrawIndexed(ResolveItemIndexCount(item, meshResources), 1U,
                                         ResolveItemFirstIndex(item));
            ++drawCalls;
        }
        return drawCalls;
    }

    /// Uploads one decal command's window of the plan into that decal's own buffers, creating them
    /// the first time it paints and re-uploading afterwards. Returns nullptr when the device refuses
    /// the upload, in which case the decal is skipped rather than drawn from stale geometry.
    [[nodiscard]] const DecalGpu* UploadDecalGeometry(const DecalDrawCommand& command,
                                                            const std::span<const Asset::MeshVertex> vertices,
                                                            const std::span<const std::uint32_t> indices) {
        const std::uint64_t key = (static_cast<std::uint64_t>(command.decal.generation) << 32U) |
                                  static_cast<std::uint64_t>(command.decal.index);
        DecalGpu& entry = decalBuffers[key];
        const std::span<const std::byte> vertexBytes = std::as_bytes(vertices);
        const std::span<const std::byte> indexBytes = std::as_bytes(indices);

        if (entry.vertexBuffer == kInvalidBufferHandle || entry.vertexBytes != vertexBytes.size()) {
            DestroyBufferIfValid(renderDevice, entry.vertexBuffer);
            entry.vertexBuffer = renderDevice.CreateBuffer(
                BufferDesc{vertexBytes.size(), BufferUsage::Vertex, BufferMemory::Auto,
                           BufferUpdateFrequency::Dynamic}, vertexBytes);
            entry.vertexBytes = vertexBytes.size();
        } else if (!renderDevice.UpdateBuffer(entry.vertexBuffer, vertexBytes)) {
            return nullptr;
        }
        if (entry.indexBuffer == kInvalidBufferHandle || entry.indexBytes != indexBytes.size()) {
            DestroyBufferIfValid(renderDevice, entry.indexBuffer);
            entry.indexBuffer = renderDevice.CreateBuffer(
                BufferDesc{indexBytes.size(), BufferUsage::Index, BufferMemory::Auto,
                           BufferUpdateFrequency::Dynamic}, indexBytes);
            entry.indexBytes = indexBytes.size();
        } else if (!renderDevice.UpdateBuffer(entry.indexBuffer, indexBytes)) {
            return nullptr;
        }
        if (entry.vertexBuffer == kInvalidBufferHandle || entry.indexBuffer == kInvalidBufferHandle) {
            return nullptr;
        }
        entry.frame = decalFrame;
        return &entry;
    }

    /// Records this frame's decal patches, one draw per decal, in the plan's own (back-to-front)
    /// order so overlapping decals blend in the order the artist sees them. Returns the number of
    /// draw calls recorded.
    ///
    /// The pass runs inside the main color pass, after the meshes it paints over and before the
    /// effects that sit on top of them: a decal is blended into the shaded surface it landed on, so
    /// it has to be recorded once that surface exists.
    [[nodiscard]] std::size_t RecordDecalItems(const DecalDrawPlan& plan,
                                                  const FrameUniforms& frameUniforms,
                                                  ICommandBuffer& commandBuffer) {
        if (!decalProgram->IsValid() || plan.commands.empty()) {
            return 0U;
        }
        // Frame constants once: the per-decal values below overwrite only their own names, and
        // ApplyTo() flushes the whole pending set on every draw.
        decalProgram->SetMatrix4x4("uViewProjection", frameUniforms.viewProjection);
        decalProgram->SetVector3("uViewPosition", frameUniforms.viewPosition);

        std::size_t drawCalls = 0U;
        for (const DecalDrawCommand& command : plan.commands) {
            const std::span<const Asset::MeshVertex> vertices(plan.vertices.data() + command.firstVertex,
                                                                 command.vertexCount);
            const std::span<const std::uint32_t> indices(plan.indices.data() + command.firstIndex,
                                                         command.indexCount);
            const DecalGpu* const geometry = UploadDecalGeometry(command, vertices, indices);
            if (geometry == nullptr) {
                continue;
            }

            // The material's texture, resolved exactly as the mesh path resolves one: a GUID that
            // is unset, still loading or failed gets the 1x1 white fallback, which leaves the flat
            // colour and the full alpha intact rather than dropping the decal for the frame.
            const TextureHandle albedoTexture =
                ResolveTextureGpuHandle(command.albedoTextureGuid, fallbackWhiteTexture)
                    .value_or(fallbackWhiteTexture);
            decalProgram->SetInt("uAlbedoTexture", static_cast<std::int32_t>(kAlbedoTextureSlot));
            decalProgram->SetMatrix4x4("uWorldToUnit", command.worldToUnit);
            decalProgram->SetVector3("uProjectionDirection", command.projectionDirection);
            decalProgram->SetVector3("uBaseColor", command.baseColor);
            decalProgram->SetVector3("uEmissionColor", command.emissionColor);
            decalProgram->SetFloat("uAlphaScale", command.alphaScale);
            decalProgram->SetFloat("uNormalFade", command.normalFade);
            decalProgram->SetFloat("uUpperFade", command.upperFade);
            decalProgram->SetFloat("uLowerFade", command.lowerFade);
            decalProgram->SetFloat("uDistanceFadeEnabled", command.distanceFadeEnabled ? 1.0F : 0.0F);
            decalProgram->SetFloat("uDistanceFadeBegin", command.distanceFadeBegin);
            decalProgram->SetFloat("uDistanceFadeLength", command.distanceFadeLength);
            decalProgram->ApplyTo(commandBuffer);
            commandBuffer.BindTexture(albedoTexture, kAlbedoTextureSlot);
            if (clampLinearSampler != kInvalidSamplerHandle) {
                commandBuffer.BindSampler(clampLinearSampler, kAlbedoTextureSlot);
            }
            commandBuffer.BindVertexBuffer(geometry->vertexBuffer);
            commandBuffer.BindIndexBuffer(geometry->indexBuffer);
            commandBuffer.DrawIndexed(command.indexCount);
            ++drawCalls;
        }
        return drawCalls;
    }

    [[nodiscard]] std::size_t RecordParticleItems(const ParticleDrawRecording& recording,
                                                       const FrameUniforms& frameUniforms,
                                                       ICommandBuffer& commandBuffer) {
        if (!particleProgram->IsValid() || recording.commands.empty() ||
            particleVertexBuffer == kInvalidBufferHandle) {
            return 0U;
        }

        const std::size_t commandCount =
            std::min(recording.commands.size(), activeMaxParticleGpuDrawCommands);
        particleVertexStaging.clear();
        particleVertexStaging.reserve(commandCount * kParticleVerticesPerCommand);
        const auto appendVertex = [this](const ParticleDrawCommand& command, float xOffset, float yOffset) {
            const float alpha = std::clamp(command.remainingLifetimeSeconds, 0.15F, 1.0F);
            particleVertexStaging.push_back(ParticleVertex{
                Math::Vector3{command.position.x + xOffset, command.position.y + yOffset, command.position.z},
                1.0F, 0.45F, 0.08F, alpha});
        };
        for (std::size_t commandIndex = 0U; commandIndex < commandCount; ++commandIndex) {
            const ParticleDrawCommand& command = recording.commands[commandIndex];
            appendVertex(command, -kParticleHalfExtent, -kParticleHalfExtent);
            appendVertex(command, kParticleHalfExtent, -kParticleHalfExtent);
            appendVertex(command, kParticleHalfExtent, kParticleHalfExtent);
            appendVertex(command, -kParticleHalfExtent, -kParticleHalfExtent);
            appendVertex(command, kParticleHalfExtent, kParticleHalfExtent);
            appendVertex(command, -kParticleHalfExtent, kParticleHalfExtent);
        }

        if (!renderDevice.UpdateBuffer(particleVertexBuffer, std::as_bytes(std::span(particleVertexStaging)))) {
            return 0U;
        }
        particleProgram->SetMatrix4x4("uViewProjection", frameUniforms.viewProjection);
        particleProgram->ApplyTo(commandBuffer);
        commandBuffer.BindVertexBuffer(particleVertexBuffer);
        commandBuffer.Draw(static_cast<std::uint32_t>(particleVertexStaging.size()));
        return commandCount;
    }

    /// Foundation WP3: stable probe identity for an opaque mesh item - FNV-1a over the mesh GUID
    /// and the world-matrix bits. The same object in the same place maps to the same probe frame
    /// after frame; a moved one starts fresh and is therefore drawn, never skipped on old data.
    [[nodiscard]] static std::uint64_t OcclusionProbeKey(const RenderItem& item) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](const std::uint64_t value) noexcept {
            for (std::uint32_t byteIndex = 0U; byteIndex < 8U; ++byteIndex) {
                hash ^= (value >> (byteIndex * 8U)) & 0xFFULL;
                hash *= 1099511628211ULL;
            }
        };
        mix(item.meshHandle.GetGuid().value);
        for (const auto& row : item.worldMatrix.m) {
            for (const float element : row) {
                std::uint32_t bits = 0U;
                std::memcpy(&bits, &element, sizeof(bits));
                mix(bits);
            }
        }
        return hash;
    }

    /// The unit cube spans [-0.5, 0.5]; scaled by the world box's size and inflated a little, so a
    /// face lying exactly on a flat mesh (a floor plane has a zero-thickness box) still sits in
    /// front of it under LessOrEqual instead of tying with it at the edge of depth precision.
    [[nodiscard]] static Math::Matrix4x4 OcclusionProxyMatrix(const Math::Aabb& worldBounds) noexcept {
        const Math::Vector3 size = worldBounds.max - worldBounds.min;
        const float inflate = std::max(0.01F, 0.02F * std::max(size.x, std::max(size.y, size.z)));
        const Math::Vector3 scale{size.x + inflate, size.y + inflate, size.z + inflate};
        return Math::Matrix4x4::ComposeTrs(worldBounds.GetCenter(), Math::Quaternion{}, scale);
    }

    void DestroyOcclusionProbeQueries(OcclusionProbe& probe) {
        for (QueryHandle& query : probe.queries) {
            if (query != kInvalidQueryHandle) {
                static_cast<void>(renderDevice.DestroyQuery(query));
                query = kInvalidQueryHandle;
            }
        }
        probe.issued = {{false, false}};
    }

    void DestroyOcclusionProbes() {
        for (auto& [key, probe] : occlusionProbes) {
            static_cast<void>(key);
            DestroyOcclusionProbeQueries(probe);
        }
        occlusionProbes.clear();
        occlusionProxyDraws.clear();
    }

    /// Foundation WP3: applies last frame's completed brackets to this frame's opaque items and
    /// plans this frame's proxy draws from the same walk. Conservative by construction - see
    /// CullingSettings::gpuOcclusionCullingEnabled for the exact skip rule. Never called for
    /// a reflection-probe capture; that view keeps its own full item list.
    void ApplyOcclusionVerdicts(RenderQueue& queue, const Math::Matrix4x4& viewProjection,
                                   const bool enabled) {
        occlusionProxyDraws.clear();
        lastFrameDiagnostics.occludedItemsSkipped = 0U;
        const bool supported =
            renderDevice.GetCapabilities().occlusionQueries != RenderFeatureSupport::Unsupported;
        lastFrameDiagnostics.occlusionQueriesSupported = supported;
        if (!enabled || !supported || occlusionQueryCreationFailed || occlusionProxyProgram == nullptr ||
            !occlusionProxyProgram->IsValid()) {
            if (!occlusionProbes.empty()) {
                DestroyOcclusionProbes();
            }
            return;
        }
        ++occlusionFrame;
        const std::uint32_t writeSlot = static_cast<std::uint32_t>(occlusionFrame & 1U);
        const std::uint32_t readSlot = writeSlot ^ 1U;
        auto& kept = occlusionKeptItems;
        kept.clear();
        kept.reserve(queue.opaqueItems.size());
        std::size_t skipped = 0U;
        for (const RenderItem& item : queue.opaqueItems) {
            const Asset::MeshAsset* const mesh = item.meshHandle.TryGet();
            if (occlusionQueryCreationFailed || mesh == nullptr || !IsOrderedFiniteAabb(mesh->localBounds)) {
                kept.push_back(item);
                continue;
            }
            const Math::Aabb worldBounds = mesh->localBounds.Transform(item.worldMatrix);
            if (!IsOrderedFiniteAabb(worldBounds)) {
                kept.push_back(item);
                continue;
            }
            const std::uint64_t key = OcclusionProbeKey(item);
            auto probeIt = occlusionProbes.find(key);
            if (probeIt == occlusionProbes.end()) {
                OcclusionProbe probe;
                bool created = true;
                for (QueryHandle& query : probe.queries) {
                    const RenderOperationResult result =
                        renderDevice.CreateQuery(RenderQueryKind::AnySamplesPassed, query);
                    if (result != RenderOperationResult::Success && result != RenderOperationResult::Recorded) {
                        created = false;
                        break;
                    }
                }
                if (!created) {
                    DestroyOcclusionProbeQueries(probe);
                    occlusionQueryCreationFailed = true;
                    UVE_WARNING("Renderer3D: the device refused an occlusion query; GPU occlusion culling is "
                                "off for the rest of this renderer's life");
                    kept.push_back(item);
                    continue;
                }
                probeIt = occlusionProbes.emplace(key, probe).first;
            }
            OcclusionProbe& probe = probeIt->second;
            probe.lastSeenFrame = occlusionFrame;
            bool occluded = false;
            if (probe.issued[readSlot]) {
                RenderQueryResult result{};
                const RenderOperationResult status =
                    renderDevice.GetQueryResult(probe.queries[readSlot], result, false);
                occluded = status == RenderOperationResult::Success && result.value == 0U &&
                           probe.viewProjections[readSlot] == viewProjection;
            }
            occlusionProxyDraws.push_back(OcclusionProxyDraw{key, OcclusionProxyMatrix(worldBounds), writeSlot});
            if (occluded) {
                ++skipped;
            } else {
                kept.push_back(item);
            }
        }
        if (occlusionQueryCreationFailed) {
            DestroyOcclusionProbes();
            kept = queue.opaqueItems;
            skipped = 0U;
        }
        queue.opaqueItems.swap(kept);
        kept.clear();
        // Probes nobody referenced for two frames belong to objects that moved, left the view or
        // were destroyed; their queries go back to the device.
        for (auto it = occlusionProbes.begin(); it != occlusionProbes.end();) {
            if (it->second.lastSeenFrame + 2U < occlusionFrame) {
                DestroyOcclusionProbeQueries(it->second);
                it = occlusionProbes.erase(it);
            } else {
                ++it;
            }
        }
        lastFrameDiagnostics.occludedItemsSkipped = skipped;
    }

    /// Foundation WP3: records this frame's proxy brackets at the end of the main colour pass, after
    /// every opaque writer. Each proxy is one depth-tested, colour-less cube under a
    /// BeginQuery/EndQuery pair; a backend that refuses the bracket leaves the probe
    /// unissued, which reads as "draw it" next frame.
    [[nodiscard]] std::size_t RecordOcclusionProxies(const FrameUniforms& frameUniforms,
                                                        ICommandBuffer& commandBuffer) {
        if (occlusionProxyDraws.empty() || occlusionProxyProgram == nullptr || !occlusionProxyProgram->IsValid()) {
            return 0U;
        }
        const MeshGpuResources& cube = ResolvePrimitiveMeshGpuResources(Scene::PrimitiveMeshKind::Cube);
        if (!IsValidMeshGpuResources(cube)) {
            return 0U;
        }
        std::size_t issued = 0U;
        for (const OcclusionProxyDraw& draw : occlusionProxyDraws) {
            const auto probeIt = occlusionProbes.find(draw.key);
            if (probeIt == occlusionProbes.end()) {
                continue;
            }
            OcclusionProbe& probe = probeIt->second;
            const QueryHandle query = probe.queries[draw.slot];
            occlusionProxyProgram->SetMatrix4x4("uModel", draw.worldMatrix);
            occlusionProxyProgram->SetMatrix4x4("uViewProjection", frameUniforms.viewProjection);
            occlusionProxyProgram->ApplyTo(commandBuffer);
            commandBuffer.BindVertexBuffer(cube.vertexBuffer);
            commandBuffer.BindIndexBuffer(cube.indexBuffer);
            const RenderOperationResult begun = commandBuffer.BeginQuery(query);
            if (begun != RenderOperationResult::Recorded && begun != RenderOperationResult::Success) {
                probe.issued[draw.slot] = false;
                continue;
            }
            commandBuffer.DrawIndexed(cube.indexCount);
            const RenderOperationResult ended = commandBuffer.EndQuery(query);
            probe.issued[draw.slot] =
                ended == RenderOperationResult::Recorded || ended == RenderOperationResult::Success;
            probe.viewProjections[draw.slot] = frameUniforms.viewProjection;
            if (probe.issued[draw.slot]) {
                ++issued;
            }
        }
        return issued;
    }

    /// Draws one frame's primitive list through the lit material path: the same program selection,
    /// the same frame + material uniforms and the same texture bindings an imported mesh uses, over
    /// the primitive's own geometry. A primitive with no material - and every unmaterialed imported
    /// mesh, which shares this list - draws through the built-in default lit material, so "no
    /// material" is the PBR path with neutral inputs rather than a second, simpler shading model.
    [[nodiscard]] std::size_t RecordPrimitiveItems(const std::vector<PrimitiveRenderItem>& items,
                                                       const FrameUniforms& frameUniforms,
                                                       ICommandBuffer& commandBuffer) {
        std::size_t drawCalls = 0U;
        const Shader::ShaderProgram* lastProgram = nullptr;
        const Asset::MaterialAsset* lastMaterial = nullptr;
        const MaterialGpuResources* lastMaterialResources = nullptr;
        bool lastBlendedVariant = false;
        BufferHandle lastVertexBuffer = kInvalidBufferHandle;
        BufferHandle lastIndexBuffer = kInvalidBufferHandle;
        std::uint32_t lastRenderLayers = 0U;
        float lastOpacity = -1.0F;
        Math::Vector3 lastAlbedoColor{-1.0F, -1.0F, -1.0F};
        for (const PrimitiveRenderItem& item : items) {
            const MeshGpuResources& meshResources =
                item.skinned != nullptr ? *item.skinned
                : item.mesh != nullptr  ? ResolveMeshGpuResources(item.meshGuid, item.mesh)
                                        : ResolvePrimitiveMeshGpuResources(item.kind);
            if (!IsValidMeshGpuResources(meshResources)) {
                continue;
            }

            const bool usesDefaultMaterial = item.materialGuid == Asset::kInvalidAssetGuid;
            const Asset::MaterialAsset* material = &kDefaultLitMaterial;
            const MaterialGpuResources* materialResources = &defaultLitMaterialResources;
            if (!usesDefaultMaterial) {
                // Start unresolved: a material still loading on a worker returns nullptr from
                // TryGet, and leaving the default resources in place here used to let that
                // nullptr reach ApplyFrameAndMaterialUniforms and crash the frame.
                material = nullptr;
                materialResources = nullptr;
                const auto materialIt = primitiveMaterialHandles.find(item.materialGuid);
                if (materialIt != primitiveMaterialHandles.end()) {
                    material = materialIt->second.TryGet();
                    if (material != nullptr) {
                        materialResources = ResolveMaterialGpuResources(materialIt->second);
                    }
                }
                if (material == nullptr || materialResources == nullptr) {
                    if (item.mesh != nullptr) {
                        material = &kDefaultLitMaterial;
                        materialResources = &defaultLitMaterialResources;
                    } else {
                        continue;
                    }
                }
            }

            // Only an imported mesh (every skinned mesh is one) is held on screen by the fallbacks
            // below. A built-in primitive keeps the strict rule: it is drawn when its own program is
            // linked and not before.
            const bool isImportedMesh = item.mesh != nullptr;
            bool blendedVariant = item.opacity < 1.0F;
            Shader::ShaderProgram* program = MeshColorProgram(*materialResources, blendedVariant);
            if (program == nullptr && isImportedMesh && materialResources != &defaultLitMaterialResources) {
                // The material's own program is not usable - still compiling on a worker, a texture
                // binding / metallic-roughness / normal-map variant that did not link, or a program that
                // failed. Dropping the draw here made an imported mesh show for a frame on the default
                // material and then vanish the moment its material finished loading; draw it with the
                // default lit program instead, so a mesh never disappears because its material resolved.
                material = &kDefaultLitMaterial;
                materialResources = &defaultLitMaterialResources;
                program = MeshColorProgram(*materialResources, blendedVariant);
            }
            if (program == nullptr && isImportedMesh && blendedVariant) {
                // Even the default lit blended variant is not linked yet: an opaque draw of the mesh is
                // better than none, and the blended one takes over as soon as it is ready.
                blendedVariant = false;
                program = MeshColorProgram(*materialResources, blendedVariant);
            }
            if (program == nullptr) {
                continue; // Still compiling or invalid: never bind a stale pipeline.
            }

            const Math::Matrix3x3 normalMatrix = ComputeNormalMatrix(item.worldMatrix);
            const bool overrideAlbedo = usesDefaultMaterial || materialResources == &defaultLitMaterialResources;
            const Math::Vector3 effectiveAlbedo =
                overrideAlbedo ? item.baseColor : Math::ToVector3(material->albedoColor);
            const bool stateChanged = program != lastProgram || material != lastMaterial ||
                                      materialResources != lastMaterialResources ||
                                      blendedVariant != lastBlendedVariant;
            if (stateChanged) {
                program->SetMatrix4x4("uModel", item.worldMatrix);
                // Normal matrix = transpose(inverse(model)); see ComputeNormalMatrix, shared with
                // the lit mesh path so the two cannot disagree under non-uniform scale.
                program->SetMatrix3x3("uNormalMatrix", normalMatrix);
                ApplyFrameAndMaterialUniforms(*program, *material, frameUniforms, blendedVariant);
                // Base Color is the DEFAULT material's albedo, so it only replaces that material's own
                // (white) albedo. An assigned material owns the surface outright - tinting it here would
                // mean a primitive could never show the material an author picked.
                if (overrideAlbedo) {
                    program->SetVector3("uAlbedoColor", item.baseColor);
                }
                program->SetInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                program->SetFloat("uSurfaceOpacity", item.opacity);
                program->ApplyTo(commandBuffer);
                BindMaterialTextures(*materialResources, frameUniforms, commandBuffer);
                lastProgram = program;
                lastMaterial = material;
                lastMaterialResources = materialResources;
                lastBlendedVariant = blendedVariant;
                lastRenderLayers = item.renderLayers;
                lastOpacity = item.opacity;
                lastAlbedoColor = effectiveAlbedo;
            } else if (commandBuffer.RetainsUniformStatePerPipeline()) {
                program->SetMatrix4x4("uModel", item.worldMatrix);
                program->SetMatrix3x3("uNormalMatrix", normalMatrix);
                if (overrideAlbedo && (effectiveAlbedo.x != lastAlbedoColor.x ||
                                       effectiveAlbedo.y != lastAlbedoColor.y ||
                                       effectiveAlbedo.z != lastAlbedoColor.z)) {
                    program->SetVector3("uAlbedoColor", effectiveAlbedo);
                    lastAlbedoColor = effectiveAlbedo;
                }
                if (item.renderLayers != lastRenderLayers) {
                    program->SetInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                    lastRenderLayers = item.renderLayers;
                }
                if (item.opacity != lastOpacity) {
                    program->SetFloat("uSurfaceOpacity", item.opacity);
                    lastOpacity = item.opacity;
                }
                program->ApplyTo(commandBuffer);
            } else {
                commandBuffer.SetUniformMatrix4x4("uModel", item.worldMatrix);
                commandBuffer.SetUniformMatrix4x4("uNormalMatrix", Math::ToMatrix4x4(normalMatrix));
                if (overrideAlbedo && (effectiveAlbedo.x != lastAlbedoColor.x ||
                                       effectiveAlbedo.y != lastAlbedoColor.y ||
                                       effectiveAlbedo.z != lastAlbedoColor.z)) {
                    commandBuffer.SetUniformVector3("uAlbedoColor", effectiveAlbedo);
                    lastAlbedoColor = effectiveAlbedo;
                }
                if (item.renderLayers != lastRenderLayers) {
                    commandBuffer.SetUniformInt("uMeshRenderLayers", static_cast<std::int32_t>(item.renderLayers));
                    lastRenderLayers = item.renderLayers;
                }
                if (item.opacity != lastOpacity) {
                    commandBuffer.SetUniformFloat("uSurfaceOpacity", item.opacity);
                    lastOpacity = item.opacity;
                }
            }
            if (meshResources.vertexBuffer != lastVertexBuffer) {
                commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                lastVertexBuffer = meshResources.vertexBuffer;
            }
            if (meshResources.indexBuffer != lastIndexBuffer) {
                commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                lastIndexBuffer = meshResources.indexBuffer;
            }
            const std::uint32_t count = item.indexCount == 0U ? meshResources.indexCount : item.indexCount;
            const std::uint32_t first = item.indexCount == 0U ? 0U : item.firstIndex;
            commandBuffer.DrawIndexed(count, 1U, first);
            ++drawCalls;
        }
        return drawCalls;
    }

    /// Records consecutive UI quads as texture-bound batches. Solid quads sample the white
    /// fallback, images use their resolved asset textures, and glyphs sample the font atlas. Only
    /// adjacent quads sharing a texture are combined, preserving the UIDrawBatch paint order
    /// even when differently-sorted canvases interleave text and images. A not-yet-ready image or
    /// unavailable font atlas is omitted for this frame and retried on the next one.
    [[nodiscard]] std::size_t RecordUIOverlayItems(const UI::UIDrawBatch& batch,
                                                       const Math::Matrix4x4& projection,
                                                       ICommandBuffer& commandBuffer) {
        if (!uiOverlayProgram->IsValid() || batch.quads.empty() || uiVertexBuffer == kInvalidBufferHandle) {
            return 0U;
        }
        const std::size_t maxVertices = kMaximumUIQuads * kUIVerticesPerQuad;
        const auto appendQuad = [this](const UI::UIQuad& quad) {
            const float x0 = quad.rect.position.x;
            const float y0 = quad.rect.position.y;
            const float x1 = quad.rect.position.x + quad.rect.size.x;
            const float y1 = quad.rect.position.y + quad.rect.size.y;
            const auto makeVertex = [&quad](const float x, const float y, const float u, const float v) {
                return UIVertex{x, y, u, v, quad.color.x, quad.color.y, quad.color.z, quad.alpha};
            };
            uiVertexStaging.push_back(makeVertex(x0, y0, quad.u0, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y0, quad.u1, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y1, quad.u1, quad.v1));
            uiVertexStaging.push_back(makeVertex(x0, y0, quad.u0, quad.v0));
            uiVertexStaging.push_back(makeVertex(x1, y1, quad.u1, quad.v1));
            uiVertexStaging.push_back(makeVertex(x0, y1, quad.u0, quad.v1));
        };
        const auto drawQuads = [this, &appendQuad, &projection, &commandBuffer](
                                   const TextureHandle texture,
                                   const std::vector<UI::UIQuad>& quads) -> bool {
            uiVertexStaging.clear();
            for (const UI::UIQuad& quad : quads) {
                if (uiVertexStaging.size() + kUIVerticesPerQuad > maxVertices) {
                    break;
                }
                appendQuad(quad);
            }
            if (uiVertexStaging.empty() ||
                !renderDevice.UpdateBuffer(uiVertexBuffer, std::as_bytes(std::span(uiVertexStaging)))) {
                return false;
            }
            uiOverlayProgram->SetMatrix4x4("uProjection", projection);
            uiOverlayProgram->SetInt("uSourceTexture", 0);
            uiOverlayProgram->ApplyTo(commandBuffer);
            commandBuffer.BindTexture(texture, 0U);
            if (clampLinearSampler != kInvalidSamplerHandle) {
                commandBuffer.BindSampler(clampLinearSampler, 0U);
            }
            commandBuffer.BindVertexBuffer(uiVertexBuffer);
            commandBuffer.Draw(static_cast<std::uint32_t>(uiVertexStaging.size()));
            return true;
        };

        struct UITextureBatch final {
            TextureHandle texture = kInvalidTextureHandle;
            std::vector<UI::UIQuad> quads;
        };
        std::vector<UITextureBatch> orderedTextureBatches;
        for (const UI::UIQuad& quad : batch.quads) {
            TextureHandle texture = kInvalidTextureHandle;
            switch (quad.kind) {
            case UI::UIDrawItemKind::SolidColor:
                texture = fallbackWhiteTexture;
                break;
            case UI::UIDrawItemKind::Image: {
                const std::optional<TextureHandle> resolved =
                    ResolveTextureGpuHandle(quad.imageAssetGuid, fallbackWhiteTexture);
                if (!resolved.has_value()) {
                    continue; // The async load remains live; the image appears once its texture is ready.
                }
                texture = *resolved;
                break;
            }
            case UI::UIDrawItemKind::Glyph:
                if (uiFontAtlasTexture == kInvalidTextureHandle) {
                    continue;
                }
                texture = uiFontAtlasTexture;
                break;
            case UI::UIDrawItemKind::View3D: {
                const auto capture = uiViewCaptures.find(quad.viewKey);
                if (capture == uiViewCaptures.end() || !capture->second.ready) {
                    continue; // drawn from the next frame, once its stage has been captured
                }
                texture = capture->second.color;
                break;
            }
            }
            if (orderedTextureBatches.empty() || orderedTextureBatches.back().texture != texture ||
                orderedTextureBatches.back().quads.size() >= kMaximumUIQuads) {
                orderedTextureBatches.push_back(UITextureBatch{texture, {}});
            }
            orderedTextureBatches.back().quads.push_back(quad);
        }

        std::size_t drawCalls = 0U;
        for (const UITextureBatch& batchForTexture : orderedTextureBatches) {
            if (drawQuads(batchForTexture.texture, batchForTexture.quads)) {
                ++drawCalls;
            }
        }
        return drawCalls;
    }

};

Renderer3D::Renderer3D(IRenderDevice& renderDevice, IRenderSystem& renderSystem,
                              IMeshRenderer& meshRenderer, ICameraSystem& cameraSystem,
                              ILightSystem& lightSystem, Shader::IShaderManager& shaderManager,
                              Asset::IAssetManager& assetManager, Asset::IAssetDatabase& assetDatabase,
                              Events::IEventSystem& eventSystem, std::uint32_t targetWidth,
                              std::uint32_t targetHeight, Math::Vector3 ambientColor,
                              std::uint32_t shadowMapResolution, float shadowMapHalfExtent,
                              float shadowMapNearPlane, float shadowMapFarPlane, float shadowFrustumPadding,
                              float shadowCascadeSplitLambda, float shadowCascadeBlendRatio,
                              std::uint32_t shadowPcfKernelRadius)
    : m_impl(std::make_unique<Impl>(renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
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
    m_impl->rendererProfileResolution = ResolveEffectiveRendererProfile(
        Platform::DefaultRendererProfileForHost(), renderDevice.GetCapabilities());
    if (std::optional<Impl::SizedTargetSet> initialSet =
            m_impl->CreateTargetSet(targetWidth, targetHeight);
        initialSet.has_value()) {
        const auto inserted =
            m_impl->targetSetCache.emplace(std::pair{targetWidth, targetHeight}, *initialSet);
        m_impl->ActivateTargetSet(inserted.first->second, targetWidth, targetHeight);
    } else {
        UVE_ERROR("Renderer3D: main render target creation failed; frame rendering will be skipped");
    }
    m_impl->fallbackWhiteTexture = renderDevice.CreateTexture(
        TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1}, std::as_bytes(std::span(kWhitePixel)));
    // Tier 2.3: the probe sampler's cube fallback (six white faces, level-major).
    std::array<std::uint8_t, 24U> whiteCubePixels{};
    whiteCubePixels.fill(0xFF);
    TextureDesc whiteCubeDesc{1, 1, TextureFormat::RGBA8Unorm, 1};
    whiteCubeDesc.type = TextureType::Cubemap;
    whiteCubeDesc.arrayLayers = 6U;
    m_impl->fallbackWhiteCubeTexture = renderDevice.CreateTexture(
        whiteCubeDesc, std::as_bytes(std::span(whiteCubePixels)));
    m_impl->fallbackNormalTexture = renderDevice.CreateTexture(
        TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1}, std::as_bytes(std::span(kFlatNormalPixel)));
    for (TextureHandle& shadowMapTarget : m_impl->shadowMapTargets) {
        shadowMapTarget = renderDevice.CreateTexture(
            TextureDesc{shadowMapResolution, shadowMapResolution, TextureFormat::Depth32Float, 1});
    }

    // Tier 2.2: the shadow/depth point sampler — point/point, no mips, clamp-to-edge.
    // Creation failure is non-fatal (invalid handle ⇒ binds skipped).
    SamplerDesc shadowSamplerDesc;
    shadowSamplerDesc.magFilter = SamplerFilter::Point;
    shadowSamplerDesc.minFilter = SamplerFilter::Point;
    shadowSamplerDesc.mipMode = SamplerMipMode::None;
    m_impl->shadowPointSampler = renderDevice.CreateSampler(shadowSamplerDesc);

    m_impl->RebuildMaterialRepeatSampler();

    SamplerDesc clampLinearDesc;
    clampLinearDesc.magFilter = SamplerFilter::Linear;
    clampLinearDesc.minFilter = SamplerFilter::Linear;
    clampLinearDesc.mipMode = SamplerMipMode::None;
    clampLinearDesc.wrapU = SamplerWrap::ClampToEdge;
    clampLinearDesc.wrapV = SamplerWrap::ClampToEdge;
    clampLinearDesc.wrapW = SamplerWrap::ClampToEdge;
    clampLinearDesc.maxAnisotropy = 1.0F;
    m_impl->clampLinearSampler = renderDevice.CreateSampler(clampLinearDesc);

    SamplerDesc envEquirectDesc;
    envEquirectDesc.magFilter = SamplerFilter::Linear;
    envEquirectDesc.minFilter = SamplerFilter::Linear;
    envEquirectDesc.mipMode = SamplerMipMode::None;
    envEquirectDesc.wrapU = SamplerWrap::Repeat;
    envEquirectDesc.wrapV = SamplerWrap::ClampToEdge;
    envEquirectDesc.wrapW = SamplerWrap::ClampToEdge;
    envEquirectDesc.maxAnisotropy = 1.0F;
    m_impl->environmentEquirectSampler = renderDevice.CreateSampler(envEquirectDesc);

    Shader::ShaderProgramDesc shadowProgramDesc;
    shadowProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kShadowDepthVirtualPath);
    shadowProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kShadowDepthSource);
    shadowProgramDesc.vertexLayout = MeshVertexLayout();
    shadowProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
    shadowProgramDesc.depthTestEnabled = true;
    shadowProgramDesc.depthWriteEnabled = true;
    // Tier 2.1: the shadow-depth pipeline opts OUT of hardware depth bias with explicit zeros
    // rather than the defaults, so a future default change can't silently bias shadow maps.
    // (The instanced twin below inherits these via the copy.)
    shadowProgramDesc.depthBiasEnabled = false;
    shadowProgramDesc.depthBiasConstantFactor = 0.0F;
    shadowProgramDesc.depthBiasSlopeFactor = 0.0F;
    shadowProgramDesc.debugName = "ShadowDepth";
    m_impl->shadowProgram = shaderManager.CreateProgram(shadowProgramDesc);

    // The instanced twin of the shadow program requires SSBO transforms (std430), so gate on real
    // storage-buffer capability rather than compile-time OS macros.
    if (renderDevice.GetCapabilities().storageBuffers != RenderFeatureSupport::Unsupported) {
        Shader::ShaderProgramDesc instancedShadowProgramDesc = shadowProgramDesc;
        instancedShadowProgramDesc.extraDefines.emplace_back("UVE_INSTANCED", "1");
        instancedShadowProgramDesc.debugName = "ShadowDepthInstanced";
        m_impl->instancedShadowProgram = shaderManager.CreateProgram(instancedShadowProgramDesc);
    }

    Shader::ShaderProgramDesc toneMappingProgramDesc;
    toneMappingProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kFullscreenQuadVirtualPath);
    toneMappingProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kFullscreenQuadSource);
    toneMappingProgramDesc.depthTestEnabled = false;
    toneMappingProgramDesc.depthWriteEnabled = false;
    toneMappingProgramDesc.debugName = "ToneMapping";
    m_impl->toneMappingProgram = shaderManager.CreateProgram(toneMappingProgramDesc);

    Shader::ShaderProgramDesc proceduralSkyProgramDesc;
    proceduralSkyProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kProceduralSkyVirtualPath);
    proceduralSkyProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kProceduralSkySource);
    proceduralSkyProgramDesc.depthTestEnabled = false;
    proceduralSkyProgramDesc.depthWriteEnabled = false;
    proceduralSkyProgramDesc.debugName = "ProceduralSky";
    m_impl->proceduralSkyProgram = shaderManager.CreateProgram(proceduralSkyProgramDesc);

    Shader::ShaderProgramDesc bloomBrightPassProgramDesc;
    bloomBrightPassProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBloomBrightPassVirtualPath);
    bloomBrightPassProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBloomBrightPassSource);
    bloomBrightPassProgramDesc.depthTestEnabled = false;
    bloomBrightPassProgramDesc.depthWriteEnabled = false;
    bloomBrightPassProgramDesc.debugName = "BloomBrightPass";
    m_impl->bloomBrightPassProgram = shaderManager.CreateProgram(bloomBrightPassProgramDesc);

    Shader::ShaderProgramDesc bloomDownsampleProgramDesc;
    bloomDownsampleProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBloomDownsampleVirtualPath);
    bloomDownsampleProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBloomDownsampleSource);
    bloomDownsampleProgramDesc.depthTestEnabled = false;
    bloomDownsampleProgramDesc.depthWriteEnabled = false;
    bloomDownsampleProgramDesc.debugName = "BloomDownsample";
    m_impl->bloomDownsampleProgram = shaderManager.CreateProgram(bloomDownsampleProgramDesc);

    Shader::ShaderProgramDesc bloomBlurProgramDesc;
    bloomBlurProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kBloomBlurVirtualPath);
    bloomBlurProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBloomBlurSource);
    bloomBlurProgramDesc.depthTestEnabled = false;
    bloomBlurProgramDesc.depthWriteEnabled = false;
    bloomBlurProgramDesc.debugName = "BloomBlur";
    m_impl->bloomBlurProgram = shaderManager.CreateProgram(bloomBlurProgramDesc);

    Shader::ShaderProgramDesc ssaoProgramDesc;
    ssaoProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kSsaoVirtualPath);
    ssaoProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kSsaoSource);
    ssaoProgramDesc.depthTestEnabled = false;
    ssaoProgramDesc.depthWriteEnabled = false;
    ssaoProgramDesc.debugName = "SSAO";
    m_impl->ssaoProgram = shaderManager.CreateProgram(ssaoProgramDesc);

    Shader::ShaderProgramDesc ssaoBlurProgramDesc;
    ssaoBlurProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kFullscreenCopyVirtualPath);
    ssaoBlurProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kFullscreenCopySource);
    ssaoBlurProgramDesc.depthTestEnabled = false;
    ssaoBlurProgramDesc.depthWriteEnabled = false;
    ssaoBlurProgramDesc.blendMode = PipelineBlendMode::Opaque;
    ssaoBlurProgramDesc.debugName = "SSAOBlur";
    m_impl->ssaoBlurProgram = shaderManager.CreateProgram(ssaoBlurProgramDesc);

    Shader::ShaderProgramDesc contactShadowProgramDesc;
    contactShadowProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kContactShadowVirtualPath);
    contactShadowProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kContactShadowSource);
    contactShadowProgramDesc.depthTestEnabled = false;
    contactShadowProgramDesc.depthWriteEnabled = false;
    contactShadowProgramDesc.debugName = "ContactShadow";
    m_impl->contactShadowProgram = shaderManager.CreateProgram(contactShadowProgramDesc);

    Shader::ShaderProgramDesc ssrProgramDesc;
    ssrProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kSsrVirtualPath);
    ssrProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kSsrSource);
    ssrProgramDesc.depthTestEnabled = false;
    ssrProgramDesc.depthWriteEnabled = false;
    ssrProgramDesc.debugName = "SSR";
    m_impl->ssrProgram = shaderManager.CreateProgram(ssrProgramDesc);

    Shader::ShaderProgramDesc taaProgramDesc;
    taaProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kTaaResolveVirtualPath);
    taaProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kTaaResolveSource);
    taaProgramDesc.depthTestEnabled = false;
    taaProgramDesc.depthWriteEnabled = false;
    taaProgramDesc.debugName = "TAAResolve";
    m_impl->taaResolveProgram = shaderManager.CreateProgram(taaProgramDesc);

    Shader::ShaderProgramDesc wboitCompositeDesc;
    wboitCompositeDesc.virtualFilePath = std::string(Shader::BuiltIn::kWboitCompositeVirtualPath);
    wboitCompositeDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kWboitCompositeSource);
    wboitCompositeDesc.depthTestEnabled = false;
    wboitCompositeDesc.depthWriteEnabled = false;
    wboitCompositeDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
    wboitCompositeDesc.debugName = "WBOITComposite";
    m_impl->wboitCompositeProgram = shaderManager.CreateProgram(wboitCompositeDesc);

    if (m_impl->rendererProfileResolution.effective == Platform::RendererProfile::ForwardPlus &&
        renderDevice.GetCapabilities().halfFloatColorTargets != RenderFeatureSupport::Unsupported) {
        Shader::ShaderProgramDesc wboitLitDesc;
        wboitLitDesc.virtualFilePath = std::string(Shader::BuiltIn::kLitShadowed3DVirtualPath);
        wboitLitDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kLitShadowed3DSource);
        wboitLitDesc.vertexLayout = MeshVertexLayout();
        wboitLitDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
        wboitLitDesc.depthTestEnabled = true;
        wboitLitDesc.depthWriteEnabled = false;
        wboitLitDesc.blendMode = PipelineBlendMode::Additive;
        wboitLitDesc.cullMode = CullMode::None;
        wboitLitDesc.extraDefines.emplace_back("UVE_WBOIT", "1");
        wboitLitDesc.debugName = "LitWBOIT";
        m_impl->wboitLitProgram = shaderManager.CreateProgram(wboitLitDesc);
    }

    Shader::ShaderProgramDesc particleProgramDesc;
    particleProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kParticleVirtualPath);
    particleProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kParticleSource);
    particleProgramDesc.vertexLayout = {
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, offsetof(ParticleVertex, position)},
        VertexAttribute{"COLOR", VertexAttributeFormat::Float4, offsetof(ParticleVertex, red)},
    };
    particleProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(ParticleVertex));
    particleProgramDesc.depthTestEnabled = true;
    particleProgramDesc.depthWriteEnabled = false;
    particleProgramDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
    particleProgramDesc.debugName = "Particle";
    m_impl->particleProgram = shaderManager.CreateProgram(particleProgramDesc);
    m_impl->particleVertexStaging.reserve(kMaximumParticleGpuDrawCommands * kParticleVerticesPerCommand);
    m_impl->particleVertexBuffer = renderDevice.CreateBuffer(
        BufferDesc{sizeof(ParticleVertex) * kMaximumParticleGpuDrawCommands * kParticleVerticesPerCommand,
                      BufferUsage::Vertex, BufferMemory::Auto, BufferUpdateFrequency::Dynamic});

    Shader::ShaderProgramDesc decalProgramDesc;
    decalProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kDecalVirtualPath);
    decalProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kDecalSource);
    decalProgramDesc.vertexLayout = MeshVertexLayout();
    decalProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
    decalProgramDesc.depthTestEnabled = true;
    // Paint, not geometry: a decal is blended into the surface it landed on, so it must not stop the
    // surface behind it from being drawn - and it must not write depth either, or the next decal in
    // the same frame would be depth-rejected by one that is merely in front of it.
    decalProgramDesc.depthWriteEnabled = false;
    decalProgramDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
    decalProgramDesc.debugName = "Decal";
    m_impl->decalProgram = shaderManager.CreateProgram(decalProgramDesc);

    // The built-in lit material: `kLitShadowed3DSource` split into its two stages by the defines
    // the source itself branches on. Profile variants (`UVE_COMPATIBILITY`, `UVE_MOBILE_PROFILE`,
    // or full Forward+) are built lazily per effective RendererProfile.
    Shader::ShaderProgramStagesDesc defaultLitDesc;
    defaultLitDesc.vertexSource.stage = ShaderStage::Vertex;
    defaultLitDesc.vertexSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kLitShadowed3DSource);
    defaultLitDesc.vertexSource.extraDefines = {{"VERTEX_SHADER", "1"}};
    defaultLitDesc.vertexSource.debugName = "BuiltInLit vertex";
    defaultLitDesc.fragmentSource.stage = ShaderStage::Fragment;
    defaultLitDesc.fragmentSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kLitShadowed3DSource);
    defaultLitDesc.fragmentSource.extraDefines = {{"FRAGMENT_SHADER", "1"}};
    defaultLitDesc.fragmentSource.debugName = "BuiltInLit fragment";
    defaultLitDesc.vertexLayout = MeshVertexLayout();
    defaultLitDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
    defaultLitDesc.depthTestEnabled = true;
    defaultLitDesc.depthWriteEnabled = true;
    defaultLitDesc.cullMode = CullMode::Back;
    defaultLitDesc.debugName = "BuiltInLit default material";
    m_impl->defaultLitStagesDesc = defaultLitDesc;
    m_impl->EnsureDefaultLitProgramsForProfile(m_impl->rendererProfileResolution.effective);
    m_impl->defaultLitMaterialResources.supportsInstancing = false;
    m_impl->defaultLitMaterialResources.albedoTexture = m_impl->fallbackWhiteTexture;
    m_impl->defaultLitMaterialResources.normalTexture = m_impl->fallbackNormalTexture;
    m_impl->defaultLitMaterialResources.aoTexture = m_impl->fallbackWhiteTexture;
    m_impl->defaultLitMaterialResources.metallicRoughnessTexture = m_impl->fallbackWhiteTexture;
    m_impl->defaultLitMaterialResources.emissiveTexture = m_impl->fallbackWhiteTexture;

    // WP3 occlusion proxy: the mesh vertex layout (only aPosition is read, like shadow_depth),
    // depth-tested but never written, and an additive zero colour so nothing visible changes.
    Shader::ShaderProgramDesc occlusionProxyDesc;
    occlusionProxyDesc.virtualFilePath = std::string(Shader::BuiltIn::kOcclusionProxyVirtualPath);
    occlusionProxyDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kOcclusionProxySource);
    occlusionProxyDesc.vertexLayout = MeshVertexLayout();
    occlusionProxyDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
    occlusionProxyDesc.depthTestEnabled = true;
    occlusionProxyDesc.depthWriteEnabled = false;
    occlusionProxyDesc.depthCompare = DepthCompare::LessOrEqual;
    occlusionProxyDesc.blendMode = PipelineBlendMode::Additive;
    occlusionProxyDesc.debugName = "OcclusionProxy";
    m_impl->occlusionProxyProgram = shaderManager.CreateProgram(occlusionProxyDesc);

    Shader::ShaderProgramDesc uiOverlayProgramDesc;
    uiOverlayProgramDesc.virtualFilePath = std::string(Shader::BuiltIn::kUIOverlayVirtualPath);
    uiOverlayProgramDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kUIOverlaySource);
    uiOverlayProgramDesc.vertexLayout = {
        VertexAttribute{"POSITION", VertexAttributeFormat::Float2, offsetof(UIVertex, x)},
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, offsetof(UIVertex, u)},
        VertexAttribute{"COLOR", VertexAttributeFormat::Float4, offsetof(UIVertex, red)},
    };
    uiOverlayProgramDesc.vertexStride = static_cast<std::uint32_t>(sizeof(UIVertex));
    uiOverlayProgramDesc.depthTestEnabled = false;
    uiOverlayProgramDesc.depthWriteEnabled = false;
    uiOverlayProgramDesc.blendMode = PipelineBlendMode::SourceAlphaOver;
    uiOverlayProgramDesc.debugName = "UIOverlay";
    m_impl->uiOverlayProgram = shaderManager.CreateProgram(uiOverlayProgramDesc);
    m_impl->uiVertexStaging.reserve(kMaximumUIQuads * kUIVerticesPerQuad);
    m_impl->uiVertexBuffer = renderDevice.CreateBuffer(
        BufferDesc{sizeof(UIVertex) * kMaximumUIQuads * kUIVerticesPerQuad, BufferUsage::Vertex,
                   BufferMemory::Auto, BufferUpdateFrequency::Dynamic});

    m_impl->vfxSpriteRenderer.Initialize(renderDevice, shaderManager);

    Impl* const implPtr = m_impl.get();
    static_cast<void>(m_impl->indirectCull.Initialize());
    m_impl->reloadSubscription = eventSystem.Subscribe<Asset::AssetReloadedEvent>(
        [implPtr](const Asset::AssetReloadedEvent& event) { implPtr->OnAssetReloaded(event); });
    m_impl->loadCompletedSubscription = eventSystem.Subscribe<Asset::AssetLoadCompletedEvent>(
        [implPtr](const Asset::AssetLoadCompletedEvent& event) { implPtr->OnAssetLoadCompleted(event); });
}

Renderer3D::~Renderer3D() {
    m_impl->eventSystem.Unsubscribe(m_impl->reloadSubscription);
    m_impl->eventSystem.Unsubscribe(m_impl->loadCompletedSubscription);
    m_impl->vfxSpriteRenderer.Destroy(m_impl->renderDevice);
    m_impl->vfxMaterialHandles.clear();
    m_impl->vfxReferencedTextureGuids.clear();
    m_impl->DestroyOcclusionProbes();
    for (const auto& [guid, meshResources] : m_impl->meshCache) {
        DestroyBufferIfValid(m_impl->renderDevice, meshResources.vertexBuffer);
        DestroyBufferIfValid(m_impl->renderDevice, meshResources.indexBuffer);
    }
    for (const auto& [entity, skinned] : m_impl->skinnedMeshCache) {
        DestroyBufferIfValid(m_impl->renderDevice, skinned.resources.vertexBuffer);
    }
    for (const auto& [kind, meshResources] : m_impl->primitiveMeshCache) {
        DestroyBufferIfValid(m_impl->renderDevice, meshResources.vertexBuffer);
        DestroyBufferIfValid(m_impl->renderDevice, meshResources.indexBuffer);
    }
    for (const auto& [key, decalResources] : m_impl->decalBuffers) {
        static_cast<void>(key);
        DestroyBufferIfValid(m_impl->renderDevice, decalResources.vertexBuffer);
        DestroyBufferIfValid(m_impl->renderDevice, decalResources.indexBuffer);
    }
    // MaterialGpuResources holds shared ShaderProgram references only. Releasing the cache
    // lets ShaderManager-owned program deleters retire their pipelines exactly once.
    m_impl->materialCache.clear();
    for (const auto& [guid, textureHandle] : m_impl->textureCache) {
        DestroyTextureIfValid(m_impl->renderDevice, textureHandle);
    }
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->instanceTransformBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->instanceNormalTransformBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->instanceBaseBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->clusteredLightBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->clusteredCellBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->clusteredIndexBuffer);
    // The cache owns every size-dependent target, INCLUDING the active one - colorTarget and its
    // siblings are mirrors of a cached set's handles, not separate allocations. Destroying both
    // would be a double free, so the mirrors are only cleared, and are destroyed exactly once here
    // as cache entries.
    for (const auto& [cachedSize, cachedSet] : m_impl->targetSetCache) {
        static_cast<void>(cachedSize);
        m_impl->DestroyTargetSet(cachedSet);
    }
    m_impl->targetSetCache.clear();
    m_impl->colorTarget = kInvalidTextureHandle;
    m_impl->depthTarget = kInvalidTextureHandle;
    m_impl->bloomMipTargets = {};
    m_impl->bloomMipTargetCount = 0U;
    m_impl->ssaoTarget = kInvalidTextureHandle;
    m_impl->ssaoBlurTarget = kInvalidTextureHandle;
    m_impl->contactShadowTarget = kInvalidTextureHandle;
    m_impl->ssrTarget = kInvalidTextureHandle;
    for (auto& [entity, cache] : m_impl->reflectionProbeCaptures) {
        static_cast<void>(entity);
        m_impl->DestroyReflectionProbeGpuCache(cache);
    }
    m_impl->reflectionProbeCaptures.clear();
    for (const auto& [resolution, captureDepth] : m_impl->reflectionProbeCaptureDepths) {
        static_cast<void>(resolution);
        DestroyTextureIfValid(m_impl->renderDevice, captureDepth);
    }
    m_impl->reflectionProbeCaptureDepths.clear();
    DestroyTextureIfValid(m_impl->renderDevice, m_impl->fallbackWhiteTexture);
    DestroyTextureIfValid(m_impl->renderDevice, m_impl->fallbackWhiteCubeTexture);
    DestroyTextureIfValid(m_impl->renderDevice, m_impl->fallbackNormalTexture);
    for (const TextureHandle shadowMapTarget : m_impl->shadowMapTargets) {
        DestroyTextureIfValid(m_impl->renderDevice, shadowMapTarget);
    }
    DestroySamplerIfValid(m_impl->renderDevice, m_impl->shadowPointSampler);
    DestroySamplerIfValid(m_impl->renderDevice, m_impl->materialRepeatSampler);
    DestroySamplerIfValid(m_impl->renderDevice, m_impl->clampLinearSampler);
    DestroySamplerIfValid(m_impl->renderDevice, m_impl->environmentEquirectSampler);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->particleVertexBuffer);
    DestroyBufferIfValid(m_impl->renderDevice, m_impl->uiVertexBuffer);
    DestroyTextureIfValid(m_impl->renderDevice, m_impl->uiFontAtlasTexture);
    m_impl->DestroyStudioFloorResources();
}

bool Renderer3D::ResizeTargets(const std::uint32_t width, const std::uint32_t height) {
    return m_impl->ResizeTargets(width, height);
}

void Renderer3D::CaptureDueReflectionProbes(Scene::IEntityManager& entityManager,
                                                 const Scene::Entity cameraEntity) {
    m_impl->EvictDeadReflectionProbeCaptures(entityManager);
    struct Due final {
        Scene::Entity entity;
        Scene::ReflectionProbe3DFrame frame;
        std::uint32_t generation = 0;
    };
    std::vector<Due> due;
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::ReflectionProbe3DComponent>(
        [this, &due](const Scene::Entity entity, const Scene::WorldTransformComponent& world,
                     const Scene::ReflectionProbe3DComponent& probe) {
            if (world.dirty || !probe.enabled || !probe.capturedOnce ||
                !Scene::IsReflectionProbe3DObjectComponentValid(probe)) {
                return;
            }
            Scene::ReflectionProbe3DFrame frame{};
            if (!Scene::TryMakeReflectionProbe3DFrame(probe, world.worldPosition, world.worldRotation, frame)) {
                return;
            }
            frame.entity = entity;
            const auto cacheIt = m_impl->reflectionProbeCaptures.find(entity);
            if (cacheIt != m_impl->reflectionProbeCaptures.end() && cacheIt->second.ready &&
                cacheIt->second.captureGeneration == probe.captureGeneration &&
                cacheIt->second.captureResolution == frame.captureResolution) {
                return;
            }
            due.push_back(Due{entity, frame, probe.captureGeneration});
        });
    if (due.empty()) {
        return;
    }
    const std::uint32_t savedWidth = m_impl->targetWidth;
    const std::uint32_t savedHeight = m_impl->targetHeight;
    m_impl->probeCaptureViewActive = true;
    std::size_t captured = 0U;
    for (Due& candidate : due) {
        if (captured >= Scene::kMaximumReflectionProbeCapturesPerTick) {
            break;
        }
        const std::uint32_t resolution = candidate.frame.captureResolution;
        if (!m_impl->ResizeTargets(resolution, resolution)) {
            continue;
        }
        const TextureHandle captureDepth = m_impl->EnsureReflectionProbeCaptureDepth(resolution);
        if (captureDepth == kInvalidTextureHandle) {
            continue;
        }
        Impl::ReflectionProbeGpuCache& cache = m_impl->reflectionProbeCaptures[candidate.entity];
        if (!m_impl->EnsureReflectionProbeCube(cache, resolution)) {
            m_impl->reflectionProbeCaptures.erase(candidate.entity);
            continue;
        }
        const float maxHalf = std::fmax(candidate.frame.halfExtents.x,
                                        std::fmax(candidate.frame.halfExtents.y, candidate.frame.halfExtents.z));
        m_impl->probeCaptureVisibilityLayers = candidate.frame.visibilityLayers;
        m_impl->probeCapturePosition = candidate.frame.worldPosition;
        m_impl->probeCaptureNear = 0.05F;
        m_impl->probeCaptureFar = std::fmax(50.0F, maxHalf * 8.0F);
        if (!(m_impl->probeCaptureFar > m_impl->probeCaptureNear)) {
            continue;
        }
        bool facesOk = true;
        for (std::size_t faceIndex = 0; faceIndex < Scene::kReflectionProbeCubemapFaceCount; ++faceIndex) {
            Math::Quaternion rotation{};
            if (!Scene::TryMakeCubemapFaceCameraRotation(static_cast<Scene::CubemapFace>(faceIndex),
                                                            rotation)) {
                facesOk = false;
                break;
            }
            m_impl->probeCaptureRotation = rotation;
            RenderFrameToTarget(entityManager, cameraEntity, cache.cube, captureDepth, resolution,
                                   resolution, static_cast<std::uint32_t>(faceIndex));
        }
        if (facesOk) {
            cache.captureGeneration = candidate.generation;
            cache.ready = true;
            ++captured;
        } else {
            m_impl->DestroyReflectionProbeGpuCache(cache);
            m_impl->reflectionProbeCaptures.erase(candidate.entity);
        }
    }
    m_impl->probeCaptureViewActive = false;
    m_impl->probeCaptureVisibilityLayers = 0xFFFFFFFFU;
    static_cast<void>(m_impl->ResizeTargets(savedWidth, savedHeight));
}

void Renderer3D::CaptureUIViews(Scene::IEntityManager& entityManager, const Scene::Entity cameraEntity) {
    if (m_impl->uiRuntimeForFrame == nullptr || !entityManager.IsAlive(cameraEntity) ||
        !entityManager.HasComponent<Scene::CameraComponent>(cameraEntity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(cameraEntity)) {
        return;
    }
    // Only the 3D Views the UI drew last: a hidden menu costs nothing.
    std::vector<std::pair<std::uint64_t, Math::Vector2>> shown;
    for (const UI::UIQuad& quad : m_impl->uiRuntimeForFrame->GetDrawBatch().quads) {
        if (quad.kind == UI::UIDrawItemKind::View3D) {
            shown.emplace_back(quad.viewKey, quad.rect.size);
        }
    }
    for (auto it = m_impl->uiViewCaptures.begin(); it != m_impl->uiViewCaptures.end();) {
        const bool stillShown = std::any_of(shown.begin(), shown.end(), [&it](const auto& view) { return view.first == it->first; });
        if (!stillShown) {
            m_impl->DestroyUIViewCapture(it->second);
            it = m_impl->uiViewCaptures.erase(it);
        } else {
            ++it;
        }
    }
    if (shown.empty()) {
        return;
    }
    const std::uint32_t savedWidth = m_impl->targetWidth;
    const std::uint32_t savedHeight = m_impl->targetHeight;
    const std::optional<ViewportRect> savedRegion = m_impl->destinationViewportOverride;
    m_impl->destinationViewportOverride.reset();
    m_impl->probeCaptureViewActive = true;
    m_impl->captureTransparentBackground = true;
    m_impl->uiViewCaptureActive = true;
    for (const auto& [key, size] : shown) {
        const Scene::Entity widget{static_cast<std::uint32_t>(key & 0xFFFFFFFFU), static_cast<std::uint32_t>(key >> 32U)};
        if (!entityManager.IsAlive(widget) || !entityManager.HasComponent<Scene::UIView3DComponent>(widget)) {
            continue;
        }
        const Scene::UIView3DComponent& view = entityManager.GetComponent<Scene::UIView3DComponent>(widget);
        if (!entityManager.IsAlive(view.stageRoot)) {
            continue;
        }
        const std::uint32_t width = static_cast<std::uint32_t>(std::clamp(std::lround(size.x), 16L, 2048L));
        const std::uint32_t height = static_cast<std::uint32_t>(std::clamp(std::lround(size.y), 16L, 2048L));
        Impl::UIViewCapture& capture = m_impl->uiViewCaptures[key];
        if (capture.width != width || capture.height != height) {
            m_impl->DestroyUIViewCapture(capture);
            capture = Impl::UIViewCapture{};
            capture.color = m_impl->renderDevice.CreateTexture(TextureDesc{width, height, TextureFormat::RGBA8Unorm, 1});
            capture.depth = m_impl->renderDevice.CreateTexture(TextureDesc{width, height, TextureFormat::Depth32Float, 1});
            capture.width = width;
            capture.height = height;
        }
        if (capture.color == kInvalidTextureHandle || capture.depth == kInvalidTextureHandle ||
            !m_impl->ResizeTargets(width, height)) {
            continue;
        }
        // The widget's camera, around its stage: back from the look-at point along its tilt.
        const Math::Vector3 stage = Scene::UIView3DStagePosition(view.stageSlot);
        const Math::Vector3 target{stage.x, stage.y + view.lookAtHeight, stage.z};
        const float pitch = view.pitchDegrees * (Math::kPi / 180.0F);
        Math::Quaternion rotation{};
        if (!Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F}, pitch, rotation)) {
            continue;
        }
        const Math::Vector3 forward = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
        m_impl->probeCapturePosition = target - forward * view.cameraDistance;
        m_impl->probeCaptureRotation = rotation;
        m_impl->probeCaptureFovY = view.fieldOfViewDegrees * (Math::kPi / 180.0F);
        if (view.useWidgetStage && view.stageCameraValid) {
            // The widget's Stage: through its StageCamera, as the Stage tab frames it.
            m_impl->probeCapturePosition = view.stageCameraPosition;
            m_impl->probeCaptureRotation = view.stageCameraRotation;
            m_impl->probeCaptureFovY = std::clamp(view.stageCameraFieldOfViewDegrees, 5.0F, 150.0F) * (Math::kPi / 180.0F);
        }
        m_impl->captureTransparentBackground = view.transparentBackground;
        m_impl->probeCaptureNear = 0.05F;
        m_impl->probeCaptureFar = std::max(20.0F, view.cameraDistance * 6.0F);
        m_impl->probeCaptureVisibilityLayers = 0xFFFFFFFFU;
        RenderFrameToTarget(entityManager, cameraEntity, capture.color, capture.depth, width, height, 0U);
        capture.ready = true;
    }
    m_impl->probeCaptureViewActive = false;
    m_impl->captureTransparentBackground = false;
    m_impl->uiViewCaptureActive = false;
    m_impl->probeCaptureFovY = Math::kPi * 0.5F;
    m_impl->destinationViewportOverride = savedRegion;
    static_cast<void>(m_impl->ResizeTargets(savedWidth, savedHeight));
}

void Renderer3D::RenderFrame(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity) {
    if (!m_impl->probeCaptureViewActive) {
        CaptureUIViews(entityManager, cameraEntity);
    }
    const bool capturingProbe = m_impl->probeCaptureViewActive;
    if (!capturingProbe) {
        m_impl->lastFrameDiagnostics = Renderer3DFrameDiagnostics{};
        m_impl->deferredMeshesThisFrame.clear();
        // Reset here, not beside the main pass: the shadow cascades are recorded BEFORE it, so a reset
        // at the main pass would discard the count this counter exists to report.
        m_impl->shadowInstancedDrawCallsThisFrame = 0U;
        m_impl->indirectInstancedDrawCallsThisFrame = 0U;
        m_impl->nextIndirectCullIndex = 0U;
        m_impl->lastFrameDiagnostics.renderTargetWidth = m_impl->targetWidth;
        m_impl->lastFrameDiagnostics.renderTargetHeight = m_impl->targetHeight;
    }
    if (m_impl->colorTarget == kInvalidTextureHandle || m_impl->depthTarget == kInvalidTextureHandle) {
        return;
    }
    const Scene::CameraComponent& camera =
        entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    const bool cameraValid = Scene::IsCameraComponentValid(camera);
    UVE_ASSERT(cameraValid);
    if (!cameraValid) {
        UVE_ERROR("Renderer3D: RenderFrame received invalid camera parameters");
        return;
    }
    const Scene::WorldTransformComponent& cameraWorldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(cameraEntity);
    Math::Quaternion normalizedCameraRotation;
    const bool cameraTransformValid =
        Math::IsFinite(cameraWorldTransform.worldPosition) &&
        Math::TryNormalize(cameraWorldTransform.worldRotation, normalizedCameraRotation);
    UVE_ASSERT(cameraTransformValid);
    if (!cameraTransformValid) {
        UVE_ERROR("Renderer3D: RenderFrame received an invalid camera world transform");
        return;
    }
    if (m_impl->targetWidth == 0U || m_impl->targetHeight == 0U) {
        UVE_ERROR("Renderer3D: RenderFrame cannot render to a zero-sized target");
        return;
    }
    if (!capturingProbe) {
        CaptureDueReflectionProbes(entityManager, cameraEntity);
        m_impl->lastFrameDiagnostics = Renderer3DFrameDiagnostics{};
        m_impl->deferredMeshesThisFrame.clear();
        m_impl->shadowInstancedDrawCallsThisFrame = 0U;
        m_impl->indirectInstancedDrawCallsThisFrame = 0U;
        m_impl->nextIndirectCullIndex = 0U;
        m_impl->lastFrameDiagnostics.renderTargetWidth = m_impl->targetWidth;
        m_impl->lastFrameDiagnostics.renderTargetHeight = m_impl->targetHeight;
    } else {
        m_impl->ClearBoundReflectionProbe();
    }
    const float aspectRatio = static_cast<float>(m_impl->targetWidth) / static_cast<float>(m_impl->targetHeight);
    const bool aspectRatioValid = std::isfinite(aspectRatio) && aspectRatio > 0.0F;
    UVE_ASSERT(aspectRatioValid);
    if (!aspectRatioValid) {
        UVE_ERROR("Renderer3D: RenderFrame computed an invalid target aspect ratio");
        return;
    }
    m_impl->environmentFrame = Scene::ResolveWorldEnvironmentFrame(entityManager, m_impl->ambientColor);
    // The sky's clock. Advanced here, after the environment is resolved, rather than in the sky
    // pass: a frame that skips the sky - a probe capture, a frame with no environment - must not
    // stop time for the next one that draws it, and a sky told not to animate must not advance.
    // The timestamp is recorded either way, so freezing and resuming does not accumulate the
    // elapsed time into one jump.
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (m_impl->skyClockLast.time_since_epoch().count() != 0 && m_impl->environmentFrame.skyAnimated) {
            const float elapsed = std::chrono::duration<float>(now - m_impl->skyClockLast).count();
            // Clamped: a breakpoint, a stall or a window drag must not teleport the clouds across
            // the sky on the frame the program resumes.
            m_impl->skyTimeSeconds += std::clamp(elapsed, 0.0F, 0.1F);
        }
        m_impl->skyClockLast = now;
    }
    // Editor preview environment, applied strictly in the gap a real WorldEnvironment would fill.
    // The check is on the RESOLVED frame rather than on the injected value so a scene that gains a
    // WorldEnvironment mid-frame immediately takes priority, with no editor bookkeeping to reset.
    const bool usingPreviewEnvironment =
        !m_impl->environmentFrame.hasEnvironment && m_impl->previewEnvironment.has_value();
    if (usingPreviewEnvironment) {
        m_impl->environmentFrame = *m_impl->previewEnvironment;
    }
    const bool isOrthographic =
        !capturingProbe && camera.projection == Scene::CameraProjectionMode::Orthographic;
    const float orthoHalfHeight = isOrthographic ? camera.orthographicSize : 0.0F;
    const Asset::AssetGuid previousSkyTextureGuid = m_impl->skyTextureGuid;
    m_impl->skyTextureGuid = Asset::kInvalidAssetGuid;
    m_impl->skyTextureHandle = kInvalidTextureHandle;
    m_impl->skyTextureEnabled = false;
    if (!m_impl->environmentFrame.skyAssetPath.empty()) {
        m_impl->skyTextureGuid = m_impl->assetDatabase.Register(m_impl->environmentFrame.skyAssetPath);
        const std::optional<TextureHandle> resolvedSkyTexture =
            m_impl->ResolveTextureGpuHandle(m_impl->skyTextureGuid, kInvalidTextureHandle);
        if (resolvedSkyTexture.has_value() && *resolvedSkyTexture != kInvalidTextureHandle) {
            m_impl->skyTextureHandle = *resolvedSkyTexture;
            m_impl->skyTextureEnabled = true;
        }
    }
    const Asset::AssetGuid previousLutTextureGuid = m_impl->lutTextureGuid;
    m_impl->lutTextureGuid = Asset::kInvalidAssetGuid;
    m_impl->lutTextureHandle = kInvalidTextureHandle;
    m_impl->lutTextureEnabled = false;
    if (m_impl->environmentFrame.lutEnabled && !m_impl->environmentFrame.lutAssetPath.empty()) {
        m_impl->lutTextureGuid = m_impl->assetDatabase.Register(m_impl->environmentFrame.lutAssetPath);
        const std::optional<TextureHandle> resolvedLutTexture =
            m_impl->ResolveTextureGpuHandle(m_impl->lutTextureGuid, kInvalidTextureHandle);
        if (resolvedLutTexture.has_value() && *resolvedLutTexture != kInvalidTextureHandle) {
            m_impl->lutTextureHandle = *resolvedLutTexture;
            m_impl->lutTextureEnabled = true;
        }
    }
    if (previousSkyTextureGuid != m_impl->skyTextureGuid || previousLutTextureGuid != m_impl->lutTextureGuid) {
        m_impl->EvictUnreferencedTextureCacheEntries();
    }
    Math::Matrix4x4 viewProjection{};
    Math::Vector3 viewPosition{};
    Math::Quaternion viewRotation = normalizedCameraRotation;
    if (capturingProbe) {
        if (!Math::TryNormalize(m_impl->probeCaptureRotation, viewRotation)) {
            UVE_ERROR("Renderer3D: probe capture rotation is invalid");
            return;
        }
        viewPosition = m_impl->probeCapturePosition;
        m_impl->humanEyeEnabled = false;
        m_impl->humanEyeCenterScale = 1.0F;
        m_impl->environmentCameraNear = m_impl->probeCaptureNear;
        m_impl->environmentCameraFar = m_impl->probeCaptureFar;
        viewProjection = Math::Matrix4x4::Perspective(m_impl->probeCaptureFovY, aspectRatio, m_impl->probeCaptureNear,
                                                            m_impl->probeCaptureFar) *
                         Math::Matrix4x4::ViewFromPositionAndRotation(viewPosition, viewRotation);
        m_impl->skyTanHalfFov = std::tan(m_impl->probeCaptureFovY * 0.5F);
    } else {
        m_impl->humanEyeEnabled = camera.projection == Scene::CameraProjectionMode::HumanEye;
        m_impl->humanEyeCenterScale =
            m_impl->humanEyeEnabled ? Scene::HumanEyeCenterScale(aspectRatio) : 1.0F;
        m_impl->environmentCameraNear = camera.nearPlane;
        m_impl->environmentCameraFar = camera.farPlane;
        viewProjection = m_impl->cameraSystem.ComputeViewProjection(entityManager, cameraEntity, aspectRatio);
        viewPosition = m_impl->cameraSystem.GetWorldPosition(entityManager, cameraEntity);
        m_impl->lastSceneDepth = SceneDepthExport{};
        if (Math::TryInverse(viewProjection, m_impl->lastSceneDepth.inverseViewProjection)) {
            m_impl->lastSceneDepth.valid = m_impl->depthTarget != kInvalidTextureHandle;
            m_impl->lastSceneDepth.depthTexture = m_impl->depthTarget;
            m_impl->lastSceneDepth.width = m_impl->targetWidth;
            m_impl->lastSceneDepth.height = m_impl->targetHeight;
        }
        const float skyFovY = camera.projection == Scene::CameraProjectionMode::Orthographic
                                  ? 60.0F
                                  : Scene::VerticalFieldOfViewDegrees(camera, aspectRatio);
        m_impl->skyTanHalfFov = std::tan(skyFovY * (Math::kPi / 360.0F));
    }
    const bool motionBlurHistoryReady =
        !capturingProbe && m_impl->motionBlurHistoryValid &&
        m_impl->previousMotionBlurCameraEntity == cameraEntity &&
        std::fabs(m_impl->previousMotionBlurAspect - aspectRatio) <= 1.0e-4F;
    const Math::Matrix4x4 previousMotionBlurViewProjection =
        motionBlurHistoryReady ? m_impl->previousMotionBlurViewProjection : viewProjection;
    m_impl->humanEyeTexelX = 1.0F / static_cast<float>(m_impl->targetWidth);
    m_impl->humanEyeTexelY = 1.0F / static_cast<float>(m_impl->targetHeight);
    m_impl->environmentAspect = aspectRatio;
    m_impl->skyCameraRight = Math::RotateVector(viewRotation, Math::Vector3{1.0F, 0.0F, 0.0F});
    m_impl->skyCameraUp = Math::RotateVector(viewRotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    m_impl->skyCameraForward = Math::RotateVector(viewRotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    m_impl->lastFrameDiagnostics.primitiveProgramReady =
        m_impl->defaultLitMaterialResources.program != nullptr &&
        m_impl->defaultLitMaterialResources.program->IsValid();
    m_impl->lastFrameDiagnostics.particleProgramReady = m_impl->particleProgram->IsValid();
    m_impl->lastFrameDiagnostics.toneMappingProgramReady = m_impl->toneMappingProgram->IsValid();

    const Math::Frustum frustum = m_impl->cameraSystem.ExtractFrustum(viewProjection);
    // SSAO reconstructs view-space position from depth using only the projection step (see
    // ssao.glsl's doc comment) - a singular projection (never expected in practice for a valid
    // perspective camera) just means SSAO is skipped this frame, not a fatal error.
    const Math::Matrix4x4 projection =
        capturingProbe
            ? Math::Matrix4x4::Perspective(m_impl->probeCaptureFovY, aspectRatio, m_impl->probeCaptureNear,
                                                 m_impl->probeCaptureFar)
            : m_impl->cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, aspectRatio);
    Math::Matrix4x4 inverseProjection{};
    const bool projectionInvertible = Math::TryInverse(projection, inverseProjection);
    // Selected by contribution at the camera, not by whichever four the ECS happened to visit
    // first. The old order was not merely arbitrary - it could change when an unrelated entity was
    // created or destroyed, so a light could vanish from the player's face for no visible reason.
    const LightList lights =
        m_impl->lightSystem.ExtractActiveLightsForView(entityManager, viewPosition);
    m_impl->environmentCameraPosition = viewPosition;
    m_impl->sunEnergy = 0.0F;
    m_impl->sunVolumetricFogEnergy = 0.0F;
    m_impl->sunDirection = Math::Normalize(Math::Vector3{0.35F, 0.80F, 0.48F});
    m_impl->sunColor = Math::Vector3{1.0F, 1.0F, 1.0F};
    m_impl->sunDiscAngularDiameterDegrees = 0.0F;
    m_impl->sunDiscIntensity = 0.0F;
    m_impl->sunDiscLimbDarkening = 0.0F;
    const LightData* fallbackInternalDirectional = nullptr;
    for (const LightData& light : lights) {
        if (light.type != Scene::LightType::Directional || light.intensity <= 0.0F) {
            continue;
        }
        const Math::Vector3 incoming = Math::Vector3{-light.direction.x, -light.direction.y, -light.direction.z};
        if (!Math::IsFinite(incoming) || Math::LengthSquared(incoming) < 1.0e-8F) {
            continue;
        }
        if (light.isEditorInternal) {
            if (fallbackInternalDirectional == nullptr) {
                fallbackInternalDirectional = &light;
            }
            continue;
        }
        m_impl->sunDirection = Math::Normalize(incoming);
        m_impl->sunColor = Math::ToVector3(light.color);
        m_impl->sunEnergy = light.intensity;
        m_impl->sunDiscAngularDiameterDegrees = light.discAngularDiameterDegrees;
        m_impl->sunDiscIntensity = light.discIntensity;
        m_impl->sunDiscLimbDarkening = light.discLimbDarkening;
        m_impl->sunVolumetricFogEnergy = light.volumetricFogEnergy;
        fallbackInternalDirectional = nullptr;
        break;
    }
    if (fallbackInternalDirectional != nullptr) {
        const Math::Vector3 incoming = Math::Vector3{-fallbackInternalDirectional->direction.x,
                                                          -fallbackInternalDirectional->direction.y,
                                                          -fallbackInternalDirectional->direction.z};
        const bool previewSunDisc = fallbackInternalDirectional->castsShadows;
        m_impl->sunDirection = Math::Normalize(incoming);
        m_impl->sunColor = Math::ToVector3(fallbackInternalDirectional->color);
        m_impl->sunEnergy = fallbackInternalDirectional->intensity;
        m_impl->sunDiscAngularDiameterDegrees = previewSunDisc ? 3.8F : 0.0F;
        m_impl->sunDiscIntensity = previewSunDisc ? 1.0F : 0.0F;
        m_impl->sunDiscLimbDarkening = 0.0F;
        m_impl->sunVolumetricFogEnergy = fallbackInternalDirectional->volumetricFogEnergy;
    }
    if (!(usingPreviewEnvironment && fallbackInternalDirectional != nullptr)) {
        Scene::ApplySunToWorldEnvironmentFrame(m_impl->environmentFrame, m_impl->sunDirection, m_impl->sunColor,
                                                  m_impl->sunEnergy);
    }
    m_impl->fogVolumeCount =
        Scene::CollectFogVolume3DFrames(entityManager, viewPosition, m_impl->fogVolumes);
    if (!capturingProbe) {
        m_impl->BindReflectionProbeForView(entityManager, viewPosition);
    }
    const Math::Vector3 ambientColor = m_impl->environmentFrame.ambientColor;

    // Built ONCE for the whole frame, then culled against each of the four frusta below. Before
    // this, the full extraction walk ran per frustum - three shadow cascades plus the main view -
    // re-resolving the same asset handles and recomputing the same world matrices and bounds four
    // times over to reach four different plane tests. Only the plane test ever differed.
    // Distances for LodGroup3D are measured from here. Set before the build because the build is
    // where the level is resolved and the far entities are dropped.
    m_impl->visibilitySet.cameraWorldPosition = viewPosition;
    // The debug freeze belongs to the colour view only. A reflection-probe capture renders one cube
    // face through this same path, and freezing it would submit the entire scene six times over for
    // a capture whose frustum is the point of the exercise.
    const bool frustumCullingFrozen = !capturingProbe && !m_impl->cullingSettings.frustumCullingEnabled;
    m_impl->lastFrameDiagnostics.frustumCullingFrozen = frustumCullingFrozen;
    // Set before the build, like shadowPass and viewLayerMask: this flag is per-cull state on a set
    // that survives across frames, and every cull below states its own value explicitly.
    m_impl->visibilitySet.viewLayerMask =
        capturingProbe ? m_impl->probeCaptureVisibilityLayers : 0xFFFFFFFFU;
    m_impl->visibilitySet.frustumTestsDisabled = frustumCullingFrozen;
    const LightData* const earlyShadowCaster = FindShadowCaster(lights);
    if (!frustumCullingFrozen && earlyShadowCaster == nullptr) {
        m_impl->meshRenderer.SynchronizeSpatialAcceleration(entityManager, m_impl->assetManager,
                                                               m_impl->assetDatabase);
        m_impl->meshRenderer.SetActiveCullFrustum(frustum);
    } else {
        m_impl->meshRenderer.ClearActiveCullFrustum();
    }
    m_impl->meshRenderer.BuildVisibilitySet(entityManager, m_impl->assetManager, m_impl->assetDatabase,
                                               m_impl->visibilitySet);
    m_impl->meshRenderer.ClearActiveCullFrustum();
    m_impl->lastFrameDiagnostics.placementCacheHits = m_impl->visibilitySet.placementCacheHits;
    m_impl->lastFrameDiagnostics.placementCacheMisses = m_impl->visibilitySet.placementCacheMisses;
    m_impl->lastFrameDiagnostics.visibilityClusters = m_impl->visibilitySet.clusters.size();
    m_impl->lastFrameDiagnostics.distanceCulledEntities = m_impl->visibilitySet.distanceCulledEntities;

    m_impl->ExtractPrimitiveItems(entityManager, frustum, viewPosition, frustumCullingFrozen,
                                     m_impl->primitiveItems);
    m_impl->ExtractUnmaterialedMeshItems(entityManager, frustum, viewPosition, frustumCullingFrozen,
                                            m_impl->primitiveItems);

    // Foliage / MultiMeshInstance3D extraction with active wind zone sway.
    Scene::FoliageWindZone3DComponent activeWind{};
    activeWind.enabled = false;
    entityManager.ForEach<Scene::FoliageWindZone3DComponent>(
        [&](const Scene::Entity, const Scene::FoliageWindZone3DComponent& zone) {
            if (zone.enabled && !activeWind.enabled) {
                activeWind = zone;
            }
        });
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::MultiMeshInstance3DComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& wt,
            const Scene::MultiMeshInstance3DComponent& mm) {
            if (!mm.enabled || wt.dirty || mm.meshAssetGuid == Asset::kInvalidAssetGuid || mm.instances.empty() ||
                IsHiddenInHierarchy(entityManager, entity)) {
                return;
            }
            auto handleIt = m_impl->unmaterialedMeshHandles.find(mm.meshAssetGuid);
            if (handleIt == m_impl->unmaterialedMeshHandles.end()) {
                handleIt = m_impl->unmaterialedMeshHandles
                               .emplace(mm.meshAssetGuid, m_impl->assetManager.Load<Asset::MeshAsset>(
                                                              mm.meshAssetGuid, m_impl->assetDatabase))
                               .first;
            }
            const Asset::MeshAsset* const mesh = handleIt->second.TryGet();
            if (mesh == nullptr || mesh->indices.empty()) {
                return;
            }
            Asset::AssetGuid resolvedMaterialGuid = mm.materialAssetGuid;
            if (resolvedMaterialGuid != Asset::kInvalidAssetGuid) {
                if (m_impl->assetDatabase.Resolve(resolvedMaterialGuid).empty()) {
                    resolvedMaterialGuid = Asset::kInvalidAssetGuid;
                } else if (m_impl->primitiveMaterialHandles.find(resolvedMaterialGuid) ==
                           m_impl->primitiveMaterialHandles.end()) {
                    m_impl->primitiveMaterialHandles.emplace(
                        resolvedMaterialGuid,
                        m_impl->assetManager.Load<Asset::MaterialAsset>(resolvedMaterialGuid,
                                                                              m_impl->assetDatabase));
                }
            }
            Math::Quaternion parentRot{};
            if (!Math::TryNormalize(wt.worldRotation, parentRot)) {
                return;
            }
            const Math::Matrix4x4 parentWorld =
                Math::Matrix4x4::ComposeTrs(wt.worldPosition, parentRot, wt.worldScale);
            const std::size_t maxCount =
                mm.visibleInstanceCount >= 0
                    ? std::min<std::size_t>(static_cast<std::size_t>(mm.visibleInstanceCount), mm.instances.size())
                    : mm.instances.size();
            const std::size_t drawLimit = std::min(maxCount, m_impl->activeMaxInstancesPerFrame);
            for (std::size_t i = 0U; i < drawLimit; ++i) {
                const Scene::MultiMeshInstanceTransform3D& inst = mm.instances[i];
                Math::Quaternion instRot{};
                if (!Math::TryNormalize(inst.rotation, instRot)) {
                    instRot = Math::Quaternion{};
                }
                Math::Vector3 pos = inst.position;
                if (activeWind.enabled) {
                    pos = pos + Scene::EvaluateFoliageWindDisplacement(
                                    wt.worldPosition + pos, 1.0F, m_impl->skyTimeSeconds, activeWind);
                }
                const Math::Matrix4x4 instWorld =
                    parentWorld * Math::Matrix4x4::ComposeTrs(pos, instRot, inst.scale);
                const Math::Aabb bounds = mesh->localBounds.Transform(instWorld);
                if (!frustumCullingFrozen && !frustum.Intersects(bounds)) {
                    continue;
                }
                PrimitiveRenderItem item{};
                item.worldMatrix = instWorld;
                item.baseColor = Math::Vector3{inst.customColor.r, inst.customColor.g, inst.customColor.b};
                item.materialGuid = resolvedMaterialGuid;
                item.meshGuid = mm.meshAssetGuid;
                item.mesh = mesh;
                m_impl->primitiveItems.push_back(item);
            }
        });
    if (m_impl->primitiveItems.size() > 1U) {
        std::sort(m_impl->primitiveItems.begin(), m_impl->primitiveItems.end(),
                  [](const PrimitiveRenderItem& a, const PrimitiveRenderItem& b) noexcept {
                      const bool aBlend = a.opacity < 1.0F;
                      const bool bBlend = b.opacity < 1.0F;
                      if (aBlend != bBlend) {
                          return !aBlend;
                      }
                      if (a.materialGuid != b.materialGuid) {
                          return a.materialGuid.value < b.materialGuid.value;
                      }
                      if (a.meshGuid != b.meshGuid) {
                          return a.meshGuid.value < b.meshGuid.value;
                      }
                      return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                  });
    }
    m_impl->lastFrameDiagnostics.primitiveItemsExtracted = m_impl->primitiveItems.size();
    m_impl->lastFrameDiagnostics.skinnedMeshesDrawn = m_impl->skinnedMeshesThisFrame;

    const LightData* const shadowCaster = FindShadowCaster(lights);
    bool shadowsReady = !capturingProbe && shadowCaster != nullptr && m_impl->shadowProgram->IsValid() &&
                        AreShadowMapTargetsValid(m_impl->shadowMapTargets);
    ShadowCascadeMatrices lightSpaceMatrices{};
    ShadowCascadeSplits cascadeSplits{};
    std::int32_t cascadeCount = 0;
    const Platform::RendererProfile effectiveProfile = m_impl->rendererProfileResolution.effective;
    const std::size_t activeCascadeCount =
        effectiveProfile == Platform::RendererProfile::Compatibility
            ? 1U
            : (effectiveProfile == Platform::RendererProfile::Mobile
                   ? std::min<std::size_t>(2U, kShadowCascadeCount)
                   : kShadowCascadeCount);
    if (shadowsReady) {
        // The caster may keep its shadow closer than the camera sees (a sharper shadow where it
        // matters) and choose how its cascades share that distance.
        const float minShadowFar = std::min(camera.nearPlane * 2.0F, camera.farPlane);
        const float requestedShadowFar = isOrthographic && shadowCaster->shadowMaxDistance > 0.0F
                                             ? std::max(shadowCaster->shadowMaxDistance, camera.orthographicSize * 8.0F)
                                             : shadowCaster->shadowMaxDistance;
        const float shadowFar = requestedShadowFar > 0.0F
                                    ? std::clamp(requestedShadowFar, minShadowFar, camera.farPlane)
                                    : camera.farPlane;
        const float splitLambda = isOrthographic
                                      ? 0.0F
                                      : (shadowCaster->shadowSplitBlend >= 0.0F ? shadowCaster->shadowSplitBlend
                                                                                 : m_impl->shadowCascadeSplitLambda);
        cascadeSplits = ComputeCascadeSplits(camera.nearPlane, shadowFar, splitLambda);
        const bool cascadeSplitsValid = AreCascadeSplitsValid(cascadeSplits, camera.nearPlane, shadowFar);
        if (!cascadeSplitsValid) {
            UVE_ERROR("Renderer3D: shadow cascade split math produced invalid output; skipping shadow cascades");
            shadowsReady = false;
        } else {
            if (activeCascadeCount < kShadowCascadeCount) {
                if (activeCascadeCount == 1U) {
                    cascadeSplits[0] = std::min(shadowFar, std::max(camera.nearPlane * 2.0F, 45.0F));
                } else {
                    cascadeSplits[0] = std::min(shadowFar * 0.25F, std::max(camera.nearPlane * 2.0F, 18.0F));
                    cascadeSplits[1] = shadowFar;
                }
            }
            const Math::Matrix4x4 lightView =
                Math::Matrix4x4::ViewFromPositionAndRotation(shadowCaster->position, shadowCaster->rotation);
            const CameraFrustumCorners cameraCorners =
                m_impl->cameraSystem.ComputeFrustumCorners(entityManager, cameraEntity, aspectRatio);
            const bool shadowViewInputsValid = IsFiniteMatrix(lightView) &&
                                               std::all_of(cameraCorners.cbegin(), cameraCorners.cend(),
                                                           [](const Math::Vector3& corner) {
                                                               return Math::IsFinite(corner);
                                                           });
            if (!shadowViewInputsValid) {
                UVE_ERROR("Renderer3D: shadow view inputs are non-finite; skipping shadow cascades");
                shadowsReady = false;
            }
            float cascadeNearPlane = camera.nearPlane;
            for (std::size_t cascadeIndex = 0;
                 cascadeIndex < activeCascadeCount && shadowsReady; ++cascadeIndex) {
            const float cascadeFarPlane = cascadeSplits[cascadeIndex];
            const float nearRatio = (cascadeNearPlane - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            const float farRatio = (cascadeFarPlane - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            const CameraFrustumCorners cascadeCorners =
                ComputeCascadeFrustumCorners(cameraCorners, nearRatio, farRatio);
            const Math::Aabb fittedBounds = ComputeLightSpaceCameraBounds(cascadeCorners, lightView);
            Math::Aabb stabilizedBounds = StabilizeLightSpaceShadowBounds(
                fittedBounds, m_impl->shadowFrustumPadding, m_impl->shadowMapResolution);
            // A caster that stands outside THIS cascade's slice - nearer the sun, or in a farther
            // cascade - can still throw its shadow into this slice (a box far from the camera
            // shadowing the floor close to it). The fitted box only spans the slice itself, so
            // that caster was clipped by the near plane and culled, and the receiver showed no
            // shadow at all. Pull the near plane toward the light so those casters are drawn.
            constexpr float kShadowCasterReachMeters = 60.0F;
            stabilizedBounds.max.z += kShadowCasterReachMeters;
            const Math::Matrix4x4 lightProjection = Math::Matrix4x4::Orthographic(
                stabilizedBounds.min.x, stabilizedBounds.max.x, stabilizedBounds.min.y, stabilizedBounds.max.y,
                -stabilizedBounds.max.z, -stabilizedBounds.min.z);
            lightSpaceMatrices[cascadeIndex] = lightProjection * lightView;
            const bool cascadeBoundsValid = IsOrderedFiniteAabb(fittedBounds) &&
                                             IsOrderedFiniteAabb(stabilizedBounds) &&
                                             IsFiniteMatrix(lightProjection) &&
                                             IsFiniteMatrix(lightSpaceMatrices[cascadeIndex]);
            if (!cascadeBoundsValid) {
                UVE_ERROR("Renderer3D: shadow cascade bounds produced non-finite output; skipping cascades");
                shadowsReady = false;
                break;
            }
            const Math::Frustum lightFrustum =
                m_impl->cameraSystem.ExtractFrustum(lightSpaceMatrices[cascadeIndex]);
            m_impl->visibilitySet.shadowPass = true;
            // Explicitly unfrozen: a cascade culls against its own light frustum, and freezing the
            // colour view is not a reason to draw every caster in the scene into all three maps.
            m_impl->visibilitySet.frustumTestsDisabled = false;
            m_impl->meshRenderer.CullVisibilitySetInto(m_impl->visibilitySet, lightFrustum,
                                                          m_impl->shadowQueues[cascadeIndex]);
            // Mesh order, not depth order: this cascade renders depth only, and the shadow
            // batcher merges adjacent same-mesh items. See SortForDepthOnlyPass.
            m_impl->shadowQueues[cascadeIndex].SortForDepthOnlyPass();
            m_impl->CollectShadowPrimitiveDraws(entityManager, lightFrustum,
                                                   m_impl->shadowPrimitiveQueues[cascadeIndex]);
                cascadeNearPlane = cascadeFarPlane;
            }
            if (shadowsReady) {
                cascadeCount = static_cast<std::int32_t>(activeCascadeCount);
            } else {
                cascadeSplits = ShadowCascadeSplits{};
                lightSpaceMatrices = ShadowCascadeMatrices{};
            }
        }
    }

    float shadowDistanceFadeRange = 0.0F;
    float shadowBias = m_impl->shadowBiasDefault * 0.025F;
    float shadowNormalBias = m_impl->shadowNormalBiasDefault;
    float shadowOpacity = 1.0F;
    std::int32_t shadowPcfKernelRadius = m_impl->shadowPcfKernelRadius;
    if (shadowCaster != nullptr) {
        if (std::isfinite(shadowCaster->shadowDistanceFadeRange) &&
            shadowCaster->shadowDistanceFadeRange >= 0.0F) {
            shadowDistanceFadeRange = shadowCaster->shadowDistanceFadeRange;
        }
        if (std::isfinite(shadowCaster->shadowBias) && shadowCaster->shadowBias >= 0.0F) {
            shadowBias = shadowCaster->shadowBias;
        }
        if (std::isfinite(shadowCaster->shadowNormalBias) && shadowCaster->shadowNormalBias >= 0.0F) {
            shadowNormalBias = shadowCaster->shadowNormalBias;
        }
        shadowOpacity = shadowCaster->shadowOpacity;
        if (shadowCaster->shadowBlur >= 0.0F && std::isfinite(shadowCaster->shadowBlur)) {
            shadowPcfKernelRadius =
                static_cast<std::int32_t>(std::clamp(std::lround(shadowCaster->shadowBlur), 0L, 2L));
        }
        if (effectiveProfile != Platform::RendererProfile::ForwardPlus) {
            shadowPcfKernelRadius = std::min(shadowPcfKernelRadius, m_impl->shadowPcfKernelRadius);
        }
    }

    // ---- Atlas shadows (spot + point) ----------------------------------------------------------
    // Planned AFTER the directional block because these lights inherit the frame's resolved bias and
    // opacity, and BEFORE the frame uniforms because the plan is what fills them. The atlas itself is
    // created on demand: 64 MiB of Depth32F is not a cost an empty scene or Compatibility profile pays.
    LightShadowPlan shadowPlan{};
    std::array<std::size_t, kMaxShadowTiles> activeShadowTiles{};
    std::size_t activeShadowTileCount = 0U;
    m_impl->spotShadowCandidatesThisFrame = 0U;
    m_impl->pointShadowCandidatesThisFrame = 0U;
    if (!capturingProbe && effectiveProfile != Platform::RendererProfile::Compatibility &&
        m_impl->shadowProgram != nullptr && m_impl->shadowProgram->IsValid()) {
        for (std::size_t lightIndex = 0; lightIndex < lights.Size(); ++lightIndex) {
            const LightData& light = lights[lightIndex];
            if (IsSpotLightShadowCandidate(light, frustum)) {
                ++m_impl->spotShadowCandidatesThisFrame;
            } else if (IsPointLightShadowCandidate(light, frustum)) {
                ++m_impl->pointShadowCandidatesThisFrame;
            }
        }
        if (m_impl->spotShadowCandidatesThisFrame > 0U ||
            m_impl->pointShadowCandidatesThisFrame > 0U) {
            if (!m_impl->shadowAtlas.has_value()) {
                m_impl->shadowAtlas.emplace(m_impl->renderDevice);
                if (m_impl->shadowAtlas->GetAtlasTexture() == kInvalidTextureHandle) {
                    UVE_WARNING("Renderer3D: could not create the {}x{} shadow atlas; atlas "
                                "shadows disabled for this device",
                                kShadowAtlasResolution, kShadowAtlasResolution);
                }
            }
            if (m_impl->shadowAtlas->GetAtlasTexture() != kInvalidTextureHandle) {
                shadowPlan = BuildLightShadowPlan(lights, frustum, *m_impl->shadowAtlas,
                                                     m_impl->shadowAtlasTileResolution);
                // Cull each tile's casters now, and drop tiles that culled nothing: an empty tile
                // still holds the frame clear's 1.0, which the shader reads as "fully lit", so the
                // pass would cost a draw-less render pass and change nothing.
                for (std::size_t planSlot = 0; planSlot < shadowPlan.count; ++planSlot) {
                    const LightShadowAssignment& assignment = shadowPlan.assignments[planSlot];
                    const Math::Frustum lightFrustum =
                        m_impl->cameraSystem.ExtractFrustum(assignment.lightViewProjection);
                    m_impl->visibilitySet.shadowPass = true;
                    m_impl->visibilitySet.frustumTestsDisabled = false;
                    m_impl->meshRenderer.CullVisibilitySetInto(m_impl->visibilitySet, lightFrustum,
                                                                  m_impl->shadowAtlasQueues[planSlot]);
                    m_impl->shadowAtlasQueues[planSlot].SortForDepthOnlyPass();
                    m_impl->CollectShadowPrimitiveDraws(entityManager, lightFrustum,
                                                           m_impl->shadowAtlasPrimitiveQueues[planSlot]);
                    if (m_impl->shadowAtlasQueues[planSlot].opaqueItems.empty() &&
                        m_impl->shadowAtlasPrimitiveQueues[planSlot].empty()) {
                        continue;
                    }
                    activeShadowTiles[activeShadowTileCount++] = planSlot;
                }
            }
        }
    }
    m_impl->lastFrameDiagnostics.spotShadowCandidates = m_impl->spotShadowCandidatesThisFrame;
    m_impl->lastFrameDiagnostics.pointShadowCandidates = m_impl->pointShadowCandidatesThisFrame;
    m_impl->lastFrameDiagnostics.shadowAtlasTileResolution =
        activeShadowTileCount > 0U ? shadowPlan.tileResolution : 0U;

    // Tiles that survived the caster cull, split by light type for the diagnostics.
    std::size_t activeSpotShadowTileCount = 0U;
    std::size_t activePointShadowTileCount = 0U;
    for (std::size_t activeIndex = 0; activeIndex < activeShadowTileCount; ++activeIndex) {
        const LightShadowAssignment& assignment = shadowPlan.assignments[activeShadowTiles[activeIndex]];
        if (assignment.isPoint) {
            ++activePointShadowTileCount;
        } else {
            ++activeSpotShadowTileCount;
        }
    }
    m_impl->lastFrameDiagnostics.spotShadowTilesActive = activeSpotShadowTileCount;
    m_impl->lastFrameDiagnostics.pointShadowTilesActive = activePointShadowTileCount;

    const bool taaActive =
        !capturingProbe && m_impl->postProcessSettings.temporalAAEnabled &&
        m_impl->taaResolveProgram != nullptr && m_impl->taaResolveProgram->IsValid() &&
        m_impl->EnsureTaaTargets();
    const Math::Matrix4x4 renderViewMatrix =
        Math::Matrix4x4::ViewFromPositionAndRotation(viewPosition, viewRotation);
    const Math::Matrix4x4 effectiveViewProjection =
        taaActive ? (ApplyTaaProjectionJitter(projection, m_impl->taaFrameIndex,
                                                 m_impl->targetWidth, m_impl->targetHeight) *
                     renderViewMatrix)
                  : viewProjection;

    FrameUniforms frameUniforms{
        effectiveViewProjection,
        viewPosition,
        lights,
        ambientColor,
        m_impl->environmentFrame.skyAmbient,
        m_impl->environmentFrame.groundAmbient,
        m_impl->environmentFrame.ambientSource,
        m_impl->skyTextureHandle,
        m_impl->environmentFrame.ambientSource == Scene::WorldEnvironmentAmbientSource::EnvironmentMap &&
            m_impl->skyTextureEnabled,
        lightSpaceMatrices,
        cascadeSplits,
        cascadeCount,
        m_impl->shadowCascadeBlendRatio,
        shadowDistanceFadeRange,
        shadowBias,
        shadowNormalBias,
        shadowOpacity,
        shadowPcfKernelRadius};

    frameUniforms.skyReflectionsLinked = m_impl->environmentFrame.skyReflectionsLinked;
    frameUniforms.skyReflectionSky = m_impl->environmentFrame.reflectionSky;
    frameUniforms.skyReflectionHorizon = m_impl->environmentFrame.reflectionHorizon;
    frameUniforms.skyReflectionGround = m_impl->environmentFrame.reflectionGround;
    frameUniforms.isOrthographic = isOrthographic;
    frameUniforms.activeLightCap = m_impl->activeMaxLightsPerFrame;
    frameUniforms.cameraForward = m_impl->skyCameraForward;
    frameUniforms.clusterNear = m_impl->environmentCameraNear;
    frameUniforms.clusterFar = m_impl->environmentCameraFar;
    frameUniforms.viewportWidth = static_cast<float>(std::max<std::uint32_t>(m_impl->targetWidth, 1U));
    frameUniforms.viewportHeight = static_cast<float>(std::max<std::uint32_t>(m_impl->targetHeight, 1U));
    const Math::Color sceneGiIrradiance =
        Scene::EvaluateIndirectGI3DIrradianceAtPoint(entityManager, viewPosition);
    frameUniforms.giIndirectIrradiance =
        Math::Vector3{sceneGiIrradiance.r, sceneGiIrradiance.g, sceneGiIrradiance.b};

    // Forward+ clustered lighting: runs whenever explicitly enabled or when the scene has more
    // active lights than the 16-slot uniform path can hold.
    if (!capturingProbe && m_impl->postProcessSettings.clusteredLightingEnabled &&
        m_impl->EnsureClusteredLightBuffers()) {
        const std::vector<LightData> clusteredLights = ExtractClusteredLightsForView(
            entityManager, viewPosition, m_impl->activeMaxClusteredLights);
        if (!clusteredLights.empty()) {
            BuildLightClusterGrid(clusteredLights, &shadowPlan, renderViewMatrix, projection,
                                     m_impl->environmentCameraNear, m_impl->environmentCameraFar,
                                     m_impl->clusteredLightGrid);
            const auto& grid = m_impl->clusteredLightGrid;
            const std::span<const std::byte> lightBytes{
                reinterpret_cast<const std::byte*>(grid.lights.data()),
                grid.lights.size() * sizeof(ClusteredGpuLight)};
            const std::span<const std::byte> cellBytes{
                reinterpret_cast<const std::byte*>(grid.cells.data()),
                grid.cells.size() * sizeof(LightClusterCell)};
            const std::uint32_t dummyIndex = 0U;
            const std::span<const std::byte> indexBytes =
                grid.lightIndices.empty()
                    ? std::span<const std::byte>{reinterpret_cast<const std::byte*>(&dummyIndex), sizeof(dummyIndex)}
                    : std::span<const std::byte>{reinterpret_cast<const std::byte*>(grid.lightIndices.data()),
                                                 grid.lightIndices.size() * sizeof(std::uint32_t)};
            if (m_impl->renderDevice.UpdateBuffer(m_impl->clusteredLightBuffer, lightBytes) &&
                m_impl->renderDevice.UpdateBuffer(m_impl->clusteredCellBuffer, cellBytes) &&
                m_impl->renderDevice.UpdateBuffer(m_impl->clusteredIndexBuffer, indexBytes)) {
                frameUniforms.clusteredLightingEnabled = true;
                m_impl->lastFrameDiagnostics.clusteredLightingPassActive = true;
                m_impl->lastFrameDiagnostics.clusteredLightsSubmitted = grid.lights.size();
            }
        }
    }

    // Indexed by uLights[] slot, which is exactly how the shader indexes its own arrays - a tile
    // for light 3 must land in slot 3 even when two other lights between them have none.
    for (std::size_t activeIndex = 0; activeIndex < activeShadowTileCount; ++activeIndex) {
        const LightShadowAssignment& assignment =
            shadowPlan.assignments[activeShadowTiles[activeIndex]];
        const std::size_t slot = assignment.shadowSlot;
        frameUniforms.lightShadowSlots[assignment.lightIndex] = static_cast<std::int32_t>(slot) + 1;
        frameUniforms.lightShadowEnabled[slot] = 1;
        // (near, far) for the shader's world-space bias conversion. See ShadowLinearDepth /
        // ShadowDepthSlope - the CPU and GPU halves of one formula, pinned together by
        // light_shadow_planner_uve_tests.cpp.
        frameUniforms.lightShadowDepthRanges[slot] =
            Math::Vector2{assignment.nearPlane, assignment.farPlane};
        if (assignment.isPoint) {
            frameUniforms.lightShadowCubeRects[slot * kPointShadowFaceCount +
                                               assignment.faceIndex] = assignment.tile.normalizedRect;
            frameUniforms.pointShadowTilesActive = true;
        } else {
            frameUniforms.lightShadowMatrices[slot] = assignment.lightViewProjection;
            frameUniforms.lightShadowTileRects[slot] = assignment.tile.normalizedRect;
        }
    }
    if (activeShadowTileCount > 0U && m_impl->shadowAtlas.has_value()) {
        frameUniforms.shadowAtlasTexture = m_impl->shadowAtlas->GetAtlasTexture();
    }

    RenderQueue& queue = m_impl->frameQueue;
    // Counted before the cull rather than inside it: CullVisibilitySetInto runs four times a
    // frame against four different frusta, and a rejection ratio averaged across a main view and
    // three much wider cascades describes none of them. This is the main view's alone.
    // Counted only when the cull will actually honour a rejection: a frozen view submits every
    // cluster, so counting the ones its frustum misses would report rejections that never happened.
    if (!frustumCullingFrozen) {
        for (const MeshVisibilitySet::CandidateCluster& cluster : m_impl->visibilitySet.clusters) {
            if (!frustum.Intersects(cluster.bounds)) {
                ++m_impl->lastFrameDiagnostics.visibilityClustersRejected;
            }
        }
    }
    m_impl->visibilitySet.shadowPass = false;
    m_impl->visibilitySet.frustumTestsDisabled = frustumCullingFrozen;
    m_impl->meshRenderer.CullVisibilitySetInto(m_impl->visibilitySet, frustum, queue);

    // The decal pass, run against the same frame data the mesh pass just built: the receiving
    // surfaces are already resolved, placed and bounds-transformed, so this asks where each decal's
    // volume lands rather than walking the scene a second time to find out what it landed on.
    if (!capturingProbe) {
        m_impl->decalRenderer.BuildDrawList(entityManager, m_impl->assetManager, m_impl->assetDatabase,
                                                m_impl->visibilitySet, viewPosition, frustum,
                                                kMainViewReceiverLayerMask, m_impl->decalDraws);
        BuildDecalDrawPlan(m_impl->decalDraws, m_impl->decalPlan);
        // A decal buffer the plan did not touch this frame belongs to a decal that is gone, hidden or
        // no longer painting - the same lifetime rule the skinned-mesh cache applies to its own.
        for (auto it = m_impl->decalBuffers.begin(); it != m_impl->decalBuffers.end();) {
            if (it->second.frame != m_impl->decalFrame) {
                DestroyBufferIfValid(m_impl->renderDevice, it->second.vertexBuffer);
                DestroyBufferIfValid(m_impl->renderDevice, it->second.indexBuffer);
                it = m_impl->decalBuffers.erase(it);
            } else {
                ++it;
            }
        }
        ++m_impl->decalFrame;
        m_impl->lastFrameDiagnostics.decalsConsidered = m_impl->decalDraws.decalsConsidered;
        m_impl->lastFrameDiagnostics.decalDrawsExtracted = m_impl->decalDraws.draws.size();
        m_impl->lastFrameDiagnostics.decalPatchesExtracted = m_impl->decalDraws.GetPatchCount();
        m_impl->lastFrameDiagnostics.decalTrianglesExtracted = m_impl->decalDraws.GetTriangleCount();
        m_impl->lastFrameDiagnostics.decalsWithoutReceivers = m_impl->decalDraws.decalsWithoutReceivers;
        m_impl->lastFrameDiagnostics.decalDrawsDropped = m_impl->decalPlan.drawsTruncated +
                                                         m_impl->decalPlan.drawsWithoutMaterial +
                                                         m_impl->decalPlan.drawsWithoutGeometry +
                                                         m_impl->decalPlan.drawsWithoutInverse +
                                                         m_impl->decalPlan.drawsWithoutPaint;
    }
    if (m_impl->particleRuntimeForFrame != nullptr && !capturingProbe) {
        ParticleRenderSnapshot particleSnapshot =
            ParticleRenderBridge::Extract(*m_impl->particleRuntimeForFrame);
        const std::size_t extracted = particleSnapshot.items.size();
        std::vector<Scene::Occluder3DSnapshot> occluders;
        Scene::CollectOccluder3DSnapshots(entityManager, occluders);
        std::erase_if(particleSnapshot.items, [&entityManager, &occluders, &viewPosition](
                                                  const ParticleRenderItem& item) {
            return Scene::IsWorldPartition3DDrawHidden(entityManager, item.entity) ||
                   Scene::IsVisibilityRegion3DDrawHidden(entityManager, item.entity) ||
                   Scene::IsOccluder3DPointDrawHidden(occluders, viewPosition, item.position);
        });
        if (particleSnapshot.items.size() != extracted && !particleSnapshot.truncated) {
            particleSnapshot.sourceParticleCount = particleSnapshot.items.size();
        }
        queue.AppendParticleSnapshot(particleSnapshot);
        m_impl->lastFrameDiagnostics.particleItemsExtracted = particleSnapshot.items.size();
        m_impl->lastFrameDiagnostics.particleItemsTruncated = particleSnapshot.truncated;
    }
    queue.Sort();
    // WP3: last frame's completed occlusion brackets decide which opaque items this frame skips;
    // this frame's proxies are planned from the same walk and recorded at the end of MainColor.
    // A reflection-probe capture is left alone: it is a different view with its own full list.
    if (!capturingProbe) {
        m_impl->ApplyOcclusionVerdicts(queue, frameUniforms.viewProjection,
                                          m_impl->cullingSettings.gpuOcclusionCullingEnabled);
    } else {
        m_impl->occlusionProxyDraws.clear();
    }
    ParticleDrawRecorder::RecordInto(queue, m_impl->activeMaxParticleGpuDrawCommands,
                                            m_impl->particleDrawRecording);
    m_impl->lastFrameDiagnostics.particleDrawCommandsRecorded = m_impl->particleDrawRecording.commands.size();
    m_impl->lastFrameDiagnostics.particleDrawCommandsSubmissionTruncated =
        m_impl->particleDrawRecording.truncated;
    m_impl->lastFrameDiagnostics.meshItemsExtracted = queue.opaqueItems.size() + queue.transparentItems.size();
    m_impl->lastFrameDiagnostics.invalidAssetReferences = queue.invalidAssetReferences;
    m_impl->lastFrameDiagnostics.pendingAssetLoads = queue.pendingAssetLoads;
    m_impl->lastFrameDiagnostics.failedAssetLoads = queue.failedAssetLoads;

    if (m_impl->vfxDrawListForFrame != nullptr && !capturingProbe) {
        const bool vfxZeroToOneDepth = m_impl->renderDevice.UsesZeroToOneDepthBuffer();
        m_impl->vfxFrameContext = VfxSpriteFrameContext{
            viewProjection,
            inverseProjection,
            viewPosition,
            m_impl->skyCameraRight,
            m_impl->skyCameraUp,
            m_impl->skyCameraForward,
            m_impl->depthTarget,
            m_impl->targetWidth,
            m_impl->targetHeight,
            vfxZeroToOneDepth ? 1.0F : 2.0F,
            vfxZeroToOneDepth ? 0.0F : -1.0F,
        };
        m_impl->lastFrameDiagnostics.vfxBatchesExtracted =
            m_impl->vfxDrawListForFrame->stats.batchesExtracted;
        m_impl->lastFrameDiagnostics.vfxInstancesExtracted =
            m_impl->vfxDrawListForFrame->stats.instancesExtracted;
        m_impl->lastFrameDiagnostics.vfxInstancesTruncated =
            m_impl->vfxDrawListForFrame->stats.instancesTruncated;
        m_impl->vfxSpriteRenderer.PrepareFrameBatches(
            m_impl->renderDevice, *m_impl->vfxDrawListForFrame, m_impl->vfxFrameContext);
    } else if (!capturingProbe) {
        m_impl->vfxFrameContext = VfxSpriteFrameContext{};
        m_impl->vfxSpriteRenderer.PrepareFrameBatches(
            m_impl->renderDevice, VfxDrawList{}, m_impl->vfxFrameContext);
    }

    RenderGraph& renderGraph = m_impl->renderGraph;
    renderGraph.Clear();
    // +6 resources beyond the shadow cascades at the compatibility one-level setting: color, depth,
    // SSAO, and three bloom targets. Every additional bloom scale adds three resources and four
    // passes (downsample, two blur directions, additive composite). UI and optional sky resources
    // can raise these hints; RenderGraph grows safely if they do.
    const std::size_t additionalBloomLevels =
        static_cast<std::size_t>(kMaximumBloomMipCount - kDefaultBloomMipCount);
    renderGraph.Reserve(kShadowCascadeCount + 6U + additionalBloomLevels * 3U,
                           kShadowCascadeCount + 8U + additionalBloomLevels * 4U);
    std::array<RenderGraphResourceHandle, kShadowCascadeCount> shadowResources{};
    if (shadowsReady) {
        for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
            shadowResources[cascadeIndex] = renderGraph.ImportTexture(
                m_impl->shadowMapTargets[cascadeIndex], GetRendererUniformNames().shadowPasses[cascadeIndex]);
        }
    }
    const RenderGraphResourceHandle colorResource = renderGraph.ImportTexture(m_impl->colorTarget, "MainColor");
    const RenderGraphResourceHandle depthResource = renderGraph.ImportTexture(m_impl->depthTarget, "MainDepth");

    if (shadowsReady) {
        for (std::size_t cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex) {
            const std::string_view passName = GetRendererUniformNames().shadowPasses[cascadeIndex];
            const std::array<RenderGraphResourceUse, 1U> shadowPassResources{
                RenderGraphResourceUse{shadowResources[cascadeIndex], RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                passName, shadowPassResources,
                [this, &lightSpaceMatrices, shadowCaster, cascadeIndex](ICommandBuffer& commandBuffer) {
                    const ShadowPassCounters cascadeCounters{
                        &m_impl->lastFrameDiagnostics.shadowBatchesRecorded,
                        &m_impl->lastFrameDiagnostics.shadowBatchedItems,
                        &m_impl->lastFrameDiagnostics.shadowInstancedDrawCallsRecorded};
                    static_cast<void>(m_impl->RecordShadowPass(
                        m_impl->shadowQueues[cascadeIndex].opaqueItems,
                        m_impl->shadowPrimitiveQueues[cascadeIndex],
                        lightSpaceMatrices[cascadeIndex],
                        m_impl->shadowMapTargets[cascadeIndex], shadowCaster != nullptr,
                        m_impl->shadowBatches[cascadeIndex], commandBuffer, cascadeCounters));
                });
        }
    }

    // Atlas passes. The clear is a SEPARATE, full-attachment pass on purpose: Vulkan attachment
    // clears ignore the scissor, so a per-tile Clear would wipe every tile rendered before it.
    // Clearing once and loading per tile is the only shape that is correct on both backends.
    RenderGraphResourceHandle shadowAtlasResource{};
    if (activeShadowTileCount > 0U && m_impl->shadowAtlas.has_value()) {
        shadowAtlasResource = renderGraph.ImportTexture(
            m_impl->shadowAtlas->GetAtlasTexture(), "SpotShadowAtlas");
        RenderGraphPassDesc atlasClearDesc;
        atlasClearDesc.debugName = "SpotShadowAtlasClear";
        atlasClearDesc.resources = {{shadowAtlasResource, RenderGraphResourceAccess::Write}};
        atlasClearDesc.recordCallback = [this](ICommandBuffer& commandBuffer) {
            RenderPassDesc passDesc;
            passDesc.colorAttachment = kInvalidTextureHandle;
            passDesc.depthAttachment = m_impl->shadowAtlas->GetAtlasTexture();
            passDesc.depthLoadOp = LoadOp::Clear;
            passDesc.clearDepth = 1.0F;
            commandBuffer.BeginRenderPass(passDesc);
            commandBuffer.EndRenderPass();
            ++m_impl->lastFrameDiagnostics.shadowAtlasPassesRecorded;
        };
        RenderGraphPassHandle previousAtlasPass = renderGraph.AddPass(std::move(atlasClearDesc));
        for (std::size_t activeIndex = 0; activeIndex < activeShadowTileCount; ++activeIndex) {
            const std::size_t planSlot = activeShadowTiles[activeIndex];
            RenderGraphPassDesc atlasPassDesc;
            atlasPassDesc.debugName = "AtlasShadow" + std::to_string(activeIndex);
            atlasPassDesc.resources = {{shadowAtlasResource, RenderGraphResourceAccess::Write}};
            // Explicit WAW ordering between sibling writers. The graph orders a reader after its
            // writers on its own, but two passes that write the same tile share no read edge, and
            // their relative order is what decides which tile ends up where.
            atlasPassDesc.after = {previousAtlasPass};
            atlasPassDesc.recordCallback =
                [this, &shadowPlan, planSlot](ICommandBuffer& commandBuffer) {
                    const LightShadowAssignment& assignment = shadowPlan.assignments[planSlot];
                    const ShadowPassCounters atlasCounters{
                        &m_impl->lastFrameDiagnostics.shadowAtlasBatchesRecorded,
                        &m_impl->lastFrameDiagnostics.shadowAtlasBatchedItems,
                        &m_impl->lastFrameDiagnostics.shadowAtlasInstancedDrawCallsRecorded};
                    m_impl->lastFrameDiagnostics.shadowAtlasDrawCallsRecorded +=
                        m_impl->RecordShadowPass(
                            m_impl->shadowAtlasQueues[planSlot].opaqueItems,
                            m_impl->shadowAtlasPrimitiveQueues[planSlot],
                            assignment.lightViewProjection,
                            m_impl->shadowAtlas->GetAtlasTexture(), true,
                            m_impl->shadowAtlasBatches[planSlot], commandBuffer, atlasCounters,
                            assignment.tile.pixelRect, LoadOp::Load);
                    ++m_impl->lastFrameDiagnostics.shadowAtlasPassesRecorded;
                };
            previousAtlasPass = renderGraph.AddPass(std::move(atlasPassDesc));
        }
    }

    std::array<RenderGraphResourceUse, kShadowCascadeCount + 3U> mainResources{};
    mainResources[0] = RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Write};
    mainResources[1] = RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Write};
    std::size_t mainResourceCount = 2U;
    if (shadowsReady) {
        for (const RenderGraphResourceHandle shadowResource : shadowResources) {
            mainResources[mainResourceCount++] =
                RenderGraphResourceUse{shadowResource, RenderGraphResourceAccess::Read};
        }
    }
    if (shadowAtlasResource.IsValid()) {
        // Declared Read so the graph orders every atlas writer before the colour pass; the shader
        // samples it through uShadowAtlasTexture.
        mainResources[mainResourceCount++] =
            RenderGraphResourceUse{shadowAtlasResource, RenderGraphResourceAccess::Read};
    }
    const bool wboitActive =
        !capturingProbe && m_impl->postProcessSettings.orderIndependentTransparencyEnabled &&
        !queue.transparentItems.empty() &&
        m_impl->wboitLitProgram != nullptr && m_impl->wboitLitProgram->IsValid() &&
        m_impl->wboitCompositeProgram != nullptr && m_impl->wboitCompositeProgram->IsValid() &&
        m_impl->EnsureWboitTargets();

    renderGraph.AddPass(
        "MainColor", std::span<const RenderGraphResourceUse>{mainResources.data(), mainResourceCount},
        [this, &queue, &frameUniforms, wboitActive](ICommandBuffer& commandBuffer) {
            RenderPassDesc passDesc;
            passDesc.colorAttachment = m_impl->colorTarget;
            passDesc.depthAttachment = m_impl->depthTarget;
            passDesc.colorLoadOp = LoadOp::Clear;
            passDesc.clearColor = {m_impl->sceneClearColor[0], m_impl->sceneClearColor[1],
                                   m_impl->sceneClearColor[2], 0.0F};
            if (m_impl->environmentFrame.hasEnvironment) {
                const Math::Vector3& horizon = m_impl->environmentFrame.horizonColor;
                passDesc.clearColor = {horizon.x, horizon.y, horizon.z, 0.0F};
            }
            m_impl->lastFrameDiagnostics.mainPassRecorded = true;
            commandBuffer.BeginRenderPass(passDesc);
            m_impl->BeginFrameUniformScope();
            m_impl->instancedDrawCallsThisFrame = 0U;
            m_impl->instancedObjectsThisFrame = 0U;
            m_impl->indirectInstancedDrawCallsThisFrame = 0U;
            m_impl->nextIndirectCullIndex = 0U;
            m_impl->lastFrameDiagnostics.meshDrawCallsRecorded += m_impl->RecordItemsInstanced(
                queue.opaqueItems, m_impl->opaqueBatches, frameUniforms, commandBuffer);
            if (!wboitActive) {
                m_impl->lastFrameDiagnostics.meshDrawCallsRecorded += m_impl->RecordItemsInstanced(
                    queue.transparentItems, m_impl->transparentBatches, frameUniforms, commandBuffer);
            }
            m_impl->lastFrameDiagnostics.instancedDrawCallsRecorded = m_impl->instancedDrawCallsThisFrame;
            m_impl->lastFrameDiagnostics.instancedObjectsRecorded = m_impl->instancedObjectsThisFrame;
            m_impl->lastFrameDiagnostics.indirectInstancedDrawCallsRecorded =
                m_impl->indirectInstancedDrawCallsThisFrame;
            m_impl->lastFrameDiagnostics.shadowInstancedDrawCallsRecorded =
                m_impl->shadowInstancedDrawCallsThisFrame;
            m_impl->lastFrameDiagnostics.decalDrawCallsRecorded =
                m_impl->probeCaptureViewActive
                    ? 0U
                    : m_impl->RecordDecalItems(m_impl->decalPlan, frameUniforms, commandBuffer);
            m_impl->lastFrameDiagnostics.particleDrawCommandsSubmitted =
                m_impl->probeCaptureViewActive
                    ? 0U
                    : m_impl->RecordParticleItems(m_impl->particleDrawRecording, frameUniforms, commandBuffer);
            m_impl->lastFrameDiagnostics.particleDrawCallsRecorded =
                m_impl->lastFrameDiagnostics.particleDrawCommandsSubmitted > 0U ? 1U : 0U;
            m_impl->lastFrameDiagnostics.primitiveDrawCallsRecorded +=
                m_impl->RecordPrimitiveItems(m_impl->primitiveItems, frameUniforms, commandBuffer);
            if (!m_impl->probeCaptureViewActive &&
                m_impl->vfxSpriteRenderer.HasOpaqueBatches() &&
                m_impl->vfxSpriteRenderer.AreOpaqueProgramsReady()) {
                m_impl->lastFrameDiagnostics.vfxPassRecorded = true;
                m_impl->lastFrameDiagnostics.vfxDrawCallsRecorded +=
                    m_impl->vfxSpriteRenderer.RecordOpaqueBatches(
                        commandBuffer, m_impl->vfxFrameContext,
                        [this](const std::string_view texPath, const std::string_view matPath) {
                            return m_impl->ResolveVfxBatchAppearance(texPath, matPath);
                        },
                        &m_impl->lastFrameDiagnostics.vfxInstancesDrawn);
            }
            // WP3: the occlusion proxies go last, after every opaque writer of this pass, so a
            // zero-sample bracket means "behind everything that was drawn this frame".
            m_impl->lastFrameDiagnostics.occlusionQueriesIssued =
                m_impl->probeCaptureViewActive ? 0U : m_impl->RecordOcclusionProxies(frameUniforms, commandBuffer);
            if (m_impl->renderDevice.GetBackendName() == "OpenGL") {
                m_impl->lastFrameDiagnostics.glDrawCallsIssued =
                    m_impl->lastFrameDiagnostics.meshDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.primitiveDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.particleDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.decalDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.vfxDrawCallsRecorded +
                    m_impl->lastFrameDiagnostics.occlusionQueriesIssued;
            }
            commandBuffer.EndRenderPass();
        });

    if (wboitActive) {
        if (auto* const wboitSet = m_impl->ActiveTargetSetMutable(); wboitSet != nullptr) {
            const RenderGraphResourceHandle accumResource =
                renderGraph.ImportTexture(wboitSet->wboitAccumTarget, "WBOITAccum");
            const RenderGraphResourceHandle revealResource =
                renderGraph.ImportTexture(wboitSet->wboitRevealTarget, "WBOITReveal");
            const std::array<RenderGraphResourceUse, 3U> wboitAccumUses{
                RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{accumResource, RenderGraphResourceAccess::Write},
                RenderGraphResourceUse{revealResource, RenderGraphResourceAccess::Write}};
            const TextureHandle accumTex = wboitSet->wboitAccumTarget;
            const TextureHandle revealTex = wboitSet->wboitRevealTarget;
            renderGraph.AddPass(
                "WBOITAccum", wboitAccumUses,
                [this, &queue, &frameUniforms, accumTex, revealTex](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = accumTex;
                    passDesc.depthAttachment = m_impl->depthTarget;
                    passDesc.colorLoadOp = LoadOp::Clear;
                    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 0.0F};
                    passDesc.depthLoadOp = LoadOp::Load;
                    passDesc.extraColorAttachments[0].target = revealTex;
                    passDesc.extraColorAttachments[0].loadOp = LoadOp::Clear;
                    passDesc.extraColorAttachments[0].clearColor = {0.0F, 0.0F, 0.0F, 0.0F};
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->BeginFrameUniformScope();
                    for (const RenderItem& item : queue.transparentItems) {
                        const MaterialGpuResources* const materialResources =
                            m_impl->ResolveMaterialGpuResources(item.materialHandle);
                        const Asset::MaterialAsset* const material = item.materialHandle.TryGet();
                        if (materialResources == nullptr || material == nullptr) {
                            continue;
                        }
                        const MeshGpuResources& meshResources = m_impl->ResolveMeshGpuResources(item);
                        if (!IsValidMeshGpuResources(meshResources)) {
                            continue;
                        }
                        m_impl->wboitLitProgram->SetMatrix4x4("uModel", item.worldMatrix);
                        m_impl->wboitLitProgram->SetMatrix3x3("uNormalMatrix", Impl::ComputeNormalMatrix(item.worldMatrix));
                        m_impl->ApplyFrameAndMaterialUniforms(*m_impl->wboitLitProgram, *material, frameUniforms, true);
                        m_impl->wboitLitProgram->SetInt("uMeshRenderLayers",
                                                           static_cast<std::int32_t>(item.renderLayers));
                        m_impl->wboitLitProgram->SetFloat("uSurfaceOpacity", item.opacity);
                        m_impl->wboitLitProgram->ApplyTo(commandBuffer);
                        m_impl->BindMaterialTextures(*materialResources, frameUniforms, commandBuffer);
                        commandBuffer.BindVertexBuffer(meshResources.vertexBuffer);
                        commandBuffer.BindIndexBuffer(meshResources.indexBuffer);
                        commandBuffer.DrawIndexed(ResolveItemIndexCount(item, meshResources), 1U,
                                                     ResolveItemFirstIndex(item));
                        ++m_impl->lastFrameDiagnostics.meshDrawCallsRecorded;
                    }
                    commandBuffer.EndRenderPass();
                    m_impl->lastFrameDiagnostics.wboitPassRecorded = true;
                });

            const std::array<RenderGraphResourceUse, 3U> wboitCompUses{
                RenderGraphResourceUse{accumResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{revealResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "WBOITComposite", wboitCompUses,
                [this, accumTex, revealTex](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->colorTarget;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::Load;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->wboitCompositeProgram->SetInt("uWboitAccumTexture", 0);
                    m_impl->wboitCompositeProgram->SetInt("uWboitRevealTexture", 1);
                    m_impl->wboitCompositeProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(accumTex, 0U);
                    commandBuffer.BindTexture(revealTex, 1U);
                    if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, 0U);
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, 1U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });
        }
    }

    const bool sunDiscActive = m_impl->sunEnergy > 0.0F &&
                               m_impl->sunDiscAngularDiameterDegrees > 0.0F &&
                               m_impl->sunDiscIntensity > 0.0F;
    const bool skyActive = !m_impl->captureTransparentBackground &&
                           (m_impl->environmentFrame.hasEnvironment || sunDiscActive) &&
                           m_impl->proceduralSkyProgram && m_impl->proceduralSkyProgram->IsValid();
    if (skyActive) {
        const std::array<RenderGraphResourceUse, 5U> skyResources{
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Write},
            RenderGraphResourceUse{shadowResources[0], RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{shadowResources[1], RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{shadowResources[2], RenderGraphResourceAccess::Read}};
        const std::size_t skyResourceCount = shadowsReady ? 5U : 2U;
        renderGraph.AddPass(
            "ProceduralSky", std::span<const RenderGraphResourceUse>{skyResources.data(), skyResourceCount},
            [this, shadowsReady, &lightSpaceMatrices, &cascadeSplits, shadowOpacity, shadowBias](ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->colorTarget;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::Load;
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->proceduralSkyProgram->SetInt("uSceneDepthTexture", 0);
                m_impl->proceduralSkyProgram->SetVector3("uCameraRight", m_impl->skyCameraRight);
                m_impl->proceduralSkyProgram->SetVector3("uCameraUp", m_impl->skyCameraUp);
                m_impl->proceduralSkyProgram->SetVector3("uCameraForward", m_impl->skyCameraForward);
                m_impl->proceduralSkyProgram->SetFloat("uTanHalfFov", m_impl->skyTanHalfFov);
                m_impl->proceduralSkyProgram->SetFloat("uAspect", m_impl->environmentAspect);
                m_impl->proceduralSkyProgram->SetInt(
                    "uHasEnvironment", m_impl->environmentFrame.hasEnvironment ? 1 : 0);
                m_impl->proceduralSkyProgram->SetVector3("uSkyColor", m_impl->environmentFrame.skyColor);
                m_impl->proceduralSkyProgram->SetVector3("uHorizonColor", m_impl->environmentFrame.horizonColor);
                m_impl->proceduralSkyProgram->SetVector3(
                    "uGroundColor", m_impl->environmentFrame.hasEnvironment
                                        ? m_impl->environmentFrame.groundColor
                                        : Math::Vector3{m_impl->sceneClearColor[0], m_impl->sceneClearColor[1],
                                                           m_impl->sceneClearColor[2]});
                m_impl->proceduralSkyProgram->SetFloat("uSkyCurve", m_impl->environmentFrame.skyCurve);
                m_impl->proceduralSkyProgram->SetFloat("uGroundCurve", m_impl->environmentFrame.groundCurve);
                m_impl->proceduralSkyProgram->SetVector3("uSunDirection", m_impl->sunDirection);
                m_impl->proceduralSkyProgram->SetVector3("uSunColor", m_impl->sunColor);
                m_impl->proceduralSkyProgram->SetFloat("uSunEnergy", m_impl->sunEnergy);
                m_impl->proceduralSkyProgram->SetFloat("uSunAngularDiameter",
                                                          m_impl->sunDiscAngularDiameterDegrees);
                m_impl->proceduralSkyProgram->SetFloat("uSunDiscIntensity", m_impl->sunDiscIntensity);
                m_impl->proceduralSkyProgram->SetFloat("uSunLimbDarkening", m_impl->sunDiscLimbDarkening);
                m_impl->proceduralSkyProgram->SetInt("uAtmosphericSky",
                                                         m_impl->environmentFrame.atmosphericSky ? 1 : 0);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereRayleigh",
                                                           m_impl->environmentFrame.atmosphereRayleigh);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereMie",
                                                           m_impl->environmentFrame.atmosphereMie);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereOzone",
                                                           m_impl->environmentFrame.atmosphereOzone);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereMieAnisotropy",
                                                           m_impl->environmentFrame.atmosphereMieAnisotropy);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereMultiScatter",
                                                           m_impl->environmentFrame.atmosphereMultiScatter);
                m_impl->proceduralSkyProgram->SetFloat("uAtmosphereIntensity",
                                                           m_impl->environmentFrame.atmosphereIntensity);
                m_impl->proceduralSkyProgram->SetInt(
                    "uAtmosphereSteps", static_cast<std::int32_t>(m_impl->environmentFrame.atmosphereSamples));
                m_impl->proceduralSkyProgram->SetVector3("uCameraPosition", m_impl->environmentCameraPosition);
                m_impl->proceduralSkyProgram->SetFloat("uSkyTime", m_impl->skyTimeSeconds);
                m_impl->proceduralSkyProgram->SetFloat("uStarDensity", m_impl->environmentFrame.starDensity);
                m_impl->proceduralSkyProgram->SetFloat("uStarBrightness", m_impl->environmentFrame.starBrightness);
                m_impl->proceduralSkyProgram->SetFloat("uStarSeed", m_impl->environmentFrame.starSeed);
                m_impl->proceduralSkyProgram->SetFloat("uMilkyWayStrength",
                                                          m_impl->environmentFrame.milkyWayStrength);
                m_impl->proceduralSkyProgram->SetInt("uCloudsEnabled",
                                                        m_impl->environmentFrame.cloudsEnabled ? 1 : 0);
                m_impl->proceduralSkyProgram->SetFloat("uCloudCoverage", m_impl->environmentFrame.cloudCoverage);
                m_impl->proceduralSkyProgram->SetFloat("uCloudDensity", m_impl->environmentFrame.cloudDensity);
                m_impl->proceduralSkyProgram->SetFloat("uCloudAltitude", m_impl->environmentFrame.cloudAltitude);
                m_impl->proceduralSkyProgram->SetVector2("uCloudWind", m_impl->environmentFrame.cloudWind);
                m_impl->proceduralSkyProgram->SetFloat("uCloudSoftness", m_impl->environmentFrame.cloudSoftness);
                m_impl->proceduralSkyProgram->SetFloat("uCloudSilverLining",
                                                          m_impl->environmentFrame.cloudSilverLining);
                m_impl->proceduralSkyProgram->SetFloat("uSkyLuminance", m_impl->environmentFrame.skyLuminance);
                m_impl->proceduralSkyProgram->SetInt("uSkyTexture", static_cast<std::int32_t>(kSkyTextureSlot));
                m_impl->proceduralSkyProgram->SetInt("uSkyTextureEnabled", m_impl->skyTextureEnabled ? 1 : 0);
                // Shadows belong on real floor/terrain geometry, never projected onto empty sky void.
                m_impl->proceduralSkyProgram->SetInt("uSkyGroundShadowEnabled", 0);
                if (shadowsReady) {
                    m_impl->proceduralSkyProgram->SetInt("uSkyGroundShadowMap0",
                                                            static_cast<std::int32_t>(kShadowCascadeFirstTextureSlot));
                    m_impl->proceduralSkyProgram->SetInt("uSkyGroundShadowMap1",
                                                            static_cast<std::int32_t>(kShadowCascadeFirstTextureSlot + 1U));
                    m_impl->proceduralSkyProgram->SetInt("uSkyGroundShadowMap2",
                                                            static_cast<std::int32_t>(kShadowCascadeFirstTextureSlot + 2U));
                    m_impl->proceduralSkyProgram->SetMatrix4x4("uSkyGroundLightMatrix0", lightSpaceMatrices[0]);
                    m_impl->proceduralSkyProgram->SetMatrix4x4("uSkyGroundLightMatrix1", lightSpaceMatrices[1]);
                    m_impl->proceduralSkyProgram->SetMatrix4x4("uSkyGroundLightMatrix2", lightSpaceMatrices[2]);
                    m_impl->proceduralSkyProgram->SetVector3(
                        "uSkyGroundCascadeSplits",
                        Math::Vector3{cascadeSplits[0], cascadeSplits[1], cascadeSplits[2]});
                    m_impl->proceduralSkyProgram->SetFloat("uSkyGroundShadowOpacity", shadowOpacity);
                    m_impl->proceduralSkyProgram->SetFloat("uSkyGroundShadowBias", shadowBias);
                }
                m_impl->proceduralSkyProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->depthTarget, 0U);
                if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->shadowPointSampler, 0U);
                }
                if (m_impl->skyTextureEnabled) {
                    commandBuffer.BindTexture(m_impl->skyTextureHandle, kSkyTextureSlot);
                    if (m_impl->environmentEquirectSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->environmentEquirectSampler, kSkyTextureSlot);
                    }
                }
                if (shadowsReady) {
                    for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
                        const std::uint32_t slot =
                            kShadowCascadeFirstTextureSlot + static_cast<std::uint32_t>(cascadeIndex);
                        commandBuffer.BindTexture(m_impl->shadowMapTargets[cascadeIndex], slot);
                        if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                            commandBuffer.BindSampler(m_impl->shadowPointSampler, slot);
                        }
                    }
                }
                commandBuffer.Draw(3);
                commandBuffer.EndRenderPass();
            });
    }

    if (!capturingProbe &&
        m_impl->studioFloorSettings.has_value() &&
        m_impl->studioFloorSettings->enabled &&
        m_impl->EnsureStudioFloorResources()) {
        const StudioFloorSettings floorSettings = *m_impl->studioFloorSettings;
        const std::array<RenderGraphResourceUse, 2U> studioFloorResources{
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::ReadWrite},
            RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "StudioFloor", studioFloorResources,
            [this, viewProjection, viewPosition, floorSettings, lights](ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->colorTarget;
                passDesc.depthAttachment = m_impl->depthTarget;
                passDesc.colorLoadOp = LoadOp::Load;
                passDesc.depthLoadOp = LoadOp::Load;
                commandBuffer.BeginRenderPass(passDesc);

                m_impl->studioFloorProgram->SetMatrix4x4("uViewProjection", viewProjection);
                m_impl->studioFloorProgram->SetFloat("uFloorY", floorSettings.heightY);
                m_impl->studioFloorProgram->SetVector3("uViewPosition", viewPosition);
                m_impl->studioFloorProgram->SetVector3("uPivotPosition", floorSettings.pivotPosition);
                m_impl->studioFloorProgram->SetFloat("uTileScale", floorSettings.tileScale);
                m_impl->studioFloorProgram->SetFloat("uFadeStartRadius", floorSettings.fadeStartRadiusMeters);
                m_impl->studioFloorProgram->SetFloat("uFadeEndRadius", floorSettings.fadeEndRadiusMeters);
                m_impl->studioFloorProgram->SetVector3("uBaseTint", floorSettings.baseTint);
                m_impl->studioFloorProgram->SetFloat("uRoughnessScale", floorSettings.roughnessScale);
                m_impl->studioFloorProgram->SetFloat("uSpecularStrength", floorSettings.specularStrength);
                m_impl->studioFloorProgram->SetVector3("uSkyTopColor", m_impl->environmentFrame.skyColor);
                m_impl->studioFloorProgram->SetVector3("uSkyHorizonColor", m_impl->environmentFrame.horizonColor);
                m_impl->studioFloorProgram->SetVector3("uSkyGroundColor", m_impl->environmentFrame.groundColor);
                m_impl->studioFloorProgram->SetFloat("uAmbientEnergy", 0.55F);
                m_impl->studioFloorProgram->SetInt("uAlbedoMap", 0);
                m_impl->studioFloorProgram->SetInt("uNormalMap", 1);
                m_impl->studioFloorProgram->SetInt("uOrmMap", 2);

                std::array<Math::Vector3, 4U> lightPositions{};
                std::array<Math::Vector3, 4U> lightColors{};
                std::array<float, 4U> lightRanges{{16.0F, 16.0F, 16.0F, 16.0F}};
                std::int32_t lightCount = 0;
                for (const LightData& light : lights) {
                    if (light.type == Scene::LightType::Directional || light.intensity <= 0.0F) {
                        continue;
                    }
                    if (lightCount >= 4) {
                        break;
                    }
                    const std::size_t idx = static_cast<std::size_t>(lightCount++);
                    lightPositions[idx] = light.position;
                    lightColors[idx] = Math::ToVector3(light.color) * light.intensity;
                    lightRanges[idx] = std::max(light.range, 1.0F);
                }
                if (lightCount == 0) {
                    // Built-in 3-point studio light rig centered around the studio floor pivot.
                    const Math::Vector3 p = floorSettings.pivotPosition;
                    lightCount = 3;
                    lightPositions[0U] = Math::Vector3{p.x + 3.5F, floorSettings.heightY + 5.2F, p.z + 3.8F};
                    lightColors[0U] = Math::Vector3{1.0F, 0.96F, 0.90F} * 2.2F;
                    lightRanges[0U] = 22.0F;
                    lightPositions[1U] = Math::Vector3{p.x - 4.0F, floorSettings.heightY + 3.8F, p.z - 3.5F};
                    lightColors[1U] = Math::Vector3{0.58F, 0.72F, 1.0F} * 1.45F;
                    lightRanges[1U] = 20.0F;
                    lightPositions[2U] = Math::Vector3{p.x - 2.8F, floorSettings.heightY + 2.6F, p.z + 4.2F};
                    lightColors[2U] = Math::Vector3{0.78F, 0.84F, 0.96F} * 0.95F;
                    lightRanges[2U] = 18.0F;
                }
                m_impl->studioFloorProgram->SetInt("uLightCount", lightCount);
                for (std::size_t i = 0U; i < 4U; ++i) {
                    const std::string idxStr = std::to_string(i);
                    m_impl->studioFloorProgram->SetVector3("uLightPositions[" + idxStr + "]", lightPositions[i]);
                    m_impl->studioFloorProgram->SetVector3("uLightColors[" + idxStr + "]", lightColors[i]);
                    m_impl->studioFloorProgram->SetFloat("uLightRanges[" + idxStr + "]", lightRanges[i]);
                }

                m_impl->studioFloorProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->studioFloorAlbedoTexture, 0U);
                commandBuffer.BindTexture(m_impl->studioFloorNormalTexture, 1U);
                commandBuffer.BindTexture(m_impl->studioFloorOrmTexture, 2U);
                if (m_impl->materialRepeatSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->materialRepeatSampler, 0U);
                    commandBuffer.BindSampler(m_impl->materialRepeatSampler, 1U);
                    commandBuffer.BindSampler(m_impl->materialRepeatSampler, 2U);
                }
                commandBuffer.BindVertexBuffer(m_impl->studioFloorVertexBuffer, 0U);
                commandBuffer.BindIndexBuffer(m_impl->studioFloorIndexBuffer);
                commandBuffer.DrawIndexed(6U, 1U, 0U);
                if (m_impl->renderDevice.GetBackendName() == "OpenGL") {
                    ++m_impl->lastFrameDiagnostics.glDrawCallsIssued;
                }
                commandBuffer.EndRenderPass();
            });
    }

    if (!capturingProbe &&
        m_impl->vfxSpriteRenderer.HasTranslucentBatches() &&
        m_impl->vfxSpriteRenderer.AreTranslucentProgramsReady()) {
        const std::array<RenderGraphResourceUse, 2U> vfxPassResources{
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "VfxParticles", vfxPassResources,
            [this](ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->colorTarget;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::Load;
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->lastFrameDiagnostics.vfxPassRecorded = true;
                const std::size_t translucentDraws =
                    m_impl->vfxSpriteRenderer.RecordTranslucentBatches(
                        commandBuffer, m_impl->vfxFrameContext,
                        [this](const std::string_view texPath, const std::string_view matPath) {
                            return m_impl->ResolveVfxBatchAppearance(texPath, matPath);
                        },
                        &m_impl->lastFrameDiagnostics.vfxInstancesDrawn);
                m_impl->lastFrameDiagnostics.vfxDrawCallsRecorded += translucentDraws;
                if (m_impl->renderDevice.GetBackendName() == "OpenGL") {
                    m_impl->lastFrameDiagnostics.glDrawCallsIssued += translucentDraws;
                }
                commandBuffer.EndRenderPass();
            });
    }

    const bool usesZeroToOneDepth = m_impl->renderDevice.UsesZeroToOneDepthBuffer();
    const float depthNdcScale = usesZeroToOneDepth ? 1.0F : 2.0F;
    const float depthNdcBias = usesZeroToOneDepth ? 0.0F : -1.0F;
    const float p00 = projection.m[0][0];
    const float p11 = projection.m[1][1];
    const float p22 = projection.m[2][2];
    const float p23 = projection.m[2][3];
    const bool fastDepthReconstruct =
        !isOrthographic && std::isfinite(p00) && std::isfinite(p11) && std::isfinite(p22) && std::isfinite(p23) &&
        std::fabs(p00) > 1.0e-6F && std::fabs(p11) > 1.0e-6F &&
        std::fabs(projection.m[0][2]) < 1.0e-5F && std::fabs(projection.m[1][2]) < 1.0e-5F &&
        std::fabs(projection.m[3][2] + 1.0F) < 1.0e-5F && std::fabs(projection.m[3][3]) < 1.0e-5F;
    const float projA = fastDepthReconstruct ? -p22 : 1.0F;
    const float projB = fastDepthReconstruct ? p23 : 0.0F;
    const float invProjX = fastDepthReconstruct ? (1.0F / p00) : 1.0F;
    const float invProjY = fastDepthReconstruct ? (1.0F / p11) : 1.0F;

    const auto& baseBloomTargets = m_impl->bloomMipTargets[0U];
    const bool bloomTargetsValid = m_impl->bloomMipTargetCount > 0U &&
                                   baseBloomTargets.bright != kInvalidTextureHandle &&
                                   baseBloomTargets.blurA != kInvalidTextureHandle &&
                                   baseBloomTargets.blurB != kInvalidTextureHandle;
    const bool hasPostOverride = m_impl->environmentFrame.hasPostProcessingOverride;
    const bool environmentAllowsPost = !hasPostOverride || m_impl->environmentFrame.postProcessingEnabled;
    const bool environmentBloom = !hasPostOverride || m_impl->environmentFrame.bloomEnabled;
    const bool environmentSsao = !hasPostOverride || m_impl->environmentFrame.ssaoEnabled;
    const bool ssaoActive = !capturingProbe && environmentAllowsPost && environmentSsao &&
                            m_impl->postProcessSettings.ssaoEnabled &&
                            m_impl->ssaoTarget != kInvalidTextureHandle && projectionInvertible &&
                            m_impl->ssaoProgram != nullptr && m_impl->ssaoProgram->IsValid();
    const bool ssaoBlurActive = ssaoActive && m_impl->postProcessSettings.ssaoBlurEnabled &&
                                m_impl->ssaoBlurTarget != kInvalidTextureHandle &&
                                m_impl->ssaoBlurProgram != nullptr && m_impl->ssaoBlurProgram->IsValid();
    const bool bloomBaseProgramsReady = m_impl->bloomBrightPassProgram && m_impl->bloomBrightPassProgram->IsValid() &&
                                        m_impl->bloomBlurProgram && m_impl->bloomBlurProgram->IsValid();
    const bool bloomRequested = !capturingProbe && environmentAllowsPost && environmentBloom &&
                                m_impl->postProcessSettings.bloomEnabled;
    std::uint32_t bloomMipCountUsed = 0U;
    if (bloomRequested && bloomTargetsValid && bloomBaseProgramsReady) {
        const std::uint32_t requestedBloomMipCount = hasPostOverride
                                                         ? m_impl->environmentFrame.bloomMipCount
                                                         : kDefaultBloomMipCount;
        const bool canBuildBloomPyramid =
            requestedBloomMipCount == kDefaultBloomMipCount ||
            (m_impl->bloomDownsampleProgram && m_impl->bloomDownsampleProgram->IsValid());
        // The first-scale bright-pass remains available while the optional pyramid shader is
        // still compiling or has failed; avoid allocating its extra targets until it can run.
        bloomMipCountUsed = m_impl->EnsureBloomMipTargets(
            canBuildBloomPyramid ? requestedBloomMipCount : kDefaultBloomMipCount);
    }
    const bool bloomActive = bloomRequested && bloomTargetsValid && bloomBaseProgramsReady && bloomMipCountUsed > 0U;

    RenderGraphResourceHandle ssaoResource{};
    if (ssaoActive) {
        m_impl->lastFrameDiagnostics.ssaoPassRecorded = true;
        ssaoResource = renderGraph.ImportTexture(m_impl->ssaoTarget, "SSAO");
        const std::array<RenderGraphResourceUse, 2U> ssaoPassResources{
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{ssaoResource, RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "SSAO", ssaoPassResources,
            [this, &projection, &inverseProjection, isOrthographic, hasPostOverride,
             depthNdcScale, depthNdcBias, projA, projB, invProjX, invProjY, fastDepthReconstruct](
                ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->ssaoTarget;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::DontCare;
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->ssaoProgram->SetInt("uDepthTexture", 0);
                m_impl->ssaoProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
                m_impl->ssaoProgram->SetMatrix4x4("uProjection", projection);
                m_impl->ssaoProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
                m_impl->ssaoProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                m_impl->ssaoProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                m_impl->ssaoProgram->SetFloat("uProjA", projA);
                m_impl->ssaoProgram->SetFloat("uProjB", projB);
                m_impl->ssaoProgram->SetFloat("uInvProjX", invProjX);
                m_impl->ssaoProgram->SetFloat("uInvProjY", invProjY);
                m_impl->ssaoProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
                const float ssaoRadius = hasPostOverride
                                             ? m_impl->environmentFrame.ssaoRadius
                                             : m_impl->postProcessSettings.ssaoRadius;
                const float ssaoIntensity = hasPostOverride
                                                ? m_impl->environmentFrame.ssaoIntensity
                                                : m_impl->postProcessSettings.ssaoIntensity;
                const std::uint32_t quality = std::min(m_impl->postProcessSettings.ssaoQuality, 2U);
                m_impl->ssaoProgram->SetFloat("uRadius", ssaoRadius);
                m_impl->ssaoProgram->SetFloat("uBias", kSsaoBias);
                m_impl->ssaoProgram->SetFloat("uIntensity", ssaoIntensity);
                m_impl->ssaoProgram->SetFloat("uPower", m_impl->postProcessSettings.ssaoPower);
                m_impl->ssaoProgram->SetInt("uSampleCount", kSsaoSamplesPerQuality[quality]);
                m_impl->ssaoProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->depthTarget, 0U);
                if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->shadowPointSampler, 0U);
                }
                commandBuffer.Draw(3);
                commandBuffer.EndRenderPass();
            });

        if (ssaoBlurActive) {
            const RenderGraphResourceHandle ssaoBlurResource =
                renderGraph.ImportTexture(m_impl->ssaoBlurTarget, "SSAOBlur");
            const std::array<RenderGraphResourceUse, 3U> ssaoBlurHResources{
                RenderGraphResourceUse{ssaoResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{ssaoBlurResource, RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "SSAOBlurH", ssaoBlurHResources,
                [this, &inverseProjection, isOrthographic, depthNdcScale, depthNdcBias,
                 projA, projB, fastDepthReconstruct](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->ssaoBlurTarget;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->ssaoBlurProgram->SetInt("uSourceTexture", 0);
                    m_impl->ssaoBlurProgram->SetInt("uDepthTexture", 1);
                    m_impl->ssaoBlurProgram->SetInt("uSsaoBlurEnabled", 1);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralAxisX", 1.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralAxisY", 0.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralDepthSharpness", 12.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                    m_impl->ssaoBlurProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                    m_impl->ssaoBlurProgram->SetFloat("uProjA", projA);
                    m_impl->ssaoBlurProgram->SetFloat("uProjB", projB);
                    m_impl->ssaoBlurProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
                    m_impl->ssaoBlurProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
                    m_impl->ssaoBlurProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
                    m_impl->ssaoBlurProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->ssaoTarget, 0U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                    }
                    commandBuffer.BindTexture(m_impl->depthTarget, 1U);
                    if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, 1U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });

            const std::array<RenderGraphResourceUse, 3U> ssaoBlurVResources{
                RenderGraphResourceUse{ssaoBlurResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{ssaoResource, RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "SSAOBlurV", ssaoBlurVResources,
                [this, &inverseProjection, isOrthographic, depthNdcScale, depthNdcBias,
                 projA, projB, fastDepthReconstruct](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->ssaoTarget;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->ssaoBlurProgram->SetInt("uSourceTexture", 0);
                    m_impl->ssaoBlurProgram->SetInt("uDepthTexture", 1);
                    m_impl->ssaoBlurProgram->SetInt("uSsaoBlurEnabled", 1);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralAxisX", 0.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralAxisY", 1.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uBilateralDepthSharpness", 12.0F);
                    m_impl->ssaoBlurProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                    m_impl->ssaoBlurProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                    m_impl->ssaoBlurProgram->SetFloat("uProjA", projA);
                    m_impl->ssaoBlurProgram->SetFloat("uProjB", projB);
                    m_impl->ssaoBlurProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
                    m_impl->ssaoBlurProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
                    m_impl->ssaoBlurProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
                    m_impl->ssaoBlurProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->ssaoBlurTarget, 0U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                    }
                    commandBuffer.BindTexture(m_impl->depthTarget, 1U);
                    if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, 1U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });
        }
    }

    // Contact shadows reuse the same depth buffer SSAO reads - it is already resolved and sampleable
    // by the time post-processing runs - and need only the sun direction in view space. Deriving it
    // from the camera basis the sky pass already uses keeps one definition of "which way the camera
    // faces" rather than a second matrix that could disagree with it.
    const ContactShadowSettings contactShadowSettings =
        SanitizeContactShadowSettings(ContactShadowSettings{
            m_impl->postProcessSettings.contactShadowsEnabled,
            m_impl->postProcessSettings.contactShadowLengthMeters,
            m_impl->postProcessSettings.contactShadowThicknessMeters,
            m_impl->postProcessSettings.contactShadowStrength,
            m_impl->postProcessSettings.contactShadowStepCount});
    const Math::Vector3 sunDirectionInView{
        Math::Dot(m_impl->sunDirection, m_impl->skyCameraRight),
        Math::Dot(m_impl->sunDirection, m_impl->skyCameraUp),
        -Math::Dot(m_impl->sunDirection, m_impl->skyCameraForward)};
    const float sunDirectionInViewLengthSquared = Math::LengthSquared(sunDirectionInView);
    struct ContactShadowFrame final {
        ContactShadowSettings settings;
        Math::Vector3 lightDirectionView;
    };
    const ContactShadowFrame settingsInView{
        contactShadowSettings,
        sunDirectionInViewLengthSquared > 1.0e-8F ? Math::Normalize(sunDirectionInView)
                                                  : Math::Vector3{0.0F, 0.0F, 0.0F}};
    const bool contactShadowActive = !capturingProbe && environmentAllowsPost &&
                                     contactShadowSettings.enabled &&
                                     m_impl->contactShadowTarget != kInvalidTextureHandle && projectionInvertible &&
                                     sunDirectionInViewLengthSquared > 1.0e-8F &&
                                     m_impl->contactShadowProgram != nullptr &&
                                     m_impl->contactShadowProgram->IsValid();

    RenderGraphResourceHandle contactShadowResource{};
    if (contactShadowActive) {
        m_impl->lastFrameDiagnostics.contactShadowPassRecorded = true;
        contactShadowResource =
            renderGraph.ImportTexture(m_impl->contactShadowTarget, "ContactShadow");
        const std::array<RenderGraphResourceUse, 2U> contactShadowPassResources{
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{contactShadowResource, RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "ContactShadows", contactShadowPassResources,
            [this, &projection, &inverseProjection, settingsInView, isOrthographic,
             depthNdcScale, depthNdcBias, projA, projB, invProjX, invProjY, fastDepthReconstruct](
                ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->contactShadowTarget;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::DontCare;
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->contactShadowProgram->SetInt("uDepthTexture", 0);
                m_impl->contactShadowProgram->SetMatrix4x4("uProjection", projection);
                m_impl->contactShadowProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
                m_impl->contactShadowProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
                m_impl->contactShadowProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                m_impl->contactShadowProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                m_impl->contactShadowProgram->SetFloat("uProjA", projA);
                m_impl->contactShadowProgram->SetFloat("uProjB", projB);
                m_impl->contactShadowProgram->SetFloat("uInvProjX", invProjX);
                m_impl->contactShadowProgram->SetFloat("uInvProjY", invProjY);
                m_impl->contactShadowProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
                m_impl->contactShadowProgram->SetVector3("uLightDirectionView", settingsInView.lightDirectionView);
                m_impl->contactShadowProgram->SetFloat("uLengthMeters", settingsInView.settings.lengthMeters);
                m_impl->contactShadowProgram->SetFloat("uThicknessMeters", settingsInView.settings.thicknessMeters);
                m_impl->contactShadowProgram->SetFloat("uStrength", settingsInView.settings.strength);
                m_impl->contactShadowProgram->SetInt("uStepCount",
                                                        static_cast<std::int32_t>(settingsInView.settings.stepCount));
                m_impl->contactShadowProgram->SetFloat("uDepthBiasMeters", kContactShadowDepthBiasMeters);
                m_impl->contactShadowProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->depthTarget, 0U);
                if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->shadowPointSampler, 0U);
                }
                commandBuffer.Draw(3);
                commandBuffer.EndRenderPass();
            });
    }

    const bool ssrActive = !capturingProbe && environmentAllowsPost &&
                           m_impl->postProcessSettings.ssrEnabled &&
                           m_impl->ssrTarget != kInvalidTextureHandle && projectionInvertible &&
                           m_impl->ssrProgram != nullptr && m_impl->ssrProgram->IsValid();
    RenderGraphResourceHandle ssrResource{};
    if (ssrActive) {
        m_impl->lastFrameDiagnostics.ssrPassRecorded = true;
        ssrResource =
            renderGraph.ImportTexture(m_impl->ssrTarget, "ScreenSpaceReflections");
        const std::array<RenderGraphResourceUse, 3U> ssrPassResources{
            RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{ssrResource, RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "ScreenSpaceReflections", ssrPassResources,
            [this, projection, inverseProjection, isOrthographic,
             depthNdcScale, depthNdcBias, projA, projB, invProjX, invProjY, fastDepthReconstruct](
                ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->ssrTarget;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::Clear;
                passDesc.clearColor = {0.0F, 0.0F, 0.0F, 0.0F};
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->ssrProgram->SetInt("uColorTexture", 0);
                m_impl->ssrProgram->SetInt("uDepthTexture", 1);
                m_impl->ssrProgram->SetMatrix4x4("uProjection", projection);
                m_impl->ssrProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
                m_impl->ssrProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
                m_impl->ssrProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                m_impl->ssrProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                m_impl->ssrProgram->SetFloat("uProjA", projA);
                m_impl->ssrProgram->SetFloat("uProjB", projB);
                m_impl->ssrProgram->SetFloat("uInvProjX", invProjX);
                m_impl->ssrProgram->SetFloat("uInvProjY", invProjY);
                m_impl->ssrProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
                m_impl->ssrProgram->SetVector3("uCameraPosition", m_impl->environmentCameraPosition);
                m_impl->ssrProgram->SetFloat("uCameraNear", m_impl->environmentCameraNear);
                m_impl->ssrProgram->SetFloat("uCameraFar", m_impl->environmentCameraFar);
                m_impl->ssrProgram->SetInt("uMaxSteps", static_cast<std::int32_t>(m_impl->postProcessSettings.ssrMaxSteps));
                m_impl->ssrProgram->SetFloat("uDepthTolerance", m_impl->postProcessSettings.ssrDepthTolerance);
                m_impl->ssrProgram->SetFloat("uFadeIn", m_impl->postProcessSettings.ssrFadeIn);
                m_impl->ssrProgram->SetFloat("uFadeOut", m_impl->postProcessSettings.ssrFadeOut);
                m_impl->ssrProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->colorTarget, 0U);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                }
                commandBuffer.BindTexture(m_impl->depthTarget, 1U);
                if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->shadowPointSampler, 1U);
                }
                commandBuffer.Draw(3);
                commandBuffer.EndRenderPass();
            });
    }

    RenderGraphResourceHandle finalBloomResource{};
    if (bloomActive) {
        m_impl->lastFrameDiagnostics.bloomPassRecorded = true;
        m_impl->lastFrameDiagnostics.bloomMipCountUsed = bloomMipCountUsed;
        std::array<RenderGraphResourceHandle, kMaximumBloomMipCount> bloomBrightResources{};
        std::array<RenderGraphResourceHandle, kMaximumBloomMipCount> bloomBlurAResources{};
        std::array<RenderGraphResourceHandle, kMaximumBloomMipCount> bloomBlurBResources{};
        for (std::uint32_t mipIndex = 0U; mipIndex < bloomMipCountUsed; ++mipIndex) {
            const std::string suffix = std::to_string(mipIndex);
            bloomBrightResources[mipIndex] = renderGraph.ImportTexture(
                m_impl->bloomMipTargets[mipIndex].bright, std::string{"BloomBright"} + suffix);
            bloomBlurAResources[mipIndex] = renderGraph.ImportTexture(
                m_impl->bloomMipTargets[mipIndex].blurA, std::string{"BloomBlurA"} + suffix);
            bloomBlurBResources[mipIndex] = renderGraph.ImportTexture(
                m_impl->bloomMipTargets[mipIndex].blurB, std::string{"BloomBlurB"} + suffix);
        }

        const float bloomThreshold = hasPostOverride
                                         ? m_impl->environmentFrame.bloomThreshold
                                         : kBloomThreshold;
        const float configuredBloomIntensity = hasPostOverride
                                                   ? m_impl->environmentFrame.bloomIntensity
                                                   : 1.0F;
        const float bloomSoftKnee = hasPostOverride
                                        ? m_impl->environmentFrame.bloomSoftKnee
                                        : kBloomSoftKnee;
        const float bloomExposure = m_impl->environmentFrame.exposure > 0.0F
                                        ? m_impl->environmentFrame.exposure
                                        : 1.0F;
        const std::array<RenderGraphResourceUse, 2U> brightPassResources{
            RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Read},
            RenderGraphResourceUse{bloomBrightResources[0U], RenderGraphResourceAccess::Write}};
        renderGraph.AddPass(
            "BloomBrightPass", brightPassResources,
            [this, bloomThreshold, configuredBloomIntensity, bloomSoftKnee, bloomExposure](
                ICommandBuffer& commandBuffer) {
                RenderPassDesc passDesc;
                passDesc.colorAttachment = m_impl->bloomMipTargets[0U].bright;
                passDesc.depthAttachment = kInvalidTextureHandle;
                passDesc.colorLoadOp = LoadOp::DontCare;
                commandBuffer.BeginRenderPass(passDesc);
                m_impl->bloomBrightPassProgram->SetInt("uSourceTexture", 0);
                m_impl->bloomBrightPassProgram->SetFloat("uBloomThreshold", bloomThreshold);
                m_impl->bloomBrightPassProgram->SetFloat("uBloomIntensity", configuredBloomIntensity);
                m_impl->bloomBrightPassProgram->SetFloat("uBloomSoftKnee", bloomSoftKnee);
                m_impl->bloomBrightPassProgram->SetFloat("uExposure", bloomExposure);
                m_impl->bloomBrightPassProgram->ApplyTo(commandBuffer);
                commandBuffer.BindTexture(m_impl->colorTarget, 0U);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                }
                commandBuffer.Draw(3);
                commandBuffer.EndRenderPass();
            });

        std::array<std::pair<std::uint32_t, std::uint32_t>, kMaximumBloomMipCount> mipSizes{};
        mipSizes[0U] = {HalfExtent(m_impl->targetWidth), HalfExtent(m_impl->targetHeight)};
        for (std::uint32_t mipIndex = 1U; mipIndex < bloomMipCountUsed; ++mipIndex) {
            mipSizes[mipIndex] = {HalfExtent(mipSizes[mipIndex - 1U].first),
                                  HalfExtent(mipSizes[mipIndex - 1U].second)};
            const std::array<RenderGraphResourceUse, 2U> downsampleResources{
                RenderGraphResourceUse{bloomBrightResources[mipIndex - 1U], RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{bloomBrightResources[mipIndex], RenderGraphResourceAccess::Write}};
            const std::string downsampleName = "BloomDownsample" + std::to_string(mipIndex);
            renderGraph.AddPass(
                downsampleName, downsampleResources,
                [this, mipIndex](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->bloomMipTargets[mipIndex].bright;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->bloomDownsampleProgram->SetInt("uSourceTexture", 0);
                    m_impl->bloomDownsampleProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->bloomMipTargets[mipIndex - 1U].bright, 0U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });
        }

        if (bloomMipCountUsed == 1U) {
            const float texelSizeX = 1.0F / static_cast<float>(mipSizes[0U].first);
            const float texelSizeY = 1.0F / static_cast<float>(mipSizes[0U].second);
            const std::array<RenderGraphResourceUse, 2U> blurHResources{
                RenderGraphResourceUse{bloomBrightResources[0U], RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{bloomBlurAResources[0U], RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "BloomBlurH", blurHResources,
                [this, texelSizeX, texelSizeY](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->bloomMipTargets[0U].blurA;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->bloomBlurProgram->SetInt("uSourceTexture", 0);
                    m_impl->bloomBlurProgram->SetInt("uUpsampleMode", 0);
                    m_impl->bloomBlurProgram->SetFloat("uBlurDirectionX", 1.0F);
                    m_impl->bloomBlurProgram->SetFloat("uBlurDirectionY", 0.0F);
                    m_impl->bloomBlurProgram->SetFloat("uTexelSizeX", texelSizeX);
                    m_impl->bloomBlurProgram->SetFloat("uTexelSizeY", texelSizeY);
                    m_impl->bloomBlurProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->bloomMipTargets[0U].bright, 0U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });

            const std::array<RenderGraphResourceUse, 2U> blurVResources{
                RenderGraphResourceUse{bloomBlurAResources[0U], RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{bloomBlurBResources[0U], RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "BloomBlurV", blurVResources,
                [this, texelSizeX, texelSizeY](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = m_impl->bloomMipTargets[0U].blurB;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->bloomBlurProgram->SetInt("uSourceTexture", 0);
                    m_impl->bloomBlurProgram->SetInt("uUpsampleMode", 0);
                    m_impl->bloomBlurProgram->SetFloat("uBlurDirectionX", 0.0F);
                    m_impl->bloomBlurProgram->SetFloat("uBlurDirectionY", 1.0F);
                    m_impl->bloomBlurProgram->SetFloat("uTexelSizeX", texelSizeX);
                    m_impl->bloomBlurProgram->SetFloat("uTexelSizeY", texelSizeY);
                    m_impl->bloomBlurProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->bloomMipTargets[0U].blurA, 0U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                });
        } else {
            // Progressive 3x3 tent upsample from coarsest mip down to mip 0 into blurB[0].
            for (std::uint32_t step = bloomMipCountUsed - 1U; step > 0U; --step) {
                const std::uint32_t fineMip = step - 1U;
                const bool firstStep = (step == bloomMipCountUsed - 1U);
                const RenderGraphResourceHandle coarseResource =
                    firstStep ? bloomBrightResources[step] : bloomBlurBResources[step];
                const TextureHandle coarseTexture =
                    firstStep ? m_impl->bloomMipTargets[step].bright : m_impl->bloomMipTargets[step].blurB;
                const TextureHandle fineTexture = m_impl->bloomMipTargets[fineMip].bright;
                const TextureHandle dstTexture = m_impl->bloomMipTargets[fineMip].blurB;
                const float coarseTexelX = 1.0F / static_cast<float>(mipSizes[step].first);
                const float coarseTexelY = 1.0F / static_cast<float>(mipSizes[step].second);
                const std::array<RenderGraphResourceUse, 3U> upsampleResources{
                    RenderGraphResourceUse{coarseResource, RenderGraphResourceAccess::Read},
                    RenderGraphResourceUse{bloomBrightResources[fineMip], RenderGraphResourceAccess::Read},
                    RenderGraphResourceUse{bloomBlurBResources[fineMip], RenderGraphResourceAccess::Write}};
                const std::string upsampleName = "BloomUpsample" + std::to_string(fineMip);
                renderGraph.AddPass(
                    upsampleName, upsampleResources,
                    [this, coarseTexture, fineTexture, dstTexture, coarseTexelX, coarseTexelY](
                        ICommandBuffer& commandBuffer) {
                        RenderPassDesc passDesc;
                        passDesc.colorAttachment = dstTexture;
                        passDesc.depthAttachment = kInvalidTextureHandle;
                        passDesc.colorLoadOp = LoadOp::DontCare;
                        commandBuffer.BeginRenderPass(passDesc);
                        m_impl->bloomBlurProgram->SetInt("uSourceTexture", 0);
                        m_impl->bloomBlurProgram->SetInt("uHigherMipTexture", 1);
                        m_impl->bloomBlurProgram->SetInt("uUpsampleMode", 1);
                        m_impl->bloomBlurProgram->SetFloat("uUpsampleScatter", 0.65F);
                        m_impl->bloomBlurProgram->SetFloat("uTexelSizeX", coarseTexelX);
                        m_impl->bloomBlurProgram->SetFloat("uTexelSizeY", coarseTexelY);
                        m_impl->bloomBlurProgram->ApplyTo(commandBuffer);
                        commandBuffer.BindTexture(coarseTexture, 0U);
                        if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                            commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                        }
                        commandBuffer.BindTexture(fineTexture, 1U);
                        if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                            commandBuffer.BindSampler(m_impl->clampLinearSampler, 1U);
                        }
                        commandBuffer.Draw(3);
                        commandBuffer.EndRenderPass();
                    });
            }
        }
        finalBloomResource = bloomBlurBResources[0U];
    }

    RenderGraphResourceHandle toneMappingColorResource = colorResource;
    TextureHandle toneMappingColorTexture = m_impl->colorTarget;
    if (taaActive) {
        if (auto* const taaSet = m_impl->ActiveTargetSetMutable(); taaSet != nullptr) {
            const std::uint32_t writeIdx = taaSet->taaHistoryWriteIndex & 1U;
            const std::uint32_t readIdx = 1U - writeIdx;
            const TextureHandle historyReadTex = taaSet->taaHistoryTargets[readIdx];
            const TextureHandle historyWriteTex = taaSet->taaHistoryTargets[writeIdx];
            const bool historyReady = taaSet->taaHistoryValid && motionBlurHistoryReady;
            Math::Matrix4x4 inverseCurrentVp{};
            const bool vpInvertible = Math::TryInverse(viewProjection, inverseCurrentVp);
            const RenderGraphResourceHandle taaHistoryReadRes =
                renderGraph.ImportTexture(historyReadTex, "TAAHistoryRead");
            const RenderGraphResourceHandle taaHistoryWriteRes =
                renderGraph.ImportTexture(historyWriteTex, "TAAHistoryWrite");
            const std::array<RenderGraphResourceUse, 4U> taaUses{
                RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{taaHistoryReadRes, RenderGraphResourceAccess::Read},
                RenderGraphResourceUse{taaHistoryWriteRes, RenderGraphResourceAccess::Write}};
            renderGraph.AddPass(
                "TAAResolve", taaUses,
                [this, historyReadTex, historyWriteTex, historyReady, vpInvertible, inverseCurrentVp,
                 previousMotionBlurViewProjection, depthNdcScale, depthNdcBias](ICommandBuffer& commandBuffer) {
                    RenderPassDesc passDesc;
                    passDesc.colorAttachment = historyWriteTex;
                    passDesc.depthAttachment = kInvalidTextureHandle;
                    passDesc.colorLoadOp = LoadOp::DontCare;
                    commandBuffer.BeginRenderPass(passDesc);
                    m_impl->taaResolveProgram->SetInt("uCurrentColorTexture", 0);
                    m_impl->taaResolveProgram->SetInt("uHistoryColorTexture", 1);
                    m_impl->taaResolveProgram->SetInt("uDepthTexture", 2);
                    m_impl->taaResolveProgram->SetMatrix4x4("uInverseViewProjection", inverseCurrentVp);
                    m_impl->taaResolveProgram->SetMatrix4x4("uPreviousViewProjection",
                                                               previousMotionBlurViewProjection);
                    m_impl->taaResolveProgram->SetFloat("uDepthNdcScale", depthNdcScale);
                    m_impl->taaResolveProgram->SetFloat("uDepthNdcBias", depthNdcBias);
                    m_impl->taaResolveProgram->SetFloat(
                        "uHistoryWeight",
                        std::clamp(m_impl->postProcessSettings.taaHistoryWeight, 0.0F, 0.96F));
                    m_impl->taaResolveProgram->SetInt("uHistoryValid", (historyReady && vpInvertible) ? 1 : 0);
                    m_impl->taaResolveProgram->SetVector2(
                        "uTexelSize",
                        Math::Vector2{1.0F / static_cast<float>(std::max<std::uint32_t>(m_impl->targetWidth, 1U)),
                                         1.0F / static_cast<float>(std::max<std::uint32_t>(m_impl->targetHeight, 1U))});
                    m_impl->taaResolveProgram->ApplyTo(commandBuffer);
                    commandBuffer.BindTexture(m_impl->colorTarget, 0U);
                    commandBuffer.BindTexture(historyReadTex, 1U);
                    commandBuffer.BindTexture(m_impl->depthTarget, 2U);
                    if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 0U);
                        commandBuffer.BindSampler(m_impl->clampLinearSampler, 1U);
                    }
                    if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, 2U);
                    }
                    commandBuffer.Draw(3);
                    commandBuffer.EndRenderPass();
                    m_impl->lastFrameDiagnostics.temporalAAPassApplied = true;
                });
            toneMappingColorResource = taaHistoryWriteRes;
            toneMappingColorTexture = historyWriteTex;
            taaSet->taaHistoryValid = true;
            taaSet->taaHistoryWriteIndex = 1U - writeIdx;
            ++m_impl->taaFrameIndex;
        }
    }

    // The default framebuffer is an external presentation surface, not a TextureHandle; the
    // scene color, depth, shadow cascades, and active post-process targets are declared explicitly
    // as Read dependencies while this unified pass writes that external output.
    std::array<RenderGraphResourceUse, 9U> toneMappingResources{};
    std::size_t toneMappingResourceCount = 0U;
    toneMappingResources[toneMappingResourceCount++] =
        RenderGraphResourceUse{toneMappingColorResource, RenderGraphResourceAccess::Read};
    toneMappingResources[toneMappingResourceCount++] =
        RenderGraphResourceUse{depthResource, RenderGraphResourceAccess::Read};
    if (shadowsReady) {
        for (const RenderGraphResourceHandle shadowResource : shadowResources) {
            toneMappingResources[toneMappingResourceCount++] =
                RenderGraphResourceUse{shadowResource, RenderGraphResourceAccess::Read};
        }
    }
    if (ssaoActive && ssaoResource.IsValid()) {
        toneMappingResources[toneMappingResourceCount++] =
            RenderGraphResourceUse{ssaoResource, RenderGraphResourceAccess::Read};
    }
    if (contactShadowActive && contactShadowResource.IsValid()) {
        toneMappingResources[toneMappingResourceCount++] =
            RenderGraphResourceUse{contactShadowResource, RenderGraphResourceAccess::Read};
    }
    if (ssrActive && ssrResource.IsValid()) {
        toneMappingResources[toneMappingResourceCount++] =
            RenderGraphResourceUse{ssrResource, RenderGraphResourceAccess::Read};
    }
    if (bloomActive && finalBloomResource.IsValid()) {
        toneMappingResources[toneMappingResourceCount++] =
            RenderGraphResourceUse{finalBloomResource, RenderGraphResourceAccess::Read};
    }

    // An unlinked program records no work: do not declare a phantom read (and barrier) while
    // shader compilation is pending. The callback retains its defensive readiness check.
    if (m_impl->toneMappingProgram->IsValid()) renderGraph.AddPass(
        "ToneMapping",
        std::span<const RenderGraphResourceUse>{toneMappingResources.data(), toneMappingResourceCount},
        [this, toneMappingColorTexture, inverseProjection, previousMotionBlurViewProjection, motionBlurHistoryReady,
         shadowsReady, isOrthographic, orthoHalfHeight, sunDiscActive, capturingProbe, hasPostOverride,
         ssaoActive, contactShadowActive, ssrActive, bloomActive,
         depthNdcScale, depthNdcBias, projA, projB, invProjX, invProjY, fastDepthReconstruct,
         &lightSpaceMatrices, &cascadeSplits](
            ICommandBuffer& commandBuffer) {
            if (!m_impl->toneMappingProgram->IsValid()) {
                return;
            }
            m_impl->lastFrameDiagnostics.toneMappingPassRecorded = true;
            const bool fastApproximateAAEnabled =
                m_impl->postProcessSettings.fastApproximateAAEnabled && !m_impl->probeCaptureViewActive;
            m_impl->lastFrameDiagnostics.fastApproximateAAPassApplied = fastApproximateAAEnabled;
            RenderPassDesc passDesc;
            passDesc.colorAttachment = m_impl->destinationTextureOverride.has_value()
                                           ? m_impl->destinationTextureOverride->first
                                           : kInvalidTextureHandle;
            passDesc.depthAttachment = m_impl->destinationTextureOverride.has_value()
                                           ? m_impl->destinationTextureOverride->second
                                           : kInvalidTextureHandle;
            // Tier 2.3: the override target's rendered layers (explicit zeros when unset, so a
            // full-frame call keeps targeting layer 0 even if this desc object is ever reused).
            passDesc.colorLayer = m_impl->destinationTextureOverride.has_value()
                                      ? m_impl->destinationColorLayerOverride
                                      : 0U;
            passDesc.depthLayer = m_impl->destinationTextureOverride.has_value()
                                      ? m_impl->destinationDepthLayerOverride
                                      : 0U;
            // A caller-supplied destination texture is generally not the size of the presentation
            // surface, and a render pass that names no viewport inherits the surface's. Without
            // this the fullscreen tone-mapping triangle would be rasterised at surface size while
            // only the texture-sized corner of it was captured, cropping the frame rather than
            // filling the texture. destinationViewportOverride (RenderFrameToRegion's explicit
            // sub-region) still wins when set, since that caller is asking for a crop on purpose.
            passDesc.viewportOverride = m_impl->destinationViewportOverride;
            if (!passDesc.viewportOverride.has_value() && m_impl->destinationTextureOverride.has_value() &&
                m_impl->destinationTextureSizeOverride.has_value()) {
                passDesc.viewportOverride =
                    ViewportRect{Math::Vector2i{0, 0},
                                    Math::Vector2i{static_cast<std::int32_t>(m_impl->destinationTextureSizeOverride->first),
                                                      static_cast<std::int32_t>(m_impl->destinationTextureSizeOverride->second)}};
            }
            commandBuffer.BeginRenderPass(passDesc);
            m_impl->toneMappingProgram->SetInt("uSourceTexture", 0);
            m_impl->toneMappingProgram->SetInt("uNearestUpscaling",
                                                 m_impl->postProcessSettings.nearestUpscalingEnabled ? 1 : 0);
            const std::int32_t toneMappingMethod =
                !m_impl->postProcessSettings.acesToneMappingEnabled
                    ? 0
                    : static_cast<std::int32_t>(std::min(m_impl->postProcessSettings.toneMappingMethod, 2U));
            m_impl->toneMappingProgram->SetInt("uToneMappingMethod", toneMappingMethod);
            m_impl->toneMappingProgram->SetFloat("uSharpeningAmount",
                                                   m_impl->postProcessSettings.sharpeningAmount);
            m_impl->toneMappingProgram->SetInt("uDitheringEnabled",
                                                 m_impl->postProcessSettings.ditheringEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uFastApproximateAA", fastApproximateAAEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt(
                "uScreenSpaceAAQuality", static_cast<std::int32_t>(m_impl->postProcessSettings.fastApproximateAAQuality));
            m_impl->toneMappingProgram->SetFloat("uFxaaTexelX", m_impl->humanEyeTexelX);
            m_impl->toneMappingProgram->SetFloat("uFxaaTexelY", m_impl->humanEyeTexelY);
            m_impl->toneMappingProgram->SetInt("uSceneDepthTexture", 1);
            // A 3D View with a sky behind it is opaque; a probe or a see-through view keeps coverage.
            m_impl->toneMappingProgram->SetInt("uWriteCoverageAlpha",
                                                   m_impl->destinationTextureOverride.has_value() &&
                                                           !(m_impl->uiViewCaptureActive && !m_impl->captureTransparentBackground)
                                                       ? 1
                                                       : 0);
            const bool encodeSrgbOutput =
                (!capturingProbe || m_impl->uiViewCaptureActive) &&
                (m_impl->destinationTextureOverride.has_value() ||
                 !m_impl->renderDevice.IsPresentationSurfaceSrgb());
            m_impl->toneMappingProgram->SetInt("uEncodeSrgbOutput", encodeSrgbOutput ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uSsaoCompositeEnabled", ssaoActive ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uContactShadowCompositeEnabled", contactShadowActive ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uSsrCompositeEnabled", ssrActive ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uBloomCompositeEnabled", bloomActive ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uHasColorGrading",
                                                 hasPostOverride && m_impl->environmentFrame.postProcessingEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uHasLutTexture", m_impl->lutTextureEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uLutTexture",
                                                 static_cast<std::int32_t>(kToneMappingLutTextureSlot));
            m_impl->toneMappingProgram->SetInt("uSsaoTexture",
                                                 static_cast<std::int32_t>(kToneMappingSsaoTextureSlot));
            m_impl->toneMappingProgram->SetInt("uContactShadowTexture",
                                                 static_cast<std::int32_t>(kToneMappingContactShadowTextureSlot));
            m_impl->toneMappingProgram->SetInt("uSsrTexture",
                                                 static_cast<std::int32_t>(kToneMappingSsrTextureSlot));
            m_impl->toneMappingProgram->SetInt("uBloomTexture",
                                                 static_cast<std::int32_t>(kToneMappingBloomTextureSlot));
            m_impl->toneMappingProgram->SetFloat("uDepthNdcScale", depthNdcScale);
            m_impl->toneMappingProgram->SetFloat("uDepthNdcBias", depthNdcBias);
            m_impl->toneMappingProgram->SetFloat("uProjA", projA);
            m_impl->toneMappingProgram->SetFloat("uProjB", projB);
            m_impl->toneMappingProgram->SetFloat("uInvProjX", invProjX);
            m_impl->toneMappingProgram->SetFloat("uInvProjY", invProjY);
            m_impl->toneMappingProgram->SetInt("uFastReconstruct", fastDepthReconstruct ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uHumanEye", m_impl->humanEyeEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetFloat("uHumanEyeCenterScale", m_impl->humanEyeCenterScale);
            m_impl->toneMappingProgram->SetFloat("uHumanEyeTexelX", m_impl->humanEyeTexelX);
            m_impl->toneMappingProgram->SetFloat("uHumanEyeTexelY", m_impl->humanEyeTexelY);
            m_impl->toneMappingProgram->SetFloat("uExposure", m_impl->environmentFrame.exposure);
            m_impl->toneMappingProgram->SetInt("uFogEnabled", m_impl->environmentFrame.fogEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uFogMode",
                                                   static_cast<std::int32_t>(m_impl->environmentFrame.fogMode));
            m_impl->toneMappingProgram->SetVector3("uFogColor", m_impl->environmentFrame.fogColor);
            m_impl->toneMappingProgram->SetFloat("uFogDensity", m_impl->environmentFrame.fogDensity);
            m_impl->toneMappingProgram->SetFloat("uFogStart", m_impl->environmentFrame.fogStart);
            m_impl->toneMappingProgram->SetFloat("uFogEnd", m_impl->environmentFrame.fogEnd);
            m_impl->toneMappingProgram->SetMatrix4x4("uInverseProjection", inverseProjection);
            m_impl->toneMappingProgram->SetFloat("uCameraNear", m_impl->environmentCameraNear);
            m_impl->toneMappingProgram->SetFloat("uCameraFar", m_impl->environmentCameraFar);
            m_impl->toneMappingProgram->SetFloat("uFogSkyAffect", m_impl->environmentFrame.fogSkyAffect);
            m_impl->toneMappingProgram->SetInt(
                "uSkyCovers", m_impl->environmentFrame.hasEnvironment && !m_impl->captureTransparentBackground ? 1 : 0);
            m_impl->toneMappingProgram->SetInt("uOrthographic", isOrthographic ? 1 : 0);
            m_impl->toneMappingProgram->SetFloat("uOrthoHalfHeight", orthoHalfHeight);
            m_impl->toneMappingProgram->SetFloat("uBrightness", m_impl->environmentFrame.brightness);
            m_impl->toneMappingProgram->SetFloat("uContrast", m_impl->environmentFrame.contrast);
            m_impl->toneMappingProgram->SetFloat("uSaturation", m_impl->environmentFrame.saturation);
            m_impl->toneMappingProgram->SetVector3("uColorFilter", m_impl->environmentFrame.colorFilter);
            const float vignetteIntensity = m_impl->environmentFrame.postProcessingEnabled
                                                ? m_impl->environmentFrame.vignetteIntensity
                                                : 0.0F;
            m_impl->toneMappingProgram->SetFloat("uVignetteIntensity", vignetteIntensity);
            m_impl->toneMappingProgram->SetFloat("uVignetteRadius", m_impl->environmentFrame.vignetteRadius);
            const float chromaticAberrationIntensity = m_impl->environmentFrame.postProcessingEnabled
                                                           ? m_impl->environmentFrame.chromaticAberrationIntensity
                                                           : 0.0F;
            m_impl->toneMappingProgram->SetFloat("uChromaticAberrationIntensity",
                                                    chromaticAberrationIntensity);
            const float filmGrainIntensity = m_impl->environmentFrame.postProcessingEnabled
                                                 ? m_impl->environmentFrame.filmGrainIntensity
                                                 : 0.0F;
            m_impl->toneMappingProgram->SetFloat("uFilmGrainIntensity", filmGrainIntensity);
            m_impl->toneMappingProgram->SetInt(
                "uFilmGrainFrame", static_cast<std::int32_t>(m_impl->renderSystem.GetFrameIndex() % 4096U));
            const float lensDistortionIntensity = m_impl->environmentFrame.postProcessingEnabled
                                                      ? m_impl->environmentFrame.lensDistortionIntensity
                                                      : 0.0F;
            m_impl->toneMappingProgram->SetFloat("uLensDistortionIntensity", lensDistortionIntensity);
            const bool depthOfFieldEnabled = m_impl->environmentFrame.postProcessingEnabled &&
                                             m_impl->environmentFrame.depthOfFieldEnabled;
            m_impl->toneMappingProgram->SetInt("uDepthOfFieldEnabled", depthOfFieldEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetInt(
                "uDepthOfFieldFocusMode",
                static_cast<std::int32_t>(m_impl->environmentFrame.depthOfFieldFocusMode));
            m_impl->toneMappingProgram->SetInt(
                "uDepthOfFieldBokehShape",
                static_cast<std::int32_t>(m_impl->environmentFrame.depthOfFieldBokehShape));
            m_impl->toneMappingProgram->SetFloat("uDepthOfFieldFocusDistance",
                                                    m_impl->environmentFrame.depthOfFieldFocusDistance);
            m_impl->toneMappingProgram->SetFloat("uDepthOfFieldAperture",
                                                    m_impl->environmentFrame.depthOfFieldAperture);
            m_impl->toneMappingProgram->SetInt(
                "uDepthOfFieldQuality",
                static_cast<std::int32_t>(std::min(m_impl->environmentFrame.depthOfFieldQuality, 2U)));
            const bool motionBlurEnabled = m_impl->environmentFrame.postProcessingEnabled &&
                                           m_impl->environmentFrame.motionBlurEnabled && motionBlurHistoryReady;
            m_impl->toneMappingProgram->SetInt("uMotionBlurEnabled", motionBlurEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetFloat(
                "uMotionBlurStrength", motionBlurEnabled ? m_impl->environmentFrame.motionBlurStrength : 0.0F);
            m_impl->toneMappingProgram->SetInt(
                "uMotionBlurSampleCount",
                static_cast<std::int32_t>(std::clamp(m_impl->environmentFrame.motionBlurSampleCount, 4U, 12U)));
            m_impl->toneMappingProgram->SetMatrix4x4("uPreviousViewProjection", previousMotionBlurViewProjection);
            m_impl->toneMappingProgram->SetVector3("uCameraPosition", m_impl->environmentCameraPosition);
            m_impl->toneMappingProgram->SetVector3("uCameraRight", m_impl->skyCameraRight);
            m_impl->toneMappingProgram->SetVector3("uCameraUp", m_impl->skyCameraUp);
            m_impl->toneMappingProgram->SetVector3("uCameraForward", m_impl->skyCameraForward);
            m_impl->toneMappingProgram->SetFloat("uTanHalfFov", m_impl->skyTanHalfFov);
            m_impl->toneMappingProgram->SetFloat("uAspect", m_impl->environmentAspect);
            m_impl->toneMappingProgram->SetVector3("uSunDirection", m_impl->sunDirection);
            m_impl->toneMappingProgram->SetVector3("uSunColor", m_impl->sunColor);
            m_impl->toneMappingProgram->SetFloat("uSunEnergy", m_impl->sunEnergy);
            m_impl->toneMappingProgram->SetFloat(
                "uSunAngularDiameter", sunDiscActive ? m_impl->sunDiscAngularDiameterDegrees : 0.0F);
            m_impl->toneMappingProgram->SetFloat("uFogHeight", m_impl->environmentFrame.fogHeight);
            m_impl->toneMappingProgram->SetFloat("uFogHeightFalloff", m_impl->environmentFrame.fogHeightFalloff);
            m_impl->toneMappingProgram->SetFloat("uFogSunScatter", m_impl->environmentFrame.fogSunScatter);
            m_impl->toneMappingProgram->SetFloat("uLightVolumetricFogEnergy", m_impl->sunVolumetricFogEnergy);
            m_impl->toneMappingProgram->SetInt("uFogVolumeCount",
                                                   static_cast<std::int32_t>(m_impl->fogVolumeCount));
            const auto& fogNames = GetRendererUniformNames().fogVolumes;
            const std::size_t activeFogVolumes =
                std::min(m_impl->fogVolumeCount, Scene::kMaximumFogVolumesPerFrame);
            for (std::size_t volumeIndex = 0; volumeIndex < activeFogVolumes; ++volumeIndex) {
                const Scene::FogVolume3DFrame& volume = m_impl->fogVolumes[volumeIndex];
                const FogVolumeUniformNames& names = fogNames[volumeIndex];
                m_impl->toneMappingProgram->SetVector3(names.position, volume.worldPosition);
                m_impl->toneMappingProgram->SetVector3(names.axisX, volume.axisX);
                m_impl->toneMappingProgram->SetVector3(names.axisY, volume.axisY);
                m_impl->toneMappingProgram->SetVector3(names.axisZ, volume.axisZ);
                m_impl->toneMappingProgram->SetVector3(names.scale, volume.worldScale);
                m_impl->toneMappingProgram->SetVector3(names.size, volume.size);
                m_impl->toneMappingProgram->SetVector3(names.albedo, volume.albedo);
                m_impl->toneMappingProgram->SetVector3(names.emission, volume.emission);
                m_impl->toneMappingProgram->SetFloat(names.density, volume.density);
                m_impl->toneMappingProgram->SetFloat(names.heightFalloff, volume.heightFalloff);
                m_impl->toneMappingProgram->SetFloat(names.edgeFade, volume.edgeFade);
                m_impl->toneMappingProgram->SetInt(names.shape, static_cast<std::int32_t>(volume.shape));
            }
            const bool volumetricShadowsActive = shadowsReady && m_impl->sunVolumetricFogEnergy > 0.0F;
            m_impl->toneMappingProgram->SetInt("uVolumetricShadowsEnabled", volumetricShadowsActive ? 1 : 0);
            const auto& uniformNames = GetRendererUniformNames();
            for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
                m_impl->toneMappingProgram->SetMatrix4x4(
                    uniformNames.lightSpaceMatrices[cascadeIndex],
                    lightSpaceMatrices[cascadeIndex]);
                m_impl->toneMappingProgram->SetFloat(
                    uniformNames.shadowCascadeSplits[cascadeIndex],
                    cascadeSplits[cascadeIndex]);
                m_impl->toneMappingProgram->SetInt(
                    uniformNames.shadowMapTextures[cascadeIndex],
                    static_cast<std::int32_t>(kShadowCascadeFirstTextureSlot + cascadeIndex));
            }
            if (volumetricShadowsActive) {
                m_impl->lastFrameDiagnostics.volumetricFogPassRecorded = true;
            }
            m_impl->toneMappingProgram->SetInt("uLutEnabled", m_impl->environmentFrame.lutEnabled ? 1 : 0);
            m_impl->toneMappingProgram->SetFloat("uLutContribution", m_impl->environmentFrame.lutContribution);
            m_impl->toneMappingProgram->ApplyTo(commandBuffer);
            commandBuffer.BindTexture(toneMappingColorTexture, 0U);
            const SamplerHandle colorSampler = m_impl->postProcessSettings.nearestUpscalingEnabled
                                                      ? m_impl->shadowPointSampler
                                                      : m_impl->clampLinearSampler;
            if (colorSampler != kInvalidSamplerHandle) {
                commandBuffer.BindSampler(colorSampler, 0U);
            }
            commandBuffer.BindTexture(m_impl->depthTarget, 1U);
            if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                commandBuffer.BindSampler(m_impl->shadowPointSampler, 1U);
            }
            if (shadowsReady) {
                for (std::size_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
                    const std::uint32_t slot =
                        kShadowCascadeFirstTextureSlot + static_cast<std::uint32_t>(cascadeIndex);
                    commandBuffer.BindTexture(m_impl->shadowMapTargets[cascadeIndex], slot);
                    if (m_impl->shadowPointSampler != kInvalidSamplerHandle) {
                        commandBuffer.BindSampler(m_impl->shadowPointSampler, slot);
                    }
                }
            }
            if (m_impl->lutTextureEnabled && m_impl->lutTextureHandle != kInvalidTextureHandle) {
                commandBuffer.BindTexture(m_impl->lutTextureHandle, kToneMappingLutTextureSlot);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, kToneMappingLutTextureSlot);
                }
            }
            if (ssaoActive) {
                commandBuffer.BindTexture(m_impl->ssaoTarget, kToneMappingSsaoTextureSlot);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, kToneMappingSsaoTextureSlot);
                }
            }
            if (contactShadowActive) {
                commandBuffer.BindTexture(m_impl->contactShadowTarget, kToneMappingContactShadowTextureSlot);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, kToneMappingContactShadowTextureSlot);
                }
            }
            if (ssrActive) {
                commandBuffer.BindTexture(m_impl->ssrTarget, kToneMappingSsrTextureSlot);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, kToneMappingSsrTextureSlot);
                }
            }
            if (bloomActive) {
                commandBuffer.BindTexture(m_impl->bloomMipTargets[0U].blurB, kToneMappingBloomTextureSlot);
                if (m_impl->clampLinearSampler != kInvalidSamplerHandle) {
                    commandBuffer.BindSampler(m_impl->clampLinearSampler, kToneMappingBloomTextureSlot);
                }
            }
            commandBuffer.Draw(3);
            commandBuffer.EndRenderPass();
        });

    std::size_t worldCanvasesCount = 0U;
    entityManager.ForEach<Scene::CanvasComponent, Scene::WorldTransformComponent>(
        [&worldCanvasesCount](Scene::Entity, const Scene::CanvasComponent& canvas,
                              const Scene::WorldTransformComponent&) {
            if (canvas.visible && canvas.renderMode == Scene::CanvasRenderMode::WorldSpace) {
                ++worldCanvasesCount;
            }
        });
    m_impl->lastFrameDiagnostics.worldSpaceCanvasesRendered = worldCanvasesCount;

    // UI overlay renders whenever the frame's destination size is known - the plain presentation
    // surface (targetWidth/targetHeight), an explicit sub-region (destinationViewportOverride), or
    // (Phase U3b) a RenderFrameToTarget() caller-supplied texture whose size it passed directly
    // (destinationTextureSizeOverride) - TextureHandle itself has no queryable size on
    // IRenderDevice, which is why that call must supply it explicitly.
    if (m_impl->uiRuntimeForFrame != nullptr && !capturingProbe) {
        const std::uint32_t uiWidth = m_impl->destinationTextureSizeOverride.has_value()
                                           ? m_impl->destinationTextureSizeOverride->first
                                       : m_impl->destinationViewportOverride.has_value()
                                           ? static_cast<std::uint32_t>(
                                                 m_impl->destinationViewportOverride->size.x)
                                           : m_impl->targetWidth;
        const std::uint32_t uiHeight = m_impl->destinationTextureSizeOverride.has_value()
                                            ? m_impl->destinationTextureSizeOverride->second
                                        : m_impl->destinationViewportOverride.has_value()
                                            ? static_cast<std::uint32_t>(
                                                  m_impl->destinationViewportOverride->size.y)
                                            : m_impl->targetHeight;
        if (uiWidth > 0U && uiHeight > 0U) {
            const std::array<RenderGraphResourceUse, 1U> uiOverlayResources{
                RenderGraphResourceUse{colorResource, RenderGraphResourceAccess::Read}};
            renderGraph.AddPass(
                "UIOverlay", uiOverlayResources,
                [this, &entityManager, uiWidth, uiHeight, worldCanvasesCount, &viewProjection, viewPosition](ICommandBuffer& commandBuffer) {
                    const UI::UIFontAtlas& fontAtlas = m_impl->uiRuntimeForFrame->GetFontAtlas();
                    if (m_impl->uiFontAtlasTexture == kInvalidTextureHandle && fontAtlas.IsValid()) {
                        m_impl->uiFontAtlasTexture = m_impl->renderDevice.CreateTexture(
                            TextureDesc{static_cast<std::uint32_t>(UI::UIFontAtlas::kAtlasWidth),
                                           static_cast<std::uint32_t>(UI::UIFontAtlas::kAtlasHeight),
                                           TextureFormat::RGBA8Unorm, 1},
                            std::as_bytes(std::span(fontAtlas.GetBitmap())));
                    }

                    RenderPassDesc passDesc;
                    // Mirrors the ToneMapping pass's own destinationTextureOverride handling just
                    // above: when RenderFrameToTarget() is the caller, ToneMapping already wrote
                    // into that caller-owned texture pair rather than the presentation surface, so
                    // this pass must draw into the same place - kInvalidTextureHandle here would
                    // otherwise bind FBO 0 (the real window) and silently misdirect the UI overlay.
                    passDesc.colorAttachment = m_impl->destinationTextureOverride.has_value()
                                                    ? m_impl->destinationTextureOverride->first
                                                    : kInvalidTextureHandle;
                    passDesc.depthAttachment = m_impl->destinationTextureOverride.has_value()
                                                    ? m_impl->destinationTextureOverride->second
                                                    : kInvalidTextureHandle;
                    // Tier 2.3: same layers as ToneMapping above — this pass overdraws that pass's
                    // target, so a probe face's UI (when enabled) lands on the face's own layer.
                    passDesc.colorLayer = m_impl->destinationTextureOverride.has_value()
                                                  ? m_impl->destinationColorLayerOverride
                                                  : 0U;
                    passDesc.depthLayer = m_impl->destinationTextureOverride.has_value()
                                                  ? m_impl->destinationDepthLayerOverride
                                                  : 0U;
                    passDesc.colorLoadOp = LoadOp::Load;
                    // Must preserve MainColor's real depth values (RenderPassDesc's own default
                    // is Clear->1.0) - this pass now writes its own 0.0 depth for visible UI pixels
                    // (see uiOverlayProgramDesc's own comment), and clearing first would wipe every
                    // 3D mesh's real depth back to "nothing drawn here" for any host compositing
                    // against this depth buffer, exactly as colorLoadOp = Load already preserves the
                    // color buffer's own prior content.
                    passDesc.depthLoadOp = LoadOp::Load;
                    passDesc.viewportOverride = m_impl->destinationViewportOverride;
                    commandBuffer.BeginRenderPass(passDesc);

                    if (worldCanvasesCount > 0U) {
                        entityManager.ForEach<Scene::CanvasComponent, Scene::WorldTransformComponent>(
                            [this, &commandBuffer, &viewProjection, viewPosition](
                                Scene::Entity, const Scene::CanvasComponent& canvas,
                                const Scene::WorldTransformComponent& world) {
                                if (!canvas.visible || canvas.renderMode != Scene::CanvasRenderMode::WorldSpace) {
                                    return;
                                }
                                const Math::Matrix4x4 worldMatrix = canvas.billboard
                                    ? Scene::ComputeWorldSpaceCanvasBillboardTransform(
                                          canvas, world.worldPosition, viewPosition, world.worldScale)
                                    : Scene::ComputeWorldSpaceCanvasTransform(
                                          canvas, world.worldPosition, world.worldRotation, world.worldScale);
                                const Math::Matrix4x4 canvasViewProj = viewProjection * worldMatrix;
                                static_cast<void>(m_impl->RecordUIOverlayItems(
                                    m_impl->uiRuntimeForFrame->GetDrawBatch(), canvasViewProj, commandBuffer));
                            });
                    }

                    const Math::Matrix4x4 uiProjection = Math::Matrix4x4::Orthographic(
                        0.0F, static_cast<float>(uiWidth), static_cast<float>(uiHeight), 0.0F, -1.0F, 1.0F);
                    static_cast<void>(m_impl->RecordUIOverlayItems(m_impl->uiRuntimeForFrame->GetDrawBatch(),
                                                                       uiProjection, commandBuffer));
                    commandBuffer.EndRenderPass();
                });
        }
    }

    m_impl->renderSystem.BeginFrame();
    ICommandBuffer& commandBuffer = m_impl->renderSystem.GetFrameCommandBuffer();
    RenderGraphOptions graphOptions;
    graphOptions.emitBarriers = commandBuffer.GetMemoryBarrierSupport() != RenderFeatureSupport::Unsupported;
    const bool graphExecuted = renderGraph.Execute(commandBuffer, graphOptions).IsCompleted();
    UVE_ASSERT(graphExecuted && "Renderer3D must build a valid render graph");
    m_impl->renderSystem.EndFrame();
    if (!capturingProbe && graphExecuted) {
        m_impl->previousMotionBlurViewProjection = viewProjection;
        m_impl->previousMotionBlurCameraEntity = cameraEntity;
        m_impl->previousMotionBlurAspect = aspectRatio;
        m_impl->motionBlurHistoryValid = true;
    }
}

void Renderer3D::RenderFrameWithParticleRuntime(Scene::IEntityManager& entityManager,
                                                         Scene::Entity cameraEntity,
                                                         const Scene::ParticleRuntime& particleRuntime) {
    const Scene::ParticleRuntime* const previousRuntime = m_impl->particleRuntimeForFrame;
    m_impl->particleRuntimeForFrame = &particleRuntime;
    struct RuntimeFrameScope final {
        const Scene::ParticleRuntime*& slot;
        const Scene::ParticleRuntime* previous;
        ~RuntimeFrameScope() { slot = previous; }
    } scope{m_impl->particleRuntimeForFrame, previousRuntime};
    RenderFrame(entityManager, cameraEntity);
}

void Renderer3D::RenderFrameToRegion(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity,
                                            const ViewportRect& region,
                                            const Scene::ParticleRuntime* const particleRuntime) {
    const std::optional<ViewportRect> previousOverride = m_impl->destinationViewportOverride;
    m_impl->destinationViewportOverride = region;
    struct RegionScope final {
        std::optional<ViewportRect>& slot;
        std::optional<ViewportRect> previous;
        ~RegionScope() { slot = previous; }
    } regionScope{m_impl->destinationViewportOverride, previousOverride};

    const Scene::ParticleRuntime* const previousRuntime = m_impl->particleRuntimeForFrame;
    m_impl->particleRuntimeForFrame = particleRuntime;
    struct RuntimeFrameScope final {
        const Scene::ParticleRuntime*& slot;
        const Scene::ParticleRuntime* previous;
        ~RuntimeFrameScope() { slot = previous; }
    } runtimeScope{m_impl->particleRuntimeForFrame, previousRuntime};

    RenderFrame(entityManager, cameraEntity);
}

void Renderer3D::RenderFrameToTarget(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity,
                                            const TextureHandle colorTarget, const TextureHandle depthTarget,
                                            const std::uint32_t width, const std::uint32_t height,
                                            const std::uint32_t colorLayer,
                                            const std::uint32_t depthLayer) {
    const std::optional<std::pair<TextureHandle, TextureHandle>> previousOverride =
        m_impl->destinationTextureOverride;
    m_impl->destinationTextureOverride = std::make_pair(colorTarget, depthTarget);
    struct TargetScope final {
        std::optional<std::pair<TextureHandle, TextureHandle>>& slot;
        std::optional<std::pair<TextureHandle, TextureHandle>> previous;
        ~TargetScope() { slot = previous; }
    } targetScope{m_impl->destinationTextureOverride, previousOverride};

    const std::optional<std::pair<std::uint32_t, std::uint32_t>> previousSizeOverride =
        m_impl->destinationTextureSizeOverride;
    m_impl->destinationTextureSizeOverride =
        (width > 0U && height > 0U) ? std::make_optional(std::make_pair(width, height)) : std::nullopt;
    struct TargetSizeScope final {
        std::optional<std::pair<std::uint32_t, std::uint32_t>>& slot;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> previous;
        ~TargetSizeScope() { slot = previous; }
    } targetSizeScope{m_impl->destinationTextureSizeOverride, previousSizeOverride};

    const std::uint32_t previousColorLayer = m_impl->destinationColorLayerOverride;
    const std::uint32_t previousDepthLayer = m_impl->destinationDepthLayerOverride;
    m_impl->destinationColorLayerOverride = colorLayer;
    m_impl->destinationDepthLayerOverride = depthLayer;
    struct TargetLayerScope final {
        std::uint32_t& colorSlot;
        std::uint32_t& depthSlot;
        std::uint32_t previousColor;
        std::uint32_t previousDepth;
        ~TargetLayerScope() {
            colorSlot = previousColor;
            depthSlot = previousDepth;
        }
    } targetLayerScope{m_impl->destinationColorLayerOverride, m_impl->destinationDepthLayerOverride,
                       previousColorLayer, previousDepthLayer};

    RenderFrame(entityManager, cameraEntity);
}

void Renderer3D::SetPostProcessSettings(const PostProcessSettings& settings) {
    m_impl->postProcessSettings = settings;
    m_impl->postProcessSettings.ssaoRadius =
        std::isfinite(settings.ssaoRadius) ? std::clamp(settings.ssaoRadius, 0.01F, 10.0F) : 0.5F;
    m_impl->postProcessSettings.ssaoIntensity =
        std::isfinite(settings.ssaoIntensity) ? std::clamp(settings.ssaoIntensity, 0.0F, 4.0F) : 1.0F;
    m_impl->postProcessSettings.ssaoPower =
        std::isfinite(settings.ssaoPower) ? std::clamp(settings.ssaoPower, 0.1F, 4.0F) : 1.0F;
    m_impl->postProcessSettings.ssaoQuality = std::min(settings.ssaoQuality, 2U);
    m_impl->postProcessSettings.fastApproximateAAQuality =
        std::min(settings.fastApproximateAAQuality, 2U);
    m_impl->postProcessSettings.toneMappingMethod =
        std::min(settings.toneMappingMethod, 2U);
    m_impl->postProcessSettings.sharpeningAmount =
        std::isfinite(settings.sharpeningAmount) ? std::clamp(settings.sharpeningAmount, 0.0F, 1.0F) : 0.0F;
}

void Renderer3D::SetPreviewEnvironment(const std::optional<Scene::WorldEnvironmentFrame>& frame) {
    // `hasEnvironment` is deliberately forced here rather than trusted from the caller: the whole
    // point of the preview is that it stands in for the environment the scene does not have, and a
    // frame that kept the flag false would silently do nothing (the procedural sky pass and the
    // sky ambient both gate on it). The copy is per-frame data only - nothing is written back into
    // the entity manager.
    if (!frame.has_value()) {
        m_impl->previewEnvironment.reset();
        return;
    }
    Scene::WorldEnvironmentFrame preview = *frame;
    preview.hasEnvironment = true;
    m_impl->previewEnvironment = preview;
}

void Renderer3D::SetShadowBiasDefaults(const float depthBias, const float normalBias) noexcept {
    m_impl->shadowBiasDefault = std::isfinite(depthBias) ? std::clamp(depthBias, 0.0F, 10.0F) : 0.1F;
    m_impl->shadowNormalBiasDefault =
        std::isfinite(normalBias) ? std::clamp(normalBias, 0.0F, 10.0F) : 1.0F;
}

void Renderer3D::SetShadowAtlasTileResolution(const std::uint32_t resolution) noexcept {
    // Floored at the atlas manager's minimum tile and capped at the atlas itself: a request outside
    // that range is not a quality choice, it is an unrepresentable layout, and silently rounding it
    // is better than failing every atlas shadow at frame time.
    m_impl->shadowAtlasTileResolution =
        resolution == 0U ? kShadowAtlasMinTileResolution
                         : std::clamp(resolution, kShadowAtlasMinTileResolution, kShadowAtlasResolution);
}

void Renderer3D::SetMaterialSamplerSettings(const bool repeatEnabled, const float maxAnisotropy) noexcept {
    const float sanitizedAnisotropy =
        std::isfinite(maxAnisotropy) ? std::clamp(maxAnisotropy, 1.0F, 16.0F) : 1.0F;
    if (m_impl->materialTextureRepeatEnabled == repeatEnabled &&
        std::abs(m_impl->materialMaxAnisotropy - sanitizedAnisotropy) <= 1.0e-3F &&
        m_impl->materialRepeatSampler != kInvalidSamplerHandle) {
        return;
    }
    m_impl->materialTextureRepeatEnabled = repeatEnabled;
    m_impl->materialMaxAnisotropy = sanitizedAnisotropy;
    m_impl->RebuildMaterialRepeatSampler();
}

void Renderer3D::SetCullingSettings(const CullingSettings& settings) noexcept {
    m_impl->cullingSettings = settings;
}

void Renderer3D::SetShadingMode(const std::int32_t shadingMode) noexcept {
    m_impl->shadingMode = std::clamp(shadingMode, 0, 4);
}

void Renderer3D::SetMeshUploadBudget(const std::size_t meshesPerFrame) noexcept {
    m_impl->meshUploadBudget = meshesPerFrame;
}

void Renderer3D::SetScalabilityMode(const std::int32_t scalabilityMode) noexcept {
    const std::int32_t clampedLevel = std::clamp(scalabilityMode, 0, 3);
    m_impl->scalabilityMode = clampedLevel;
    if (clampedLevel == 0) {
        m_impl->postProcessSettings.ssaoQuality = 0U;
        m_impl->postProcessSettings.ssaoBlurEnabled = false;
        m_impl->postProcessSettings.bloomEnabled = false;
        m_impl->postProcessSettings.contactShadowsEnabled = false;
        m_impl->postProcessSettings.ssrEnabled = false;
        m_impl->shadowPcfKernelRadius = 0;
        m_impl->activeMaxInstancesPerFrame = 16'384U;
        m_impl->activeMaxParticleGpuDrawCommands = 4'096U;
        m_impl->activeMaxLightsPerFrame = 4U;
        m_impl->activeMaxClusteredLights = 16U;
        m_impl->postProcessSettings.temporalAAEnabled = false;
        m_impl->postProcessSettings.orderIndependentTransparencyEnabled = false;
    } else if (clampedLevel == 1) {
        m_impl->postProcessSettings.ssaoQuality = 1U;
        m_impl->postProcessSettings.ssaoBlurEnabled = true;
        m_impl->postProcessSettings.bloomEnabled = true;
        m_impl->postProcessSettings.contactShadowsEnabled = false;
        m_impl->postProcessSettings.ssrEnabled = false;
        m_impl->shadowPcfKernelRadius = 1;
        m_impl->activeMaxInstancesPerFrame = 65'536U;
        m_impl->activeMaxParticleGpuDrawCommands = 16'384U;
        m_impl->activeMaxLightsPerFrame = 8U;
        m_impl->activeMaxClusteredLights = 64U;
    } else if (clampedLevel == 2) {
        m_impl->postProcessSettings.ssaoQuality = 2U;
        m_impl->postProcessSettings.ssaoBlurEnabled = true;
        m_impl->postProcessSettings.bloomEnabled = true;
        m_impl->shadowPcfKernelRadius = 2;
        m_impl->activeMaxInstancesPerFrame = 131'072U;
        m_impl->activeMaxParticleGpuDrawCommands = kMaximumParticleGpuDrawCommands;
        m_impl->activeMaxLightsPerFrame = 12U;
        m_impl->activeMaxClusteredLights = 128U;
    } else {
        m_impl->postProcessSettings.ssaoQuality = 2U;
        m_impl->postProcessSettings.ssaoBlurEnabled = true;
        m_impl->postProcessSettings.bloomEnabled = true;
        m_impl->postProcessSettings.ssrMaxSteps =
            std::max(m_impl->postProcessSettings.ssrMaxSteps, 64U);
        m_impl->postProcessSettings.contactShadowStepCount =
            std::max(m_impl->postProcessSettings.contactShadowStepCount, 12U);
        m_impl->shadowPcfKernelRadius = 2;
        m_impl->activeMaxInstancesPerFrame = kMaximumInstancesPerFrame;
        m_impl->activeMaxLightsPerFrame = kMaxLights;
        m_impl->activeMaxClusteredLights = kMaxClusteredLights;
        m_impl->activeMaxParticleGpuDrawCommands = kMaximumParticleGpuDrawCommands;
    }
}

void Renderer3D::SetRendererProfile(const Platform::RendererProfile profile) noexcept {
    const RenderDeviceCapabilities caps = m_impl->renderDevice.GetCapabilities();
    const RendererProfileResolution resolved = ResolveEffectiveRendererProfile(profile, caps);
    const Platform::RendererProfile previousEffective = m_impl->rendererProfileResolution.effective;
    const TextureFormat previousColorFormat =
        SceneColorTargetFormat(m_impl->renderDevice, previousEffective);
    m_impl->rendererProfileResolution = resolved;
    if (resolved.fallbackApplied) {
        UVE_WARNING("Renderer3D: requested {} renderer fell back to {} ({})",
                    Platform::GetRendererProfileDisplayName(resolved.requested),
                    Platform::GetRendererProfileDisplayName(resolved.effective),
                    resolved.fallbackReason);
    }

    if (resolved.effective == Platform::RendererProfile::Compatibility) {
        m_impl->scalabilityMode = 0;
        m_impl->shadowPcfKernelRadius = 0;
        m_impl->EnsureShadowMapResolution(512U);
        m_impl->activeMaxLightsPerFrame = 4U;
        m_impl->activeMaxClusteredLights = 16U;
        m_impl->activeMaxInstancesPerFrame = 16'384U;
        m_impl->activeMaxParticleGpuDrawCommands = 4'096U;
        m_impl->postProcessSettings.ssaoEnabled = false;
        m_impl->postProcessSettings.ssaoQuality = 0U;
        m_impl->postProcessSettings.ssaoBlurEnabled = false;
        m_impl->postProcessSettings.bloomEnabled = false;
        m_impl->postProcessSettings.contactShadowsEnabled = false;
        m_impl->postProcessSettings.ssrEnabled = false;
        m_impl->postProcessSettings.temporalAAEnabled = false;
        m_impl->postProcessSettings.orderIndependentTransparencyEnabled = false;
        m_impl->postProcessSettings.clusteredLightingEnabled = false;
    } else if (resolved.effective == Platform::RendererProfile::Mobile) {
        m_impl->scalabilityMode = 1;
        m_impl->shadowPcfKernelRadius = 1;
        m_impl->EnsureShadowMapResolution(1024U);
        m_impl->activeMaxLightsPerFrame = 8U;
        m_impl->activeMaxClusteredLights = 64U;
        m_impl->activeMaxInstancesPerFrame = 65'536U;
        m_impl->activeMaxParticleGpuDrawCommands = 16'384U;
        m_impl->postProcessSettings.ssaoEnabled = false;
        m_impl->postProcessSettings.ssaoQuality = 0U;
        m_impl->postProcessSettings.ssaoBlurEnabled = false;
        m_impl->postProcessSettings.bloomEnabled = false;
        m_impl->postProcessSettings.contactShadowsEnabled = false;
        m_impl->postProcessSettings.ssrEnabled = false;
        m_impl->postProcessSettings.temporalAAEnabled = false;
        m_impl->postProcessSettings.orderIndependentTransparencyEnabled = false;
        m_impl->postProcessSettings.clusteredLightingEnabled = false;
    } else {
        m_impl->scalabilityMode = 2;
        m_impl->shadowPcfKernelRadius = 2;
        m_impl->EnsureShadowMapResolution(2048U);
        m_impl->activeMaxLightsPerFrame = kMaxLights;
        m_impl->activeMaxClusteredLights = kMaxClusteredLights;
        m_impl->activeMaxInstancesPerFrame = kMaximumInstancesPerFrame;
        m_impl->activeMaxParticleGpuDrawCommands = kMaximumParticleGpuDrawCommands;
        m_impl->postProcessSettings.ssaoEnabled = true;
        m_impl->postProcessSettings.ssaoQuality = 1U;
        m_impl->postProcessSettings.ssaoBlurEnabled = true;
        m_impl->postProcessSettings.bloomEnabled = true;
        m_impl->postProcessSettings.clusteredLightingEnabled =
            caps.storageBuffers != RenderFeatureSupport::Unsupported;
        if (m_impl->wboitLitProgram == nullptr &&
            caps.halfFloatColorTargets != RenderFeatureSupport::Unsupported) {
            Shader::ShaderProgramDesc wboitLitDesc;
            wboitLitDesc.virtualFilePath = std::string(Shader::BuiltIn::kLitShadowed3DVirtualPath);
            wboitLitDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kLitShadowed3DSource);
            wboitLitDesc.vertexLayout = MeshVertexLayout();
            wboitLitDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
            wboitLitDesc.depthTestEnabled = true;
            wboitLitDesc.depthWriteEnabled = false;
            wboitLitDesc.blendMode = PipelineBlendMode::Additive;
            wboitLitDesc.cullMode = CullMode::None;
            wboitLitDesc.extraDefines.emplace_back("UVE_WBOIT", "1");
            wboitLitDesc.debugName = "LitWBOIT";
            m_impl->wboitLitProgram = m_impl->shaderManager.CreateProgram(wboitLitDesc);
        }
    }

    if (previousEffective != resolved.effective) {
        m_impl->EnsureDefaultLitProgramsForProfile(resolved.effective);
        m_impl->materialCache.clear();
    }
    if (SceneColorTargetFormat(m_impl->renderDevice, resolved.effective) != previousColorFormat) {
        m_impl->RecreateTargetSetsForProfileFormatChange();
    }
}

RendererProfileResolution Renderer3D::GetRendererProfileResolution() const noexcept {
    return m_impl->rendererProfileResolution;
}

void Renderer3D::SetSceneClearColor(const std::array<float, 4U>& color) noexcept {
    for (const float channel : color) {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
            return;
        }
    }
    m_impl->sceneClearColor = color;
}

void Renderer3D::SetUIRuntime(const UI::UIRuntime* const uiRuntime) noexcept {
    m_impl->uiRuntimeForFrame = uiRuntime;
}

void Renderer3D::SetVfxDrawList(const VfxDrawList* const vfxDrawList) noexcept {
    m_impl->vfxDrawListForFrame = vfxDrawList;
}

void Renderer3D::SetStudioFloor(const std::optional<StudioFloorSettings>& settings) noexcept {
    m_impl->studioFloorSettings = settings;
}

void Renderer3D::SetPhysicsInterpolationAlpha(const float alpha) noexcept {
    // Stored on the visibility set because that is where it is consumed - the build applies the
    // blend once, rather than each of the frame's four culls redoing it and risking the shadow
    // cascades disagreeing with the main view about where an object is.
    //
    // Clamped rather than rejected: a timer that overshoots after a long frame should keep
    // drawing, and a non-finite value would otherwise reach a matrix compose.
    const float sanitized = std::isfinite(alpha) ? (alpha < 0.0F ? 0.0F : (alpha > 1.0F ? 1.0F : alpha)) : 0.0F;
    m_impl->visibilitySet.physicsInterpolationAlpha = sanitized;
}

SceneDepthExport Renderer3D::GetLastSceneDepth() const noexcept {
    return m_impl->lastSceneDepth;
}

Renderer3DFrameDiagnostics Renderer3D::GetLastFrameDiagnostics() const noexcept {
    return m_impl->lastFrameDiagnostics;
}

} // namespace UVE::Render
