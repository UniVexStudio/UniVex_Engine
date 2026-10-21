// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/i_light_system_uve.h"
#include "uve/render_systems/shadow_atlas_manager_uve.h"

namespace UVE::Render {

/// Most spot lights that can own a tile in the shadow atlas in one frame.
///
/// Three constants describe this one contract and must not drift: `kMaxLightsUVE` (every light slot
/// may be a spot light), this constant, and the shader's fixed `uLightShadow*[4]` arrays in
/// `lit_shadowed_3d.glsl`. The static assertion below is the guard.
inline constexpr std::size_t kMaxSpotShadowLightsUVE = 4U;

static_assert(kMaxSpotShadowLightsUVE == kMaxLightsUVE,
              "every extracted light slot must be able to hold an atlas shadow tile");

/// One spot light's place in the frame's atlas: where its tile sits and what matrix projects world
/// space into that tile.
struct SpotShadowAssignmentUVE {
    /// Slot in `uLights[]` this tile belongs to. The shader indexes `uLightShadowEnabled`,
    /// `uLightShadowMatrices`, `uLightShadowTileRects` and `uLightShadowDepthRange` by exactly
    /// this number, so it is what ties a tile to the light it shadows.
    std::size_t lightIndex = 0U;

    TileAllocUVE tile{};

    /// Projection * view for the spot cone; the same matrix the atlas pass renders with and the
    /// material shader unprojects with, which is what keeps the two from disagreeing.
    Math::Matrix4x4UVE lightViewProjection{};

    /// The projection's depth range, also handed to the shader. The shader needs it to convert a
    /// world-space bias into normalized depth at the receiver - see
    /// `SpotShadowDepthSlopeUVE`.
    float nearPlane = 0.0F;
    float farPlane = 0.0F;
};

/// This frame's spot-shadow layout. `count == 0` is the "no atlas work at all" sentinel: the
/// renderer then records no atlas passes and uploads a fully disabled uniform set.
struct SpotShadowPlanUVE {
    std::array<SpotShadowAssignmentUVE, kMaxSpotShadowLightsUVE> assignments{};
    std::size_t count = 0U;

    /// Tile edge in pixels every assignment uses, or 0 when `count == 0`. One resolution for the
    /// whole frame rather than a per-light one: a uniform tile size is what makes the quadtree
    /// allocation predictable, and mixing sizes would only trade detail between lights whose
    /// apparent sizes on screen are not comparable anyway.
    std::uint32_t tileResolution = 0U;
};

/// True when `light` can and should own an atlas tile this frame.
///
/// `viewFrustum` culls lights whose reach cannot touch anything the camera sees. The test uses the
/// light's bounding sphere (centre, radius = range) against the frustum, which is conservative: a
/// sphere that misses the frustum guarantees the light's cone - a subset of that sphere - cannot
/// light, and therefore cannot shadow, a visible pixel.
[[nodiscard]] bool IsSpotShadowCandidateUVE(const LightDataUVE& light,
                                            const Math::FrustumUVE& viewFrustum) noexcept;

/// How many lights in `lights` pass `IsSpotShadowCandidateUVE`. Separate from
/// `BuildSpotShadowPlanUVE` so the renderer can decide whether the atlas texture is worth creating
/// at all BEFORE owning one.
[[nodiscard]] std::size_t CountSpotShadowCandidatesUVE(const LightListUVE& lights,
                                                       const Math::FrustumUVE& viewFrustum) noexcept;

/// Largest power-of-two tile in [128, 4096] that fits `casterCount` tiles and does not exceed
/// `requestedResolution`. Returns 0 for `casterCount == 0`.
///
/// The atlas is 4096x4096, so at 4 lights and the 128 floor there are always at least 1024 tile
/// slots for at most 4 casters: this function can only ever REDUCE quality to make the request
/// fit, never drop a light. That is why no per-light priority scheme exists - there is nothing to
/// prioritize.
[[nodiscard]] std::uint32_t ResolveSpotShadowTileResolutionUVE(std::uint32_t requestedResolution,
                                                               std::size_t casterCount) noexcept;

/// Projection near plane for a spot light of the given `range`.
///
/// Not a fixed constant: perspective depth precision is governed by the near/far RATIO, so a fixed
/// 5 cm near plane spends most of a 50 m street lamp's depth range inside the first metre. Scaling
/// the near plane with the light's own range keeps that ratio - and therefore the achievable bias
/// - comparable between a candle and a floodlight.
[[nodiscard]] float ComputeSpotShadowNearPlaneUVE(float range) noexcept;

/// Linear view-space distance for a `[0, 1]` projection depth. Mirrors `LightShadowLinearDepthUVE`
/// in `lit_shadowed_3d.glsl` term for term; the unit tests pin the two together.
[[nodiscard]] float SpotShadowLinearDepthUVE(float nearPlane, float farPlane,
                                             float normalizedDepth) noexcept;

/// d(normalized depth) / d(world metres) at `normalizedDepth`.
///
/// This is the only honest way to turn a world-space shadow bias into the normalized-depth
/// comparison the shader performs: the same world offset is thousands of times larger in
/// normalized depth beside the near plane than it is at the light's range. Mirrors
/// `LightShadowDepthSlopeUVE` in `lit_shadowed_3d.glsl`.
[[nodiscard]] float SpotShadowDepthSlopeUVE(float nearPlane, float farPlane,
                                            float normalizedDepth) noexcept;

/// Builds this frame's spot-shadow layout, allocating one tile per candidate in `atlas`.
///
/// `atlas` is reset first: the plan owns the frame's layout, so a light that stopped casting this
/// frame cannot leak its tile into the next one. Assignments come out in `uLights[]` slot order,
/// which makes the recorded pass order and every uploaded array index stable frame to frame for a
/// stable scene.
[[nodiscard]] SpotShadowPlanUVE BuildSpotShadowPlanUVE(const LightListUVE& lights,
                                                       const Math::FrustumUVE& viewFrustum,
                                                       ShadowAtlasManagerUVE& atlas,
                                                       std::uint32_t requestedResolution) noexcept;

} // namespace UVE::Render
