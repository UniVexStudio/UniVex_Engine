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

/// Most lights that can own atlas tiles in one frame.
///
/// Three constants describe this one contract and must not drift: `kMaxLightsUVE` (every light slot
/// may cast), this constant, and the shader's fixed `uLightShadow*[4]` arrays in
/// `lit_shadowed_3d.glsl`. The static assertion below is the guard.
inline constexpr std::size_t kMaxShadowedLightsUVE = 4U;

static_assert(kMaxShadowedLightsUVE == kMaxLightsUVE,
              "every extracted light slot must be able to hold an atlas shadow tile");

/// Tiles the plan can hold: four lights, worst case six faces each.
inline constexpr std::size_t kMaxShadowTilesUVE =
    kMaxShadowedLightsUVE * static_cast<std::size_t>(kPointShadowFaceCountUVE);

/// One tile's place in the frame's atlas: where it sits and what matrix projects world space into
/// it. A spot light contributes one; a point light contributes six, one per cube face.
struct LightShadowAssignmentUVE {
    /// Slot in `uLights[]` this tile belongs to. The shader indexes `uLightShadowEnabled`,
    /// `uLightShadowMatrices`, `uLightShadowTileRects`, `uLightShadowCubeRects` and
    /// `uLightShadowDepthRange` by exactly this number, so it is what ties a tile to the light it
    /// shadows. Six point-light assignments share one `lightIndex`.
    std::size_t lightIndex = 0U;

    /// Cube face for a point light; always 0 for a spot light, which has a single tile. The shader
    /// uses it as the second index into `uLightShadowCubeRects[lightIndex * 6 + faceIndex]`.
    std::uint32_t faceIndex = 0U;

    /// Which uniform half consumes this assignment. A spot tile is sampled with the light's
    /// `uLightShadowMatrices` entry; a point tile is sampled by face direction from
    /// `uLightShadowCubeRects`. Derived at plan time rather than re-derived from `lights[]` during
    /// upload, so the plan alone is enough to lay its tiles out.
    bool isPoint = false;

    TileAllocUVE tile{};

    /// Projection * view for this tile; the same matrix the atlas pass renders with and the
    /// material shader unprojects with, which is what keeps the two from disagreeing.
    Math::Matrix4x4UVE lightViewProjection{};

    /// The projection's depth range, also handed to the shader. The shader needs it to convert a
    /// world-space bias into normalized depth at the receiver - see `ShadowDepthSlopeUVE`.
    float nearPlane = 0.0F;
    float farPlane = 0.0F;
};

/// This frame's light-shadow layout. `count == 0` is the "no atlas work at all" sentinel: the
/// renderer then records no atlas passes and uploads a fully disabled uniform set.
struct LightShadowPlanUVE {
    std::array<LightShadowAssignmentUVE, kMaxShadowTilesUVE> assignments{};
    std::size_t count = 0U;

    /// Tile edge in pixels every assignment uses, or 0 when `count == 0`. One resolution for the
    /// whole frame rather than a per-light one: a uniform tile size is what makes the quadtree
    /// allocation predictable, and mixing sizes would only trade detail between lights whose
    /// apparent sizes on screen are not comparable anyway.
    std::uint32_t tileResolution = 0U;
};

/// True when `light` is a spot light that can and should own an atlas tile this frame.
///
/// `viewFrustum` culls lights whose reach cannot touch anything the camera sees. The test uses the
/// light's bounding sphere (centre, radius = range) against the frustum, which is conservative: a
/// sphere that misses the frustum guarantees the light's cone - a subset of that sphere - cannot
/// light, and therefore cannot shadow, a visible pixel.
[[nodiscard]] bool IsSpotLightShadowCandidateUVE(const LightDataUVE& light,
                                                 const Math::FrustumUVE& viewFrustum) noexcept;

/// True when `light` is a point light that can and should own atlas tiles this frame.
///
/// Same eligibility rules as the spot predicate minus the cone test, which a point light does not
/// have, and the same influence-sphere / view-frustum test.
[[nodiscard]] bool IsPointLightShadowCandidateUVE(const LightDataUVE& light,
                                                  const Math::FrustumUVE& viewFrustum) noexcept;

