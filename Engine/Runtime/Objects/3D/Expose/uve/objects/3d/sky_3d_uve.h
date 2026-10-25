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
    /// Sunlight scattered and absorbed through a spherical shell of air: Rayleigh on the gas,
    /// Mie on the aerosol, and ozone absorbing in the high stratosphere. This is why the zenith is
    /// blue, why the horizon is pale, and why a setting sun is red - the shape of the sky comes
    /// out of the physics rather than out of a hand-placed gradient.
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

    /// Atmosphere model. Each coefficient scales one physical term of the real atmosphere, so 1.0
    /// is Earth and the numbers are not arbitrary art controls: Rayleigh density is the gas, Mie is
    /// the haze and dust, ozone is the high-altitude absorber that makes twilight blue. 0 turns one
    /// term off entirely, which is how you isolate what it was contributing.
    float atmosphereRayleigh = 1.0F;
    float atmosphereMie = 1.0F;
    float atmosphereOzone = 1.0F;

    /// Mie anisotropy: how strongly haze throws light forward. 0 scatters evenly, near 1 is a
    /// tight bright halo around the sun. Earth's haze sits around 0.76, which is why the sky
    /// brightens as you look toward the sun even away from the disc.
    float atmosphereMieAnisotropy = 0.76F;

    /// Light that scattered more than once before reaching the eye. Physically this is an
    /// integral over every bounce, which is why the horizon is bright and shadows are not black;
    /// here it is one approximation term, and this is its weight. 0 gives the hard, dark
    /// single-scatter look, 1 is the calibrated default.
    float atmosphereMultiScatter = 1.0F;

    /// Brightness of the sky as a whole, for matching a scene whose exposure is not the default.
    float atmosphereIntensity = 1.0F;

    /// View samples along each ray. More samples resolve the horizon band and the twilight
    /// gradient more finely; the cost is linear and this is the most expensive thing in the pass.
    std::uint32_t atmosphereSamples = 16U;

    // ---- Night ------------------------------------------------------------------------------
    //
    // Stars and the galactic band are drawn in every source, because they are not part of the
    // atmosphere model - they are what is behind it, and they are occluded by cloud in all of
    // them. Both default to off, so adding a Sky changes nothing until an author asks for a night
    // sky, which is the same rule the gradient's defaults follow.
    //
    /// How many star cells cover the sky. Higher means more, smaller, and fainter stars.
    float starDensity = 420.0F;
    /// Overall star brightness. 0 draws none - the default, because a scene that had no stars
    /// before this object existed should not quietly gain some.
    float starBrightness = 0.0F;
    /// Which stars are where. Changing it reshuffles the whole field, so two levels can share a
    /// sky and not the same constellations.
    float starSeed = 0.0F;
    /// Strength of the galactic band. 0 hides it.
    float milkyWayStrength = 0.0F;

    // ---- Cloud -----------------------------------------------------------------------------
    //
    /// Off by default for the same reason the stars are.
    bool cloudsEnabled = false;
    /// Fraction of the sky the deck covers, 0 bare to 1 overcast.
    float cloudCoverage = 0.45F;
    /// How opaque the covered part is; below 1 the sky and the disc still show through.
    float cloudDensity = 1.0F;
    /// Height of the deck above the eye, in metres. It sets how fast the cloud moves past as the
    /// camera travels, which is what makes a low deck feel close.
    float cloudAltitude = 1500.0F;
    /// Wind across the deck, in metres per second, and the compass bearing it blows toward.
    float cloudWindSpeed = 8.0F;
    float cloudWindDirectionDegrees = 45.0F;
    /// Width of the fade from clear to overcast at each cloud's edge. Small reads as hard-edged
    /// cumulus, large as haze.
    float cloudSoftness = 0.35F;
    /// How strongly thin cloud toward the sun brightens at its edge - the bright rim that appears
    /// when a cloud passes in front of the sun.
    float cloudSilverLining = 1.0F;

    // ---- Lighting ---------------------------------------------------------------------------
    //
    // The link between the sky the player sees and the light that falls on the scene. All three
    // default to off, because a sky that silently relights a level the moment it is added breaks
    // the rule that an added object changes nothing until it is authored.
    //
    /// Take the scene's ambient light from this sky instead of from the WorldEnvironment's
    /// authored ambient triple. Two consequences are worth the switch on their own: the fill light
    /// is the colour of the sky above (so shadows go blue rather than black) and it rises and sets
    /// with the sun, because it is computed from the sun's altitude through the same air the sky
    /// is drawn through.
    bool ambientFromSky = false;
    /// Scales that ambient. This is a brightness dial, not a physical term.
    float ambientStrength = 1.0F;
    /// Take the fog colour from this sky's horizon. Distant geometry then dissolves into the sky
    /// it is in front of, which is what aerial perspective looks like.
    bool fogFromSky = false;
    /// How far the fog moves toward the sky's own colour, 0 none to 1 the horizon exactly.
    float fogStrength = 1.0F;
    /// Reflect this sky in metallic surfaces. The reflection is this sky's own three bands, which
    /// gives a horizon line in a mirror without a capture pass - a cubemap of the equivalent sky
    /// would cost a render per frame and look the same, because the sky is smooth enough that three
    /// bands are the shape. A captured environment map or a ReflectionProbe still wins where one is
    /// present, because a capture also contains the buildings the sky does not.
    /// Off, reflections fall back to the ambient hemisphere.
    bool reflectionsFromSky = false;

    /// Off keeps the object in the scene without drawing it - the same courtesy a light's energy
    /// of zero extends, but explicit.
    bool enabled = true;

    [[nodiscard]] bool operator==(const Sky3DComponentUVE&) const = default;
};

