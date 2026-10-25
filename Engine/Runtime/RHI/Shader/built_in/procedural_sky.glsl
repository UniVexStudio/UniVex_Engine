#version 450 core

#ifdef VERTEX_SHADER
out vec2 vTexCoord;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneDepthTexture;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec3 uSkyColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform float uSkyCurve;
uniform float uGroundCurve;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunEnergy;
uniform float uSunAngularDiameter;
uniform float uSunDiscIntensity;
uniform float uSunLimbDarkening;
uniform int uAtmosphericSky;
uniform float uAtmosphereRayleigh;
uniform float uAtmosphereMie;
uniform float uAtmosphereOzone;
uniform float uAtmosphereMieAnisotropy;
uniform float uAtmosphereMultiScatter;
uniform float uAtmosphereIntensity;
uniform int uAtmosphereSteps;
uniform vec3 uCameraPosition;
uniform float uSkyTime;
uniform float uStarDensity;
uniform float uStarBrightness;
uniform float uStarSeed;
uniform float uMilkyWayStrength;
uniform int uCloudsEnabled;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudAltitude;
uniform vec2 uCloudWind;
uniform float uCloudSoftness;
uniform float uCloudSilverLining;
uniform sampler2D uSkyTexture;
uniform int uSkyTextureEnabled;
// One multiplier over every source, applied at the end: how bright this sky is, whichever of the
// three drew it. A source's own controls shape it; this is the only term that says "more" or
// "less" without changing what the sky is.
uniform float uSkyLuminance;

// The disc: a body of the light the scene is lit by, not a texture on the sky. Its size is the
// light's own angular diameter, so a moon and a sun over the same sky differ in this term and
// nothing else. cos() of the half-angle is the dot product at the rim, which is why the whole test
// is a comparison against mu and needs no branch or texture fetch.
//
// The rim is feathered over the outer 43% of the radius rather than drawn as a hard circle: at
// ordinary viewport sizes and with no anti-aliasing on this pass, an unfeathered disc at a
// realistic angular size flickers between frames as the view moves under it.
//
// Shared by both skies below, because the disc belongs to the light in either case. It is a 0
// when the light asks for no disc, so the caller can add it unconditionally.
float SunDiscUVE(float mu, float sunY) {
    float sunRadius = radians(max(uSunAngularDiameter, 0.0)) * 0.5;
    if (sunRadius <= 0.0) {
        return 0.0;
    }
    float cosRim = cos(sunRadius);
    float cosCore = cos(sunRadius * 0.57);
    float mask = smoothstep(cosRim, cosCore, mu);
    // Limb darkening: the disc's own surface is dimmer at its edge than at its centre, because the
    // line of sight leaves the photosphere at a shallower angle there. 0 is a flat disc.
    float rimT = clamp((1.0 - mu) / max(1.0 - cosRim, 1.0e-9), 0.0, 1.0);
    float limb = 1.0 - clamp(uSunLimbDarkening, 0.0, 1.0) * (1.0 - sqrt(max(1.0 - rimT, 0.0)));
    return mask * limb * smoothstep(-0.02, 0.04, sunY) * max(uSunDiscIntensity, 0.0);
}

vec3 AtmosphereUpperUVE(vec3 dir, vec3 sun) {
    float mu = clamp(dot(dir, sun), -1.0, 1.0);
    float sunY = sun.y;
    float viewY = dir.y;
    float day = smoothstep(-0.08, 0.18, sunY);
    float sunset = exp(-pow(sunY * 6.0, 2.0)) * smoothstep(-0.15, 0.0, sunY);

    vec3 noonZenith = max(uSkyColor, vec3(0.0));
    vec3 noonHorizon = max(uHorizonColor, vec3(0.0));
    vec3 sunsetZenith = vec3(0.07, 0.09, 0.26);
    vec3 sunsetHorizon = vec3(1.0, 0.36, 0.08);
    vec3 nightZenith = vec3(0.004, 0.006, 0.018);
    vec3 nightHorizon = vec3(0.018, 0.028, 0.055);

    vec3 zenith = mix(nightZenith, noonZenith, day);
    zenith = mix(zenith, sunsetZenith, sunset);
    vec3 horizon = mix(nightHorizon, noonHorizon, day);
    horizon = mix(horizon, sunsetHorizon, sunset);
    float towardSun = pow(max(mu, 0.0), 3.5);
    horizon = mix(horizon, sunsetHorizon, towardSun * max(sunset, (1.0 - day) * 0.25));

    vec3 sky = mix(horizon, zenith, pow(clamp(viewY, 0.0, 1.0), max(uSkyCurve, 0.001)));
    sky *= mix(1.0, 1.55, day * (1.0 - sunset));
    sky = mix(sky, max(uGroundColor, vec3(0.0)) * (0.05 * day), pow(1.0 - clamp(viewY, 0.0, 1.0), 8.0) * 0.08);

    float g = mix(0.76, 0.93, sunset);
    float mieDen = 1.0 + g * g - 2.0 * g * mu;
    float mie = (1.0 - g * g) / max(pow(max(mieDen, 0.001), 1.5), 0.001);
    vec3 sunCol = max(uSunColor, vec3(1.0)) * max(uSunEnergy, 1.0);
    sky += sunCol * mie * mix(0.012, 0.09, sunset) * day;

    sky += sunCol * SunDiscUVE(mu, sunY) * mix(7.5, 16.0, sunset);
    return max(sky, vec3(0.0));
}

