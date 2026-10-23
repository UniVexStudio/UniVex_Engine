// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/color_uve.h"

namespace UVE::Asset {

/// How a material turns its mesh to face the camera. Disabled keeps the authored pose.
enum class MaterialBillboardModeUVE : std::uint8_t {
    Disabled = 0,
    /// Local +Z faces the camera.
    Enabled,
    /// Local +Z faces the camera around world Y only.
    Y,
};

enum class MaterialTextureChannelUVE : std::uint8_t {
    Red = 0,
    Green = 1,
    Blue = 2,
    Alpha = 3,
    Rgb = 4,
};

enum class MaterialAlphaSourceUVE : std::uint8_t {
    AlbedoAlpha = 0,
    SeparateTextureChannel = 1,
    None = 2,
};

enum class MaterialCullModeUVE : std::uint8_t {
    Back = 0,
    Front = 1,
    Disabled = 2,
};

enum class MaterialBlendModeUVE : std::uint8_t {
    Opaque = 0,
    AlphaBlend = 1,
    PremultipliedAlpha = 2,
    Additive = 3,
    Subtractive = 4,
    Multiply = 5,
};

enum class MaterialDepthDrawModeUVE : std::uint8_t {
    OpaqueOnly = 0,
    Always = 1,
    Never = 2,
    DepthPrePass = 3,
};

/// The authoring preset a material was created as. Every kind still shades through the same
/// physically based path, so switching never changes how a material renders by itself:
/// Standard uses the metallic/roughness scalars with an optional packed map multiplying them; ORM
/// is the same path authored around the packed occlusion/roughness/metallic map (metallic 1,
/// occlusion 1) with the scalars acting as multipliers; Shader means the material supplies its own
/// shader code (`useCustomShaderCode`). Persisted so the Inspector can show which preset a file was
/// authored with; files written before this field existed load as Standard.
enum class MaterialKindUVE : std::uint8_t {
    Standard = 0,
    Orm = 1,
    Shader = 2,
};

/// Per-slot texture binding with UV channel, source channel, tint, and UV scale/offset (Part 4.1).
struct MaterialTextureSlotUVE final {
    AssetGuidUVE texture;
    std::uint8_t uvChannel = 0U;
    MaterialTextureChannelUVE sourceChannel = MaterialTextureChannelUVE::Rgb;
    Math::ColorUVE tint{1.0F, 1.0F, 1.0F};
    Math::Vector2UVE scale{1.0F, 1.0F};
    Math::Vector2UVE offset{0.0F, 0.0F};
};

/// The CPU-side, engine-native representation of a `.uvmat` asset (Part 2's file-format table):
/// a PBR material — Albedo, Normal, Metallic, Roughness, AO, Emissive (Part 7.2's
/// `MaterialSystemUVE` workflow). Textures and shaders are referenced by `AssetGuidUVE`, never a
/// file path — matching the master spec's "No file path dependencies in scenes" rule
/// (`IAssetDatabaseUVE`'s own contract). `kInvalidAssetGuidUVE` on any texture reference means
/// "unset, use the corresponding flat value/no effect instead" (there's no dedicated
/// "has texture" flag — an invalid GUID already means exactly that). Referential integrity of
/// these GUIDs (whether the referenced asset is actually loaded) is deliberately not this type's
/// concern — a referenced texture/shader may simply not be loaded yet, which isn't an error here;
/// a future `MeshRendererUVE` (Increment 13) is what decides how to handle that at render time.
struct MaterialAssetUVE {
    Math::ColorUVE albedoColor{1.0F, 1.0F, 1.0F};
    AssetGuidUVE albedoTexture;
    AssetGuidUVE normalTexture;
    float metallic = 0.0F;
    float roughness = 0.5F;
    AssetGuidUVE aoTexture;
    Math::ColorUVE emissiveColor{0.0F, 0.0F, 0.0F};
    AssetGuidUVE vertexShader;
    AssetGuidUVE fragmentShader;
    /// Drives `RenderQueueUVE`'s opaque/transparent bucketing (Increment 13).
    bool isTransparent = false;
    /// Appended: Disabled keeps the authored pose. Enabled / Y rewrite it at draw so a textured
    /// card faces the camera.
    MaterialBillboardModeUVE billboardMode = MaterialBillboardModeUVE::Disabled;
    /// Packed map: G roughness, B metallic. Multiplies the scalars. Unset is a white fallback.
    AssetGuidUVE metallicRoughnessTexture;
    AssetGuidUVE emissiveTexture;
    /// Multiplies `emissiveColor` (and the emissive texture). 1 leaves the colour as authored.
    float emissiveEnergy = 1.0F;
    /// Scales the tangent-space normal's XY. 1 is the map as authored; 0 flattens it.
    float normalScale = 1.0F;
    /// How much the AO map darkens cavities. 1 full, 0 ignores the map.
    float occlusionStrength = 1.0F;
    Math::Vector2UVE uvScale{1.0F, 1.0F};
    Math::Vector2UVE uvOffset{0.0F, 0.0F};
    /// Albedo only, no lighting. For cards that should not pick up the scene lights.
    bool unshaded = false;
    /// Discard fragments whose albedo alpha is below this. 0 never discards.
    float alphaCutoff = 0.0F;

