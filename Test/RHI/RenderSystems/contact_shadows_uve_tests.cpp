// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/contact_shadows_uve.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Render::Tests {
namespace {

using Math::Matrix4x4UVE;
using Math::Vector3UVE;

constexpr float kTestFovYRadiansUVE = 1.0471976F; // 60 degrees
constexpr std::uint32_t kTestStepCapacityUVE = 32U;

/// A camera at the origin looking down -Z, which is the convention the whole renderer uses.
[[nodiscard]] Matrix4x4UVE MakeTestProjectionUVE() {
    return Matrix4x4UVE::PerspectiveUVE(kTestFovYRadiansUVE, 16.0F / 9.0F, 0.1F, 100.0F);
}

/// Converts a view-space position into the raw depth-buffer value the projection would write, using
/// the same [0,1] clip-space convention the shader assumes (depth = 0.5 * ndc.z + 0.5).
[[nodiscard]] float RawDepthOfViewPositionUVE(const Matrix4x4UVE& projection, const Vector3UVE& viewPosition) {
    const Detail::ContactShadowClipPositionUVE clip = Detail::ProjectContactShadowPointUVE(projection, viewPosition);
    return (clip.z / clip.w) * 0.5F + 0.5F;
}

/// An analytical scene: a floor at y = -1 and a wall at x = -0.8, both infinite half-spaces inside a
/// bounded region, with the depth lookup returning whichever surface the camera would actually see
/// at that pixel. This is what makes the march testable: every expected occlusion follows from
/// geometry rather than from replaying the implementation.
class SyntheticSceneUVE final {
public:
    SyntheticSceneUVE(const Matrix4x4UVE& projection, const Matrix4x4UVE& inverseProjection)
        : m_projection(projection), m_inverseProjection(inverseProjection) {}

    [[nodiscard]] float RawDepthAtUvUVE(const float u, const float v) const {
        // Reconstruct the pixel's view-space ray. Depth 0.99 rather than 1.0 keeps the
        // inverse-projected point finite; any point along the ray gives the same direction.
        const Vector3UVE rayPoint =
            Detail::ReconstructContactShadowViewPositionUVE(m_inverseProjection, u, v, 0.99F);
        const float rayPointLengthSquared = Math::LengthSquaredUVE(rayPoint);
        if (!(rayPointLengthSquared > 1.0e-12F)) {
            return 1.0F;
        }
        const Vector3UVE rayDirection = rayPoint * (1.0F / std::sqrt(rayPointLengthSquared));

        float nearestDistance = std::numeric_limits<float>::max();
        // Floor: y = -1.
        if (rayDirection.y < -1.0e-6F) {
            const float distance = -1.0F / rayDirection.y;
            if (IsInsideSceneBoundsUVE(rayDirection * distance) && distance > 0.0F) {
                nearestDistance = distance;
            }
        }
        // Wall: x = -0.8.
        if (rayDirection.x < -1.0e-6F) {
            const float distance = -0.8F / rayDirection.x;
            if (IsInsideSceneBoundsUVE(rayDirection * distance) && distance > 0.0F &&
                distance < nearestDistance) {
                nearestDistance = distance;
            }
        }
        if (!(nearestDistance < std::numeric_limits<float>::max())) {
            return 1.0F;
        }
        return RawDepthOfViewPositionUVE(m_projection, rayDirection * nearestDistance);
    }

private:
    static bool IsInsideSceneBoundsUVE(const Vector3UVE& point) {
        return std::fabs(point.x) <= 8.0F && std::fabs(point.y) <= 6.0F && point.z <= -0.5F &&
               point.z >= -40.0F;
    }

