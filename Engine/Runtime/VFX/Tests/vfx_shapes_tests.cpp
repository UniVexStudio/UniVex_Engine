// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_shapes.h"

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "scalar.h"

namespace UVE::VFX::Tests {
namespace {

constexpr int kShapeSampleCount = 10000;

TEST(VfxShapesTest, PointStaysInside) {
    Math::Random rng(11ULL);
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapePoint(rng);
        EXPECT_FLOAT_EQ(sample.position.x, 0.0F);
        EXPECT_FLOAT_EQ(sample.position.y, 0.0F);
        EXPECT_FLOAT_EQ(sample.position.z, 0.0F);
        EXPECT_NEAR(Math::Length(sample.normal), 1.0F, 1e-4F);
    }
}

TEST(VfxShapesTest, SphereStaysInside) {
    Math::Random rng(22ULL);
    Math::Vector3 sum{};
    constexpr float kRadius = 2.5F;
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeSphere(rng, kRadius, false, false);
        EXPECT_LE(Math::Length(sample.position), kRadius + 1e-4F);
        EXPECT_NEAR(Math::Length(sample.normal), 1.0F, 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3 mean = sum * (1.0F / static_cast<float>(kShapeSampleCount));
    EXPECT_LT(Math::Length(mean), 0.1F);

    Math::Random hemiRng(23ULL);
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeSphere(hemiRng, kRadius, false, true);
        EXPECT_LE(Math::Length(sample.position), kRadius + 1e-4F);
        EXPECT_GE(sample.position.y, -1e-5F);
    }
}

TEST(VfxShapesTest, BoxStaysInside) {
    Math::Random rng(33ULL);
    const Math::Vector3 size{3.0F, 2.0F, 4.0F};
    Math::Vector3 sum{};
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeBox(rng, size, false);
        EXPECT_LE(std::abs(sample.position.x), size.x * 0.5F + 1e-4F);
        EXPECT_LE(std::abs(sample.position.y), size.y * 0.5F + 1e-4F);
        EXPECT_LE(std::abs(sample.position.z), size.z * 0.5F + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3 mean = sum * (1.0F / static_cast<float>(kShapeSampleCount));
    EXPECT_LT(Math::Length(mean), 0.1F);
}

TEST(VfxShapesTest, ConeStaysInside) {
    Math::Random rng(44ULL);
    constexpr float kAngle = 30.0F;
    constexpr float kLength = 3.0F;
    constexpr float kRadius = 0.5F;
    const float topRadius = kRadius + kLength * std::tan(Math::DegToRad(kAngle));
    Math::Vector3 sum{};
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeCone(rng, kAngle, kLength, kRadius, false);
        EXPECT_GE(sample.position.y, -1e-4F);
        EXPECT_LE(sample.position.y, kLength + 1e-4F);
        const float t = std::clamp(sample.position.y / kLength, 0.0F, 1.0F);
        const float maxR = Math::Lerp(kRadius, topRadius, t);
        const float radial = std::hypot(sample.position.x, sample.position.z);
        EXPECT_LE(radial, maxR + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3 mean = sum * (1.0F / static_cast<float>(kShapeSampleCount));
    EXPECT_LT(std::hypot(mean.x, mean.z), 0.15F);
    EXPECT_GT(mean.y, 0.0F);
    EXPECT_LT(mean.y, kLength);
}

TEST(VfxShapesTest, CylinderStaysInside) {
    Math::Random rng(55ULL);
    constexpr float kRadius = 1.8F;
    constexpr float kHeight = 3.0F;
    Math::Vector3 sum{};
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeCylinder(rng, kRadius, kHeight, false);
        EXPECT_LE(std::hypot(sample.position.x, sample.position.z), kRadius + 1e-4F);
        EXPECT_LE(std::abs(sample.position.y), kHeight * 0.5F + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3 mean = sum * (1.0F / static_cast<float>(kShapeSampleCount));
    EXPECT_LT(Math::Length(mean), 0.1F);
}

TEST(VfxShapesTest, TorusStaysInside) {
    Math::Random rng(66ULL);
    constexpr float kMajor = 2.0F;
    constexpr float kMinor = 0.5F;
    Math::Vector3 sum{};
    for (int i = 0; i < kShapeSampleCount; ++i) {
        const VfxShapeSample sample = SampleShapeTorus(rng, kMajor, kMinor, false);
        const float ringDist = std::hypot(sample.position.x, sample.position.z) - kMajor;
        const float tubeDist = std::hypot(ringDist, sample.position.y);
        EXPECT_LE(tubeDist, kMinor + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3 mean = sum * (1.0F / static_cast<float>(kShapeSampleCount));
    EXPECT_LT(Math::Length(mean), 0.1F);
}

TEST(VfxShapesTest, SurfaceSamplesLieOnTheSurface) {
    {
        Math::Random rng(77ULL);
        Math::Vector3 sum{};
        constexpr float kRadius = 2.0F;
        for (int i = 0; i < kShapeSampleCount; ++i) {
            const VfxShapeSample s = SampleShapeSphere(rng, kRadius, true, false);
            EXPECT_NEAR(Math::Length(s.position), kRadius, 1e-4F);
            EXPECT_NEAR(Math::Length(s.normal), 1.0F, 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::Length(sum * (1.0F / static_cast<float>(kShapeSampleCount))), 0.1F);
    }

    {
        Math::Random rng(78ULL);
        const Math::Vector3 size{2.0F, 4.0F, 6.0F};
        Math::Vector3 sum{};
        for (int i = 0; i < kShapeSampleCount; ++i) {
            const VfxShapeSample s = SampleShapeBox(rng, size, true);
            const float dx = std::abs(std::abs(s.position.x) - size.x * 0.5F);
            const float dy = std::abs(std::abs(s.position.y) - size.y * 0.5F);
            const float dz = std::abs(std::abs(s.position.z) - size.z * 0.5F);
            EXPECT_LT(std::min({dx, dy, dz}), 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::Length(sum * (1.0F / static_cast<float>(kShapeSampleCount))), 0.15F);
    }

    {
        Math::Random rng(79ULL);
        constexpr float kAngle = 25.0F;
        constexpr float kLength = 2.0F;
        constexpr float kRadius = 0.4F;
        const float r1 = kRadius + kLength * std::tan(Math::DegToRad(kAngle));
        for (int i = 0; i < kShapeSampleCount; ++i) {
            const VfxShapeSample s = SampleShapeCone(rng, kAngle, kLength, kRadius, true);
            const float t = std::clamp(s.position.y / kLength, 0.0F, 1.0F);
            const float expectedR = Math::Lerp(kRadius, r1, t);
            EXPECT_NEAR(std::hypot(s.position.x, s.position.z), expectedR, 1e-4F);
        }
    }

    {
        Math::Random rng(80ULL);
        constexpr float kRadius = 1.5F;
        constexpr float kHeight = 3.0F;
        Math::Vector3 sum{};
        for (int i = 0; i < kShapeSampleCount; ++i) {
            const VfxShapeSample s = SampleShapeCylinder(rng, kRadius, kHeight, true);
            EXPECT_NEAR(std::hypot(s.position.x, s.position.z), kRadius, 1e-4F);
            EXPECT_LE(std::abs(s.position.y), kHeight * 0.5F + 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::Length(sum * (1.0F / static_cast<float>(kShapeSampleCount))), 0.1F);
    }

    {
        Math::Random rng(81ULL);
        constexpr float kMajor = 2.0F;
        constexpr float kMinor = 0.5F;
        Math::Vector3 sum{};
        for (int i = 0; i < kShapeSampleCount; ++i) {
            const VfxShapeSample s = SampleShapeTorus(rng, kMajor, kMinor, true);
            const float ringDist = std::hypot(s.position.x, s.position.z) - kMajor;
            EXPECT_NEAR(std::hypot(ringDist, s.position.y), kMinor, 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::Length(sum * (1.0F / static_cast<float>(kShapeSampleCount))), 0.1F);
    }
}

} // namespace
} // namespace UVE::VFX::Tests
