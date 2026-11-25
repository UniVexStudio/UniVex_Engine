// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "light_shadow_planner.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "frustum.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"
#include "vector4.h"
#include "i_light_system.h"
#include "shadow_atlas_manager.h"

namespace UVE::Render::Tests {
namespace {

constexpr float kPi = 3.14159265358979323846F;

/// A camera at the origin looking down -Z, the engine's camera convention. Everything below is
/// framed in that space so "in front of the camera" needs no mental rotation.
[[nodiscard]] Math::Frustum MakeViewFrustum() {
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(60.0F * (kPi / 180.0F), 1.0F, 0.1F, 100.0F);
    const Math::Matrix4x4 view = Math::Matrix4x4::ViewFromPositionAndRotation(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    return Math::Frustum::FromViewProjection(projection * view);
}

[[nodiscard]] LightData MakeSpotLight(const Math::Vector3& position = Math::Vector3{0.0F, 0.0F, -5.0F},
                                           const float range = 10.0F,
                                           const float spotAngleDegrees = 30.0F) {
    LightData light{};
    light.type = Scene::LightType::Spot;
    light.position = position;
    // Default quaternion looks down -Z, matching the spot cone the matrix builder projects.
    light.direction = Math::Vector3{0.0F, 0.0F, -1.0F};
    light.rotation = Math::Quaternion{};
    light.intensity = 3.0F;
    light.range = range;
    light.spotAngleDegrees = spotAngleDegrees;
    light.castsShadows = true;
    return light;
}

[[nodiscard]] LightData MakePointLight(const Math::Vector3& position = Math::Vector3{0.0F, 0.0F, -5.0F},
                                           const float range = 12.0F) {
    LightData light{};
    light.type = Scene::LightType::Point;
    light.position = position;
    // A point light has no cone, so direction/rotation are irrelevant to its shadows - the cube
    // faces come from a fixed table. Deliberately left at the identity rotation to prove that.
    light.direction = Math::Vector3{0.0F, 0.0F, -1.0F};
    light.rotation = Math::Quaternion{};
    light.intensity = 4.0F;
    light.range = range;
    // 0 is not a valid cone and a spot-only validation would reject it; a point light must not care.
    light.spotAngleDegrees = 0.0F;
    light.castsShadows = true;
    return light;
}

/// Full clip-space transform (x, y, z and w), which is what the shadow path actually needs:
/// Matrix4x4 has no matrix*vec4 operator, and TransformPoint has already divided by w by the
/// time it returns.
[[nodiscard]] Math::Vector4 TransformClip(const Math::Matrix4x4& matrix,
                                                const Math::Vector3& point) noexcept {
    const float x = point.x;
    const float y = point.y;
    const float z = point.z;
    Math::Vector4 result{};
    result.x = matrix.m[0][0] * x + matrix.m[0][1] * y + matrix.m[0][2] * z + matrix.m[0][3];
    result.y = matrix.m[1][0] * x + matrix.m[1][1] * y + matrix.m[1][2] * z + matrix.m[1][3];
    result.z = matrix.m[2][0] * x + matrix.m[2][1] * y + matrix.m[2][2] * z + matrix.m[2][3];
    result.w = matrix.m[3][0] * x + matrix.m[3][1] * y + matrix.m[3][2] * z + matrix.m[3][3];
    return result;
}

[[nodiscard]] LightList MakeLightList(std::initializer_list<LightData> lights) {
    LightList list;
    for (const LightData& light : lights) {
        list.PushBack(light);
    }
    return list;
}

// ---------------------------------------------------------------------------------------------
// Eligibility
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlanner, Candidate_AcceptsASpotLightInsideTheViewFrustum) {
    const Math::Frustum frustum = MakeViewFrustum();
    EXPECT_TRUE(IsSpotLightShadowCandidate(MakeSpotLight(), frustum));
}

TEST(LightShadowPlanner, Candidate_RejectsLightsThatAreNotSpotLights) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData point = MakeSpotLight();
    point.type = Scene::LightType::Point;
    EXPECT_FALSE(IsSpotLightShadowCandidate(point, frustum));
    LightData directional = MakeSpotLight();
    directional.type = Scene::LightType::Directional;
    EXPECT_FALSE(IsSpotLightShadowCandidate(directional, frustum));
}

TEST(LightShadowPlanner, Candidate_RejectsZeroIntensityAndDisabledShadowFlag) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData unlit = MakeSpotLight();
    unlit.intensity = 0.0F;
    EXPECT_FALSE(IsSpotLightShadowCandidate(unlit, frustum));

