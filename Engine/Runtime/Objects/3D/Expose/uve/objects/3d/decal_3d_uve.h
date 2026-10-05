// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class DecalProjectionModeUVE : std::uint8_t {
    Box = 0,
    Cylinder,
};

/// Decal3D: projects a material onto whatever surfaces fall inside its box - bullet holes,
/// footprints, puddles, graffiti. A RenderInstance3D: its render layers, sorting and the Object3D
/// transform and visibility above that come from its bases.
struct Decal3DComponentUVE final {
    std::string materialAssetPath;
    /// The projection volume, centred on the object; the decal projects along its -Y.
    Math::Vector3UVE size{1.0F, 1.0F, 1.0F};
    DecalProjectionModeUVE projection = DecalProjectionModeUVE::Box;
    /// Seconds before the decal removes itself; 0 keeps it.
    float lifetime = 0.0F;
    bool enabled = true;

    /// Tints the projected colour; a quick way to vary one decal material across many decals.
    Math::Vector3UVE modulate{1.0F, 1.0F, 1.0F};
    /// Multiplies the material's emission.
    float emissionEnergy = 1.0F;
    /// How much of the surface's own colour the decal replaces: 1 fully, 0 not at all (normal
    /// and roughness still apply - useful for dents that keep the paint).
    float albedoMix = 1.0F;
    /// Fades the decal on surfaces facing away from the projection: 0 never, 1 at any angle.
    float normalFade = 0.0F;
    /// Fades the decal toward the top and bottom of its volume, so it does not end in a hard edge.
    float upperFade = 0.3F;
    float lowerFade = 0.3F;
    bool distanceFadeEnabled = false;
    float distanceFadeBegin = 40.0F;
    float distanceFadeLength = 10.0F;
    /// The render layers it projects onto.
    std::uint32_t cullMask = 0xFFFFFFFFU;

    /// Seconds left before the decal expires; owned by the runtime, seeded from `lifetime` the
    /// first time the engine advances it (and again when a scene is loaded). Never saved: how much
    /// of a decal's life was left when the scene was captured is a fact about the session, not the
    /// scene.
    float remainingLifetime = 0.0F;
    /// True once the countdown reached zero. The decal stops painting and stops being drawn, and
    /// the expiry is reported once as `Decal3DExpiredEventUVE` - what removes the entity, if
    /// anything does, is the system that spawned it, not the decal. A permanent decal (`lifetime`
    /// 0) never expires.
    bool expired = false;

