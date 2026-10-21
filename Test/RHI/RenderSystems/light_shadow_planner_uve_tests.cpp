// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/light_shadow_planner_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector4_uve.h"
#include "uve/render_systems/i_light_system_uve.h"
#include "uve/render_systems/shadow_atlas_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;

/// A camera at the origin looking down -Z, the engine's camera convention. Everything below is
/// framed in that space so "in front of the camera" needs no mental rotation.
[[nodiscard]] Math::FrustumUVE MakeViewFrustumUVE() {
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(60.0F * (kPiUVE / 180.0F), 1.0F, 0.1F, 100.0F);
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

[[nodiscard]] LightDataUVE MakeSpotLightUVE(const Math::Vector3UVE& position = Math::Vector3UVE{0.0F, 0.0F, -5.0F},
                                           const float range = 10.0F,
                                           const float spotAngleDegrees = 30.0F) {
    LightDataUVE light{};
    light.type = Scene::LightTypeUVE::Spot;
    light.position = position;
    // Default quaternion looks down -Z, matching the spot cone the matrix builder projects.
    light.direction = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
    light.rotation = Math::QuaternionUVE{};
    light.intensity = 3.0F;
    light.range = range;
    light.spotAngleDegrees = spotAngleDegrees;
    light.castsShadows = true;
    return light;
}

[[nodiscard]] LightDataUVE MakePointLightUVE(const Math::Vector3UVE& position = Math::Vector3UVE{0.0F, 0.0F, -5.0F},
                                           const float range = 12.0F) {
    LightDataUVE light{};
    light.type = Scene::LightTypeUVE::Point;
    light.position = position;
    // A point light has no cone, so direction/rotation are irrelevant to its shadows - the cube
    // faces come from a fixed table. Deliberately left at the identity rotation to prove that.
    light.direction = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
    light.rotation = Math::QuaternionUVE{};
    light.intensity = 4.0F;
    light.range = range;
    // 0 is not a valid cone and a spot-only validation would reject it; a point light must not care.
    light.spotAngleDegrees = 0.0F;
    light.castsShadows = true;
    return light;
}

/// Full clip-space transform (x, y, z and w), which is what the shadow path actually needs:
/// Matrix4x4UVE has no matrix*vec4 operator, and TransformPointUVE has already divided by w by the
/// time it returns.
[[nodiscard]] Math::Vector4UVE TransformClipUVE(const Math::Matrix4x4UVE& matrix,
                                                const Math::Vector3UVE& point) noexcept {
    const float x = point.x;
    const float y = point.y;
    const float z = point.z;
    Math::Vector4UVE result{};
    result.x = matrix.m[0][0] * x + matrix.m[0][1] * y + matrix.m[0][2] * z + matrix.m[0][3];
    result.y = matrix.m[1][0] * x + matrix.m[1][1] * y + matrix.m[1][2] * z + matrix.m[1][3];
    result.z = matrix.m[2][0] * x + matrix.m[2][1] * y + matrix.m[2][2] * z + matrix.m[2][3];
    result.w = matrix.m[3][0] * x + matrix.m[3][1] * y + matrix.m[3][2] * z + matrix.m[3][3];
    return result;
}

[[nodiscard]] LightListUVE MakeLightListUVE(std::initializer_list<LightDataUVE> lights) {
    LightListUVE list;
    for (const LightDataUVE& light : lights) {
        list.PushBackUVE(light);
    }
    return list;
}

// ---------------------------------------------------------------------------------------------
// Eligibility
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, Candidate_AcceptsASpotLightInsideTheViewFrustum) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    EXPECT_TRUE(IsSpotLightShadowCandidateUVE(MakeSpotLightUVE(), frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsLightsThatAreNotSpotLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE point = MakeSpotLightUVE();
    point.type = Scene::LightTypeUVE::Point;
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(point, frustum));
    LightDataUVE directional = MakeSpotLightUVE();
    directional.type = Scene::LightTypeUVE::Directional;
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(directional, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsZeroIntensityAndDisabledShadowFlag) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE unlit = MakeSpotLightUVE();
    unlit.intensity = 0.0F;
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(unlit, frustum));

    LightDataUVE noShadow = MakeSpotLightUVE();
    noShadow.castsShadows = false;
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(noShadow, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsTheEditorInternalLight) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE headlight = MakeSpotLightUVE();
    headlight.isEditorInternal = true;
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(headlight, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsRangesAndConeAnglesTheShaderCannotRepresent) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    for (const float range : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
        LightDataUVE light = MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, range);
        EXPECT_FALSE(IsSpotLightShadowCandidateUVE(light, frustum)) << "range " << range;
    }
    // The shader's cone falloff inverts past 90 degrees and collapses to a line at or below 1.
    for (const float angle : {0.0F, 0.5F, 90.0F, 120.0F}) {
        LightDataUVE light = MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, 10.0F, angle);
        EXPECT_FALSE(IsSpotLightShadowCandidateUVE(light, frustum)) << "angle " << angle;
    }
}

