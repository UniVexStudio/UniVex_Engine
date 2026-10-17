// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// 2D single and mass-instanced mesh rendering component (`meshAssetGuid`, `textureAssetGuid`,
/// `instanceOffsets2D`).
struct MeshInstance2DComponentUVE final {
    Asset::AssetGuidUVE meshAssetGuid = Asset::kInvalidAssetGuidUVE;
    Asset::AssetGuidUVE textureAssetGuid = Asset::kInvalidAssetGuidUVE;
    std::vector<Math::Vector2UVE> instanceOffsets2D{};
    bool instanced = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const MeshInstance2DComponentUVE&) const = default;
};

struct MeshInstance2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "MeshInstance2D";
    MeshInstance2DComponentUVE mesh{};
};

struct MultiMeshInstance2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "MultiMeshInstance2D";
    MeshInstance2DComponentUVE mesh{Asset::kInvalidAssetGuidUVE, Asset::kInvalidAssetGuidUVE, {},
                                    true, true};
};

void ApplyMeshInstance2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const MeshInstance2DObjectDefinitionUVE& value);
void ApplyMultiMeshInstance2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const MultiMeshInstance2DObjectDefinitionUVE& value);

/// Merges child 2D draw calls into one offscreen composite (`fitMarginPixels`, `clearMarginPixels`,
/// `groupOpacity`).
struct CanvasGroupComponentUVE final {
    float fitMarginPixels = 10.0F;
    float clearMarginPixels = 10.0F;
    float groupOpacity = 1.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CanvasGroupComponentUVE&) const = default;
};

struct CanvasGroupObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CanvasGroup";
    CanvasGroupComponentUVE group{};
};

void ApplyCanvasGroupObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const CanvasGroupObjectDefinitionUVE& value);

enum class BackBufferCopyMode2DUVE : std::uint8_t {
    Disabled = 0,
    Rect = 1,
    Viewport = 2,
};

/// Copies a 2D screen region into a shader-readable backbuffer texture (`copyMode`, `rect`).
struct BackBufferCopyComponentUVE final {
    BackBufferCopyMode2DUVE copyMode = BackBufferCopyMode2DUVE::Rect;
    Math::RectUVE rect{Math::Vector2UVE{-100.0F, -100.0F}, Math::Vector2UVE{200.0F, 200.0F}};
    bool enabled = true;

    [[nodiscard]] bool operator==(const BackBufferCopyComponentUVE&) const = default;
};

struct BackBufferCopyObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "BackBufferCopy";
    BackBufferCopyComponentUVE backBufferCopy{};
};

void ApplyBackBufferCopyObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const BackBufferCopyObjectDefinitionUVE& value);

enum class Light2DKindUVE : std::uint8_t {
    Point = 0,
    Directional = 1,
};

/// 2D point and directional lighting component (`kind`, `color`, `energy`, `rangePixels`,
/// `direction`, `shadowEnabled`).
struct Light2DComponentUVE final {
    Light2DKindUVE kind = Light2DKindUVE::Point;
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float energy = 1.0F;
    float rangePixels = 256.0F;
    Math::Vector2UVE direction{0.0F, -1.0F};
    bool shadowEnabled = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Light2DComponentUVE&) const = default;
};

struct Light2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Light2D";
    Light2DComponentUVE light{Light2DKindUVE::Point};
};
struct PointLight2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "PointLight2D";
    Light2DComponentUVE light{Light2DKindUVE::Point};
};
struct DirectionalLight2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "DirectionalLight2D";
    Light2DComponentUVE light{Light2DKindUVE::Directional};
};

void ApplyLight2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const Light2DObjectDefinitionUVE& value);
void ApplyPointLight2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const PointLight2DObjectDefinitionUVE& value);
void ApplyDirectionalLight2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const DirectionalLight2DObjectDefinitionUVE& value);

/// 2D shadow-casting occluder (`boundingRect`, `lightMask`, `enabled`).
struct LightOccluder2DComponentUVE final {
    Math::RectUVE boundingRect{Math::Vector2UVE{-16.0F, -16.0F}, Math::Vector2UVE{32.0F, 32.0F}};
    std::uint32_t lightMask = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const LightOccluder2DComponentUVE&) const = default;
};

struct LightOccluder2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "LightOccluder2D";
    LightOccluder2DComponentUVE occluder{};
};

void ApplyLightOccluder2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const LightOccluder2DObjectDefinitionUVE& value);

/// Evaluates accumulated 2D light color at `point2D`, accounting for `LightOccluder2DComponentUVE`
/// shadow blockers when `shadowEnabled` is true.
[[nodiscard]] Math::ColorUVE EvaluateLight2DIlluminationAtPointUVE(
    IEntityManagerUVE& entityManager, const Math::Vector2UVE& point2D);

} // namespace UVE::Scene