    LightData noShadow = MakeSpotLight();
    noShadow.castsShadows = false;
    EXPECT_FALSE(IsSpotLightShadowCandidate(noShadow, frustum));
}

TEST(LightShadowPlanner, Candidate_RejectsTheEditorInternalLight) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData headlight = MakeSpotLight();
    headlight.isEditorInternal = true;
    EXPECT_FALSE(IsSpotLightShadowCandidate(headlight, frustum));
}

TEST(LightShadowPlanner, Candidate_RejectsRangesAndConeAnglesTheShaderCannotRepresent) {
    const Math::Frustum frustum = MakeViewFrustum();
    for (const float range : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
        LightData light = MakeSpotLight(Math::Vector3{0.0F, 0.0F, -5.0F}, range);
        EXPECT_FALSE(IsSpotLightShadowCandidate(light, frustum)) << "range " << range;
    }
    // The shader's cone falloff inverts past 90 degrees and collapses to a line at or below 1.
    for (const float angle : {0.0F, 0.5F, 90.0F, 120.0F}) {
        LightData light = MakeSpotLight(Math::Vector3{0.0F, 0.0F, -5.0F}, 10.0F, angle);
        EXPECT_FALSE(IsSpotLightShadowCandidate(light, frustum)) << "angle " << angle;
    }
}

TEST(LightShadowPlanner, Candidate_RejectsNonFiniteTransforms) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData light = MakeSpotLight();
    light.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSpotLightShadowCandidate(light, frustum));

    LightData zeroRotation = MakeSpotLight();
    zeroRotation.rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(IsSpotLightShadowCandidate(zeroRotation, frustum));
}

TEST(LightShadowPlanner, Candidate_RejectsALightWhoseInfluenceSphereMissesTheView) {
    // Behind the camera and further away than its own reach, so nothing it lights can be seen.
    const Math::Frustum frustum = MakeViewFrustum();
    EXPECT_FALSE(IsSpotLightShadowCandidate(
        MakeSpotLight(Math::Vector3{0.0F, 0.0F, 40.0F}, 5.0F), frustum));
}

TEST(LightShadowPlanner, CountShadowTiles_CountsOnlyEligibleLights) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData point = MakeSpotLight();
    point.type = Scene::LightType::Point;
    LightData disabled = MakeSpotLight();
    disabled.castsShadows = false;
    // Two spot tiles plus the point light's six.
    const LightList lights = MakeLightList({MakeSpotLight(), point, disabled, MakeSpotLight()});
    EXPECT_EQ(CountShadowTiles(lights, frustum), 8U);
}

// ---------------------------------------------------------------------------------------------
// Tile budget
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlanner, TileResolution_IsZeroWithoutCasters) {
    EXPECT_EQ(ResolveShadowTileResolution(1024U, 0U), 0U);
}

TEST(LightShadowPlanner, TileResolution_RoundsRequestsDownToAPowerOfTwoInsideTheAtlas) {
    EXPECT_EQ(ResolveShadowTileResolution(1024U, 1U), 1024U);
    EXPECT_EQ(ResolveShadowTileResolution(1000U, 1U), 512U);
    EXPECT_EQ(ResolveShadowTileResolution(1U, 1U), kShadowAtlasMinTileResolution);
    EXPECT_EQ(ResolveShadowTileResolution(999999U, 1U), kShadowAtlasMaxTileResolution);
}

TEST(LightShadowPlanner, TileResolution_HalvesUntilEveryCasterFits) {
    // 4096x4096 holds exactly one 4096 tile, so two casters must halve once.
    EXPECT_EQ(ResolveShadowTileResolution(4096U, 2U), 2048U);
    // Four casters fit in 2048 (2x2).
    EXPECT_EQ(ResolveShadowTileResolution(4096U, 4U), 2048U);
    // The 128 floor deliberately leaves room for far more than kMaxShadowedLights squares.
    EXPECT_EQ(ResolveShadowTileResolution(128U, kMaxShadowedLights),
              kShadowAtlasMinTileResolution);
}

