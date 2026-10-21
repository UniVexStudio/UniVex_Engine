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
    EXPECT_TRUE(IsSpotShadowCandidateUVE(MakeSpotLightUVE(), frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsLightsThatAreNotSpotLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE point = MakeSpotLightUVE();
    point.type = Scene::LightTypeUVE::Point;
    EXPECT_FALSE(IsSpotShadowCandidateUVE(point, frustum));
    LightDataUVE directional = MakeSpotLightUVE();
    directional.type = Scene::LightTypeUVE::Directional;
    EXPECT_FALSE(IsSpotShadowCandidateUVE(directional, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsZeroIntensityAndDisabledShadowFlag) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE unlit = MakeSpotLightUVE();
    unlit.intensity = 0.0F;
    EXPECT_FALSE(IsSpotShadowCandidateUVE(unlit, frustum));

    LightDataUVE noShadow = MakeSpotLightUVE();
    noShadow.castsShadows = false;
    EXPECT_FALSE(IsSpotShadowCandidateUVE(noShadow, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsTheEditorInternalLight) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE headlight = MakeSpotLightUVE();
    headlight.isEditorInternal = true;
    EXPECT_FALSE(IsSpotShadowCandidateUVE(headlight, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsRangesAndConeAnglesTheShaderCannotRepresent) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    for (const float range : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
        LightDataUVE light = MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, range);
        EXPECT_FALSE(IsSpotShadowCandidateUVE(light, frustum)) << "range " << range;
    }
    // The shader's cone falloff inverts past 90 degrees and collapses to a line at or below 1.
    for (const float angle : {0.0F, 0.5F, 90.0F, 120.0F}) {
        LightDataUVE light = MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, 10.0F, angle);
        EXPECT_FALSE(IsSpotShadowCandidateUVE(light, frustum)) << "angle " << angle;
    }
}

TEST(LightShadowPlannerUVE, Candidate_RejectsNonFiniteTransforms) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE light = MakeSpotLightUVE();
    light.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSpotShadowCandidateUVE(light, frustum));

    LightDataUVE zeroRotation = MakeSpotLightUVE();
    zeroRotation.rotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(IsSpotShadowCandidateUVE(zeroRotation, frustum));
}

TEST(LightShadowPlannerUVE, Candidate_RejectsALightWhoseInfluenceSphereMissesTheView) {
    // Behind the camera and further away than its own reach, so nothing it lights can be seen.
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    EXPECT_FALSE(IsSpotShadowCandidateUVE(
        MakeSpotLightUVE(Math::Vector3UVE{0.0F, 0.0F, 40.0F}, 5.0F), frustum));
}

TEST(LightShadowPlannerUVE, CountSpotShadowCandidates_CountsOnlyEligibleLights) {
    const Math::FrustumUVE frustum = MakeViewFrustumUVE();
    LightDataUVE point = MakeSpotLightUVE();
    point.type = Scene::LightTypeUVE::Point;
    LightDataUVE disabled = MakeSpotLightUVE();
    disabled.castsShadows = false;
    const LightListUVE lights = MakeLightListUVE({MakeSpotLightUVE(), point, disabled, MakeSpotLightUVE()});
    EXPECT_EQ(CountSpotShadowCandidatesUVE(lights, frustum), 2U);
}

// ---------------------------------------------------------------------------------------------
// Tile budget
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, TileResolution_IsZeroWithoutCasters) {
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(1024U, 0U), 0U);
}

TEST(LightShadowPlannerUVE, TileResolution_RoundsRequestsDownToAPowerOfTwoInsideTheAtlas) {
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(1024U, 1U), 1024U);
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(1000U, 1U), 512U);
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(1U, 1U), kShadowAtlasMinTileResolutionUVE);
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(999999U, 1U), kShadowAtlasMaxTileResolutionUVE);
}

TEST(LightShadowPlannerUVE, TileResolution_HalvesUntilEveryCasterFits) {
    // 4096x4096 holds exactly one 4096 tile, so two casters must halve once.
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(4096U, 2U), 2048U);
    // Four casters fit in 2048 (2x2).
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(4096U, 4U), 2048U);
    // The 128 floor deliberately leaves room for far more than kMaxSpotShadowLightsUVE squares.
    EXPECT_EQ(ResolveSpotShadowTileResolutionUVE(128U, kMaxSpotShadowLightsUVE),
              kShadowAtlasMinTileResolutionUVE);
}

// ---------------------------------------------------------------------------------------------
// Projection / bias math shared with the shader
// ---------------------------------------------------------------------------------------------

TEST(LightShadowPlannerUVE, NearPlane_ScalesWithRangeAndStaysBounded) {
    EXPECT_FLOAT_EQ(ComputeSpotShadowNearPlaneUVE(1.0F), 0.02F);
    EXPECT_FLOAT_EQ(ComputeSpotShadowNearPlaneUVE(10.0F), 0.2F);
    EXPECT_FLOAT_EQ(ComputeSpotShadowNearPlaneUVE(1000.0F), 1.0F);
    // Degenerate input must still yield a usable near plane rather than a zero or a NaN.
    EXPECT_FLOAT_EQ(ComputeSpotShadowNearPlaneUVE(0.0F), 0.02F);
    EXPECT_TRUE(std::isfinite(ComputeSpotShadowNearPlaneUVE(-5.0F)));
}

