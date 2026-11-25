// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "contact_shadows.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "matrix4x4.h"
#include "vector3.h"

namespace UVE::Render::Tests {
namespace {

using Math::Matrix4x4;
using Math::Vector3;

constexpr float kTestFovYRadians = 1.0471976F; // 60 degrees
constexpr std::uint32_t kTestStepCapacity = 32U;

/// A camera at the origin looking down -Z, which is the convention the whole renderer uses.
[[nodiscard]] Matrix4x4 MakeTestProjection() {
    return Matrix4x4::Perspective(kTestFovYRadians, 16.0F / 9.0F, 0.1F, 100.0F);
}

/// Converts a view-space position into the raw depth-buffer value the projection would write, using
/// the same [0,1] clip-space convention the shader assumes (depth = 0.5 * ndc.z + 0.5).
[[nodiscard]] float RawDepthOfViewPosition(const Matrix4x4& projection, const Vector3& viewPosition) {
    const Detail::ContactShadowClipPosition clip = Detail::ProjectContactShadowPoint(projection, viewPosition);
    return (clip.z / clip.w) * 0.5F + 0.5F;
}

/// An analytical scene: a floor at y = -1 and a wall at x = -0.8, both infinite half-spaces inside a
/// bounded region, with the depth lookup returning whichever surface the camera would actually see
/// at that pixel. This is what makes the march testable: every expected occlusion follows from
/// geometry rather than from replaying the implementation.
class SyntheticScene final {
public:
    SyntheticScene(const Matrix4x4& projection, const Matrix4x4& inverseProjection)
        : m_projection(projection), m_inverseProjection(inverseProjection) {}

    [[nodiscard]] float RawDepthAtUv(const float u, const float v) const {
        // Reconstruct the pixel's view-space ray. Depth 0.99 rather than 1.0 keeps the
        // inverse-projected point finite; any point along the ray gives the same direction.
        const Vector3 rayPoint =
            Detail::ReconstructContactShadowViewPosition(m_inverseProjection, u, v, 0.99F);
        const float rayPointLengthSquared = Math::LengthSquared(rayPoint);
        if (!(rayPointLengthSquared > 1.0e-12F)) {
            return 1.0F;
        }
        const Vector3 rayDirection = rayPoint * (1.0F / std::sqrt(rayPointLengthSquared));

        float nearestDistance = std::numeric_limits<float>::max();
        // Floor: y = -1.
        if (rayDirection.y < -1.0e-6F) {
            const float distance = -1.0F / rayDirection.y;
            if (IsInsideSceneBounds(rayDirection * distance) && distance > 0.0F) {
                nearestDistance = distance;
            }
        }
        // Wall: x = -0.8.
        if (rayDirection.x < -1.0e-6F) {
            const float distance = -0.8F / rayDirection.x;
            if (IsInsideSceneBounds(rayDirection * distance) && distance > 0.0F &&
                distance < nearestDistance) {
                nearestDistance = distance;
            }
        }
        if (!(nearestDistance < std::numeric_limits<float>::max())) {
            return 1.0F;
        }
        return RawDepthOfViewPosition(m_projection, rayDirection * nearestDistance);
    }

private:
    static bool IsInsideSceneBounds(const Vector3& point) {
        return std::fabs(point.x) <= 8.0F && std::fabs(point.y) <= 6.0F && point.z <= -0.5F &&
               point.z >= -40.0F;
    }

