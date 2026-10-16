// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "uve/asset/i_asset_importer_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// Sidecar import settings document persisted next to a source asset (`<source>.uvimport`, Part 5.1).
struct AssetImportSidecarUVE final {
    std::string importerVersion = "v1";
    std::string presetName;
    bool reimportNeeded = false;
    std::vector<std::string> warnings;
    std::map<std::string, std::string> options;
};

/// Returns `<sourcePath>.uvimport` next to the source file (Part 5.1).
[[nodiscard]] std::filesystem::path GetImportSidecarPathUVE(const std::filesystem::path& sourcePath);

/// Saves `sidecar` as a JSON `.uvimport` sidecar file (Part 5.1).
[[nodiscard]] bool SaveImportSidecarUVE(const AssetImportSidecarUVE& sidecar,
                                        const std::filesystem::path& sidecarPath);

/// Loads `outSidecar` from a JSON `.uvimport` sidecar file (Part 5.1).
[[nodiscard]] bool LoadImportSidecarUVE(const std::filesystem::path& sidecarPath,
                                        AssetImportSidecarUVE& outSidecar);

enum class MeshImportNormalModeUVE : std::uint8_t {
    Import = 0,
    Recalculate = 1,
    RecalculateSmoothAngle = 2,
};

enum class MeshImportTangentModeUVE : std::uint8_t {
    Import = 0,
    Recalculate = 1,
    None = 2,
};

enum class MeshImportSplitModeUVE : std::uint8_t {
    KeepSingleMesh = 0,
    ByMaterial = 1,
    ByObject = 2,
};

enum class MeshImportCollisionModeUVE : std::uint8_t {
    None = 0,
    ConvexHull = 1,
    MultipleConvexHulls = 2,
    TriangleMesh = 3,
    SimplifiedBox = 4,
    SimplifiedCapsule = 5,
};

/// Per-source-node import override entry (Part 5.6).
struct PerNodeImportOverrideUVE final {
    std::string nodeName;
    bool skipNode = false;
    std::string overrideNodeType;
    bool markAsCollision = false;
    bool markAsLodLevel = false;
    bool markAsNavGeometry = false;
    bool extractMaterialAsset = false;
    bool extractMeshAsset = false;
};

/// Mesh, LOD, collision, skeleton, animation, and per-node import settings (Part 5.2 - 5.6).
struct MeshImportSettingsUVE final : AssetImportSettingsUVE {
    // 5.2 Mesh import
    float scaleFactor = 1.0F;
    bool detectUnitsFromSource = true;
    std::string upAxis = "+Y";
    std::string forwardAxis = "-Z";
    bool applyRootTransform = false;
    MeshImportNormalModeUVE normalMode = MeshImportNormalModeUVE::Import;
    float smoothingAngleDegrees = 60.0F;
    MeshImportTangentModeUVE tangentMode = MeshImportTangentModeUVE::Import;
    float weldPositionTolerance = 1.0e-4F;
    float weldUvTolerance = 1.0e-4F;
    bool optimizeVertexCache = true;
    MeshImportSplitModeUVE splitMode = MeshImportSplitModeUVE::KeepSingleMesh;
    bool preserveHierarchy = true;
    bool importVertexColors = true;
    bool importUv2 = true;
    bool generateLightmapUvs = false;
    float lightmapTexelDensity = 16.0F;
    bool importMorphTargets = true;

    // 5.3 Level of detail
    bool generateLods = false;
    std::uint8_t lodCount = 3U;
    float lodReductionRatio = 0.5F;
    float lodScreenCoverageThreshold = 0.05F;
    bool preserveSeamsAndBoundaries = true;
    bool crossfadeLods = false;
    bool generateShadowOnlyLod = false;
    bool generateFurthestImpostor = false;

    // 5.4 Collision generation
    MeshImportCollisionModeUVE collisionMode = MeshImportCollisionModeUVE::None;
    std::uint8_t maxConvexHulls = 8U;
    std::uint8_t maxVerticesPerHull = 32U;
    float concavityTolerance = 0.01F;
    std::string collisionNodeSuffix = "-col";
    bool collisionAsChildNode = false;
    std::uint32_t defaultCollisionLayer = 1U;
    std::uint32_t defaultCollisionMask = 0xFFFFFFFFU;
    bool generateNavMeshSource = false;
    bool generateOccluderSource = false;

    // 5.5 Skeleton and animation import
    bool importSkeleton = true;
    std::string rootBoneName;
    bool extractRootMotion = false;
    std::uint8_t maxBoneInfluences = 4U;
    bool importAnimations = true;
    float animationSampleRateHz = 30.0F;
    float positionCompressionTolerance = 1.0e-3F;
    float rotationCompressionTolerance = 1.0e-3F;
    float scaleCompressionTolerance = 1.0e-3F;
    bool stripScaleTracks = false;
    std::string retargetSourceProfile;
    std::string retargetTargetProfile;
    bool importCamerasAndLights = false;