TEST(LightShadowPlannerUVE, LinearDepth_RecoversTheProjectionEndpoints) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    EXPECT_NEAR(SpotShadowLinearDepthUVE(nearPlane, farPlane, 0.0F), nearPlane, 1.0e-4F);
    EXPECT_NEAR(SpotShadowLinearDepthUVE(nearPlane, farPlane, 1.0F), farPlane, 1.0e-3F);
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
        EXPECT_NEAR(SpotShadowLinearDepthUVE(nearPlane, farPlane, normalizedDepth), viewDepth,
                    viewDepth * 1.0e-3F)
            << "at view depth " << viewDepth;
    }
}

TEST(LightShadowPlannerUVE, DepthSlope_MatchesTheFiniteDifferenceOfLinearDepth) {
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float step = 1.0e-4F;
    for (const float depth : {0.05F, 0.3F, 0.6F, 0.95F}) {
        const float lower = SpotShadowLinearDepthUVE(nearPlane, farPlane, depth - step);
        const float upper = SpotShadowLinearDepthUVE(nearPlane, farPlane, depth + step);
        // d(world depth) / d(normalized depth); the function under test is its reciprocal.
        const float worldPerDepth = (upper - lower) / (2.0F * step);
        const float measuredSlope = 1.0F / worldPerDepth;
        const float analyticSlope = SpotShadowDepthSlopeUVE(nearPlane, farPlane, depth);
        EXPECT_NEAR(analyticSlope, measuredSlope, std::abs(measuredSlope) * 1.0e-2F + 1.0e-6F)
            << "at normalized depth " << depth;
    }
}

TEST(LightShadowPlannerUVE, DepthSlope_GrowsByOrdersOfMagnitudeTowardTheNearPlane) {
    // This is the whole reason the atlas bias is world-space: one normalized-depth value cannot be
    // right at both ends of the same ray.
    const float nearSlope = SpotShadowDepthSlopeUVE(0.2F, 10.0F, 0.0001F);
    const float farSlope = SpotShadowDepthSlopeUVE(0.2F, 10.0F, 0.9999F);
    EXPECT_GT(nearSlope, farSlope * 1000.0F);
    EXPECT_GT(farSlope, 0.0F);
}

TEST(LightShadowPlannerUVE, DepthSlope_MatchesTheClosedFormAtAFixedWorldDepth) {
    // Sanity anchor for the closed form: at a fixed world depth d, dz/dworld = n*f/((f-n)d^2).
    // Checking the identity directly keeps the algebra from silently drifting.
    constexpr float nearPlane = 0.2F;
    constexpr float farPlane = 10.0F;
    constexpr float worldDepth = 2.0F;
    // Invert d = n*f / (f - (f - n) * z) from SpotShadowLinearDepthUVE.
    const float normalizedDepth =
        farPlane * (worldDepth - nearPlane) / ((farPlane - nearPlane) * worldDepth);
    const float expected =
        (nearPlane * farPlane) / ((farPlane - nearPlane) * worldDepth * worldDepth);
    EXPECT_NEAR(SpotShadowDepthSlopeUVE(nearPlane, farPlane, normalizedDepth), expected,
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

    const SpotShadowPlanUVE plan = BuildSpotShadowPlanUVE(lights, frustum, atlas, 1024U);

    ASSERT_EQ(plan.count, 3U);
    EXPECT_EQ(plan.tileResolution, 1024U);
    EXPECT_EQ(atlas.GetAllocatedTileCountUVE(), 3U);
    for (std::size_t index = 0; index < plan.count; ++index) {
        const SpotShadowAssignmentUVE& assignment = plan.assignments[index];
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
    const SpotShadowPlanUVE plan = BuildSpotShadowPlanUVE(lights, frustum, atlas, 1024U);
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
    ASSERT_EQ(BuildSpotShadowPlanUVE(threeLights, frustum, atlas, 1024U).count, 3U);
    ASSERT_EQ(atlas.GetAllocatedTileCountUVE(), 3U);

    // A light that stops casting must not keep its tile alive: the next plan resets the layout and
    // the atlas goes back to a single free root.
    const LightListUVE oneLight = MakeLightListUVE({MakeSpotLightUVE()});
    const SpotShadowPlanUVE secondPlan = BuildSpotShadowPlanUVE(oneLight, frustum, atlas, 1024U);
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

    const SpotShadowPlanUVE plan = BuildSpotShadowPlanUVE(lights, frustum, atlas, 4096U);

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

    const SpotShadowPlanUVE plan = BuildSpotShadowPlanUVE(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const SpotShadowAssignmentUVE& assignment = plan.assignments[0];
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
    const SpotShadowPlanUVE plan = BuildSpotShadowPlanUVE(lights, frustum, atlas, 512U);
    ASSERT_EQ(plan.count, 1U);
    const Math::Vector4UVE& rect = plan.assignments[0].tile.normalizedRect;
    constexpr float inverseAtlas = 1.0F / static_cast<float>(kShadowAtlasResolutionUVE);
    EXPECT_NEAR(rect.z - rect.x, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_NEAR(rect.w - rect.y, 512.0F * inverseAtlas, 1.0e-6F);
    EXPECT_GE(rect.x, 0.0F);
    EXPECT_LE(rect.z, 1.0F);
}

} // namespace
} // namespace UVE::Render::Tests
