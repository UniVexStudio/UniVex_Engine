// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class CollisionShape2DKindUVE : std::uint8_t {
    Circle = 0,
    Rectangle = 1,
    Capsule = 2,
};

struct CollisionShape2DComponentUVE final {
    CollisionShape2DKindUVE shapeKind = CollisionShape2DKindUVE::Circle;
    float radius = 16.0F;
    Math::Vector2UVE halfExtents{16.0F, 16.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CollisionShape2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsCollisionShape2DComponentValidUVE(
    const CollisionShape2DComponentUVE& value) noexcept;
[[nodiscard]] CollisionShape2DComponentUVE SanitizeCollisionShape2DComponentUVE(
    const CollisionShape2DComponentUVE& value) noexcept;

struct CollisionShape2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "CollisionShape2D";
    static constexpr std::string_view defaultName = "CollisionShape2D";
    CollisionShape2DComponentUVE shape{};
};
void ApplyCollisionShape2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const CollisionShape2DObjectDefinitionUVE& value);

struct CollisionPolygon2DComponentUVE final {
    std::vector<Math::Vector2UVE> vertices{};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CollisionPolygon2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsCollisionPolygon2DComponentValidUVE(
    const CollisionPolygon2DComponentUVE& value) noexcept;
[[nodiscard]] CollisionPolygon2DComponentUVE SanitizeCollisionPolygon2DComponentUVE(
    const CollisionPolygon2DComponentUVE& value);

struct CollisionPolygon2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "CollisionPolygon2D";
    static constexpr std::string_view defaultName = "CollisionPolygon2D";
    CollisionPolygon2DComponentUVE polygon{};
};
void ApplyCollisionPolygon2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                                const CollisionPolygon2DObjectDefinitionUVE& value);

struct Area2DComponentUVE final {
    Math::Vector2UVE halfExtents{32.0F, 32.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 1U;
    bool monitoring = true;
    bool monitorable = true;

    // Runtime state, never persisted:
    std::vector<EntityUVE> overlappingEntities{};

    [[nodiscard]] bool operator==(const Area2DComponentUVE& other) const noexcept {
        return halfExtents == other.halfExtents && collisionLayer == other.collisionLayer &&
               collisionMask == other.collisionMask && monitoring == other.monitoring &&
               monitorable == other.monitorable;
    }
};

[[nodiscard]] bool IsArea2DComponentValidUVE(const Area2DComponentUVE& value) noexcept;
[[nodiscard]] Area2DComponentUVE SanitizeArea2DComponentUVE(
    const Area2DComponentUVE& value) noexcept;

struct Area2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Area2D";
    static constexpr std::string_view defaultName = "Area2D";
    Area2DComponentUVE area{};
};
void ApplyArea2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Area2DObjectDefinitionUVE& value);

enum class Body2DKindUVE : std::uint8_t {
    Static = 0,
    Rigid = 1,
    Character = 2,
};

struct PhysicsBody2DComponentUVE final {
    Body2DKindUVE kind = Body2DKindUVE::Rigid;
    float mass = 1.0F;
    float gravityScale = 1.0F;
    float linearDamping = 0.05F;
    Math::Vector2UVE velocity{0.0F, 0.0F};
    bool enabled = true;

    // Runtime state, never persisted:
    bool grounded = false;

    [[nodiscard]] bool operator==(const PhysicsBody2DComponentUVE& other) const noexcept {
        return kind == other.kind && mass == other.mass && gravityScale == other.gravityScale &&
               linearDamping == other.linearDamping && velocity == other.velocity &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsPhysicsBody2DComponentValidUVE(
    const PhysicsBody2DComponentUVE& value) noexcept;
[[nodiscard]] PhysicsBody2DComponentUVE SanitizePhysicsBody2DComponentUVE(
    const PhysicsBody2DComponentUVE& value) noexcept;

struct Static2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Static2D";
    PhysicsBody2DComponentUVE body{Body2DKindUVE::Static};
};
struct Rigid2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Rigid2D";
    PhysicsBody2DComponentUVE body{Body2DKindUVE::Rigid};
};
struct Character2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Character2D";
    PhysicsBody2DComponentUVE body{Body2DKindUVE::Character};
};