    const Matrix4x4UVE& m_projection;
    const Matrix4x4UVE& m_inverseProjection;
};

/// The straight-up light direction: a floor point can never be shadowed by anything in this scene,
/// because both surfaces are below or beside the ray.
[[nodiscard]] Vector3UVE UpwardLightDirectionUVE() {
    return Vector3UVE{0.0F, 1.0F, 0.0F};
}

/// A light coming from the left and above, so a floor point near the wall marches into it.
[[nodiscard]] Vector3UVE WallwardLightDirectionUVE() {
    const Vector3UVE raw{-0.25F, 1.0F, 0.0F};
    return raw * (1.0F / std::sqrt(Math::LengthSquaredUVE(raw)));
}

TEST(ContactShadowSettingsUVETest, DefaultsAreOffAndConservative) {
    const ContactShadowSettingsUVE settings;
    EXPECT_FALSE(settings.enabledUVE);
    EXPECT_GT(settings.lengthMetersUVE, 0.0F);
    EXPECT_GT(settings.thicknessMetersUVE, 0.0F);
    EXPECT_GE(settings.strengthUVE, 0.0F);
    EXPECT_LE(settings.strengthUVE, 1.0F);
    EXPECT_GE(settings.stepCountUVE, kContactShadowMinStepCountUVE);
    EXPECT_LE(settings.stepCountUVE, kContactShadowMaxStepCountUVE);
    // The defaults must survive their own sanitizer, or every frame would silently differ from the
    // settings the project authored.
    const ContactShadowSettingsUVE sanitized = SanitizeContactShadowSettingsUVE(settings);
    EXPECT_EQ(sanitized.lengthMetersUVE, settings.lengthMetersUVE);
    EXPECT_EQ(sanitized.thicknessMetersUVE, settings.thicknessMetersUVE);
    EXPECT_EQ(sanitized.strengthUVE, settings.strengthUVE);
    EXPECT_EQ(sanitized.stepCountUVE, settings.stepCountUVE);
}

TEST(ContactShadowSettingsUVETest, SanitizeClampsEveryFieldIntoItsDocumentedRange) {
    ContactShadowSettingsUVE extreme;
    extreme.lengthMetersUVE = 1000.0F;
    extreme.thicknessMetersUVE = 1000.0F;
    extreme.strengthUVE = 4.0F;
    extreme.stepCountUVE = 1000U;

    const ContactShadowSettingsUVE clamped = SanitizeContactShadowSettingsUVE(extreme);
    EXPECT_EQ(clamped.lengthMetersUVE, kContactShadowMaxLengthMetersUVE);
    EXPECT_EQ(clamped.thicknessMetersUVE, kContactShadowMaxLengthMetersUVE);
    EXPECT_EQ(clamped.strengthUVE, 1.0F);
    EXPECT_EQ(clamped.stepCountUVE, kContactShadowMaxStepCountUVE);

    ContactShadowSettingsUVE degenerate;
    degenerate.lengthMetersUVE = 0.0F;
    degenerate.thicknessMetersUVE = -1.0F;
    degenerate.strengthUVE = -1.0F;
    degenerate.stepCountUVE = 0U;

    const ContactShadowSettingsUVE floored = SanitizeContactShadowSettingsUVE(degenerate);
    EXPECT_EQ(floored.lengthMetersUVE, kContactShadowMinLengthMetersUVE);
    EXPECT_EQ(floored.thicknessMetersUVE, 0.0F);
    EXPECT_EQ(floored.strengthUVE, 0.0F);
    EXPECT_EQ(floored.stepCountUVE, kContactShadowMinStepCountUVE);
}

TEST(ContactShadowSettingsUVETest, SanitizeTreatsNonFiniteValuesAsTheFloor) {
    // A project file is external input: a NaN length must not become a NaN ray, which would make
    // every comparison false and silently disable the pass in a way nobody could debug.
    ContactShadowSettingsUVE settings;
    settings.lengthMetersUVE = std::numeric_limits<float>::quiet_NaN();
    settings.thicknessMetersUVE = std::numeric_limits<float>::quiet_NaN();
    settings.strengthUVE = std::numeric_limits<float>::quiet_NaN();

    const ContactShadowSettingsUVE sanitized = SanitizeContactShadowSettingsUVE(settings);
    EXPECT_TRUE(std::isfinite(sanitized.lengthMetersUVE));
    EXPECT_TRUE(std::isfinite(sanitized.thicknessMetersUVE));
    EXPECT_TRUE(std::isfinite(sanitized.strengthUVE));
    EXPECT_EQ(sanitized.lengthMetersUVE, kContactShadowMinLengthMetersUVE);
    EXPECT_EQ(sanitized.thicknessMetersUVE, 0.0F);
    EXPECT_EQ(sanitized.strengthUVE, 0.0F);
}

TEST(ContactShadowSettingsUVETest, RayStepsAreStrictlyIncreasingAndEndAtTheConfiguredLength) {
    ContactShadowSettingsUVE settings;
    settings.lengthMetersUVE = 0.2F;
    settings.stepCountUVE = 8U;

    std::array<float, kTestStepCapacityUVE> steps{};
    const std::uint32_t count = FillContactShadowRayStepsUVE(settings, steps.data(), kTestStepCapacityUVE);
    ASSERT_EQ(count, 8U);
    for (std::uint32_t index = 1U; index < count; ++index) {
        EXPECT_GT(steps[index], steps[index - 1U]) << "index " << index;
    }
    EXPECT_FLOAT_EQ(steps[count - 1U], 0.2F);
    // Quadratic schedule: the first sample is one sixty-fourth of the ray out, which is where the
    // contact this pass exists to find actually is.
    EXPECT_NEAR(steps[0], 0.2F / 64.0F, 1.0e-6F);
    EXPECT_LT(steps[0], 0.01F);
}

TEST(ContactShadowSettingsUVETest, RayStepsRespectTheCallersCapacity) {
    ContactShadowSettingsUVE settings;
    settings.stepCountUVE = 12U;
    std::array<float, kTestStepCapacityUVE> steps{};
    EXPECT_EQ(FillContactShadowRayStepsUVE(settings, steps.data(), 5U), 5U);
    EXPECT_EQ(FillContactShadowRayStepsUVE(settings, steps.data(), 0U), 0U);
    EXPECT_EQ(FillContactShadowRayStepsUVE(settings, nullptr, 4U), 0U);
}

TEST(ContactShadowSettingsUVETest, RayStepsAreClampedLikeTheSettingsThatProducedThem) {
    // An unsanitized step count must not walk off the end of a 16-entry array: the fill clamps first
    // and then honours the capacity, in that order.
    ContactShadowSettingsUVE settings;
    settings.stepCountUVE = 1000U;
    std::array<float, kTestStepCapacityUVE> steps{};
    EXPECT_EQ(FillContactShadowRayStepsUVE(settings, steps.data(), kTestStepCapacityUVE),
              kContactShadowMaxStepCountUVE);
}

TEST(ContactShadowSettingsUVETest, OcclusionRuleNeedsTheSampleInFrontWithinThickness) {
    const float rayZ = -5.0F;
    // Behind the ray point: the sample is farther from the camera than where the light would reach.
    EXPECT_FALSE(IsContactShadowOccludedUVE(rayZ, rayZ - 0.05F, 0.1F, 0.005F));
    // In front, comfortably inside the thickness window.
    EXPECT_TRUE(IsContactShadowOccludedUVE(rayZ, rayZ + 0.02F, 0.1F, 0.005F));
    // In front, but farther away than the thickness allows: a wall behind the ray is not contact.
    EXPECT_FALSE(IsContactShadowOccludedUVE(rayZ, rayZ + 0.5F, 0.1F, 0.005F));
    // In front, but inside the depth bias: the surface the ray started from, not an occluder.
    EXPECT_FALSE(IsContactShadowOccludedUVE(rayZ, rayZ + 0.002F, 0.1F, 0.005F));
}

TEST(ContactShadowSettingsUVETest, VisibilityIsFullWhenNothingWasObserved) {
    // "No samples tested" and "every sample clean" must both read as lit; only a real observation of
    // occlusion may darken anything.
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(0U, 0U, 1.0F), 1.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(0U, 8U, 1.0F), 1.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(8U, 8U, 0.8F), 0.2F);
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(4U, 8U, 0.8F), 0.6F);
    // Strength is clamped, so an authored 2.0 cannot invert the term into a light ADDITION.
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(8U, 8U, 2.0F), 0.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibilityUVE(8U, 8U, -1.0F), 1.0F);
}