// ---------------------------------------------------------------------------------------------
// Projection / bias math shared with the shader
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlanner, NearPlane_ScalesWithRangeAndStaysBounded) {
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlane(1.0F), 0.02F);
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlane(10.0F), 0.2F);
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlane(1000.0F), 1.0F);
    // Degenerate input must still yield a usable near plane rather than a zero or a NaN.
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlane(0.0F), 0.02F);
    EXPECT_TRUE(std::isfinite(ComputeLightShadowNearPlane(-5.0F)));
}

TEST(LightShadowPlanner, LinearDepth_RecoversTheProjectionEndpoints) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    EXPECT_NEAR(ShadowLinearDepth(nearPlane, farPlane, 0.0F), nearPlane, 1.0e-4F);
    EXPECT_NEAR(ShadowLinearDepth(nearPlane, farPlane, 1.0F), farPlane, 1.0e-3F);
}

TEST(LightShadowPlanner, LinearDepth_AgreesWithTheProjectionMatrixAtEveryDepth) {
    // The closed form must be the inverse of what Perspective actually writes, or the shader's
    // world-space bias is calibrated against a projection that does not exist.
    constexpr float nearPlane = 0.25F;
    constexpr float farPlane = 25.0F;
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(90.0F * (kPi / 180.0F), 1.0F, nearPlane, farPlane);
    for (const float viewDepth : {0.3F, 1.0F, 5.0F, 12.0F, 24.0F}) {
        const Math::Vector4 clip = TransformClip(projection, Math::Vector3{0.0F, 0.0F, -viewDepth});
        ASSERT_GT(clip.w, 0.0F) << "at view depth " << viewDepth;
        const float normalizedDepth = clip.z / clip.w;
        EXPECT_NEAR(ShadowLinearDepth(nearPlane, farPlane, normalizedDepth), viewDepth,
                    viewDepth * 1.0e-3F)
            << "at view depth " << viewDepth;
    }
}

TEST(LightShadowPlanner, DepthSlope_MatchesTheFiniteDifferenceOfLinearDepth) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float step = 1.0e-4F;
    for (const float depth : {0.05F, 0.3F, 0.6F, 0.95F}) {
        const float lower = ShadowLinearDepth(nearPlane, farPlane, depth - step);
        const float upper = ShadowLinearDepth(nearPlane, farPlane, depth + step);
        // d(world depth) / d(normalized depth); the function under test is its reciprocal.
        const float worldPerDepth = (upper - lower) / (2.0F * step);
        const float measuredSlope = 1.0F / worldPerDepth;
        const float analyticSlope = ShadowDepthSlope(nearPlane, farPlane, depth);
        EXPECT_NEAR(analyticSlope, measuredSlope, std::abs(measuredSlope) * 1.0e-2F + 1.0e-6F)
            << "at normalized depth " << depth;
    }
}

TEST(LightShadowPlanner, DepthSlope_GrowsByOrdersOfMagnitudeTowardTheNearPlane) {
    // This is the whole reason the atlas bias is world-space: one normalized-depth value cannot be
    // right at both ends of the same ray.
    const float nearSlope = ShadowDepthSlope(0.2F, 10.0F, 0.0001F);
    const float farSlope = ShadowDepthSlope(0.2F, 10.0F, 0.9999F);
    EXPECT_GT(nearSlope, farSlope * 1000.0F);
    EXPECT_GT(farSlope, 0.0F);
}

TEST(LightShadowPlanner, DepthSlope_MatchesTheClosedFormAtAFixedWorldDepth) {
    // Sanity anchor for the closed form: at a fixed world depth d, dz/dworld = n*f/((f-n)d^2).
    // Checking the identity directly keeps the algebra from silently drifting.
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float worldDepth = 2.0F;
    // Invert d = n*f / (f - (f - n) * z) from ShadowLinearDepth.
    const float normalizedDepth =
        farPlane * (worldDepth - nearPlane) / ((farPlane - nearPlane) * worldDepth);
    const float expected =
        (nearPlane * farPlane) / ((farPlane - nearPlane) * worldDepth * worldDepth);
    EXPECT_NEAR(ShadowDepthSlope(nearPlane, farPlane, normalizedDepth), expected,
                expected * 1.0e-4F);
}