// ---- Physical atmosphere ----------------------------------------------------------------
//
// Single scattering through a spherical shell of air. The shape of the sky is a result here, not
// an input: a blue zenith, a pale horizon and a red sunset all fall out of three coefficients and
// the distance the light travelled through each of them.
//
// The numbers are Earth's, in metres and inverse metres, because that is the only way the model
// stays honest - the planet's radius sets how fast the air thins, and a coefficient in invented
// units would produce a sky that only looks right at one sun angle.
const float kPlanetRadiusUVE = 6360000.0;
const float kAtmosphereRadiusUVE = 6420000.0;
const float kRayleighScaleHeightUVE = 8000.0;
const float kMieScaleHeightUVE = 1200.0;
const float kOzoneCenterUVE = 25000.0;
const float kOzoneWidthUVE = 15000.0;
// Rayleigh scattering per metre at sea level, by channel. Blue scatters roughly six times as
// strongly as red, which is the entire reason the sky is blue.
const vec3 kRayleighScatterUVE = vec3(5.8e-6, 13.5e-6, 33.1e-6);
// Ozone absorbs in the Chappuis band - mostly green, some red - and almost nowhere else. Its tent
// profile sits high in the stratosphere, and it is what turns a twilight sky from grey to violet.
const vec3 kOzoneAbsorbUVE = vec3(0.650e-6, 1.881e-6, 0.085e-6);
const float kMieScatterUVE = 21.0e-6;
// Haze both scatters and absorbs; the ratio is its single-scattering albedo, here about 0.91.
const float kMieExtinctionUVE = 1.1;
// The sun's radiance into this model. Chosen so a noon sky lands in the same range as the lit
// surfaces around it, which keeps exposure and bloom settings from needing two answers.
const float kSkySunRadianceUVE = 14.0;
// Where the eye sits above the ground, in metres. The air above a person is what a person sees.
const float kObserverHeightUVE = 120.0;
const int kLightStepsUVE = 4;
// How much of the extinction the multiple-scattering stand-in is allowed to feel. Re-scattered
// light arrives by many paths, so its own path is not the straight line to the sun.
const float kMultiScatterTauUVE = 0.12;
const float kMultiScatterGainUVE = 0.9;
// Above this much direct light the sky is already bright and the stand-in contributes nothing.
const float kMultiScatterFillUVE = 0.5;

struct AtmosphereDepthUVE {
    float rayleigh;
    float mie;
    float ozone;
};

// Entry and exit distances of a ray through a sphere centred on the planet. False when the ray
// misses it entirely; either root may be negative depending on where the ray starts.
bool RaySphereUVE(vec3 origin, vec3 dir, float radius, out float tNear, out float tFar) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        tNear = 0.0;
        tFar = 0.0;
        return false;
    }
    float root = sqrt(discriminant);
    tNear = -b - root;
    tFar = -b + root;
    return tFar > 0.0;
}

