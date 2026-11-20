// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "water_3d.h"

#include <cmath>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Water3DUVETest, DefaultComponent_IsValid) {
    const Water3DComponentUVE water{};
    EXPECT_TRUE(IsWater3DComponentValidUVE(water));
    EXPECT_FLOAT_EQ(water.waveAmplitude, 0.15F);
    EXPECT_FLOAT_EQ(water.waveFrequency, 0.80F);
    EXPECT_FLOAT_EQ(water.waveSpeed, 1.00F);
    EXPECT_FLOAT_EQ(water.clarity, 4.00F);
    EXPECT_FLOAT_EQ(water.refractionStrength, 0.05F);
    EXPECT_FLOAT_EQ(water.foamDepth, 0.30F);
}

TEST(Water3DUVETest, InvalidFields_DetectedByValidator) {
    Water3DComponentUVE water{};

    water.waveAmplitude = -0.5F;
    EXPECT_FALSE(IsWater3DComponentValidUVE(water));
    water.waveAmplitude = 0.15F;

    water.waveFrequency = 0.0F;
    EXPECT_FALSE(IsWater3DComponentValidUVE(water));
    water.waveFrequency = 0.80F;

    water.clarity = -1.0F;
    EXPECT_FALSE(IsWater3DComponentValidUVE(water));
    water.clarity = 4.00F;

    water.roughness = 1.5F;
    EXPECT_FALSE(IsWater3DComponentValidUVE(water));
    water.roughness = 0.05F;

    water.deepColor = Math::Vector3UVE{NAN, 0.0F, 0.0F};
    EXPECT_FALSE(IsWater3DComponentValidUVE(water));
}

TEST(Water3DUVETest, SanitizeWater3DComponent_RestoresSensibleDefaults) {
    Water3DComponentUVE invalid{};
    invalid.waveAmplitude = -1.0F;
    invalid.waveFrequency = -2.0F;
    invalid.clarity = 0.0F;
    invalid.roughness = 2.0F;
    invalid.deepColor = Math::Vector3UVE{NAN, -0.5F, 1.5F};

    const Water3DComponentUVE sanitized = SanitizeWater3DComponentUVE(invalid);
    EXPECT_TRUE(IsWater3DComponentValidUVE(sanitized));
    EXPECT_GE(sanitized.waveAmplitude, 0.0F);
    EXPECT_GT(sanitized.waveFrequency, 0.0F);
    EXPECT_GT(sanitized.clarity, 0.0F);
    EXPECT_LE(sanitized.roughness, 1.0F);
    EXPECT_GE(sanitized.roughness, 0.0F);
    EXPECT_GE(sanitized.deepColor.x, 0.0F);
    EXPECT_LE(sanitized.deepColor.x, 1.0F);
}

TEST(Water3DUVETest, ObjectDefinition_AppliesComponentsCorrectly) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    const EntityUVE entity = entityManager.CreateEntityUVE();
    Water3DObjectDefinitionUVE definition{};
    definition.water.waveAmplitude = 0.25F;
    ApplyWater3DObjectDefinitionUVE(entityManager, entity, definition);

    ASSERT_TRUE(entityManager.HasComponentUVE<Water3DComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity));

    const auto& waterComp = entityManager.GetComponentUVE<Water3DComponentUVE>(entity);
    EXPECT_FLOAT_EQ(waterComp.waveAmplitude, 0.25F);

    const auto& meshComp = entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(entity);
    EXPECT_EQ(meshComp.kind, PrimitiveMeshKindUVE::Plane);
}

TEST(Water3DUVETest, WaveHeightEvaluation_ProducesPeriodicDisplacement) {
    Water3DComponentUVE water{};
    water.waveAmplitude = 0.20F;
    water.waveFrequency = 1.00F;

    const float h0 = Water3DUVE::EvaluateWaveHeightUVE(water, Math::Vector2UVE{0.0F, 0.0F}, 0.0F);
    const float h1 = Water3DUVE::EvaluateWaveHeightUVE(water, Math::Vector2UVE{1.0F, 0.5F}, 1.0F);

    EXPECT_TRUE(std::isfinite(h0));
    EXPECT_TRUE(std::isfinite(h1));
    EXPECT_LE(std::abs(h0), water.waveAmplitude * 1.5F);
    EXPECT_LE(std::abs(h1), water.waveAmplitude * 1.5F);
}

TEST(Water3DUVETest, WaveNormalEvaluation_IsUnitLength) {
    Water3DComponentUVE water{};
    water.waveAmplitude = 0.15F;

    const Math::Vector3UVE normal = Water3DUVE::EvaluateWaveNormalUVE(water, Math::Vector2UVE{0.5F, 0.5F}, 0.5F);
    EXPECT_TRUE(Math::IsFiniteUVE(normal));
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    EXPECT_NEAR(length, 1.0F, 1.0e-3F);
    EXPECT_GT(normal.y, 0.0F); // Upward facing
}

TEST(Water3DUVETest, LocalBounds_EnclosesWaveDisplacement) {
    Water3DComponentUVE water{};
    water.waveAmplitude = 0.35F;

    const Math::AabbUVE bounds = Water3DUVE::LocalBoundsUVE(water);
    const Math::Vector3UVE extents = bounds.GetExtentsUVE();
    EXPECT_FLOAT_EQ(extents.x, 0.5F);
    EXPECT_FLOAT_EQ(extents.y, 0.35F);
    EXPECT_FLOAT_EQ(extents.z, 0.5F);
}

} // namespace
} // namespace UVE::Scene::Tests