    const Matrix4x4& m_projection;
    const Matrix4x4& m_inverseProjection;
};

/// The straight-up light direction: a floor point can never be shadowed by anything in this scene,
/// because both surfaces are below or beside the ray.
[[nodiscard]] Vector3 UpwardLightDirection() {
    return Vector3{0.0F, 1.0F, 0.0F};
}

/// A light coming from the left and above, so a floor point near the wall marches into it.
[[nodiscard]] Vector3 WallwardLightDirection() {
    const Vector3 raw{-0.25F, 1.0F, 0.0F};
    return raw * (1.0F / std::sqrt(Math::LengthSquared(raw)));
}

TEST(ContactShadowSettingsTest, DefaultsAreOffAndConservative) {
    const ContactShadowSettings settings;
    EXPECT_FALSE(settings.enabled);
    EXPECT_GT(settings.lengthMeters, 0.0F);
    EXPECT_GT(settings.thicknessMeters, 0.0F);
    EXPECT_GE(settings.strength, 0.0F);
    EXPECT_LE(settings.strength, 1.0F);
    EXPECT_GE(settings.stepCount, kContactShadowMinStepCount);
    EXPECT_LE(settings.stepCount, kContactShadowMaxStepCount);
    // The defaults must survive their own sanitizer, or every frame would silently differ from the
    // settings the project authored.
    const ContactShadowSettings sanitized = SanitizeContactShadowSettings(settings);
    EXPECT_EQ(sanitized.lengthMeters, settings.lengthMeters);
    EXPECT_EQ(sanitized.thicknessMeters, settings.thicknessMeters);
    EXPECT_EQ(sanitized.strength, settings.strength);
    EXPECT_EQ(sanitized.stepCount, settings.stepCount);
}

TEST(ContactShadowSettingsTest, SanitizeClampsEveryFieldIntoItsDocumentedRange) {
    ContactShadowSettings extreme;
    extreme.lengthMeters = 1000.0F;
    extreme.thicknessMeters = 1000.0F;
    extreme.strength = 4.0F;
    extreme.stepCount = 1000U;

    const ContactShadowSettings clamped = SanitizeContactShadowSettings(extreme);
    EXPECT_EQ(clamped.lengthMeters, kContactShadowMaxLengthMeters);
    EXPECT_EQ(clamped.thicknessMeters, kContactShadowMaxLengthMeters);
    EXPECT_EQ(clamped.strength, 1.0F);
    EXPECT_EQ(clamped.stepCount, kContactShadowMaxStepCount);

    ContactShadowSettings degenerate;
    degenerate.lengthMeters = 0.0F;
    degenerate.thicknessMeters = -1.0F;
    degenerate.strength = -1.0F;
    degenerate.stepCount = 0U;

    const ContactShadowSettings floored = SanitizeContactShadowSettings(degenerate);
    EXPECT_EQ(floored.lengthMeters, kContactShadowMinLengthMeters);
    EXPECT_EQ(floored.thicknessMeters, 0.0F);
    EXPECT_EQ(floored.strength, 0.0F);
    EXPECT_EQ(floored.stepCount, kContactShadowMinStepCount);
}

TEST(ContactShadowSettingsTest, SanitizeTreatsNonFiniteValuesAsTheFloor) {
    // A project file is external input: a NaN length must not become a NaN ray, which would make
    // every comparison false and silently disable the pass in a way nobody could debug.
    ContactShadowSettings settings;
    settings.lengthMeters = std::numeric_limits<float>::quiet_NaN();
    settings.thicknessMeters = std::numeric_limits<float>::quiet_NaN();
    settings.strength = std::numeric_limits<float>::quiet_NaN();

    const ContactShadowSettings sanitized = SanitizeContactShadowSettings(settings);
    EXPECT_TRUE(std::isfinite(sanitized.lengthMeters));
    EXPECT_TRUE(std::isfinite(sanitized.thicknessMeters));
    EXPECT_TRUE(std::isfinite(sanitized.strength));
    EXPECT_EQ(sanitized.lengthMeters, kContactShadowMinLengthMeters);
    EXPECT_EQ(sanitized.thicknessMeters, 0.0F);
    EXPECT_EQ(sanitized.strength, 0.0F);
}

TEST(ContactShadowSettingsTest, RayStepsAreStrictlyIncreasingAndEndAtTheConfiguredLength) {
    ContactShadowSettings settings;
    settings.lengthMeters = 0.2F;
    settings.stepCount = 8U;

    std::array<float, kTestStepCapacity> steps{};
    const std::uint32_t count = FillContactShadowRaySteps(settings, steps.data(), kTestStepCapacity);
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

TEST(ContactShadowSettingsTest, RayStepsRespectTheCallersCapacity) {
    ContactShadowSettings settings;
    settings.stepCount = 12U;
    std::array<float, kTestStepCapacity> steps{};
    EXPECT_EQ(FillContactShadowRaySteps(settings, steps.data(), 5U), 5U);
    EXPECT_EQ(FillContactShadowRaySteps(settings, steps.data(), 0U), 0U);
    EXPECT_EQ(FillContactShadowRaySteps(settings, nullptr, 4U), 0U);
}

TEST(ContactShadowSettingsTest, RayStepsAreClampedLikeTheSettingsThatProducedThem) {
    // An unsanitized step count must not walk off the end of a 16-entry array: the fill clamps first
    // and then honours the capacity, in that order.
    ContactShadowSettings settings;
    settings.stepCount = 1000U;
    std::array<float, kTestStepCapacity> steps{};
    EXPECT_EQ(FillContactShadowRaySteps(settings, steps.data(), kTestStepCapacity),
              kContactShadowMaxStepCount);
}

TEST(ContactShadowSettingsTest, OcclusionRuleNeedsTheSampleInFrontWithinThickness) {
    const float rayZ = -5.0F;
    // Behind the ray point: the sample is farther from the camera than where the light would reach.
    EXPECT_FALSE(IsContactShadowOccluded(rayZ, rayZ - 0.05F, 0.1F, 0.005F));
    // In front, comfortably inside the thickness window.
    EXPECT_TRUE(IsContactShadowOccluded(rayZ, rayZ + 0.02F, 0.1F, 0.005F));
    // In front, but farther away than the thickness allows: a wall behind the ray is not contact.
    EXPECT_FALSE(IsContactShadowOccluded(rayZ, rayZ + 0.5F, 0.1F, 0.005F));
    // In front, but inside the depth bias: the surface the ray started from, not an occluder.
    EXPECT_FALSE(IsContactShadowOccluded(rayZ, rayZ + 0.002F, 0.1F, 0.005F));
}

TEST(ContactShadowSettingsTest, VisibilityIsFullWhenNothingWasObserved) {
    // "No samples tested" and "every sample clean" must both read as lit; only a real observation of
    // occlusion may darken anything.
    EXPECT_FLOAT_EQ(ContactShadowVisibility(0U, 0U, 1.0F), 1.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibility(0U, 8U, 1.0F), 1.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibility(8U, 8U, 0.8F), 0.2F);
    EXPECT_FLOAT_EQ(ContactShadowVisibility(4U, 8U, 0.8F), 0.6F);
    // Strength is clamped, so an authored 2.0 cannot invert the term into a light ADDITION.
    EXPECT_FLOAT_EQ(ContactShadowVisibility(8U, 8U, 2.0F), 0.0F);
    EXPECT_FLOAT_EQ(ContactShadowVisibility(8U, 8U, -1.0F), 1.0F);
}

TEST(ContactShadowMarchTest, DisabledSettingsMarchNothingAndStayFullyLit) {
    const Matrix4x4 projection = MakeTestProjection();
    Matrix4x4 inverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, inverseProjection));
    const SyntheticScene scene{projection, inverseProjection};