    [[nodiscard]] bool operator==(const Decal3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsDecal3DObjectComponentValidUVE(const Decal3DComponentUVE& value) noexcept;

/// True when the decal paints anything at all right now: switched on and still alive. A disabled or
/// expired decal is skipped before any geometry is touched, so the answer to "does this cost
/// anything this frame" is one read of two flags.
[[nodiscard]] bool IsDecal3DPaintingUVE(const Decal3DComponentUVE& value) noexcept;

/// A decal's projection volume resolved into world space, once per decal per frame.
///
/// Sampling builds the basis once and then rotates per candidate point, because a decal is tested
/// against every surface inside its volume: inverting the decal's rotation per point would put a
/// quaternion inverse in the inner loop of the decal pass.
struct Decal3DProjectionUVE final {
    Math::Vector3UVE worldPosition{};
    /// The decal's world rotation - decal-local offsets become world offsets by this. Kept alongside
    /// the inverse rather than recovered from it because the projected pass builds its patches in
    /// unit space and needs to map them back to world, and conjugating a quaternion every patch to
    /// do so would be a rounding error spent on nothing.
    Math::QuaternionUVE worldRotation{};
    /// The inverse of the decal's world rotation - world offsets become decal-local by this.
    Math::QuaternionUVE inverseWorldRotation{};
    /// Half of `size`, scaled by the object's world scale and guarded non-zero.
    Math::Vector3UVE halfExtents{1.0F, 1.0F, 1.0F};
    /// Unit direction the decal projects along in world space - the object's local -Y, rotated.
    Math::Vector3UVE projectionDirection{};
    DecalProjectionModeUVE mode = DecalProjectionModeUVE::Box;
    float normalFade = 0.0F;
    float upperFade = 0.0F;
    float lowerFade = 0.0F;
    bool distanceFadeEnabled = false;
    float distanceFadeBegin = 0.0F;
    float distanceFadeLength = 0.0F;
};

/// Where one point of a receiving surface lands in the decal, and how strongly the decal paints
/// there.
struct Decal3DSampleUVE final {
    /// The point in the volume's own unit coordinates: [-1, 1] on each axis, 0 at the centre, so
    /// "inside" is a test against 1 rather than against any world size.
    Math::Vector3UVE local{};
    /// False for a point outside the volume (per the projection mode's own footprint).
    bool insideVolume = false;
    /// The three authored fades, each in [0, 1], kept apart so a caller can report WHICH fade
    /// removed a sample rather than only that it did.
    float normalFadeWeight = 1.0F;
    float depthFadeWeight = 1.0F;
    float distanceFadeWeight = 1.0F;
    /// Their product: what a blend uses. Zero paints nothing.
    float combinedWeight = 1.0F;

    /// True when this sample is painted at all - inside the volume with weight to spare.
    [[nodiscard]] bool PaintsUVE() const noexcept { return insideVolume && combinedWeight > 0.0F; }
};

/// Resolves `value`'s volume against a world pose. False - and no projection - for a degenerate
/// decal: a non-finite pose or size, a size or scale component that is zero, or a rotation that
/// cannot be inverted. Nothing painted is the honest answer there; a projection built from a
/// singular basis would paint a smear across the scene instead.
/// Maps a world point into the volume's unit coordinates: 0 at the volume's centre, 1 at its
/// surface, whatever `size` and world scale happen to be. The pass that builds projected patches
/// clips in this space, where the volume is exactly the unit box or the unit cylinder, and asks the
/// inverse below to map the clipped vertices back.
[[nodiscard]] Math::Vector3UVE Decal3DWorldToUnitUVE(const Decal3DProjectionUVE& projection,
                                                    const Math::Vector3UVE& worldPoint) noexcept;

/// The inverse of Decal3DWorldToUnitUVE, for turning a clipped unit-space vertex back into a world
/// position. Exact on the axis-aligned path and a scale-and-rotate round trip otherwise.
[[nodiscard]] Math::Vector3UVE Decal3DUnitToWorldUVE(const Decal3DProjectionUVE& projection,
                                                    const Math::Vector3UVE& unitPoint) noexcept;

[[nodiscard]] bool TryMakeDecal3DProjectionUVE(const Decal3DComponentUVE& value,
                                               const Math::Vector3UVE& worldPosition,
                                               const Math::QuaternionUVE& worldRotation,
                                               const Math::Vector3UVE& worldScale,
                                               Decal3DProjectionUVE& outProjection) noexcept;

/// Samples one receiving-surface point against a projection: where it lands, whether it is inside,
/// and how much of the decal reaches it.
///
/// The fades are the authored fields, with the meaning their names promise. `normalFade` 0 paints
/// any facing; 1 paints only surfaces turned towards the decal, and every value between fades
/// the ones at an angle. `upperFade`/`lowerFade` fade the sample out as it approaches the top or
/// bottom of the volume along the projection axis, so the decal does not end in a hard plane.
/// Distance fade applies the authored begin/length against the camera distance.
[[nodiscard]] Decal3DSampleUVE SampleDecal3DUVE(const Decal3DProjectionUVE& projection,
                                                const Math::Vector3UVE& worldPoint,
                                                const Math::Vector3UVE& worldNormal,
                                                float distanceToCamera) noexcept;

/// Advances one decal's lifetime by `deltaSeconds` and returns true exactly on the step it expires.
///
/// A decal that has not been armed yet (no remaining time, not expired) is armed from its authored
/// `lifetime` on the first advance - a naive subtraction from a zero seed expires a brand-new decal
/// on its first frame, which is a trap the runtime arms its way out of instead of leaving to every
/// spawner. `lifetime` 0 means permanent: it never expires, and its remaining time stays 0. A
/// non-finite or non-positive `deltaSeconds` changes nothing, so a paused or broken clock can never
/// expire a decal by accident.
[[nodiscard]] bool AdvanceDecal3DLifetimeUVE(Decal3DComponentUVE& value, float deltaSeconds) noexcept;

struct Decal3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Decal3D";
    Decal3DComponentUVE decal{};
};

/// The RenderInstance3D recipe under this object's name, then the decal component.
void ApplyDecal3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   const Decal3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