TEST(LightShadowPlannerUVE, Candidate_RejectsNonFiniteTransforms) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE light = MakeSpotLightUVE();
    light.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(light, frustum));

    LightDataUVE zeroRotation = MakeSpotLightUVE();
    zeroRotation.rotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(zeroRotation, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsALightWhoseInfluenceSphereMissesTheView) {
    // Behind the camera and further away than its own reach, so nothing it lights can be seen.
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(
        MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, 40.0F}, 5.0F), frustum));
}

TEST(LightShadowPlannerUVE, CountShadowTiles_CountsOnlyEligibleLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE point = MakeSpotLightUVE();
    point.type = Scene::LightTypeUVE::Point;
    LightDataUVE disabled = MakeSpotLightUVE();
    disabled.castsShadows = false;
    // Two spot tiles plus the point light's six.
    const LightListUVE lights = MakeLightListUVE({MakeSpotLightUVE(), point, disabled, MakeSpotLightUVE()});
    EXPECT_EQ(CountShadowTilesUVE(lights, frustum), 8U);
}

// ---------------------------------------------------------------------------------------------
// Tile budget
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, TileResolution_IsZeroWithoutCasters) {
    EXPECT_EQ(ResolveShadowTileResolutionUVE(1024U, 0U), 0U);
}

TEST(LightShadowPlannerUVE, TileResolution_RoundsRequestsDownToAPowerOfTwoInsideTheAtlas) {
    EXPECT_EQ(ResolveShadowTileResolutionUVE(1024U, 1U), 1024U);
    EXPECT_EQ(ResolveShadowTileResolutionUVE(1000U, 1U), 512U);
    EXPECT_EQ(ResolveShadowTileResolutionUVE(1U, 1U), kShadowAtlasMinTileResolutionUVE);
    EXPECT_EQ(ResolveShadowTileResolutionUVE(999999U, 1U), kShadowAtlasMaxTileResolutionUVE);
}

TEST(LightShadowPlannerUVE, TileResolution_HalvesUntilEveryCasterFits) {
    // 4096x4096 holds exactly one 4096 tile, so two casters must halve once.
    EXPECT_EQ(ResolveShadowTileResolutionUVE(4096U, 2U), 2048U);
    // Four casters fit in 2048 (2x2).
    EXPECT_EQ(ResolveShadowTileResolutionUVE(4096U, 4U), 2048U);
    // The 128 floor deliberately leaves room for far more than kMaxShadowedLightsUVE squares.
    EXPECT_EQ(ResolveShadowTileResolutionUVE(128U, kMaxShadowedLightsUVE),
              kShadowAtlasMinTileResolutionUVE);
}

// ---------------------------------------------------------------------------------------------
// Projection / bias math shared with the shader
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, NearPlane_ScalesWithRangeAndStaysBounded) {
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlaneUVE(1.0F), 0.02F);
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlaneUVE(10.0F), 0.2F);
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlaneUVE(1000.0F), 1.0F);
    // Degenerate input must still yield a usable near plane rather than a zero or a NaN.
    EXPECT_FLOAT_EQ(ComputeLightShadowNearPlaneUVE(0.0F), 0.02F);
    EXPECT_TRUE(std::isfinite(ComputeLightShadowNearPlaneUVE(-5.0F)));
}