// How much air one path passes through, in the same units the coefficients are per.
AtmosphereDepthUVE AtmosphereOpticalDepthUVE(vec3 origin, vec3 dir, float pathLength, const int steps,
                                             const float rayleigh, const float mie, const float ozone) {
    AtmosphereDepthUVE depth;
    depth.rayleigh = 0.0;
    depth.mie = 0.0;
    depth.ozone = 0.0;
    if (pathLength <= 0.0 || steps <= 0) {
        return depth;
    }
    // Not named `length`: GLSL would resolve the local before the built-in and every length()
    // call inside this function would stop compiling.
    float segment = pathLength / float(steps);
    for (int i = 0; i < steps; ++i) {
        vec3 point = origin + dir * (segment * (float(i) + 0.5));
        // Clamped at the ground: a sample that lands a centimetre under the surface is a rounding
        // artifact of the ray's exit point, not air at negative altitude, and exp() of a negative
        // height would hand back a density larger than the ground itself has.
        float height = max(length(point) - kPlanetRadiusUVE, -1.0);
        depth.rayleigh += exp(-height / kRayleighScaleHeightUVE) * rayleigh * segment;
        depth.mie += exp(-height / kMieScaleHeightUVE) * mie * segment;
        depth.ozone += max(0.0, 1.0 - abs(height - kOzoneCenterUVE) / kOzoneWidthUVE) * ozone * segment;
    }
    return depth;
}

vec3 PhysicalSkyUVE(vec3 viewDir, vec3 sunDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(sunDir) > 1.0e-5 ? normalize(sunDir) : vec3(0.0, 1.0, 0.0);
    vec3 eye = vec3(0.0, kPlanetRadiusUVE + kObserverHeightUVE, 0.0);

    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 ground = max(uGroundColor, vec3(0.0)) * mix(0.35, 1.0, day);

    float groundNear = 0.0;
    float groundFar = 0.0;
    if (RaySphereUVE(eye, dir, kPlanetRadiusUVE, groundNear, groundFar) && groundNear > 0.0) {
        // The ray meets the ground: there is no sky along it, only terrain the level may or may
        // not have drawn. The band stands in for the missing geometry.
        return ground;
    }

    float skyNear = 0.0;
    float skyFar = 0.0;
    if (!RaySphereUVE(eye, dir, kAtmosphereRadiusUVE, skyNear, skyFar)) {
        return ground;
    }

    int steps = clamp(uAtmosphereSteps, 4, 64);
    float rayleigh = max(uAtmosphereRayleigh, 0.0);
    float mie = max(uAtmosphereMie, 0.0);
    float ozone = max(uAtmosphereOzone, 0.0);

    float segment = skyFar / float(steps);
    float travelledRayleigh = 0.0;
    float travelledMie = 0.0;
    float travelledOzone = 0.0;
    vec3 scatteredRayleigh = vec3(0.0);
    vec3 scatteredMie = vec3(0.0);
    vec3 scatteredMultiple = vec3(0.0);

    for (int i = 0; i < steps; ++i) {
        vec3 point = eye + dir * (segment * (float(i) + 0.5));
        float height = max(length(point) - kPlanetRadiusUVE, -1.0);
        float densityRayleigh = exp(-height / kRayleighScaleHeightUVE) * rayleigh;
        float densityMie = exp(-height / kMieScaleHeightUVE) * mie;
        float densityOzone = max(0.0, 1.0 - abs(height - kOzoneCenterUVE) / kOzoneWidthUVE) * ozone;
        travelledRayleigh += densityRayleigh * segment;
        travelledMie += densityMie * segment;
        travelledOzone += densityOzone * segment;

        // Shadowed air: a sample whose line to the sun is blocked by the planet contributes no
        // light at all, which is what makes the sunset terminator a sharp edge on the ground.
        float sunNear = 0.0;
        float sunFar = 0.0;
        if (RaySphereUVE(point, sun, kPlanetRadiusUVE, sunNear, sunFar) && sunNear > 0.0) {
            continue;
        }
        float lightNear = 0.0;
        float lightFar = 0.0;
        float lightPath = RaySphereUVE(point, sun, kAtmosphereRadiusUVE, lightNear, lightFar) ? lightFar : 0.0;
        AtmosphereDepthUVE toSun =
            AtmosphereOpticalDepthUVE(point, sun, lightPath, kLightStepsUVE, rayleigh, mie, ozone);

        vec3 extinction = kRayleighScatterUVE * (travelledRayleigh + toSun.rayleigh) +
                          vec3(kMieScatterUVE * kMieExtinctionUVE) * (travelledMie + toSun.mie) +
                          kOzoneAbsorbUVE * (travelledOzone + toSun.ozone);
        vec3 transmittance = exp(-extinction);
        scatteredRayleigh += densityRayleigh * segment * transmittance;
        scatteredMie += densityMie * segment * transmittance;
        // The stand-in: the same air, but its light is allowed only part of the extinction it
        // would have earned on a straight path, because it did not take one.
        scatteredMultiple +=
            (densityRayleigh * kRayleighScatterUVE + vec3(densityMie * kMieScatterUVE)) *
            exp(-extinction * kMultiScatterTauUVE) * segment;
    }

    // Rayleigh scatters almost evenly in both directions; the 1 + mu^2 is its charge-dipole
    // pattern, which is why the sky is slightly brighter toward and away from the sun than across.
    float mu = clamp(dot(dir, sun), -1.0, 1.0);
    float phaseRayleigh = (3.0 / (16.0 * 3.14159265358979323846)) * (1.0 + mu * mu);
    // Henyey-Greenstein for the haze: g at 0.76 puts most of its light in a forward cone, which
    // is the bright halo that reaches across the sky near the sun at any hour.
    float g = clamp(uAtmosphereMieAnisotropy, -0.999, 0.999);
    float g2 = g * g;
    float phaseMie = (3.0 / (8.0 * 3.14159265358979323846)) * ((1.0 - g2) * (1.0 + mu * mu)) /
                     ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * mu, 1.0e-4), 1.5));

    float radiance = max(uAtmosphereIntensity, 0.0) * kSkySunRadianceUVE * max(uSunEnergy, 0.0);
    vec3 direct = radiance * (scatteredRayleigh * kRayleighScatterUVE * phaseRayleigh +
                              scatteredMie * (kMieScatterUVE * phaseMie));

    // Gated on where single scattering came up short instead of added everywhere: added flat, it
    // whitens the sunset it should be filling in, and the reddest part of the sky is exactly the
    // part that needs it least.
    float luminance = dot(direct, vec3(0.2126, 0.7152, 0.0722));
    float fill = clamp(1.0 - luminance / kMultiScatterFillUVE, 0.0, 1.0);
    vec3 multiple = radiance * scatteredMultiple *
                    (kMultiScatterGainUVE * max(uAtmosphereMultiScatter, 0.0) /
                     (4.0 * 3.14159265358979323846)) *
                    fill;

    vec3 sky = direct + multiple;
    // The disc is added inside the atmosphere, not on top of it, so its own light is dimmed by the
    // same air the rest of the sky came through. At the horizon that is what reddens it.
    sky += max(uSunColor, vec3(0.0)) * SunDiscUVE(mu, sun.y) * mix(7.5, 16.0, 1.0 - day);
    vec3 nightSky = vec3(0.004, 0.006, 0.018);
    return max(sky * day + nightSky * (1.0 - day), vec3(0.0));
}