TEST(ContactShadowMarchUVETest, DisabledSettingsMarchNothingAndStayFullyLit) {
    const Matrix4x4UVE projection = MakeTestProjectionUVE();
    Matrix4x4UVE inverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, inverseProjection));
    const SyntheticSceneUVE scene{projection, inverseProjection};

    ContactShadowSettingsUVE settings;
    settings.enabledUVE = false;
    const ContactShadowRayResultUVE result = MarchContactShadowRayUVE(
        settings, Vector3UVE{-0.4F, -1.0F, -5.0F}, WallwardLightDirectionUVE(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUvUVE(u, v); });
    EXPECT_FLOAT_EQ(result.visibilityUVE, 1.0F);
    EXPECT_EQ(result.testedStepsUVE, 0U);
    EXPECT_EQ(result.occludedStepsUVE, 0U);
}

TEST(ContactShadowMarchUVETest, FloorPointUnderAnOpenSkyIsNotShadowed) {
    const Matrix4x4UVE projection = MakeTestProjectionUVE();
    Matrix4x4UVE inverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, inverseProjection));
    const SyntheticSceneUVE scene{projection, inverseProjection};

    ContactShadowSettingsUVE settings;
    settings.enabledUVE = true;
    const ContactShadowRayResultUVE result = MarchContactShadowRayUVE(
        settings, Vector3UVE{0.5F, -1.0F, -6.0F}, UpwardLightDirectionUVE(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUvUVE(u, v); });
    // Steps still get tested - the ray re-enters pixels that contain the floor - so this asserts the
    // occlusion rule itself, not merely that the march returned early.
    EXPECT_GT(result.testedStepsUVE, 0U);
    EXPECT_EQ(result.occludedStepsUVE, 0U);
    EXPECT_FLOAT_EQ(result.visibilityUVE, 1.0F);
}