    ContactShadowSettings settings;
    settings.enabled = false;
    const ContactShadowRayResult result = MarchContactShadowRay(
        settings, Vector3{-0.4F, -1.0F, -5.0F}, WallwardLightDirection(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUv(u, v); });
    EXPECT_FLOAT_EQ(result.visibility, 1.0F);
    EXPECT_EQ(result.testedSteps, 0U);
    EXPECT_EQ(result.occludedSteps, 0U);
}

TEST(ContactShadowMarchTest, FloorPointUnderAnOpenSkyIsNotShadowed) {
    const Matrix4x4 projection = MakeTestProjection();
    Matrix4x4 inverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, inverseProjection));
    const SyntheticScene scene{projection, inverseProjection};

    ContactShadowSettings settings;
    settings.enabled = true;
    const ContactShadowRayResult result = MarchContactShadowRay(
        settings, Vector3{0.5F, -1.0F, -6.0F}, UpwardLightDirection(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUv(u, v); });
    // Steps still get tested - the ray re-enters pixels that contain the floor - so this asserts the
    // occlusion rule itself, not merely that the march returned early.
    EXPECT_GT(result.testedSteps, 0U);
    EXPECT_EQ(result.occludedSteps, 0U);
    EXPECT_FLOAT_EQ(result.visibility, 1.0F);
}