// ---- Night and cloud ---------------------------------------------------------------------
//
// The sky so far is what the air does to sunlight. These two are what is in front of and behind
// that air: a star field with a galactic band behind it, and a cloud deck in front. Both are
// overlays rather than parts of either sky model, so they draw over the gradient and the
// scattering model alike and are occluded in the same order by the same rule.

// A cheap integer hash. Written with 32-bit multiplies instead of sin(): sin() is fast to type and
// slow to run, and on some drivers its low bits are not random enough for a star field, which
// shows up as visible diagonal banding in the noise.
uint SkyHashUVE(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float SkyHashUnitUVE(uint x) {
    return float(x & 0x00FFFFFFU) * (1.0 / 16777215.0);
}

float SkyCellNoiseUVE(ivec3 cell) {
    uint h = SkyHashUVE(uint(cell.x) * 0x9e3779b1U);
    h = SkyHashUVE(h ^ (uint(cell.y) * 0x85ebca77U));
    h = SkyHashUVE(h ^ (uint(cell.z) * 0xc2b2ae3dU));
    return SkyHashUnitUVE(h);
}

vec4 SkyCellRandomsUVE(ivec3 cell, float seed) {
    uint h = SkyHashUVE(uint(cell.x) * 0x9e3779b1U);
    h = SkyHashUVE(h ^ (uint(cell.y) * 0x85ebca77U));
    h = SkyHashUVE(h ^ (uint(cell.z) * 0xc2b2ae3dU));
    h = SkyHashUVE(h ^ uint(max(seed, 0.0)));
    return vec4(SkyHashUnitUVE(h), SkyHashUnitUVE(SkyHashUVE(h)),
                SkyHashUnitUVE(SkyHashUVE(h ^ 0x68bc21ebU)),
                SkyHashUnitUVE(SkyHashUVE(h ^ 0x02e5be93U)));
}

// Value noise, 2D and 3D. Smoothstep-interpolated hashes rather than a gradient noise: the
// difference is invisible at cloud frequency, and this way the whole thing is integer hashing with
// no table to upload or keep in sync.
float SkyNoiseUVE(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    ivec3 base = ivec3(int(cell.x), int(cell.y), 0);
    float a = SkyCellNoiseUVE(base);
    float b = SkyCellNoiseUVE(base + ivec3(1, 0, 0));
    float c = SkyCellNoiseUVE(base + ivec3(0, 1, 0));
    float d = SkyCellNoiseUVE(base + ivec3(1, 1, 0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float SkyNoiseUVE(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 base = ivec3(cell);
    float c000 = SkyCellNoiseUVE(base);
    float c100 = SkyCellNoiseUVE(base + ivec3(1, 0, 0));
    float c010 = SkyCellNoiseUVE(base + ivec3(0, 1, 0));
    float c110 = SkyCellNoiseUVE(base + ivec3(1, 1, 0));
    float c001 = SkyCellNoiseUVE(base + ivec3(0, 0, 1));
    float c101 = SkyCellNoiseUVE(base + ivec3(1, 0, 1));
    float c011 = SkyCellNoiseUVE(base + ivec3(0, 1, 1));
    float c111 = SkyCellNoiseUVE(base + ivec3(1, 1, 1));
    float bottom = mix(mix(c000, c100, u.x), mix(c010, c110, u.x), u.y);
    float top = mix(mix(c001, c101, u.x), mix(c011, c111, u.x), u.y);
    return mix(bottom, top, u.z);
}

float SkyFbmUVE(vec2 p, const int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += SkyNoiseUVE(p) * amplitude;
        total += amplitude;
        p = p * 2.03 + vec2(17.1, 9.7);
        amplitude *= 0.5;
    }
    return sum / max(total, 1.0e-5);
}

float SkyFbmUVE(vec3 p, const int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += SkyNoiseUVE(p) * amplitude;
        total += amplitude;
        p = p * 2.03 + vec3(5.3, 11.7, 3.1);
        amplitude *= 0.5;
    }
    return sum / max(total, 1.0e-5);
}

// Stars: one candidate per cell on a shell of radius uStarDensity around the eye, so the cells are
// all the same angular size and the field has no seam or pole where they bunch up. The neighbours
// are checked too, because a star placed near a cell boundary is partly in the next cell and would
// otherwise be cut in half.
float StarFieldUVE(vec3 dir) {
    if (uStarBrightness <= 0.0 || uStarDensity <= 0.0) {
        return 0.0;
    }
    vec3 p = dir * uStarDensity;
    ivec3 base = ivec3(floor(p));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                ivec3 cell = base + ivec3(x, y, z);
                vec4 r = SkyCellRandomsUVE(cell, uStarSeed);
                // Most cells are empty; this threshold is what makes the sky mostly dark. It uses
                // a different channel from the size and the brightness on purpose - sharing one
                // would make every star the same, because a cell that passes the threshold already
                // has its threshold channel near 1.
                if (r.w < 0.97) {
                    continue;
                }
                vec3 centre = vec3(cell) + vec3(0.15 + 0.7 * r.x, 0.15 + 0.7 * r.y, 0.15 + 0.7 * r.z);
                float angle = acos(clamp(dot(dir, normalize(centre)), -1.0, 1.0));
                // 0.0004 to 0.0016 radians is roughly half a pixel to two pixels at a 60-degree
                // field of view, which is the range where a star is a point on screen and not a
                // disc. Squaring the hash packs most stars near the small end.
                float radius = mix(0.00040, 0.0016, r.z * r.z);
                float magnitude = mix(0.18, 1.0, r.y * r.y);
                sum += (1.0 - smoothstep(0.0, radius, angle)) * magnitude;
            }
        }
    }
    return sum;
}