    // 5.6 Per-node overrides
    bool keepSceneAsEditableInstance = false;
    std::vector<PerNodeImportOverrideUVE> nodeOverrides;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

/// Resolves mesh import settings from a generic `AssetImportSettingsUVE` reference (Part 5.1 / 5.2).
[[nodiscard]] MeshImportSettingsUVE ResolveMeshImportSettingsUVE(
    const AssetImportSettingsUVE& settings) noexcept;

/// Procedural primitive geometry parameters (Part 5.7).
struct PrimitiveGeometryParametersUVE final {
    Math::Vector3UVE boxSize{1.0F, 1.0F, 1.0F};
    std::uint16_t boxSubdivisionsX = 1U;
    std::uint16_t boxSubdivisionsY = 1U;
    std::uint16_t boxSubdivisionsZ = 1U;

    float sphereRadius = 0.5F;
    float sphereHeight = 1.0F;
    std::uint16_t sphereRadialSegments = 32U;
    std::uint16_t sphereRings = 16U;
    bool sphereHemisphere = false;

    float capsuleRadius = 0.5F;
    float capsuleHeight = 2.0F;
    std::uint16_t capsuleRadialSegments = 32U;
    std::uint16_t capsuleRings = 8U;

    float cylinderTopRadius = 0.5F;
    float cylinderBottomRadius = 0.5F;
    float cylinderHeight = 2.0F;
    std::uint16_t cylinderRadialSegments = 32U;
    bool cylinderCapTop = true;
    bool cylinderCapBottom = true;

    float planeWidth = 2.0F;
    float planeDepth = 2.0F;
    std::uint16_t planeSubdivisionsX = 1U;
    std::uint16_t planeSubdivisionsZ = 1U;

    float torusInnerRadius = 0.25F;
    float torusOuterRadius = 0.75F;
    std::uint16_t torusRings = 32U;
    std::uint16_t torusRingSegments = 16U;

    float prismLeftToRightSkew = 0.5F;
    Math::Vector3UVE prismSize{1.0F, 1.0F, 1.0F};

    std::string textMeshString = "Text";
    float textMeshFontSize = 16.0F;
    float textMeshExtrudeDepth = 0.1F;

    float ribbonLength = 1.0F;
    std::uint16_t ribbonResolution = 8U;

    // Shared primitive flags
    bool generateUvs = true;
    bool generateTangents = true;
    bool flipFaces = false;
    bool addCollision = false;
};

[[nodiscard]] bool IsPrimitiveGeometryParametersValidUVE(
    const PrimitiveGeometryParametersUVE& params) noexcept;

enum class AudioImportLoadModeUVE : std::uint8_t {
    InMemory = 0,
    Streamed = 1,
    DecompressOnLoad = 2,
};

/// Audio import settings (Part 6.1).
struct AudioImportSettingsUVE final : AssetImportSettingsUVE {
    AudioImportLoadModeUVE loadMode = AudioImportLoadModeUVE::InMemory;
    std::string compressionFormat = "PCM";
    float compressionQuality = 0.8F;
    bool forceMono = false;
    std::uint32_t targetSampleRateHz = 48000U;
    bool loopEnabled = false;
    std::uint64_t loopStartSample = 0U;
    std::uint64_t loopEndSample = 0U;
    float trimSilenceThreshold = 0.0F;
    bool normalizeEnabled = false;
    float targetPeakDb = -1.0F;
    std::string defaultBusName = "Master";
    bool preloadOnSceneLoad = true;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

enum class FontImportRenderingModeUVE : std::uint8_t {
    Bitmap = 0,
    SignedDistanceField = 1,
    MultiChannelSdf = 2,
};

/// Font import settings (Part 6.2).
struct FontImportSettingsUVE final : AssetImportSettingsUVE {
    FontImportRenderingModeUVE renderingMode = FontImportRenderingModeUVE::SignedDistanceField;
    std::uint16_t baseSizePixels = 32U;
    std::vector<std::uint16_t> preRenderedSizes{12U, 16U, 24U, 32U};
    std::string hintingMode = "Normal";
    bool subpixelPositioning = true;
    std::string antiAliasingMode = "Grayscale";
    std::vector<std::string> codepointRanges{"0x0020-0x007F", "0x00A0-0x00FF"};
    std::vector<std::string> fallbackFonts;
    float outlineSizePixels = 0.0F;
    bool kerningEnabled = true;
    bool ligaturesEnabled = true;
    std::uint16_t atlasWidth = 1024U;
    std::uint16_t atlasHeight = 1024U;
    std::uint8_t atlasPaddingPixels = 2U;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

} // namespace UVE::Asset