TEST(LightShadowPlannerUVE, LinearDepth_RecoversTheProjectionEndpoints) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    EXPECT_NEAR(ShadowLinearDepthUVE(nearPlane, farPlane, 0.0F), nearPlane, 1.0e-4F);
    EXPECT_NEAR(ShadowLinearDepthUVE(nearPlane, farPlane, 1.0F), farPlane, 1.0e-3F);
}

TEST(LightShadowPlannerUVE, LinearDepth_AgreesWithTheProjectionMatrixAtEveryDepth) {
    // The closed form must be the inverse of what PerspectiveUVE actually writes, or the shader's
    // world-space bias is calibrated against a projection that does not exist.
    constexpr float nearPlane = 0.25F;
    constexpr float farPlane = 25.0F;
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(90.0F * (kPiUVE / 180.0F), 1.0F, nearPlane, farPlane);
    for (const float viewDepth : {0.3F, 1.0F, 5.0F, 12.0F, 24.0F}) {
        const Math::Vector4UVE clip = TransformClipUVE(projection, Math::Vector3UVE{0.0F, 0.0F, -viewDepth});
        ASSERT_GT(clip.w, 0.0F) << "at view depth " << viewDepth;
        const float normalizedDepth = clip.z / clip.w;
        EXPECT_NEAR(ShadowLinearDepthUVE(nearPlane, farPlane, normalizedDepth), viewDepth,
                    viewDepth * 1.0e-3F)
            << "at view depth " << viewDepth;
    }
}

TEST(LightShadowPlannerUVE, DepthSlope_MatchesTheFiniteDifferenceOfLinearDepth) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float step = 1.0e-4F;
    for (const float depth : {0.05F, 0.3F, 0.6F, 0.95F}) {
        const float lower = ShadowLinearDepthUVE(nearPlane, farPlane, depth - step);
        const float upper = ShadowLinearDepthUVE(nearPlane, farPlane, depth + step);
        // d(world depth) / d(normalized depth); the function under test is its reciprocal.
        const float worldPerDepth = (upper - lower) / (2.0F * step);
        const float measuredSlope = 1.0F / worldPerDepth;
        const float analyticSlope = ShadowDepthSlopeUVE(nearPlane, farPlane, depth);
        EXPECT_NEAR(analyticSlope, measuredSlope, std::abs(measuredSlope) * 1.0e-2F + 1.0e-6F)
            << "at normalized depth " << depth;
    }
}

TEST(LightShadowPlannerUVE, DepthSlope_GrowsByOrdersOfMagnitudeTowardTheNearPlane) {
    // This is the whole reason the atlas bias is world-space: one normalized-depth value cannot be
    // right at both ends of the same ray.
    const float nearSlope = ShadowDepthSlopeUVE(0.2F, 10.0F, 0.0001F);
    const float farSlope = ShadowDepthSlopeUVE(0.2F, 10.0F, 0.9999F);
    EXPECT_GT(nearSlope, farSlope * 1000.0F);
    EXPECT_GT(farSlope, 0.0F);
}

TEST(LightShadowPlannerUVE, DepthSlope_MatchesTheClosedFormAtAFixedWorldDepth) {
    // Sanity anchor for the closed form: at a fixed world depth d, dz/dworld = n*f/((f-n)d^2).
    // Checking the identity directly keeps the algebra from silently drifting.
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float worldDepth = 2.0F;
    // Invert d = n*f / (f - (f - n) * z) from ShadowLinearDepthUVE.
    const float normalizedDepth =
        farPlane * (worldDepth - nearPlane) / ((farPlane - nearPlane) * worldDepth);
    const float expected =
        (nearPlane * farPlane) / ((farPlane - nearPlane) * worldDepth * worldDepth);
    EXPECT_NEAR(ShadowDepthSlopeUVE(nearPlane, farPlane, normalizedDepth), expected,
                expected * 1.0e-4F);
}