// ---------------------------------------------------------------------------------------------
// Plan construction against a real atlas
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlanner, Plan_AllocatesOneNonOverlappingTilePerCandidate) {
    const Math::Frustum frustum = MakeViewFrustum();
    const LightList lights = MakeLightList(
        {MakeSpotLight(Math::Vector3{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, 2.0F, -6.0F}, 8.0F)});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 1024U);

    ASSERT_EQ(plan.count, 3U);
    EXPECT_EQ(plan.tileResolution, 1024U);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 3U);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const LightShadowAssignment& assignment = plan.assignments[index];
        EXPECT_EQ(assignment.lightIndex, index);
        EXPECT_EQ(assignment.tile.pixelRect.size.x, 1024);
        EXPECT_EQ(assignment.tile.pixelRect.size.y, 1024);
        EXPECT_GT(assignment.farPlane, assignment.nearPlane);
        EXPECT_TRUE(atlas.IsTileAllocated(assignment.tile.tileId));
    }
    // Tiles must not overlap - the allocator's whole job - checked through the normalized rects the
    // shader actually samples with.
    for (std::size_t first = 0; first < plan.count; ++first) {
        for (std::size_t second = first + 1U; second < plan.count; ++second) {
            const Math::Vector4& a = plan.assignments[first].tile.normalizedRect;
            const Math::Vector4& b = plan.assignments[second].tile.normalizedRect;
            const bool disjoint = a.z <= b.x || b.z <= a.x || a.w <= b.y || b.w <= a.y;
            EXPECT_TRUE(disjoint) << "tiles " << first << " and " << second << " overlap";
        }
    }
}

TEST(LightShadowPlanner, Plan_IsEmptyWhenNothingCasts) {
    const Math::Frustum frustum = MakeViewFrustum();
    ShadowAtlasManager atlas;
    const LightList lights = MakeLightList({});
    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 1024U);
    EXPECT_EQ(plan.count, 0U);
    EXPECT_EQ(plan.tileResolution, 0U);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 0U);
}

TEST(LightShadowPlanner, Plan_ReusesTheAtlasWithoutLeakingBetweenFrames) {
    const Math::Frustum frustum = MakeViewFrustum();
    ShadowAtlasManager atlas;
    const LightList threeLights = MakeLightList(
        {MakeSpotLight(Math::Vector3{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, 2.0F, -6.0F}, 8.0F)});
    ASSERT_EQ(BuildLightShadowPlan(threeLights, frustum, atlas, 1024U).count, 3U);
    ASSERT_EQ(atlas.GetAllocatedTileCount(), 3U);

    // A light that stops casting must not keep its tile alive: the next plan resets the layout and
    // the atlas goes back to a single free root.
    const LightList oneLight = MakeLightList({MakeSpotLight()});
    const LightShadowPlan secondPlan = BuildLightShadowPlan(oneLight, frustum, atlas, 1024U);
    EXPECT_EQ(secondPlan.count, 1U);
    // Three leaked tiles would show up here as four live allocations.
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 1U);
}

TEST(LightShadowPlanner, Plan_DegradesTileSizeInsteadOfDroppingLights) {
    const Math::Frustum frustum = MakeViewFrustum();
    const LightList lights = MakeLightList(
        {MakeSpotLight(Math::Vector3{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, 2.0F, -6.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, -2.0F, -6.0F}, 8.0F)});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 4096U);

    // Four lights at the requested 4096 would need four atlases; the planner halves once instead.
    EXPECT_EQ(plan.count, 4U);
    EXPECT_EQ(plan.tileResolution, 2048U);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 4U);
}

TEST(LightShadowPlanner, Plan_ShadowSlotsAreDecoupledFromLightIndex) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData off = MakeSpotLight();
    off.castsShadows = false;
    const LightList lights = MakeLightList(
        {off, MakeSpotLight(Math::Vector3{-2.0F, 0.0F, -5.0F}, 8.0F), off,
         MakeSpotLight(Math::Vector3{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, 2.0F, -6.0F}, 8.0F),
         MakeSpotLight(Math::Vector3{0.0F, -2.0F, -6.0F}, 8.0F)});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 1024U);

    ASSERT_EQ(plan.count, 4U);
    const std::size_t expectedLights[] = {1U, 3U, 4U, 5U};
    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        EXPECT_EQ(plan.assignments[slot].lightIndex, expectedLights[slot]);
        EXPECT_EQ(plan.assignments[slot].shadowSlot, slot);
        EXPECT_EQ(plan.lightToShadowSlot[expectedLights[slot]], static_cast<std::int32_t>(slot));
    }
    EXPECT_EQ(plan.lightToShadowSlot[0], -1);
    EXPECT_EQ(plan.lightToShadowSlot[2], -1);
}

