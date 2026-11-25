// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "water_3d.h"

#include <cmath>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Water3DTest, DefaultComponent_IsValid) {
    const Water3DComponent water{};
    EXPECT_TRUE(IsWater3DComponentValid(water));
    EXPECT_FLOAT_EQ(water.waveAmplitude, 0.15F);
    EXPECT_FLOAT_EQ(water.waveFrequency, 0.80F);
    EXPECT_FLOAT_EQ(water.waveSpeed, 1.00F);
    EXPECT_FLOAT_EQ(water.clarity, 4.00F);
    EXPECT_FLOAT_EQ(water.refractionStrength, 0.05F);
    EXPECT_FLOAT_EQ(water.foamDepth, 0.30F);
}

TEST(Water3DTest, InvalidFields_DetectedByValidator) {
    Water3DComponent water{};

    water.waveAmplitude = -0.5F;
    EXPECT_FALSE(IsWater3DComponentValid(water));
    water.waveAmplitude = 0.15F;

    water.waveFrequency = 0.0F;
    EXPECT_FALSE(IsWater3DComponentValid(water));
    water.waveFrequency = 0.80F;

    water.clarity = -1.0F;
    EXPECT_FALSE(IsWater3DComponentValid(water));
    water.clarity = 4.00F;

    water.roughness = 1.5F;
    EXPECT_FALSE(IsWater3DComponentValid(water));
    water.roughness = 0.05F;

    water.deepColor = Math::Vector3{NAN, 0.0F, 0.0F};
    EXPECT_FALSE(IsWater3DComponentValid(water));
}

TEST(Water3DTest, SanitizeWater3DComponent_RestoresSensibleDefaults) {
    Water3DComponent invalid{};
    invalid.waveAmplitude = -1.0F;
    invalid.waveFrequency = -2.0F;
    invalid.clarity = 0.0F;
    invalid.roughness = 2.0F;
    invalid.deepColor = Math::Vector3{NAN, -0.5F, 1.5F};

    const Water3DComponent sanitized = SanitizeWater3DComponent(invalid);
    EXPECT_TRUE(IsWater3DComponentValid(sanitized));
    EXPECT_GE(sanitized.waveAmplitude, 0.0F);
    EXPECT_GT(sanitized.waveFrequency, 0.0F);
    EXPECT_GT(sanitized.clarity, 0.0F);
    EXPECT_LE(sanitized.roughness, 1.0F);
    EXPECT_GE(sanitized.roughness, 0.0F);
    EXPECT_GE(sanitized.deepColor.x, 0.0F);
    EXPECT_LE(sanitized.deepColor.x, 1.0F);
}

TEST(Water3DTest, ObjectDefinition_AppliesComponentsCorrectly) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    const Entity entity = entityManager.CreateEntity();
    Water3DObjectDefinition definition{};
    definition.water.waveAmplitude = 0.25F;
    ApplyWater3DObjectDefinition(entityManager, entity, definition);

    ASSERT_TRUE(entityManager.HasComponent<Water3DComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<PrimitiveMeshComponent>(entity));

    const auto& waterComp = entityManager.GetComponent<Water3DComponent>(entity);
    EXPECT_FLOAT_EQ(waterComp.waveAmplitude, 0.25F);

    const auto& meshComp = entityManager.GetComponent<PrimitiveMeshComponent>(entity);
    EXPECT_EQ(meshComp.kind, PrimitiveMeshKind::Plane);
}

TEST(Water3DTest, WaveHeightEvaluation_ProducesPeriodicDisplacement) {
    Water3DComponent water{};
    water.waveAmplitude = 0.20F;
    water.waveFrequency = 1.00F;

    const float h0 = Water3D::EvaluateWaveHeight(water, Math::Vector2{0.0F, 0.0F}, 0.0F);
    const float h1 = Water3D::EvaluateWaveHeight(water, Math::Vector2{1.0F, 0.5F}, 1.0F);

    EXPECT_TRUE(std::isfinite(h0));
    EXPECT_TRUE(std::isfinite(h1));
    EXPECT_LE(std::abs(h0), water.waveAmplitude * 1.5F);
    EXPECT_LE(std::abs(h1), water.waveAmplitude * 1.5F);
}

TEST(Water3DTest, WaveNormalEvaluation_IsUnitLength) {
    Water3DComponent water{};
    water.waveAmplitude = 0.15F;

    const Math::Vector3 normal = Water3D::EvaluateWaveNormal(water, Math::Vector2{0.5F, 0.5F}, 0.5F);
    EXPECT_TRUE(Math::IsFinite(normal));
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    EXPECT_NEAR(length, 1.0F, 1.0e-3F);
    EXPECT_GT(normal.y, 0.0F); // Upward facing
}

TEST(Water3DTest, LocalBounds_EnclosesWaveDisplacement) {
    Water3DComponent water{};
    water.waveAmplitude = 0.35F;

    const Math::Aabb bounds = Water3D::LocalBounds(water);
    const Math::Vector3 extents = bounds.GetExtents();
    EXPECT_FLOAT_EQ(extents.x, 0.5F);
    EXPECT_FLOAT_EQ(extents.y, 0.35F);
    EXPECT_FLOAT_EQ(extents.z, 0.5F);
}

} // namespace
} // namespace UVE::Scene::Tests
