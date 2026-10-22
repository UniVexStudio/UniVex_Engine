// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"

/// Screen-space contact shadows: a short ray march through the depth buffer that recovers the thin
/// shadow a shadow map cannot resolve at the point where two surfaces touch.
///
/// The problem it solves is physical, not artistic. A shadow map answers "is there a caster between
/// this surface and the light" by sampling a depth texel that is, at typical resolution, several
/// centimetres wide on screen. Contact geometry is smaller than that texel, so the shadow either
/// disappears (the caster never covers a whole texel) or detaches (the normal bias pushes the test
/// far enough off the surface to miss a caster standing right on it). Both read as objects floating
/// above the floor.
///
/// The fix is to ask the same question against the camera's own depth buffer: march a few steps
/// from the surface toward the light and note whether anything is closer to the camera than the ray
/// point. That buffer is screen-resolution, so it resolves exactly the contact the shadow map misses.
///
/// The cost is a depth fetch per step per covered pixel, and the failure modes are classic screen
/// space ones - an occluder that is off-screen or behind the surface is invisible to the march, so
/// this REFINES a shadow map, it does not replace one. It is therefore off by default.
///
/// `contact_shadow.glsl` implements this contract; the functions below are the CPU mirror of exactly
/// the same arithmetic (schedule, occlusion rule, visibility mapping) so the algorithm can be tested
/// without a GPU. The two must be changed together - the tests in
/// Test/RHI/RenderSystems/contact_shadows_uve_tests.cpp pin the CPU half against an analytically
/// known scene, which is what keeps the pair honest.
namespace UVE::Render {

/// Tunables for the contact-shadow pass. Every value is in world metres except the step count and
/// the strength, so a project can reason about it against its own scale rather than in screen-space
/// percentages.
struct ContactShadowSettingsUVE final {
    /// Off by default: this is a refinement of an existing shadow, it costs a depth fetch per step
    /// per covered pixel, and it can only ever darken. A project turns it on deliberately.
    bool enabledUVE = false;
    /// How far along the light direction the ray marches. This is the scale of the feature: a
    /// 0.2 m ray recovers feet on a floor and a crate on a table, not a wall on the ground.
    float lengthMetersUVE = 0.2F;
    /// How far behind the ray a depth sample may sit and still count as an occluder. Without an
    /// upper bound every distant wall behind a grazing ray would darken the surface; with it, the
    /// pass only sees geometry close enough to be touching.
    float thicknessMetersUVE = 0.1F;
    /// Fraction of the unoccluded result the shadow may remove, 0..1. Contact shadow is a
    /// refinement, so it never fully removes the light: at 1.0 a fully occluded ray is pitch black,
    /// which reads as a hole rather than as contact.
    float strengthUVE = 0.8F;
    /// Ray samples per pixel. More steps find thinner occluders at the far end of the ray; the
    /// schedule below is quadratic, so the extra samples land near the surface where contact lives.
    std::uint32_t stepCountUVE = 8U;
};

/// Bounds that keep an authored setting from becoming a per-pixel loop with no upper bound.
inline constexpr std::uint32_t kContactShadowMinStepCountUVE = 1U;
inline constexpr std::uint32_t kContactShadowMaxStepCountUVE = 16U;
inline constexpr float kContactShadowMinLengthMetersUVE = 0.001F;
inline constexpr float kContactShadowMaxLengthMetersUVE = 5.0F;
/// Depth samples within this of the ray point are treated as the surface the ray started from, not
/// as an occluder. It absorbs depth-buffer quantization on the surface itself.
inline constexpr float kContactShadowDepthBiasMetersUVE = 0.005F;
/// A depth sample this close to the far plane is background (sky or a cleared buffer) and is never
/// an occluder - nothing is behind it, and treating it as solid would shadow the horizon.
inline constexpr float kContactShadowBackgroundDepthUVE = 0.9999F;

/// Returns `settings` with every field inside its documented range. Out-of-range values are clamped
/// rather than rejected: a project that asks for a 40-step ray gets a 16-step one and a frame that
/// still runs, not a build error or a stall.
[[nodiscard]] constexpr ContactShadowSettingsUVE SanitizeContactShadowSettingsUVE(
    ContactShadowSettingsUVE settings) noexcept {
    if (!(settings.lengthMetersUVE >= kContactShadowMinLengthMetersUVE)) {
        settings.lengthMetersUVE = kContactShadowMinLengthMetersUVE;
    }
    if (settings.lengthMetersUVE > kContactShadowMaxLengthMetersUVE) {
        settings.lengthMetersUVE = kContactShadowMaxLengthMetersUVE;
    }
    if (!(settings.thicknessMetersUVE >= 0.0F)) {
        settings.thicknessMetersUVE = 0.0F;
    }
    if (settings.thicknessMetersUVE > kContactShadowMaxLengthMetersUVE) {
        settings.thicknessMetersUVE = kContactShadowMaxLengthMetersUVE;
    }
    if (!(settings.strengthUVE >= 0.0F)) {
        settings.strengthUVE = 0.0F;
    }
    if (settings.strengthUVE > 1.0F) {
        settings.strengthUVE = 1.0F;
    }
    if (settings.stepCountUVE < kContactShadowMinStepCountUVE) {
        settings.stepCountUVE = kContactShadowMinStepCountUVE;
    }
    if (settings.stepCountUVE > kContactShadowMaxStepCountUVE) {
        settings.stepCountUVE = kContactShadowMaxStepCountUVE;
    }
    return settings;
}

/// Fills `steps[0, count)` with the ray distances a march uses and returns `count`, or 0 when
/// `capacity` is 0. Distances are metres from the surface, strictly increasing, and the last one is
/// exactly the configured length.
///
/// The spacing is quadratic - `length * ((i + 1) / count)^2` - which is a deliberate bias toward the
/// surface: the first sample lands at `length / count^2`, so an 8-step 0.2 m ray starts 3 mm out,
/// where contact shadows actually are, and only the last samples reach 0.2 m. Uniform spacing would
/// spend most of the budget in a region no contact shadow occupies and miss thin occluders near the
/// surface, which is the whole feature.
///
/// `contact_shadow.glsl` computes the same schedule inline in its loop; keep them equal.
[[nodiscard]] std::uint32_t FillContactShadowRayStepsUVE(const ContactShadowSettingsUVE& settings,
                                                        float* steps,
                                                        std::uint32_t capacity) noexcept;

/// The occlusion rule, stated once. `rayViewZ` is the view-space depth (metres, negative in front
/// of the camera, matching `Math::Vector3UVE`'s view convention) of the point on the ray;
/// `sceneViewZ` is the depth the depth buffer reports at the same screen position.
///
/// The sample occludes when it sits in FRONT of the ray point by more than `biasMeters` - it is
/// nearer the camera, so the light cannot reach the ray point - and behind it by less than
/// `thicknessMeters`. The upper bound is what stops a wall metres behind the ray from darkening a
/// surface it is nowhere near touching.
[[nodiscard]] constexpr bool IsContactShadowOccludedUVE(const float rayViewZ,
                                                       const float sceneViewZ,
                                                       const float thicknessMeters,
                                                       const float biasMeters) noexcept {
    const float delta = sceneViewZ - rayViewZ;
    return delta > biasMeters && delta < thicknessMeters;
}

/// The result of one march. Kept as a struct rather than a bare float because "how many samples were
/// tested" is what distinguishes "no occluder found" from "every sample fell off-screen", and those
/// two must not both read as full occlusion.
struct ContactShadowRayResultUVE final {
    /// 1.0 = fully lit, 0.0 = fully shadowed by contact geometry.
    float visibilityUVE = 1.0F;
    std::uint32_t testedStepsUVE = 0U;
    std::uint32_t occludedStepsUVE = 0U;
};

/// Maps an occlusion count to the visibility the shader writes: the fraction of tested steps that
/// were occluded, scaled by `strength`. A march that could not test any step (an all-background or
/// all-off-screen ray) returns full visibility - it observed nothing, so it must not darken
/// anything.
[[nodiscard]] constexpr float ContactShadowVisibilityUVE(const std::uint32_t occludedSteps,
                                                        const std::uint32_t testedSteps,
                                                        const float strength) noexcept {
    if (testedSteps == 0U) {
        return 1.0F;
    }
    float clampedStrength = strength;
    if (!(clampedStrength >= 0.0F)) {
        clampedStrength = 0.0F;
    }
    if (clampedStrength > 1.0F) {
        clampedStrength = 1.0F;
    }
    const float occludedFraction = static_cast<float>(occludedSteps) / static_cast<float>(testedSteps);
    return 1.0F - clampedStrength * occludedFraction;
}

namespace Detail {

/// `matrix * vec4(point, 1)` without the perspective divide, matching the row-major, column-vector
/// convention `Math::Matrix4x4UVE` documents. Returned as four floats because the divide is the
/// caller's next step and the w component is what decides whether the divide is safe.
struct ContactShadowClipPositionUVE final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

[[nodiscard]] constexpr ContactShadowClipPositionUVE ProjectContactShadowPointUVE(
    const Math::Matrix4x4UVE& projection, const Math::Vector3UVE& point) noexcept {
    return ContactShadowClipPositionUVE{
        projection.m[0][0] * point.x + projection.m[0][1] * point.y + projection.m[0][2] * point.z +
            projection.m[0][3],
        projection.m[1][0] * point.x + projection.m[1][1] * point.y + projection.m[1][2] * point.z +
            projection.m[1][3],
        projection.m[2][0] * point.x + projection.m[2][1] * point.y + projection.m[2][2] * point.z +
            projection.m[2][3],
        projection.m[3][0] * point.x + projection.m[3][1] * point.y + projection.m[3][2] * point.z +
            projection.m[3][3],
    };
}

/// Inverse of the above: turns a normalized device coordinate plus a raw depth-buffer sample back
/// into a view-space position, exactly as `ssao.glsl` and `contact_shadow.glsl` do.
[[nodiscard]] constexpr Math::Vector3UVE ReconstructContactShadowViewPositionUVE(
    const Math::Matrix4x4UVE& inverseProjection, const float u, const float v, const float rawDepth) noexcept {
    const Math::Vector3UVE ndc{u * 2.0F - 1.0F, v * 2.0F - 1.0F, rawDepth * 2.0F - 1.0F};
    const ContactShadowClipPositionUVE clip =
        ProjectContactShadowPointUVE(inverseProjection, ndc);
    if (!(clip.w > 1.0e-8F) && !(clip.w < -1.0e-8F)) {
        return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    return Math::Vector3UVE{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace Detail

/// The whole march, in one place, driven by a caller-supplied depth lookup so it can run against a
/// real depth texture, a synthetic scene, or nothing at all.
///
/// `rawDepthAtUv(u, v)` returns the value the depth buffer holds at that screen coordinate in
/// [0, 1], with the same convention the shader samples (`texture(uDepthTexture, uv).r`).
///
/// Returns full visibility when the settings are disabled, when the light direction is degenerate,
/// or when no step could be tested. The shader's loop is the same sequence in the same order, so
/// the CPU test binary and the GPU disagree only if one of them is edited without the other.
template <typename TRawDepthAtUvUVE>
[[nodiscard]] ContactShadowRayResultUVE MarchContactShadowRayUVE(
    const ContactShadowSettingsUVE& settings,
    const Math::Vector3UVE& originView,
    const Math::Vector3UVE& lightDirectionView,
    const Math::Matrix4x4UVE& projection,
    const Math::Matrix4x4UVE& inverseProjection,
    TRawDepthAtUvUVE&& rawDepthAtUv) {
    ContactShadowRayResultUVE result{};
    const ContactShadowSettingsUVE clamped = SanitizeContactShadowSettingsUVE(settings);
    if (!clamped.enabledUVE) {
        return result;
    }
    const float directionLengthSq = lightDirectionView.x * lightDirectionView.x +
                                    lightDirectionView.y * lightDirectionView.y +
                                    lightDirectionView.z * lightDirectionView.z;
    if (!(directionLengthSq > 1.0e-12F)) {
        return result;
    }
    const float inverseDirectionLength = 1.0F / std::sqrt(directionLengthSq);
    const Math::Vector3UVE direction{lightDirectionView.x * inverseDirectionLength,
                                     lightDirectionView.y * inverseDirectionLength,
                                     lightDirectionView.z * inverseDirectionLength};

    std::array<float, kContactShadowMaxStepCountUVE> steps{};
    const std::uint32_t stepCount =
        FillContactShadowRayStepsUVE(clamped, steps.data(), static_cast<std::uint32_t>(steps.size()));
    for (std::uint32_t stepIndex = 0U; stepIndex < stepCount; ++stepIndex) {
        const float distance = steps[stepIndex];
        const Math::Vector3UVE samplePoint{originView.x + direction.x * distance,
                                           originView.y + direction.y * distance,
                                           originView.z + direction.z * distance};
        const Detail::ContactShadowClipPositionUVE clip =
            Detail::ProjectContactShadowPointUVE(projection, samplePoint);
        if (!(clip.w > 1.0e-4F) && !(clip.w < -1.0e-4F)) {
            continue;
        }
        const float u = (clip.x / clip.w) * 0.5F + 0.5F;
        const float v = (clip.y / clip.w) * 0.5F + 0.5F;
        if (!(u >= 0.0F) || !(u <= 1.0F) || !(v >= 0.0F) || !(v <= 1.0F)) {
            continue;
        }
        const float rawDepth = rawDepthAtUv(u, v);
        if (!(rawDepth < kContactShadowBackgroundDepthUVE)) {
            continue;
        }
        const Math::Vector3UVE sceneViewPosition =
            Detail::ReconstructContactShadowViewPositionUVE(inverseProjection, u, v, rawDepth);
        ++result.testedStepsUVE;
        if (IsContactShadowOccludedUVE(samplePoint.z, sceneViewPosition.z, clamped.thicknessMetersUVE,
                                       kContactShadowDepthBiasMetersUVE)) {
            ++result.occludedStepsUVE;
        }
    }
    result.visibilityUVE = ContactShadowVisibilityUVE(result.occludedStepsUVE, result.testedStepsUVE,
                                                     clamped.strengthUVE);
    return result;
}

} // namespace UVE::Render