    // Part 4 extended material properties (defaulted so existing aggregate initialisers stay valid).
    float albedoAlpha = 1.0F;
    AssetGuidUVE metallicTexture;
    AssetGuidUVE roughnessTexture;
    MaterialAlphaSourceUVE alphaSource = MaterialAlphaSourceUVE::AlbedoAlpha;
    bool alphaHashingEnabled = false;
    bool useVertexColorAsAlbedo = false;
    bool vertexColorIsSrgb = true;
    MaterialCullModeUVE cullMode = MaterialCullModeUVE::Back;
    MaterialBlendModeUVE blendMode = MaterialBlendModeUVE::Opaque;
    MaterialDepthDrawModeUVE depthDrawMode = MaterialDepthDrawModeUVE::OpaqueOnly;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    std::int32_t renderPriority = 0;
    bool disableAmbientLight = false;
    bool disableFog = false;
    float dielectricSpecular = 0.5F;
    bool normalFlipGreen = false;
    bool aoAffectsDirectLight = false;
    AssetGuidUVE heightTexture;
    float heightScale = 0.05F;
    std::uint8_t parallaxMinLayers = 8U;
    std::uint8_t parallaxMaxLayers = 32U;
    float rimAmount = 0.0F;
    Math::ColorUVE rimTint{1.0F, 1.0F, 1.0F};
    float clearcoatAmount = 0.0F;
    float clearcoatRoughness = 0.1F;
    float anisotropyAmount = 0.0F;
    float subsurfaceStrength = 0.0F;
    float refractionAmount = 0.0F;
    Math::Vector2UVE uv2Scale{1.0F, 1.0F};
    Math::Vector2UVE uv2Offset{0.0F, 0.0F};
    bool triplanarEnabled = false;
    bool worldSpaceTriplanar = false;
    float triplanarBlendSharpness = 1.0F;
    float proximityFadeDistance = 0.0F;
    float distanceFadeNear = 0.0F;
    float distanceFadeFar = 0.0F;
    bool receiveShadows = true;
    std::uint32_t renderLayerMask = 0xFFFFFFFFU;
    AssetGuidUVE parentMaterial;
    AssetGuidUVE nextPassMaterial;

    /// Advanced custom shader code injection and texture overrides
    bool useCustomShaderCode = false;
    AssetGuidUVE customShader;
    Math::ColorUVE textureTint{1.0F, 1.0F, 1.0F};
    float textureTileX = 1.0F;
    float textureTileY = 1.0F;

    /// The preset this material was authored as (see MaterialKindUVE). Appended so existing
    /// aggregate initialisers stay valid.
    MaterialKindUVE kind = MaterialKindUVE::Standard;
};

/// Returns true when the material should be bucketed into the transparent render pass (Part 3.7 / 4.1).
[[nodiscard]] constexpr bool ShouldBucketMaterialTransparentUVE(const MaterialAssetUVE& material) noexcept {
    return material.isTransparent || material.albedoAlpha < 1.0F ||
           material.blendMode != MaterialBlendModeUVE::Opaque;
}

/// Validates the CPU material descriptor before persistence or renderer handoff. Albedo channels,
/// metallic/roughness, occlusion strength and alpha cutoff are finite in [0,1]; emissive channels
/// and energy, and normal scale, are finite and nonnegative; UV scale/offset are finite; GUID
/// references may be invalid to represent an intentionally unset texture or shader. Billboard is a
/// known enumerator.
[[nodiscard]] bool IsMaterialAssetValidUVE(const MaterialAssetUVE& material) noexcept;

/// Writes a rotation that points local +Z at the camera. Disabled, a degenerate view, or
/// non-finite inputs return false and leave `outRotation` untouched.
[[nodiscard]] bool TryMakeMaterialBillboardRotationUVE(MaterialBillboardModeUVE mode,
                                                       const Math::Vector3UVE& objectPosition,
                                                       const Math::Vector3UVE& cameraPosition,
                                                       Math::QuaternionUVE& outRotation) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::Material`, filling `outMaterial`.
/// Returns false (logging the reason) if the file is missing/malformed, isn't actually a
/// Material asset, or is missing an expected field.
[[nodiscard]] bool LoadMaterialAssetUVE(const std::filesystem::path& path, MaterialAssetUVE& outMaterial);

/// Writes `material` to `path` as a `.uve*` envelope with `AssetKindUVE::Material`. Returns false
/// (logging the reason) for invalid finite/range fields or a file publication failure; invalid
/// descriptors are rejected before opening the destination.
[[nodiscard]] bool SaveMaterialAssetUVE(const MaterialAssetUVE& material, const std::filesystem::path& path);

} // namespace UVE::Asset