// ---------------------------------------------------------------------------------------------
// Plan construction against a real atlas
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, Plan_AllocatesOneNonOverlappingTilePerCandidate) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const LightListUVE lights = MakeLightListUVE(
        {MakeSpotLightUVE(Math::Vector3UVE{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{0.0F, 2.0F, -6.0F}, 8.0F)});
    ShadowAtlasManagerUVE atlas;

    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 1024U);

    ASSERT_EQ(plan.count, 3U);
    EXPECT_EQ(plan.tileResolution, 1024U);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), 3U);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const LightShadowAssignmentUVE& assignment = plan.assignments[index];
        EXPECT_EQ(assignment.lightIndex, index);
        EXPECT_EQ(assignment.tile.pixelRect.size.x, 1024);
        EXPECT_EQ(assignment.tile.pixelRect.size.y, 1024);
        EXPECT_GT(assignment.farPlane, assignment.nearPlane);
        EXPECT_TRUE(atlas.IsTileAllocatedUVE(assignment.tile.tileId));
    }
    // Tiles must not overlap - the allocator's whole job - checked through the normalized rects the
    // shader actually samples with.
    for (std::size_t first = 0; first < plan.count; ++first) {
        for (std::size_t second = first + 1U; second < plan.count; ++second) {
            const Math::Vector4UVE& a = plan.assignments[first].tile.normalizedRect;
            const Math::Vector4UVE& b = plan.assignments[second].tile.normalizedRect;
            const bool disjoint = a.z <= b.x || b.z <= a.x || a.w <= b.y || b.w <= a.y;
            EXPECT_TRUE(disjoint) << "tiles " << first << " and " << second << " overlap";
        }
    }
}

TEST(LightShadowPlannerUVE, Plan_IsEmptyWhenNothingCasts) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    ShadowAtlasManagerUVE atlas;
    const LightListUVE lights = MakeLightListUVE({});
    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 1024U);
    EXPECT_EQ(plan.count, 0U);
    EXPECT_EQ(plan.tileResolution, 0U);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), 0U);
}

TEST(LightShadowPlannerUVE, Plan_ReusesTheAtlasWithoutLeakingBetweenFrames) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    ShadowAtlasManagerUVE atlas;
    const LightListUVE threeLights = MakeLightListUVE(
        {MakeSpotLightUVE(Math::Vector3UVE{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{0.0F, 2.0F, -6.0F}, 8.0F)});
    ASSERT_EQ(BuildLightShadowPlanUVE(threeLights, frustum, atlas, 1024U).count, 3U);
    ASSERT_EQ(atlas.GetAllocatedTileCountUVE(), 3U);

    // A light that stops casting must not keep its tile alive: the next plan resets the layout and
    // the atlas goes back to a single free root.
    const LightListUVE oneLight = MakeLightListUVE({MakeSpotLightUVE()});
    const LightShadowPlanUVE secondPlan = BuildLightShadowPlanUVE(oneLight, frustum, atlas, 1024U);
    EXPECT_EQ(secondPlan.count, 1U);
    // Three leaked tiles would show up here as four live allocations.
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), 1U);
}

TEST(LightShadowPlannerUVE, Plan_DegradesTileSizeInsteadOfDroppingLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const LightListUVE lights = MakeLightListUVE(
        {MakeSpotLightUVE(Math::Vector3UVE{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{2.0F, 0.0F, -5.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{0.0F, 2.0F, -6.0F}, 8.0F),
         MakeSpotLightUVE(Math::Vector3UVE{0.0F, -2.0F, -6.0F}, 8.0F)});
    ShadowAtlasManagerUVE atlas;

    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 4096U);

    // Four lights at the requested 4096 would need four atlases; the planner halves once instead.
    EXPECT_EQ(plan.count, 4U);
    EXPECT_EQ(plan.tileResolution, 2048U);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), 4U);
}