TEST(LightShadowPlanner, Plan_ProjectsTheConeAxisThroughTheTileCentre) {
    // The matrix the pass renders with and the matrix the shader unprojects with are the same
    // object, so one check pins both: a point straight down the light's -Z axis must land at the
    // tile's centre in normalized device coordinates.
    const Math::Frustum frustum = MakeViewFrustum();
    const Math::Vector3 lightPosition{1.0F, 2.0F, -8.0F};
    const LightList lights = MakeLightList({MakeSpotLight(lightPosition, 12.0F, 25.0F)});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const LightShadowAssignment& assignment = plan.assignments[0];
    const Math::Vector4 clip = TransformClip(
        assignment.lightViewProjection,
        Math::Vector3{lightPosition.x, lightPosition.y, lightPosition.z - 4.0F});
    ASSERT_GT(clip.w, 0.0F);
    EXPECT_NEAR(clip.x / clip.w, 0.0F, 1.0e-5F);
    EXPECT_NEAR(clip.y / clip.w, 0.0F, 1.0e-5F);
    const float normalizedDepth = clip.z / clip.w;
    EXPECT_GT(normalizedDepth, 0.0F);
    EXPECT_LT(normalizedDepth, 1.0F);
}

TEST(LightShadowPlanner, Plan_TileRectMatchesThePixelRectItRendersWith) {
    const Math::Frustum frustum = MakeViewFrustum();
    ShadowAtlasManager atlas;
    const LightList lights = MakeLightList({MakeSpotLight()});
    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const Math::Vector4& rect = plan.assignments[0].tile.normalizedRect;
    constexpr float inverseAtlas = 1.0F / static_cast<float>(kShadowAtlasResolution);
    EXPECT_NEAR(rect.z - rect.x, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_NEAR(rect.w - rect.y, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_GE(rect.x, 0.0F);
    EXPECT_LE(rect.z, 1.0F);
}

// ---------------------------------------------------------------------------------------------
// Point lights: six cube faces each
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlanner, Candidate_AcceptsAPointLightRegardlessOfItsConeAngle) {
    const Math::Frustum frustum = MakeViewFrustum();
    EXPECT_TRUE(IsPointLightShadowCandidate(MakePointLight(), frustum));
    // The cone fields belong to spot lights; leaving them unset must not disqualify a point light.
    LightData noCone = MakePointLight();
    noCone.spotAngleDegrees = 0.0F;
    EXPECT_TRUE(IsPointLightShadowCandidate(noCone, frustum));
    // ... and a spot light is not a point light, so each type only ever gets its own tiles.
    EXPECT_FALSE(IsPointLightShadowCandidate(MakeSpotLight(), frustum));
    EXPECT_FALSE(IsSpotLightShadowCandidate(MakePointLight(), frustum));
}

TEST(LightShadowPlanner, Candidate_AppliesTheSameSharedRulesToPointLights) {
    const Math::Frustum frustum = MakeViewFrustum();
    LightData noShadow = MakePointLight();
    noShadow.castsShadows = false;
    EXPECT_FALSE(IsPointLightShadowCandidate(noShadow, frustum));
    LightData unlit = MakePointLight();
    unlit.intensity = 0.0F;
    EXPECT_FALSE(IsPointLightShadowCandidate(unlit, frustum));
    LightData headlight = MakePointLight();
    headlight.isEditorInternal = true;
    EXPECT_FALSE(IsPointLightShadowCandidate(headlight, frustum));
    LightData badRange = MakePointLight(Math::Vector3{0.0F, 0.0F, -5.0F}, 0.0F);
    EXPECT_FALSE(IsPointLightShadowCandidate(badRange, frustum));
    LightData nanPosition = MakePointLight();
    nanPosition.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPointLightShadowCandidate(nanPosition, frustum));
    // Behind the camera and beyond its own reach.
    EXPECT_FALSE(IsPointLightShadowCandidate(
        MakePointLight(Math::Vector3{0.0F, 0.0F, 40.0F}, 5.0F), frustum));
}

