// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "asset_guid.h"
#include "entity.h"
#include "aabb.h"
#include "vector2.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class CSGPrimitiveKind : std::uint8_t {
    Box = 0,
    Sphere = 1,
    Cylinder = 2,
    Torus = 3,
    Mesh = 4,
    Polygon = 5,
    Combiner = 6,
};

enum class CSGOperation : std::uint8_t {
    Union = 0,
    Intersection = 1,
    Subtraction = 2,
};

/// Constructive solid geometry shape & boolean combiner component built on `SurfaceInstance3DBase`.
struct CSGShape3DComponent final {
    CSGPrimitiveKind primitiveKind = CSGPrimitiveKind::Box;
    CSGOperation operation = CSGOperation::Union;
    Math::Vector3 boxSize{1.0F, 1.0F, 1.0F};
    float radius = 0.5F;
    float innerRadius = 0.25F;
    float height = 1.0F;
    Asset::AssetGuid meshAssetGuid = Asset::kInvalidAssetGuid;
    Asset::AssetGuid materialAssetGuid = Asset::kInvalidAssetGuid;
    std::vector<Math::Vector2> polygonVertices2D{};
    bool useCollision = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CSGShape3DComponent&) const = default;
};

[[nodiscard]] bool IsCSGShape3DComponentValid(const CSGShape3DComponent& value) noexcept;

[[nodiscard]] CSGShape3DComponent SanitizeCSGShape3DComponent(
    const CSGShape3DComponent& value);

struct CSGBox3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGBox3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Box};
};
struct CSGSphere3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGSphere3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Sphere};
};
struct CSGCylinder3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGCylinder3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Cylinder};
};
struct CSGTorus3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGTorus3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Torus};
};
struct CSGMesh3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGMesh3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Mesh};
};
struct CSGPolygon3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGPolygon3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Polygon};
};
struct CSGCombiner3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CSGCombiner3D";
    CSGShape3DComponent csg{CSGPrimitiveKind::Combiner};
};

void ApplyCSGBox3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const CSGBox3DObjectDefinition& value);
void ApplyCSGSphere3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const CSGSphere3DObjectDefinition& value);
void ApplyCSGCylinder3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const CSGCylinder3DObjectDefinition& value);
void ApplyCSGTorus3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const CSGTorus3DObjectDefinition& value);
void ApplyCSGMesh3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const CSGMesh3DObjectDefinition& value);
void ApplyCSGPolygon3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const CSGPolygon3DObjectDefinition& value);
void ApplyCSGCombiner3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const CSGCombiner3DObjectDefinition& value);

class CSGShape3D final {
public:
    CSGShape3D() = delete;

    /// Computes the local-space AABB of `shape`.
    [[nodiscard]] static Math::Aabb ComputeLocalAabb(
        const CSGShape3DComponent& shape) noexcept;

    /// Evaluates the local-space signed distance field of `shape` at `localPoint` (negative inside,
    /// positive outside).
    [[nodiscard]] static float EvaluateLocalSignedDistance(
        const CSGShape3DComponent& shape, const Math::Vector3& localPoint) noexcept;

    /// Combines two signed-distance values `sdfA` and `sdfB` under `operation` (`Union = min`,
    /// `Intersection = max`, `Subtraction = max(sdfA, -sdfB)`).
    [[nodiscard]] static float CombineSignedDistance(CSGOperation operation, float sdfA,
                                                        float sdfB) noexcept;
};

} // namespace UVE::Scene