TEST(ContactShadowMarchTest, FloorPointBesideTheWallIsDarkenedByIt) {
    const Matrix4x4 projection = MakeTestProjection();
    Matrix4x4 inverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, inverseProjection));
    const SyntheticScene scene{projection, inverseProjection};

    ContactShadowSettings settings;
    settings.enabled = true;
    settings.lengthMeters = 0.3F;
    settings.thicknessMeters = 0.1F;
    settings.strength = 0.8F;
    settings.stepCount = 16U;

    // One centimetre off the wall, lit from its side: the march immediately leaves the floor and
    // its samples land on the wall's screen footprint, which is nearer the camera than the ray.
    const ContactShadowRayResult result = MarchContactShadowRay(
        settings, Vector3{-0.79F, -0.999F, -6.0F}, WallwardLightDirection(), projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUv(u, v); });
    EXPECT_GT(result.testedSteps, 0U);
    EXPECT_GT(result.occludedSteps, 0U);
    EXPECT_LT(result.visibility, 1.0F);
    // Bounded by the strength: the pass refines a shadow, it never blacks the surface out.
    EXPECT_GE(result.visibility, 1.0F - settings.strength - 1.0e-5F);
}

TEST(ContactShadowMarchTest, ThicknessIsWhatSeparatesContactFromMerelyNearbyGeometry) {
    const Matrix4x4 projection = MakeTestProjection();
    Matrix4x4 inverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, inverseProjection));
    const SyntheticScene scene{projection, inverseProjection};
    const auto depthAtUv = [&scene](const float u, const float v) { return scene.RawDepthAtUv(u, v); };

    ContactShadowSettings settings;
    settings.enabled = true;
    settings.lengthMeters = 0.3F;
    settings.strength = 0.8F;
    settings.stepCount = 16U;

    // Same geometry and same ray, two thicknesses. The wall still stands in front of the ray, but
    // with a hair-thin tolerance it is "nearby geometry" rather than "touching geometry".
    settings.thicknessMeters = 0.1F;
    const ContactShadowRayResult closeEnough = MarchContactShadowRay(
        settings, Vector3{-0.79F, -0.999F, -6.0F}, WallwardLightDirection(), projection, inverseProjection,
        depthAtUv);
    EXPECT_GT(closeEnough.occludedSteps, 0U);

    settings.thicknessMeters = 1.0e-6F;
    const ContactShadowRayResult tooThin = MarchContactShadowRay(
        settings, Vector3{-0.79F, -0.999F, -6.0F}, WallwardLightDirection(), projection, inverseProjection,
        depthAtUv);
    EXPECT_EQ(tooThin.occludedSteps, 0U);
    EXPECT_FLOAT_EQ(tooThin.visibility, 1.0F);
    // Both marches tested the same pixels, so the difference really is the rule and not a skipped ray.
    EXPECT_EQ(tooThin.testedSteps, closeEnough.testedSteps);
}

TEST(ContactShadowMarchTest, DegenerateLightDirectionLeavesTheSurfaceLit) {
    const Matrix4x4 projection = MakeTestProjection();
    Matrix4x4 inverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, inverseProjection));
    const SyntheticScene scene{projection, inverseProjection};

    ContactShadowSettings settings;
    settings.enabled = true;
    const ContactShadowRayResult result = MarchContactShadowRay(
        settings, Vector3{-0.79F, -0.999F, -6.0F}, Vector3{0.0F, 0.0F, 0.0F}, projection, inverseProjection,
        [&scene](const float u, const float v) { return scene.RawDepthAtUv(u, v); });
    EXPECT_FLOAT_EQ(result.visibility, 1.0F);
    EXPECT_EQ(result.testedSteps, 0U);
}

} // namespace
} // namespace UVE::Render::Tests