// The galactic band: a great circle of concentrated starlight, with dust lanes through it. The
// band is the plane through the eye whose normal is fixed, so it passes through the zenith at a
// set tilt rather than sitting at a constant altitude.
float MilkyWayUVE(vec3 dir) {
    if (uMilkyWayStrength <= 0.0) {
        return 0.0;
    }
    const vec3 normal = vec3(0.3714, 0.7428, 0.5571);
    float distanceFromBand = dot(dir, normal);
    float band = exp(-distanceFromBand * distanceFromBand * 90.0);
    if (band < 0.002) {
        return 0.0;
    }
    // Stretched noise along the band: a rift reads as structure, isotropic noise reads as fog.
    float broad = SkyFbmUVE(dir * 5.0, 4);
    float fine = SkyFbmUVE(dir * 14.0, 3);
    float stars = SkyFbmUVE(dir * 40.0, 2);
    float dust = smoothstep(0.30, 0.80, broad * 0.62 + fine * 0.38);
    return band * dust * (0.55 + 0.45 * stars);
}

// The cloud deck: a plane at uCloudAltitude, sampled where the view ray crosses it. Flat rather
// than volumetric, which is the honest trade - it is one texture lookup's worth of cost and it
// reads correctly at every angle except straight up through a broken deck.
vec4 CloudsUVE(vec3 dir, vec3 sun, float day) {
    if (uCloudsEnabled == 0 || dir.y <= 0.004 || uCloudAltitude <= 0.0) {
        return vec4(0.0);
    }
    float travel = uCloudAltitude / dir.y;
    // Bounded so a ray skimming the horizon does not project to infinity and sample the same
    // stretched texel across the whole bottom of the screen.
    travel = min(travel, 60000.0);
    vec3 crossing = uCameraPosition + dir * travel;
    // One noise cell is about 830 metres of deck at this scale, with the octaves below it taking
    // the detail down to roughly a hundred - the range where a cumulus edge still reads as an edge
    // rather than as a smear.
    vec2 uv = (crossing.xz + uCloudWind * uSkyTime) * 0.0012;

    float broad = SkyFbmUVE(uv, 4);
    float detail = SkyFbmUVE(uv * 3.7 + vec2(31.4, 12.9), 3);
    // The threshold is the coverage: raising it pushes the noise field further under the cut, so
    // more of the sky is clear. Erring the field by a second, finer noise is what stops the deck
    // reading as one smooth blob and gives it ragged edges.
    float threshold = 1.0 - clamp(uCloudCoverage, 0.0, 1.0);
    float softness = max(uCloudSoftness, 0.001);
    float value = broad + (detail - 0.5) * 0.28;
    float amount = smoothstep(threshold, threshold + softness, value);
    amount *= clamp(uCloudDensity, 0.0, 1.0);
    // Faded out near the horizon. A flat deck projects to infinitely compressed detail there, and
    // the compressed stripes shimmer as the camera turns; the fade is also what the atmosphere
    // itself would do to a deck seen at that angle.
    amount *= smoothstep(0.0, 0.06, dir.y);
    if (amount <= 0.001) {
        return vec4(0.0);
    }

    // Thickness from the same field: the middle of a cloud is thicker than its edge, and the
    // difference is what makes it read as a body instead of a stencil.
    float thickness = smoothstep(threshold, min(threshold + softness * 3.0, 1.0), value);
    float mu = clamp(dot(dir, sun), 0.0, 1.0);
    // Silver lining: thin cloud toward the sun transmits it, and the brighter the thinner it is.
    // The pow() on mu keeps the effect near the sun rather than washing the whole deck.
    float lining = pow(mu, 6.0) * (1.0 - thickness) * max(uCloudSilverLining, 0.0);
    float forward = pow(mu, 2.0);

    // Underside in shade, topside lit. At night only the moonlit value survives, which is why the
    // night factor is applied to the lit term and not to the base colour.
    vec3 shade = vec3(0.30, 0.33, 0.40);
    vec3 lit = vec3(0.96, 0.97, 1.0);
    vec3 body = mix(shade, lit, thickness * 0.75 + 0.25 * forward);
    // Warm the deck at a low sun, so it takes the horizon colour the rest of the sky has.
    float lowSun = smoothstep(0.35, 0.0, sun.y) * day;
    body = mix(body, body * vec3(1.18, 0.94, 0.78), lowSun * 0.7);
    body = body * mix(0.10, 1.0, day);
    body += max(uSunColor, vec3(0.0)) * lining * day * 2.4;

    return vec4(max(body, vec3(0.0)), amount);
}