TEST(LightShadowPlannerUVE, Plan_ProjectsTheConeAxisThroughTheTileCentre) {
    // The matrix the pass renders with and the matrix the shader unprojects with are the same
    // object, so one check pins both: a point straight down the light's -Z axis must land at the
    // tile's centre in normalized device coordinates.
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const Math::Vector3UVE lightPosition{1.0F, 2.0F, -8.0F};
    const LightListUVE lights = MakeLightListUVE({MakeSpotLightUVE(lightPosition, 12.0F, 25.0F)});
    ShadowAtlasManagerUVE atlas;

    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const LightShadowAssignmentUVE& assignment = plan.assignments[0];
    const Math::Vector4UVE clip = TransformClipUVE(
        assignment.lightViewProjection,
        Math::Vector3UVE{lightPosition.x, lightPosition.y, lightPosition.z - 4.0F});
    ASSERT_GT(clip.w, 0.0F);
    EXPECT_NEAR(clip.x / clip.w, 0.0F, 1.0e-5F);
    EXPECT_NEAR(clip.y / clip.w, 0.0F, 1.0e-5F);
    const float normalizedDepth = clip.z / clip.w;
    EXPECT_GT(normalizedDepth, 0.0F);
    EXPECT_LT(normalizedDepth, 1.0F);
}

TEST(LightShadowPlannerUVE, Plan_TileRectMatchesThePixelRectItRendersWith) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    ShadowAtlasManagerUVE atlas;
    const LightListUVE lights = MakeLightListUVE({MakeSpotLightUVE()});
    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const Math::Vector4UVE& rect = plan.assignments[0].tile.normalizedRect;
    constexpr float inverseAtlas = 1.0F / static_cast<float>(kShadowAtlasResolutionUVE);
    EXPECT_NEAR(rect.z - rect.x, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_NEAR(rect.w - rect.y, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_GE(rect.x, 0.0F);
    EXPECT_LE(rect.z, 1.0F);
}

// ---------------------------------------------------------------------------------------------
// Point lights: six cube faces each
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, Candidate_AcceptsAPointLightRegardlessOfItsConeAngle) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    EXPECT_TRUE(IsPointLightShadowCandidateUVE(MakePointLightUVE(), frustum));
    // The cone fields belong to spot lights; leaving them unset must not disqualify a point light.
    LightDataUVE noCone = MakePointLightUVE();
    noCone.spotAngleDegrees = 0.0F;
    EXPECT_TRUE(IsPointLightShadowCandidateUVE(noCone, frustum));
    // ... and a spot light is not a point light, so each type only ever gets its own tiles.
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(MakeSpotLightUVE(), frustum));
    EXPECT_FALSE(IsSpotLightShadowCandidateUVE(MakePointLightUVE(), frustum));
}

TEST(LightShadowPlannerUVE, Candidate_AppliesTheSameSharedRulesToPointLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE noShadow = MakePointLightUVE();
    noShadow.castsShadows = false;
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(noShadow, frustum));
    LightDataUVE unlit = MakePointLightUVE();
    unlit.intensity = 0.0F;
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(unlit, frustum));
    LightDataUVE headlight = MakePointLightUVE();
    headlight.isEditorInternal = true;
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(headlight, frustum));
    LightDataUVE badRange = MakePointLightUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, 0.0F);
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(badRange, frustum));
    LightDataUVE nanPosition = MakePointLightUVE();
    nanPosition.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(nanPosition, frustum));
    // Behind the camera and beyond its own reach.
    EXPECT_FALSE(IsPointLightShadowCandidateUVE(
        MakePointLightUVE(Math::Vector3UVE{0.0F, 0.0F, 40.0F}, 5.0F), frustum));
}