void ApplyStatic2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Static2DObjectDefinitionUVE& value);
void ApplyRigid2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const Rigid2DObjectDefinitionUVE& value);
void ApplyCharacter2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const Character2DObjectDefinitionUVE& value);

struct RayCast2DComponentUVE final {
    Math::Vector2UVE targetPosition{0.0F, -100.0F};
    std::uint32_t collisionMask = 1U;
    bool enabled = true;

    // Runtime state, never persisted:
    bool hit = false;
    EntityUVE hitEntity = kInvalidEntityUVE;
    Math::Vector2UVE hitPoint{0.0F, 0.0F};
    float hitDistance = 0.0F;

    [[nodiscard]] bool operator==(const RayCast2DComponentUVE& other) const noexcept {
        return targetPosition == other.targetPosition && collisionMask == other.collisionMask &&
               enabled == other.enabled;
    }
};

struct ShapeCast2DComponentUVE final {
    float radius = 8.0F;
    Math::Vector2UVE targetPosition{0.0F, -100.0F};
    std::uint32_t collisionMask = 1U;
    bool enabled = true;

    // Runtime state, never persisted:
    bool hit = false;
    EntityUVE hitEntity = kInvalidEntityUVE;
    float hitDistance = 0.0F;

    [[nodiscard]] bool operator==(const ShapeCast2DComponentUVE& other) const noexcept {
        return radius == other.radius && targetPosition == other.targetPosition &&
               collisionMask == other.collisionMask && enabled == other.enabled;
    }
};

struct RayCast2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "RayCast2D";
    RayCast2DComponentUVE rayCast{};
};
struct ShapeCast2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "ShapeCast2D";
    ShapeCast2DComponentUVE shapeCast{};
};

void ApplyRayCast2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const RayCast2DObjectDefinitionUVE& value);
void ApplyShapeCast2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const ShapeCast2DObjectDefinitionUVE& value);

enum class Joint2DKindUVE : std::uint8_t {
    Pin = 0,
    Groove = 1,
    DampedSpring = 2,
};

struct Joint2DComponentUVE final {
    Joint2DKindUVE kind = Joint2DKindUVE::Pin;
    EntityUVE nodeA = kInvalidEntityUVE;
    EntityUVE nodeB = kInvalidEntityUVE;
    float restLength = 0.0F;
    float stiffness = 20.0F;
    float damping = 1.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Joint2DComponentUVE&) const = default;
};

struct PinJoint2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "PinJoint2D";
    Joint2DComponentUVE joint{Joint2DKindUVE::Pin};
};
struct GrooveJoint2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "GrooveJoint2D";
    Joint2DComponentUVE joint{Joint2DKindUVE::Groove};
};
struct DampedSpringJoint2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "DampedSpringJoint2D";
    Joint2DComponentUVE joint{Joint2DKindUVE::DampedSpring, kInvalidEntityUVE, kInvalidEntityUVE,
                              50.0F, 20.0F, 1.0F, true};
};

void ApplyPinJoint2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const PinJoint2DObjectDefinitionUVE& value);
void ApplyGrooveJoint2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const GrooveJoint2DObjectDefinitionUVE& value);
void ApplyDampedSpringJoint2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const DampedSpringJoint2DObjectDefinitionUVE& value);

/// Steps 2D physics bodies, solves 2D joints, updates `Area2D` overlaps, and evaluates `RayCast2D`
/// / `ShapeCast2D` queries against `CollisionShape2DComponentUVE` entities.
std::size_t UpdatePhysics2DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds,
                                      const Math::Vector2UVE& gravity2D = Math::Vector2UVE{0.0F, -980.0F});

} // namespace UVE::Scene