// Draws the two night layers and the deck in the order light arrives at the eye: stars and the
// band are behind the air, cloud is in front of it. Called by both sky models, so a scene that
// switches source keeps its stars and its weather.
vec3 ApplySkyOverlaysUVE(vec3 base, vec3 dir, vec3 sun, float day) {
    float night = 1.0 - smoothstep(-0.08, 0.18, sun.y);
    if (uStarBrightness > 0.0 && night > 0.002) {
        // Slightly cool white: real starlight is not neutral, and a pure white field reads as
        // noise rather than as sky.
        vec3 tint = vec3(0.86, 0.92, 1.0);
        base += tint * (StarFieldUVE(dir) * uStarBrightness * night);
        base += tint * (MilkyWayUVE(dir) * uMilkyWayStrength * night * 0.35);
    }
    vec4 cloud = CloudsUVE(dir, sun, day);
    return mix(base, cloud.rgb, cloud.a);
}

vec3 ProceduralSkyUVE(vec3 viewDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);

    // A scene choosing Sky > Atmosphere gets the scattering model instead of the gradient. Both
    // read the same sun and the same ground colour, so switching between them in the Inspector
    // changes how the sky is built without changing what it is lit by.
    if (uAtmosphericSky != 0) {
        return ApplySkyOverlaysUVE(PhysicalSkyUVE(dir, sun), dir, sun, smoothstep(-0.08, 0.18, sun.y));
    }

    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 groundBase = max(uGroundColor, vec3(0.0));
    vec3 groundLit = groundBase * mix(0.40, 1.0, day);
    vec3 horizonLit = max(uHorizonColor, vec3(0.0)) * mix(0.35, 1.0, day);

    if (dir.y < -0.001) {
        float groundT = clamp(-dir.y, 0.0, 1.0);
        float groundCurve = clamp(max(uGroundCurve, 0.01), 0.1, 4.0);
        float falloff = pow(groundT, groundCurve);
        vec3 ground = mix(horizonLit, groundLit, falloff);
        float seamBlend = smoothstep(-0.015, 0.0, dir.y);
        return mix(ground, horizonLit, seamBlend);
    }

    vec3 upperSky = AtmosphereUpperUVE(dir, sun);
    float horizonBlend = smoothstep(0.0, 0.015, dir.y);
    return mix(horizonLit, upperSky, horizonBlend);
}

