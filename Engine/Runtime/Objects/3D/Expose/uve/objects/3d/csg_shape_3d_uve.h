// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class CSGPrimitiveKindUVE : std::uint8_t {
    Box = 0,
    Sphere = 1,
    Cylinder = 2,
    Torus = 3,
    Mesh = 4,
    Polygon = 5,
    Combiner = 6,
};

enum class CSGOperationUVE : std::uint8_t {
    Union = 0,
    Intersection = 1,
    Subtraction = 2,
};

/// Constructive solid geometry shape & boolean combiner component built on `SurfaceInstance3DBaseUVE`.
struct CSGShape3DComponentUVE final {
    CSGPrimitiveKindUVE primitiveKind = CSGPrimitiveKindUVE::Box;
    CSGOperationUVE operation = CSGOperationUVE::Union;
    Math::Vector3UVE boxSize{1.0F, 1.0F, 1.0F};
    float radius = 0.5F;
    float innerRadius = 0.25F;
    float height = 1.0F;
    Asset::AssetGuidUVE meshAssetGuid = Asset::kInvalidAssetGuidUVE;
    Asset::AssetGuidUVE materialAssetGuid = Asset::kInvalidAssetGuidUVE;
    std::vector<Math::Vector2UVE> polygonVertices2D{};
    bool useCollision = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CSGShape3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsCSGShape3DComponentValidUVE(const CSGShape3DComponentUVE& value) noexcept;

[[nodiscard]] CSGShape3DComponentUVE SanitizeCSGShape3DComponentUVE(
    const CSGShape3DComponentUVE& value);

struct CSGBox3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGBox3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Box};
};
struct CSGSphere3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGSphere3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Sphere};
};
struct CSGCylinder3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGCylinder3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Cylinder};
};
struct CSGTorus3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGTorus3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Torus};
};
struct CSGMesh3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGMesh3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Mesh};
};
struct CSGPolygon3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGPolygon3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Polygon};
};
struct CSGCombiner3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CSGCombiner3D";
    CSGShape3DComponentUVE csg{CSGPrimitiveKindUVE::Combiner};
};

void ApplyCSGBox3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const CSGBox3DObjectDefinitionUVE& value);
void ApplyCSGSphere3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const CSGSphere3DObjectDefinitionUVE& value);
void ApplyCSGCylinder3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const CSGCylinder3DObjectDefinitionUVE& value);
void ApplyCSGTorus3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const CSGTorus3DObjectDefinitionUVE& value);
void ApplyCSGMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const CSGMesh3DObjectDefinitionUVE& value);
void ApplyCSGPolygon3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const CSGPolygon3DObjectDefinitionUVE& value);
void ApplyCSGCombiner3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const CSGCombiner3DObjectDefinitionUVE& value);

class CSGShape3DUVE final {
public:
    CSGShape3DUVE() = delete;

    /// Computes the local-space AABB of `shape`.
    [[nodiscard]] static Math::AabbUVE ComputeLocalAabbUVE(
        const CSGShape3DComponentUVE& shape) noexcept;

    /// Evaluates the local-space signed distance field of `shape` at `localPoint` (negative inside,
    /// positive outside).
    [[nodiscard]] static float EvaluateLocalSignedDistanceUVE(
        const CSGShape3DComponentUVE& shape, const Math::Vector3UVE& localPoint) noexcept;

    /// Combines two signed-distance values `sdfA` and `sdfB` under `operation` (`Union = min`,
    /// `Intersection = max`, `Subtraction = max(sdfA, -sdfB)`).
    [[nodiscard]] static float CombineSignedDistanceUVE(CSGOperationUVE operation, float sdfA,
                                                        float sdfB) noexcept;
};

} // namespace UVE::Scene
