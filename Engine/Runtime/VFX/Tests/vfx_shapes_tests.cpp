// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_shapes.h"

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "scalar.h"

namespace UVE::VFX::Tests {
namespace {

constexpr int kShapeSampleCountUVE = 10000;

TEST(VfxShapesUVETest, PointStaysInside) {
    Math::RandomUVE rng(11ULL);
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapePointUVE(rng);
        EXPECT_FLOAT_EQ(sample.position.x, 0.0F);
        EXPECT_FLOAT_EQ(sample.position.y, 0.0F);
        EXPECT_FLOAT_EQ(sample.position.z, 0.0F);
        EXPECT_NEAR(Math::LengthUVE(sample.normal), 1.0F, 1e-4F);
    }
}

TEST(VfxShapesUVETest, SphereStaysInside) {
    Math::RandomUVE rng(22ULL);
    Math::Vector3UVE sum{};
    constexpr float kRadius = 2.5F;
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeSphereUVE(rng, kRadius, false, false);
        EXPECT_LE(Math::LengthUVE(sample.position), kRadius + 1e-4F);
        EXPECT_NEAR(Math::LengthUVE(sample.normal), 1.0F, 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3UVE mean = sum * (1.0F / static_cast<float>(kShapeSampleCountUVE));
    EXPECT_LT(Math::LengthUVE(mean), 0.1F);

    Math::RandomUVE hemiRng(23ULL);
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeSphereUVE(hemiRng, kRadius, false, true);
        EXPECT_LE(Math::LengthUVE(sample.position), kRadius + 1e-4F);
        EXPECT_GE(sample.position.y, -1e-5F);
    }
}

TEST(VfxShapesUVETest, BoxStaysInside) {
    Math::RandomUVE rng(33ULL);
    const Math::Vector3UVE size{3.0F, 2.0F, 4.0F};
    Math::Vector3UVE sum{};
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeBoxUVE(rng, size, false);
        EXPECT_LE(std::abs(sample.position.x), size.x * 0.5F + 1e-4F);
        EXPECT_LE(std::abs(sample.position.y), size.y * 0.5F + 1e-4F);
        EXPECT_LE(std::abs(sample.position.z), size.z * 0.5F + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3UVE mean = sum * (1.0F / static_cast<float>(kShapeSampleCountUVE));
    EXPECT_LT(Math::LengthUVE(mean), 0.1F);
}

TEST(VfxShapesUVETest, ConeStaysInside) {
    Math::RandomUVE rng(44ULL);
    constexpr float kAngle = 30.0F;
    constexpr float kLength = 3.0F;
    constexpr float kRadius = 0.5F;
    const float topRadius = kRadius + kLength * std::tan(Math::DegToRadUVE(kAngle));
    Math::Vector3UVE sum{};
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeConeUVE(rng, kAngle, kLength, kRadius, false);
        EXPECT_GE(sample.position.y, -1e-4F);
        EXPECT_LE(sample.position.y, kLength + 1e-4F);
        const float t = std::clamp(sample.position.y / kLength, 0.0F, 1.0F);
        const float maxR = Math::LerpUVE(kRadius, topRadius, t);
        const float radial = std::hypot(sample.position.x, sample.position.z);
        EXPECT_LE(radial, maxR + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3UVE mean = sum * (1.0F / static_cast<float>(kShapeSampleCountUVE));
    EXPECT_LT(std::hypot(mean.x, mean.z), 0.15F);
    EXPECT_GT(mean.y, 0.0F);
    EXPECT_LT(mean.y, kLength);
}

TEST(VfxShapesUVETest, CylinderStaysInside) {
    Math::RandomUVE rng(55ULL);
    constexpr float kRadius = 1.8F;
    constexpr float kHeight = 3.0F;
    Math::Vector3UVE sum{};
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeCylinderUVE(rng, kRadius, kHeight, false);
        EXPECT_LE(std::hypot(sample.position.x, sample.position.z), kRadius + 1e-4F);
        EXPECT_LE(std::abs(sample.position.y), kHeight * 0.5F + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3UVE mean = sum * (1.0F / static_cast<float>(kShapeSampleCountUVE));
    EXPECT_LT(Math::LengthUVE(mean), 0.1F);
}

TEST(VfxShapesUVETest, TorusStaysInside) {
    Math::RandomUVE rng(66ULL);
    constexpr float kMajor = 2.0F;
    constexpr float kMinor = 0.5F;
    Math::Vector3UVE sum{};
    for (int i = 0; i < kShapeSampleCountUVE; ++i) {
        const VfxShapeSampleUVE sample = SampleShapeTorusUVE(rng, kMajor, kMinor, false);
        const float ringDist = std::hypot(sample.position.x, sample.position.z) - kMajor;
        const float tubeDist = std::hypot(ringDist, sample.position.y);
        EXPECT_LE(tubeDist, kMinor + 1e-4F);
        sum += sample.position;
    }
    const Math::Vector3UVE mean = sum * (1.0F / static_cast<float>(kShapeSampleCountUVE));
    EXPECT_LT(Math::LengthUVE(mean), 0.1F);
}

TEST(VfxShapesUVETest, SurfaceSamplesLieOnTheSurface) {
    {
        Math::RandomUVE rng(77ULL);
        Math::Vector3UVE sum{};
        constexpr float kRadius = 2.0F;
        for (int i = 0; i < kShapeSampleCountUVE; ++i) {
            const VfxShapeSampleUVE s = SampleShapeSphereUVE(rng, kRadius, true, false);
            EXPECT_NEAR(Math::LengthUVE(s.position), kRadius, 1e-4F);
            EXPECT_NEAR(Math::LengthUVE(s.normal), 1.0F, 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::LengthUVE(sum * (1.0F / static_cast<float>(kShapeSampleCountUVE))), 0.1F);
    }

    {
        Math::RandomUVE rng(78ULL);
        const Math::Vector3UVE size{2.0F, 4.0F, 6.0F};
        Math::Vector3UVE sum{};
        for (int i = 0; i < kShapeSampleCountUVE; ++i) {
            const VfxShapeSampleUVE s = SampleShapeBoxUVE(rng, size, true);
            const float dx = std::abs(std::abs(s.position.x) - size.x * 0.5F);
            const float dy = std::abs(std::abs(s.position.y) - size.y * 0.5F);
            const float dz = std::abs(std::abs(s.position.z) - size.z * 0.5F);
            EXPECT_LT(std::min({dx, dy, dz}), 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::LengthUVE(sum * (1.0F / static_cast<float>(kShapeSampleCountUVE))), 0.15F);
    }

    {
        Math::RandomUVE rng(79ULL);
        constexpr float kAngle = 25.0F;
        constexpr float kLength = 2.0F;
        constexpr float kRadius = 0.4F;
        const float r1 = kRadius + kLength * std::tan(Math::DegToRadUVE(kAngle));
        for (int i = 0; i < kShapeSampleCountUVE; ++i) {
            const VfxShapeSampleUVE s = SampleShapeConeUVE(rng, kAngle, kLength, kRadius, true);
            const float t = std::clamp(s.position.y / kLength, 0.0F, 1.0F);
            const float expectedR = Math::LerpUVE(kRadius, r1, t);
            EXPECT_NEAR(std::hypot(s.position.x, s.position.z), expectedR, 1e-4F);
        }
    }

    {
        Math::RandomUVE rng(80ULL);
        constexpr float kRadius = 1.5F;
        constexpr float kHeight = 3.0F;
        Math::Vector3UVE sum{};
        for (int i = 0; i < kShapeSampleCountUVE; ++i) {
            const VfxShapeSampleUVE s = SampleShapeCylinderUVE(rng, kRadius, kHeight, true);
            EXPECT_NEAR(std::hypot(s.position.x, s.position.z), kRadius, 1e-4F);
            EXPECT_LE(std::abs(s.position.y), kHeight * 0.5F + 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::LengthUVE(sum * (1.0F / static_cast<float>(kShapeSampleCountUVE))), 0.1F);
    }

    {
        Math::RandomUVE rng(81ULL);
        constexpr float kMajor = 2.0F;
        constexpr float kMinor = 0.5F;
        Math::Vector3UVE sum{};
        for (int i = 0; i < kShapeSampleCountUVE; ++i) {
            const VfxShapeSampleUVE s = SampleShapeTorusUVE(rng, kMajor, kMinor, true);
            const float ringDist = std::hypot(s.position.x, s.position.z) - kMajor;
            EXPECT_NEAR(std::hypot(ringDist, s.position.y), kMinor, 1e-4F);
            sum += s.position;
        }
        EXPECT_LT(Math::LengthUVE(sum * (1.0F / static_cast<float>(kShapeSampleCountUVE))), 0.1F);
    }
}

} // namespace
} // namespace UVE::VFX::Tests