TEST(ContactShadowMarchUVETest, FloorPointBesideTheWallIsDarkenedByIt) {
    const Matrix4x4UVE projection = MakeTestProjectionUVE();
    Matrix4x4UVE inverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, inverseProjection));
    const SyntheticSceneUVE scene{projection, inverseProjection};

    ContactShadowSettingsUVE settings;
    settings.enabledUVE = true;
    settings.lengthMetersUVE = 0.3F;
    settings.thicknessMetersUVE = 0.1F;
    settings.strengthUVE = 0.8F;
    settings.stepCountUVE = 16U;

    // One centimetre off the wall, lit from its side: the march immediately leaves the floor and
    // its samples land on the wall's screen footprint, which is nearer the camera than the ray.
    const ContactShadowRayResultUVE result = MarchContactShadowRayUVE(
        settings, Vector3UVE{-0.79F, -0.999F, -6.0F}, WallwardLightDirectionUVE(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUvUVE(u, v); });
    EXPECT_GT(result.testedStepsUVE, 0U);
    EXPECT_GT(result.occludedStepsUVE, 0U);
    EXPECT_LT(result.visibilityUVE, 1.0F);
    // Bounded by the strength: the pass refines a shadow, it never blacks the surface out.
    EXPECT_GE(result.visibilityUVE, 1.0F - settings.strengthUVE - 1.0e-5F);
}

TEST(ContactShadowMarchUVETest, ThicknessIsWhatSeparatesContactFromMerelyNearbyGeometry) {
    const Matrix4x4UVE projection = MakeTestProjectionUVE();
    Matrix4x4UVE inverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, inverseProjection));
    const SyntheticSceneUVE scene{projection, inverseProjection};
    const auto depthAtUv = [&scene](const float u, const float v) { return scene.RawDepthAtUvUVE(u, v); };

    ContactShadowSettingsUVE settings;
    settings.enabledUVE = true;
    settings.lengthMetersUVE = 0.3F;
    settings.strengthUVE = 0.8F;
    settings.stepCountUVE = 16U;

    // Same geometry and same ray, two thicknesses. The wall still stands in front of the ray, but
    // with a hair-thin tolerance it is "nearby geometry" rather than "touching geometry".
    settings.thicknessMetersUVE = 0.1F;
    const ContactShadowRayResultUVE closeEnough = MarchContactShadowRayUVE(
        settings, Vector3UVE{-0.79F, -0.999F, -6.0F}, WallwardLightDirectionUVE(), projection, inverseProjection,
        depthAtUv);
    EXPECT_GT(closeEnough.occludedStepsUVE, 0U);

    settings.thicknessMetersUVE = 1.0e-6F;
    const ContactShadowRayResultUVE tooThin = MarchContactShadowRayUVE(
        settings, Vector3UVE{-0.79F, -0.999F, -6.0F}, WallwardLightDirectionUVE(), projection, inverseProjection,
        depthAtUv);
    EXPECT_EQ(tooThin.occludedStepsUVE, 0U);
    EXPECT_FLOAT_EQ(tooThin.visibilityUVE, 1.0F);
    // Both marches tested the same pixels, so the difference really is the rule and not a skipped ray.
    EXPECT_EQ(tooThin.testedStepsUVE, closeEnough.testedStepsUVE);
}

TEST(ContactShadowMarchUVETest, DegenerateLightDirectionLeavesTheSurfaceLit) {
    const Matrix4x4UVE projection = MakeTestProjectionUVE();
    Matrix4x4UVE inverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, inverseProjection));
    const SyntheticSceneUVE scene{projection, inverseProjection};

    ContactShadowSettingsUVE settings;
    settings.enabledUVE = true;
    const ContactShadowRayResultUVE result = MarchContactShadowRayUVE(
        settings, Vector3UVE{-0.79F, -0.999F, -6.0F}, Vector3UVE{0.0F, 0.0F, 0.0F}, projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUvUVE(u, v); });
    EXPECT_FLOAT_EQ(result.visibilityUVE, 1.0F);
    EXPECT_EQ(result.testedStepsUVE, 0U);
}

} // namespace
} // namespace UVE::Render::Tests
