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
    sky = mix(sky, max(uGroundColor, vec3(0.0)) * (0.18 * day), pow(1.0 - clamp(viewY, 0.0, 1.0), 6.0) * 0.22);

    float g = mix(0.76, 0.93, sunset);
    float mieDen = 1.0 + g * g - 2.0 * g * mu;
    float mie = (1.0 - g * g) / max(pow(max(mieDen, 0.001), 1.5), 0.001);
    vec3 sunCol = max(uSunColor, vec3(0.0)) * max(uSunEnergy, 0.0);
    sky += sunCol * mie * mix(0.012, 0.09, sunset) * day;

    float disk = smoothstep(0.99945, 0.99982, mu) * smoothstep(-0.02, 0.04, sunY);
    sky += sunCol * disk * mix(7.5, 16.0, sunset);
    return max(sky, vec3(0.0));
}

vec3 ProceduralSkyUVE(vec3 viewDir) {
    vec3 dir = normalize(viewDir);
    vec3 sun = length(uSunDirection) > 1.0e-5 ? normalize(uSunDirection) : vec3(0.0, 1.0, 0.0);
    if (dir.y >= 0.0) {
        return AtmosphereUpperUVE(dir, sun);
    }
    vec3 horizonDir = normalize(vec3(dir.x, 0.001, dir.z));
    vec3 horizon = AtmosphereUpperUVE(horizonDir, sun);
    float day = smoothstep(-0.08, 0.18, sun.y);
    vec3 ground = max(uGroundColor, vec3(0.0)) * (0.12 + 0.88 * day);
    float groundT = pow(clamp(-dir.y, 0.0, 1.0), max(uGroundCurve, 0.001));
    return mix(horizon, ground, groundT);
}

void main() {
    if (texture(uSceneDepthTexture, vTexCoord).r < 1.0) {
        discard;
    }
    vec2 ndc = vTexCoord * 2.0 - 1.0;
    vec3 view = vec3(ndc.x * max(uTanHalfFov, 0.0) * max(uAspect, 0.0001), ndc.y * max(uTanHalfFov, 0.0), -1.0);
    vec3 worldDir = normalize(uCameraRight * view.x + uCameraUp * view.y + uCameraForward);
    FragColor = vec4(ProceduralSkyUVE(worldDir), 1.0);
}
#endif