TEST(LightShadowPlannerUVE, CountShadowTiles_ChargesSixTilesPerPointLight) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    // 2 spot + 1 point == 2 + 6 tiles. Charging one would silently under-allocate the atlas and
    // leave three of the point light's faces unrendered.
    const LightListUVE lights = MakeLightListUVE(
        {MakeSpotLightUVE(), MakeSpotLightUVE(Math::Vector3UVE{2.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLightUVE()});
    EXPECT_EQ(CountShadowTilesUVE(lights, frustum), 8U);
    EXPECT_EQ(CountShadowTilesUVE(MakeLightListUVE({MakePointLightUVE()}), frustum),
              static_cast<std::size_t>(kPointShadowFaceCountUVE));
    EXPECT_EQ(CountShadowTilesUVE(MakeLightListUVE({}), frustum), 0U);
}

TEST(LightShadowPlannerUVE, TileResolution_FitsTheWorstCaseTwentyFourTiles) {
    // Four point lights at 4096 would need 16 GiB of atlas; the planner halves until they fit.
    EXPECT_EQ(ResolveShadowTileResolutionUVE(4096U, kMaxShadowTilesUVE), 512U);
    // Six tiles fit at 1024 (a 4x4 grid), so one point light asking for 4096 lands there.
    EXPECT_EQ(ResolveShadowTileResolutionUVE(4096U, static_cast<std::size_t>(kPointShadowFaceCountUVE)), 1024U);
    // At the floor the compiler-of-truth is the atlas itself: 4096/128 squared is 1024 slots.
    EXPECT_EQ(ResolveShadowTileResolutionUVE(128U, kMaxShadowTilesUVE), kShadowAtlasMinTileResolutionUVE);
}

TEST(LightShadowPlannerUVE, Plan_PointLightAllocatesSixDistinctFaces) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const LightListUVE lights = MakeLightListUVE({MakePointLightUVE()});
    ShadowAtlasManagerUVE atlas;

    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 256U);

    ASSERT_EQ(plan.count, static_cast<std::size_t>(kPointShadowFaceCountUVE));
    EXPECT_EQ(plan.tileResolution, 256U);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), kPointShadowFaceCountUVE);
    for (std::size_t face = 0; face < plan.count; ++face) {
        const LightShadowAssignmentUVE& assignment = plan.assignments[face];
        EXPECT_EQ(assignment.lightIndex, 0U);
        EXPECT_EQ(assignment.faceIndex, static_cast<std::uint32_t>(face));
        EXPECT_TRUE(assignment.isPoint);
        EXPECT_TRUE(atlas.IsTileAllocatedUVE(assignment.tile.tileId));
        EXPECT_EQ(assignment.tile.pixelRect.size.x, 256);
        EXPECT_GT(assignment.farPlane, assignment.nearPlane);
    }
    // All six tiles disjoint, or two faces would write over each other's depth.
    for (std::size_t first = 0; first < plan.count; ++first) {
        for (std::size_t second = first + 1U; second < plan.count; ++second) {
            const Math::Vector4UVE& a = plan.assignments[first].tile.normalizedRect;
            const Math::Vector4UVE& b = plan.assignments[second].tile.normalizedRect;
            const bool disjoint = a.z <= b.x || b.z <= a.x || a.w <= b.y || b.w <= a.y;
            EXPECT_TRUE(disjoint) << "faces " << first << " and " << second << " overlap";
        }
    }
}

