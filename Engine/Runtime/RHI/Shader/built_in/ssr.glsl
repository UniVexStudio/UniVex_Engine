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

uniform sampler2D uColorTexture;
uniform sampler2D uDepthTexture;

uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform int uOrthographic;
uniform vec3 uCameraPosition;
uniform float uCameraNear;
uniform float uCameraFar;
uniform int uMaxSteps;
uniform float uDepthTolerance;
uniform float uFadeIn;
uniform float uFadeOut;

vec3 ReconstructViewPosUVE(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInverseProjection * clip;
    return view.xyz / max(abs(view.w), 1.0e-5);
}

void main() {
    float depth = texture(uDepthTexture, vTexCoord).r;
    if (depth >= 1.0) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 viewPos = ReconstructViewPosUVE(vTexCoord, depth);
    vec3 ddx = dFdx(viewPos);
    vec3 ddy = dFdy(viewPos);
    vec3 normal = normalize(cross(ddx, ddy));

    vec3 viewDir = uOrthographic != 0 ? vec3(0.0, 0.0, -1.0) : normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normal));

    if (reflectDir.z >= 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    int steps = clamp(uMaxSteps, 8, 128);
    float stepSize = 0.25;
    float currentDist = 0.1;
    vec3 hitColor = vec3(0.0);
    float hitFactor = 0.0;

    for (int i = 0; i < steps; ++i) {
        currentDist += stepSize;
        vec3 rayPos = viewPos + reflectDir * currentDist;
        if (rayPos.z > -uCameraNear || rayPos.z < -uCameraFar) {
            break;
        }

        vec4 proj = uProjection * vec4(rayPos, 1.0);
        if (proj.w <= 0.0) {
            break;
        }
        vec3 screenPos = proj.xyz / proj.w * 0.5 + 0.5;
        if (screenPos.x < 0.0 || screenPos.x > 1.0 || screenPos.y < 0.0 || screenPos.y > 1.0) {
            break;
        }

        float sampledDepth = texture(uDepthTexture, screenPos.xy).r;
        if (sampledDepth >= 1.0) {
            continue;
        }

        vec3 sampledViewPos = ReconstructViewPosUVE(screenPos.xy, sampledDepth);
        float depthDiff = sampledViewPos.z - rayPos.z;

        if (depthDiff >= 0.0 && depthDiff < max(uDepthTolerance, 0.05)) {
            vec2 edgeDist = min(screenPos.xy, 1.0 - screenPos.xy);
            float edgeFade = smoothstep(0.0, 0.1, min(edgeDist.x, edgeDist.y));
            float fresnel = clamp(pow(1.0 + dot(viewDir, normal), 3.0), 0.1, 1.0);

            hitColor = texture(uColorTexture, screenPos.xy).rgb;
            hitFactor = edgeFade * fresnel;
            break;
        }
    }

    FragColor = vec4(hitColor * hitFactor, hitFactor);
}
#endif
