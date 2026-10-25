// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Where a Sky's colour comes from, and therefore which of its fields mean anything.
enum class SkySourceUVE : std::uint8_t {
    /// Three authored bands - zenith, horizon, ground - blended by two curves. This is the sky the
    /// renderer has always drawn, exposed as an object so it can be authored instead of inherited
    /// from whatever the WorldEnvironment happened to hold.
    Gradient = 0,
    /// An equirectangular image, sampled along the view direction.
    Panorama = 1,
    /// A scattering model: Rayleigh and Mie over a spherical atmosphere, with ozone absorption.
    /// RESERVED. Selecting it keeps rendering the gradient until the model lands, so an authored
    /// scene never has a mode that draws nothing.
    Atmosphere = 2,
};

/// The sky over a scene.
///
/// WHY THIS IS AN OBJECT AND NOT MORE FIELDS ON WorldEnvironment3D. The environment answers "how
/// is the finished picture processed" - exposure, bloom, fog, depth of field. The sky answers
/// "what is behind everything and where does its light come from". They change for different
/// reasons and at different rates: a level's grade is set once, its sky moves with the time of day.
/// Keeping them in one component meant every sky edit rewrote the whole post-processing payload and
/// the two could never be replaced independently.
///
/// PRECEDENCE. A scene with a Sky uses the Sky for the sky and the WorldEnvironment for everything
/// else. With no Sky the WorldEnvironment's sky fields are used exactly as before, and a scene with
/// neither renders as it does today - so adding this object to an existing scene changes nothing
/// until its fields are authored.
struct Sky3DComponentUVE final {
    SkySourceUVE source = SkySourceUVE::Gradient;

    /// The three bands. Named as the environment named them, because these are the same values and
    /// a level that migrates must not change colour.
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};

    /// How quickly the zenith and the ground take over from the horizon band. Small values keep
    /// the horizon wide; large ones compress it into a line.
    float skyCurve = 0.15F;
    float groundCurve = 0.02F;

    /// Panorama source. `panoramaRotationDegrees` spins the image about the world's up axis so the
    /// interesting part of the picture can be pointed at the scene; `panoramaExposure` is in stops.
    std::string panoramaPath;
    float panoramaRotationDegrees = 0.0F;
    float panoramaExposure = 0.0F;

    /// Off keeps the object in the scene without drawing it - the same courtesy a light's energy
    /// of zero extends, but explicit.
    bool enabled = true;

    [[nodiscard]] bool operator==(const Sky3DComponentUVE&) const = default;
};

/// True when every field is inside the range the renderer and the serializer can work with.
/// A Sky that fails this is skipped, not clamped: a half-believed sky is worse than none, because
/// it silently disagrees with what the Inspector showed.
[[nodiscard]] bool IsSky3DObjectComponentValidUVE(const Sky3DComponentUVE& value) noexcept;

struct Sky3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Sky";
    Sky3DComponentUVE sky{};
};

/// Builds a Sky entity: Object3D's baseline, then this object's own component. A component already
/// on the entity keeps its values.
void ApplySky3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   const Sky3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