TEST(LightShadowPlannerUVE, Plan_PointFaceMatricesAgreeWithTheAtlasFaceTable) {
    // Each face's matrix must project its own basis direction through the tile centre - that is the
    // contract PointShadowFactorUVE samples against, and a mismatch would shadow the wrong side of
    // the light with a perfectly plausible-looking result.
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const Math::Vector3UVE lightPosition{1.5F, -2.0F, -6.0F};
    const LightListUVE lights = MakeLightListUVE({MakePointLightUVE(lightPosition, 15.0F)});
    ShadowAtlasManagerUVE atlas;
    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 256U);
    ASSERT_EQ(plan.count, static_cast<std::size_t>(kPointShadowFaceCountUVE));

    const auto table = PointLightShadowFaceTableUVE();
    for (std::size_t face = 0; face < plan.count; ++face) {
        const Math::Vector3UVE& forward = table[face].forward;
        const Math::Vector3UVE target{lightPosition.x + forward.x, lightPosition.y + forward.y,
                                      lightPosition.z + forward.z};
        const Math::Vector4UVE clip = TransformClipUVE(plan.assignments[face].lightViewProjection, target);
        ASSERT_GT(clip.w, 0.0F) << "face " << face;
        EXPECT_NEAR(clip.x / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        EXPECT_NEAR(clip.y / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        // The opposite direction is behind the face and must be rejected by w, not clamped into it.
        const Math::Vector3UVE behind{lightPosition.x - forward.x, lightPosition.y - forward.y,
                                      lightPosition.z - forward.z};
        const Math::Vector4UVE behindClip =
            TransformClipUVE(plan.assignments[face].lightViewProjection, behind);
        EXPECT_LT(behindClip.w, 0.0F) << "face " << face;
    }
}

TEST(LightShadowPlannerUVE, Plan_MixesSpotAndPointLightsInLightSlotOrder) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const LightListUVE lights = MakeLightListUVE(
        {MakeSpotLightUVE(Math::Vector3UVE{-2.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLightUVE(Math::Vector3UVE{2.0F, 0.0F, -6.0F}, 10.0F),
         MakeSpotLightUVE(Math::Vector3UVE{0.0F, 2.0F, -7.0F}, 8.0F)});
    ShadowAtlasManagerUVE atlas;

    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 256U);

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

TEST(LightShadowPlannerUVE, Plan_FillsTheWorstCaseTileBudgetWithoutOverflowing) {
    // Four point lights is the worst case the shader's fixed arrays can describe: 24 tiles, one per
    // face. Filling the plan exactly - no more, no fewer - is what proves PlanUVE's capacity, the
    // atlas' tile count and the uploaded uniform arrays agree on the same number.
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    const LightListUVE lights = MakeLightListUVE(
        {MakePointLightUVE(Math::Vector3UVE{-3.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLightUVE(Math::Vector3UVE{-1.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLightUVE(Math::Vector3UVE{1.0F, 0.0F, -5.0F}, 8.0F),
         MakePointLightUVE(Math::Vector3UVE{3.0F, 0.0F, -5.0F}, 8.0F)});
    ShadowAtlasManagerUVE atlas;
    const LightShadowPlanUVE plan = BuildLightShadowPlanUVE(lights, frustum, atlas, 256U);

    ASSERT_EQ(plan.count, kMaxShadowTilesUVE);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), static_cast<std::uint32_t>(kMaxShadowTilesUVE));
    EXPECT_EQ(plan.tileResolution, 256U);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const LightShadowAssignmentUVE& assignment = plan.assignments[index];
        // Assignments stay grouped per light and in face order, so each light's six tiles are
        // contiguous - which is what lets the renderer upload an indexable cube-rect range.
        EXPECT_EQ(assignment.lightIndex, index / kPointShadowFaceCountUVE) << "tile " << index;
        EXPECT_EQ(assignment.faceIndex, static_cast<std::uint32_t>(index % kPointShadowFaceCountUVE))
            << "tile " << index;
    }
}

TEST(LightShadowPlannerUVE, ProjectionDepth_RoundTripsThroughTheLinearDepth) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    for (const float axisDistance : {0.25F, 0.5F, 1.0F, 3.0F, 9.5F}) {
        const float normalizedDepth = PointShadowProjectionDepthUVE(nearPlane, farPlane, axisDistance);
        EXPECT_GT(normalizedDepth, 0.0F) << "at " << axisDistance;
        EXPECT_LT(normalizedDepth, 1.0F) << "at " << axisDistance;
        EXPECT_NEAR(ShadowLinearDepthUVE(nearPlane, farPlane, normalizedDepth), axisDistance,
                    axisDistance * 1.0e-3F)
            << "at " << axisDistance;
    }
    // At or beyond the near plane there is no meaningful projection depth; 0 is the "behind the
    // near plane" sentinel the shader rejects on, never a clamp to a plausible-looking number.
    EXPECT_FLOAT_EQ(PointShadowProjectionDepthUVE(nearPlane, farPlane, nearPlane), 0.0F);
    EXPECT_FLOAT_EQ(PointShadowProjectionDepthUVE(nearPlane, farPlane, 0.05F), 0.0F);
}

} // namespace
} // namespace UVE::Render::Tests
