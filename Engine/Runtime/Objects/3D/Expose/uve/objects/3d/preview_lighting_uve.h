// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"

namespace UVE::Scene {

/// Editor-only preview lighting for an empty scene.
///
/// WHAT THIS IS. A 3D scene that has neither a DirectionalLight3D nor a WorldEnvironment3D would
/// otherwise be lit by the engine's flat ambient term alone - near black with the default
/// settings - which makes authoring a first object look broken rather than unlit. The editor
/// therefore supplies a preview sun and a preview environment, and the moment the author adds a
/// real node of either kind, that preview steps aside and theirs is the only one that counts.
///
/// WHERE THE MODEL COMES FROM. Godot 4's "Preview Sun and Environment", introduced in
/// godotengine/godot PR #46315 and documented in tutorials/3d/environment_and_post_processing:
/// the preview is valid only while the scene has no DirectionalLight3D / WorldEnvironment, it is
/// editor-only (it never runs, is never saved into the scene, and never affects an exported
/// project), and one preview serves every scene in the project. This header keeps that contract
/// as data plus one pure function, so the rule is testable without a GL context and cannot drift
/// between the viewport renderer, the settings dialog and the Scene panel.
///
/// WHY IT LIVES IN Objects/3D AND NOT IN THE EDITOR. The values it produces are ordinary engine
/// values - a world rotation, a light colour, a WorldEnvironmentFrameUVE - and the renderer and
/// the editor mesh layer both consume them. Keeping the resolver beside WorldEnvironment3D means
/// the runtime owns the shape of the data and the editor only owns the authoring surface.
struct PreviewLightingSettingsUVE final {
    /// Godot's two toolbar toggles. Off means "show me the scene exactly as it runs".
    bool sunEnabled = true;
    bool environmentEnabled = true;

    /// Solar convention, in degrees: azimuth is the compass bearing around +Y measured from +Z
    /// towards +X, and altitude is the elevation above the horizon (90 = straight up, negative =
    /// below it). Godot exposes the same pair in its own sun dialog.
    float sunAzimuthDegrees = 45.0F;
    float sunAltitudeDegrees = 45.0F;

    /// Luminous output of the preview sun, matching LightEmitterComponentUVE::energy's units.
    float sunEnergy = 1.0F;
    Math::Vector3UVE sunColor{1.0F, 1.0F, 1.0F};

    /// The preview environment's three-band procedural sky, matching
    /// WorldEnvironment3DComponentUVE's own sky/horizon/ground triple so "Add Environment to
    /// Scene" copies it across without a second colour model.
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};

    /// Scales the sky and ground ambient terms, exactly like an environment's ambientEnergy.
    float ambientEnergy = 1.0F;
};

/// What a frame of preview lighting actually resolves to. Every field is engine-native: the mesh
/// layer turns `sunRotation`/`sunColor`/`sunEnergy` into the preview light entity, and the renderer
/// composites `environment` in place of a missing WorldEnvironment3D.
struct PreviewLightingFrameUVE final {
    bool sunActive = false;
    Math::Vector3UVE sunDirection{0.0F, 1.0F, 0.0F}; ///< Towards the sun, normalized.
    Math::Vector3UVE sunColor{1.0F, 1.0F, 1.0F};
    float sunEnergy = 0.0F;
    /// Points local +Z at the sun, matching UVE's light convention (a light shines along -Z).
    Math::QuaternionUVE sunRotation{};

    bool environmentActive = false;
    /// Ready to hand to the renderer: hasEnvironment is set so the procedural sky pass runs.
    WorldEnvironmentFrameUVE environment{};
};

/// Unit vector pointing from the scene towards the sun for an azimuth/altitude pair, in degrees.
/// Values are sanitized: a non-finite angle falls back to the horizon at due north, and the
/// altitude is clamped to [-90, 90]. Azimuth wraps rather than clamping, because 370 degrees is
/// a perfectly meaningful bearing.
[[nodiscard]] Math::Vector3UVE GetPreviewSunDirectionUVE(float azimuthDegrees, float altitudeDegrees) noexcept;

/// The preview light entity's local rotation for that pair, or nothing when the look-at basis
/// degenerates (the sun exactly overhead or exactly below, where the up reference is parallel to
/// the direction and no unique orientation exists).
[[nodiscard]] std::optional<Math::QuaternionUVE> TryMakePreviewSunRotationUVE(float azimuthDegrees,
                                                                              float altitudeDegrees) noexcept;

/// Resolves the frame for one editor frame.
///
/// `sceneHasDirectionalLight` and `sceneHasWorldEnvironment` are the two questions the preview
/// exists to answer: when either is true, the matching preview is suppressed - never blended on
/// top of authored lighting. Settings are sanitized here, so a hand-edited or corrupted settings
/// file cannot produce NaNs in the render loop.
[[nodiscard]] PreviewLightingFrameUVE ResolvePreviewLightingUVE(bool sceneHasDirectionalLight,
                                                                bool sceneHasWorldEnvironment,
                                                                const PreviewLightingSettingsUVE& settings) noexcept;

} // namespace UVE::Scene