TEST(LightShadowPlanner, CountShadowTiles_ChargesSixTilesPerPointLight) {
    const Math::Frustum frustum = MakeViewFrustum();
    // 2 spot + 1 point == 2 + 6 tiles. Charging one would silently under-allocate the atlas and
    // leave three of the point light's faces unrendered.
    const LightList lights = MakeLightList(
        {MakeSpotLight(), MakeSpotLight(Math::Vector3{2.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLight()});
    EXPECT_EQ(CountShadowTiles(lights, frustum), 8U);
    EXPECT_EQ(CountShadowTiles(MakeLightList({MakePointLight()}), frustum),
              static_cast<std::size_t>(kPointShadowFaceCount));
    EXPECT_EQ(CountShadowTiles(MakeLightList({}), frustum), 0U);
}

TEST(LightShadowPlanner, TileResolution_FitsTheWorstCaseTwentyFourTiles) {
    // Four point lights at 4096 would need 16 GiB of atlas; the planner halves until they fit.
    EXPECT_EQ(ResolveShadowTileResolution(4096U, kMaxShadowTiles), 512U);
    // Six tiles fit at 1024 (a 4x4 grid), so one point light asking for 4096 lands there.
    EXPECT_EQ(ResolveShadowTileResolution(4096U, static_cast<std::size_t>(kPointShadowFaceCount)), 1024U);
    // At the floor the compiler-of-truth is the atlas itself: 4096/128 squared is 1024 slots.
    EXPECT_EQ(ResolveShadowTileResolution(128U, kMaxShadowTiles), kShadowAtlasMinTileResolution);
}

TEST(LightShadowPlanner, Plan_PointLightAllocatesSixDistinctFaces) {
    const Math::Frustum frustum = MakeViewFrustum();
    const LightList lights = MakeLightList({MakePointLight()});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 256U);

    ASSERT_EQ(plan.count, static_cast<std::size_t>(kPointShadowFaceCount));
    EXPECT_EQ(plan.tileResolution, 256U);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), kPointShadowFaceCount);
    for (std::size_t face = 0; face < plan.count; ++face) {
        const LightShadowAssignment& assignment = plan.assignments[face];
        EXPECT_EQ(assignment.lightIndex, 0U);
        EXPECT_EQ(assignment.faceIndex, static_cast<std::uint32_t>(face));
        EXPECT_TRUE(assignment.isPoint);
        EXPECT_TRUE(atlas.IsTileAllocated(assignment.tile.tileId));
        EXPECT_EQ(assignment.tile.pixelRect.size.x, 256);
        EXPECT_GT(assignment.farPlane, assignment.nearPlane);
    }
    // All six tiles disjoint, or two faces would write over each other's depth.
    for (std::size_t first = 0; first < plan.count; ++first) {
        for (std::size_t second = first + 1U; second < plan.count; ++second) {
            const Math::Vector4& a = plan.assignments[first].tile.normalizedRect;
            const Math::Vector4& b = plan.assignments[second].tile.normalizedRect;
            const bool disjoint = a.z <= b.x || b.z <= a.x || a.w <= b.y || b.w <= a.y;
            EXPECT_TRUE(disjoint) << "faces " << first << " and " << second << " overlap";
        }
    }
}