/// Tiles `lights` demand this frame: one per spot light, `kPointShadowFaceCountUVE` per point light.
///
/// Separate from `BuildLightShadowPlanUVE` so the renderer can decide whether the atlas texture is
/// worth creating at all BEFORE owning one - 64 MiB of Depth32F is not a cost an empty scene should
/// pay.
[[nodiscard]] std::size_t CountShadowTilesUVE(const LightListUVE& lights,
                                             const Math::FrustumUVE& viewFrustum) noexcept;

/// Largest power-of-two tile in [128, 4096] that fits `tileDemand` tiles and does not exceed
/// `requestedResolution`. Returns 0 for `tileDemand == 0`.
///
/// The atlas is 4096x4096, so at the 128 floor it holds 1024 tiles - more than the 24 the plan can
/// ever ask for. This function can therefore only ever REDUCE quality to make the request fit, never
/// drop a light. That is why no per-light priority scheme exists: there is nothing to prioritize.
[[nodiscard]] std::uint32_t ResolveShadowTileResolutionUVE(std::uint32_t requestedResolution,
                                                           std::size_t tileDemand) noexcept;

/// Projection near plane for a shadow-casting light of the given `range`.
///
/// Not a fixed constant: perspective depth precision is governed by the near/far RATIO, so a fixed
/// 5 cm near plane spends most of a 50 m street lamp's depth range inside the first metre. Scaling
/// the near plane with the light's own range keeps that ratio - and therefore the achievable bias -
/// comparable between a candle and a floodlight.
[[nodiscard]] float ComputeLightShadowNearPlaneUVE(float range) noexcept;

/// Linear view-space distance for a `[0, 1]` projection depth. Mirrors `AtlasShadowDepthBiasUVE` in
/// `lit_shadowed_3d.glsl` term for term; the unit tests pin the two together. Both light types use
/// it: a perspective projection's depth-to-distance curve does not care whether the cone is 40
/// degrees wide or 90.
[[nodiscard]] float ShadowLinearDepthUVE(float nearPlane, float farPlane,
                                         float normalizedDepth) noexcept;

/// Inverse of `ShadowLinearDepthUVE`: the `[0, 1]` projection depth a point at `axisDistance`
/// along the light's view axis writes.
///
/// The point-light path needs the forward direction rather than the inverse, because it knows how
/// far along the cube face's axis the receiver sits and must predict the depth the face pass
/// stored there. Keeping both directions as named functions - instead of inlining one of them in
/// the renderer - is what lets the tests assert the round trip.
[[nodiscard]] float PointShadowProjectionDepthUVE(float nearPlane, float farPlane,
                                                  float axisDistance) noexcept;

/// d(normalized depth) / d(world metres) at `normalizedDepth`.
///
/// This is the only honest way to turn a world-space shadow bias into the normalized-depth
/// comparison the shader performs: the same world offset is thousands of times larger in normalized
/// depth beside the near plane than it is at the light's range. Mirrors
/// `AtlasShadowDepthBiasUVE` in `lit_shadowed_3d.glsl`.
[[nodiscard]] float ShadowDepthSlopeUVE(float nearPlane, float farPlane,
                                        float normalizedDepth) noexcept;

/// Builds this frame's atlas layout, allocating tiles in `atlas`.
///
/// `atlas` is reset first: the plan owns the frame's layout, so a light that stopped casting this
/// frame cannot leak its tile into the next one. Assignments come out in `uLights[]` slot order -
/// and, within a point light, in ascending face order - which makes the recorded pass order and
/// every uploaded array index stable frame to frame for a stable scene.
[[nodiscard]] LightShadowPlanUVE BuildLightShadowPlanUVE(const LightListUVE& lights,
                                                         const Math::FrustumUVE& viewFrustum,
                                                         ShadowAtlasManagerUVE& atlas,
                                                         std::uint32_t requestedResolution) noexcept;

} // namespace UVE::Render