/// The ambient light a sky throws on a scene, split by hemisphere: the light arriving from above
/// and the light arriving from below.
///
/// WHY IT IS COMPUTED RATHER THAN AUTHORED. Ambient is skylight, and skylight is the sky. An
/// authored triple can only describe one hour of one day; this resolves from the sun's altitude
/// through the same air the sky pass integrates, so the fill goes warm and dim at dusk, neutral at
/// noon, and to nothing after astronomical twilight without a keyframe anywhere. It is a pure
/// function of the sky and the sun, so it is testable without a GPU.
struct SkyAmbientUVE final {
    /// Irradiance from the upper hemisphere, in the same units the environment's ambient uses.
    Math::Vector3UVE sky{};
    /// Irradiance from the lower hemisphere: the ground band, dimmer than the sky above it.
    Math::Vector3UVE ground{};
    /// 0 at astronomical night and 1 in full daylight. Reported so a caller can fade other night
    /// effects with the same curve instead of inventing a second one.
    float daylight = 0.0F;
};

/// Sunlight left after a path through the air, per channel: 1 at the top of the atmosphere, and
/// dimmer and redder as the sun approaches the horizon. `sunAltitudeRadians` is measured from the
/// horizon, negative below it; `rayleighDensity` scales the gas, matching the atmosphere's own.
[[nodiscard]] Math::Vector3UVE SkySunTransmittanceUVE(float sunAltitudeRadians, float rayleighDensity) noexcept;

/// The ambient three bands provide for a given sun direction (a unit vector pointing toward the
/// sun). Takes the bands rather than the component because the frame the renderer builds has
/// already copied them - and by then the bands may be the Sky's, the environment's, or a mix, and
/// this must describe whichever is actually being drawn rather than re-deciding it.
///
/// Sanitised: a non-finite direction is treated as the zenith, and every value is clamped to
/// something the shader can use.
[[nodiscard]] SkyAmbientUVE ResolveSkyAmbientUVE(const Math::Vector3UVE& skyColor,
                                                 const Math::Vector3UVE& horizonColor,
                                                 const Math::Vector3UVE& groundColor,
                                                 float rayleighDensity, float strength,
                                                 const Math::Vector3UVE& sunDirection) noexcept;

/// The same, read straight off a Sky component.
[[nodiscard]] SkyAmbientUVE ResolveSkyAmbientUVE(const Sky3DComponentUVE& sky,
                                                 const Math::Vector3UVE& sunDirection) noexcept;

/// The fog colour this sky implies: the light distant geometry would be seen through, which is the
/// horizon band with a little of the ground under it. How far the scene actually moves toward it is
/// the caller's blend, because that is an authoring choice and this is physics.
[[nodiscard]] Math::Vector3UVE ResolveSkyFogColorUVE(const Math::Vector3UVE& groundColor,
                                                     const Math::Vector3UVE& horizonColor) noexcept;

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