TEST(LightShadowPlanner, Plan_PointFaceMatricesAgreeWithTheAtlasFaceTable) {
    // Each face's matrix must project its own basis direction through the tile centre - that is the
    // contract PointShadowFactor samples against, and a mismatch would shadow the wrong side of
    // the light with a perfectly plausible-looking result.
    const Math::Frustum frustum = MakeViewFrustum();
    const Math::Vector3 lightPosition{1.5F, -2.0F, -6.0F};
    const LightList lights = MakeLightList({MakePointLight(lightPosition, 15.0F)});
    ShadowAtlasManager atlas;
    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 256U);
    ASSERT_EQ(plan.count, static_cast<std::size_t>(kPointShadowFaceCount));

    const auto table = PointLightShadowFaceTable();
    for (std::size_t face = 0; face < plan.count; ++face) {
        const Math::Vector3& forward = table[face].forward;
        const Math::Vector3 target{lightPosition.x + forward.x, lightPosition.y + forward.y,
                                      lightPosition.z + forward.z};
        const Math::Vector4 clip = TransformClip(plan.assignments[face].lightViewProjection, target);
        ASSERT_GT(clip.w, 0.0F) << "face " << face;
        EXPECT_NEAR(clip.x / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        EXPECT_NEAR(clip.y / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        // The opposite direction is behind the face and must be rejected by w, not clamped into it.
        const Math::Vector3 behind{lightPosition.x - forward.x, lightPosition.y - forward.y,
                                      lightPosition.z - forward.z};
        const Math::Vector4 behindClip =
            TransformClip(plan.assignments[face].lightViewProjection, behind);
        EXPECT_LT(behindClip.w, 0.0F) << "face " << face;
    }
}

TEST(LightShadowPlanner, Plan_MixesSpotAndPointLightsInLightSlotOrder) {
    const Math::Frustum frustum = MakeViewFrustum();
    const LightList lights = MakeLightList(
        {MakeSpotLight(Math::Vector3{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLight(Math::Vector3{2.0F, 0.0F, -6.0F}, 10.0F),
         MakeSpotLight(Math::Vector3{0.0F, 2.0F, -7.0F}, 8.0F)});
    ShadowAtlasManager atlas;

    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 256U);

    // 1 + 6 + 1 tiles, emitted in increasing uLights[] slot order so the uploaded arrays and the
    // recorded pass order are stable frame to frame.
    ASSERT_EQ(plan.count, 8U);
    EXPECT_EQ(plan.assignments[0].lightIndex, 0U);
    EXPECT_FALSE(plan.assignments[0].isPoint);
    for (std::size_t index = 1U; index < 7U; ++index) {
        EXPECT_EQ(plan.assignments[index].lightIndex, 1U) << "tile " << index;
        EXPECT_TRUE(plan.assignments[index].isPoint) << "tile " << index;
        EXPECT_EQ(plan.assignments[index].faceIndex, static_cast<std::uint32_t>(index - 1U));
    }
    EXPECT_EQ(plan.assignments[7].lightIndex, 2U);
    EXPECT_FALSE(plan.assignments[7].isPoint);
    EXPECT_EQ(plan.assignments[7].faceIndex, 0U);
}

TEST(LightShadowPlanner, Plan_FillsTheWorstCaseTileBudgetWithoutOverflowing) {
    // Four point lights is the worst case the shader's fixed arrays can describe: 24 tiles, one per
    // face. Filling the plan exactly - no more, no fewer - is what proves Plan's capacity, the
    // atlas' tile count and the uploaded uniform arrays agree on the same number.
    const Math::Frustum frustum = MakeViewFrustum();
    const LightList lights = MakeLightList(
        {MakePointLight(Math::Vector3{-3.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLight(Math::Vector3{-1.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLight(Math::Vector3{1.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLight(Math::Vector3{3.0F, 0.0F, -5.0F}, 8.0F)});
    ShadowAtlasManager atlas;
    const LightShadowPlan plan = BuildLightShadowPlan(lights, frustum, atlas, 256U);

    ASSERT_EQ(plan.count, kMaxShadowTiles);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), static_cast<std::uint32_t>(kMaxShadowTiles));
    EXPECT_EQ(plan.tileResolution, 256U);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const LightShadowAssignment& assignment = plan.assignments[index];
        // Assignments stay grouped per light and in face order, so each light's six tiles are
        // contiguous - which is what lets the renderer upload an indexable cube-rect range.
        EXPECT_EQ(assignment.lightIndex, index / kPointShadowFaceCount) << "tile " << index;
        EXPECT_EQ(assignment.faceIndex, static_cast<std::uint32_t>(index % kPointShadowFaceCount))
            << "tile " << index;
    }
}

TEST(LightShadowPlanner, ProjectionDepth_RoundTripsThroughTheLinearDepth) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    for (const float axisDistance : {0.25F, 0.5F, 1.0F, 3.0F, 9.5F}) {
        const float normalizedDepth = PointShadowProjectionDepth(nearPlane, farPlane, axisDistance);
        EXPECT_GT(normalizedDepth, 0.0F) << "at " << axisDistance;
        EXPECT_LT(normalizedDepth, 1.0F) << "at " << axisDistance;
        EXPECT_NEAR(ShadowLinearDepth(nearPlane, farPlane, normalizedDepth), axisDistance,
                    axisDistance * 1.0e-3F)
            << "at " << axisDistance;
    }
    // At or beyond the near plane there is no meaningful projection depth; 0 is the "behind the
    // near plane" sentinel the shader rejects on, never a clamp to a plausible-looking number.
    EXPECT_FLOAT_EQ(PointShadowProjectionDepth(nearPlane, farPlane, nearPlane), 0.0F);
    EXPECT_FLOAT_EQ(PointShadowProjectionDepth(nearPlane, farPlane, 0.05F), 0.0F);
}

} // namespace
} // namespace UVE::Render::Tests