/// The gradient sky with the night and cloud overlays on top. Separate from the body above so the
/// ground branch below the horizon can return early without skipping them.
vec3 GradientSkyWithOverlaysUVE(vec3 dir, vec3 sun) {
    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 base = ProceduralSkyUVE(dir);
    // Below the horizon there is no sky to overlay: the ground band already stands in for the
    // terrain, and stars under it would be visible through the floor.
    if (dir.y < -0.001) {
        return base;
    }
    return ApplySkyOverlaysUVE(base, dir, sun, day);
}

vec2 SkyEquirectUvUVE(vec3 dir) {
    vec3 d = normalize(dir);
    float longitude = 0.0;
    if (abs(d.x) + abs(d.z) >= 1.0e-8) {
        longitude = atan(d.z, d.x);
    }
    float latitude = asin(clamp(d.y, -1.0, 1.0));
    return vec2(longitude * (1.0 / (2.0 * 3.14159265358979323846)) + 0.5,
                latitude * (1.0 / 3.14159265358979323846) + 0.5);
}

void main() {
    if (texture(uSceneDepthTexture, vTexCoord).r < 1.0) {
        discard;
    }
    vec2 ndc = vTexCoord * 2.0 - 1.0;
    vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
    vec3 worldDir = normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
    // Same guard the sky functions use internally: a zero direction has no normalize.
    vec3 sunDir = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
    vec3 color;
    if (uSkyTextureEnabled != 0) {
        // A panorama replaces the sky, not the weather: cloud and stars still sit in front of and
        // behind it, so a photographed sky can have a moving deck over it.
        color = texture(uSkyTexture, SkyEquirectUvUVE(worldDir)).rgb;
        if (worldDir.y > -0.001) {
            color = ApplySkyOverlaysUVE(color, worldDir, sunDir, smoothstep(-0.08, 0.18, sunDir.y));
        }
    } else if (uAtmosphericSky != 0) {
        color = ProceduralSkyUVE(worldDir);
    } else {
        color = GradientSkyWithOverlaysUVE(worldDir, sunDir);
    }
    // Clamped at zero and multiplied last, so a source can never come out negative and the dial
    // means the same thing for a panorama, a gradient and a scattering sky.
    FragColor = vec4(max(color, vec3(0.0)) * max(uSkyLuminance, 0.0), 1.0);
}
#endif
