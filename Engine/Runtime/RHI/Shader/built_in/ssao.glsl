#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
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

uniform sampler2D uDepthTexture;
uniform mat4 uInverseProjection;
uniform mat4 uProjection;
uniform int uOrthographic;
// Backend depth-buffer decode: ndcZ = depthSample * uDepthNdcScale + uDepthNdcBias.
// OpenGL default depth range uses (2.0, -1.0); Vulkan [0, 1] depth range uses (1.0, 0.0).
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
// Fast 2-FLOP perspective view-space depth reconstruction parameters:
// viewZ = uProjB / max(uProjA - ndcZ, 1e-6), viewXY = (uv * 2 - 1) * (-viewZ) * vec2(uInvProjX, uInvProjY).
uniform float uProjA;
uniform float uProjB;
uniform float uInvProjX;
uniform float uInvProjY;
uniform int uFastReconstruct;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;
uniform float uPower;
uniform int uSampleCount;

// Stratified 12-sample low-discrepancy hemisphere kernel ordered so the first 4 (Low), 8 (Medium),
// and 12 (High) taps each span all quadrants and the full radius [0.35..0.95] * uRadius.
const int kKernelSize = 12;
const vec3 kKernel[12] = vec3[](
    vec3( 0.2400,  0.1400,  0.2200),
    vec3(-0.3600,  0.2900,  0.3400),
    vec3( 0.1900, -0.5200,  0.4100),
    vec3(-0.5800, -0.4400,  0.5200),
    vec3( 0.4200, -0.1500,  0.2600),
    vec3(-0.1800, -0.3500,  0.4800),
    vec3(-0.5600,  0.4200,  0.3800),
    vec3( 0.6200,  0.5100,  0.4500),
    vec3(-0.1200,  0.2600,  0.3100),
    vec3( 0.3400,  0.6400,  0.4400),
    vec3(-0.7100, -0.1800,  0.5600),
    vec3( 0.5400, -0.6200,  0.4900)
);

float DecodeNdcDepth(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPosition(vec2 uv, float depthSample) {
    float ndcZ = DecodeNdcDepth(depthSample);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 viewPos = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return viewPos.xyz / max(abs(viewPos.w), 1.0e-6);
}

// Reconstructs a clean view-space normal from 4-neighbor minimum-difference depth taps.
// Avoids screen-space derivative UB after early returns and prevents dark halos across depth edges.
vec3 ReconstructViewNormal(vec2 uv, vec3 centerViewPos) {
    vec2 texel = 1.0 / max(vec2(textureSize(uDepthTexture, 0)), vec2(1.0));
    vec2 halfTexel = texel * 0.5;
    vec2 uvL = clamp(uv - vec2(texel.x, 0.0), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvR = clamp(uv + vec2(texel.x, 0.0), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvD = clamp(uv - vec2(0.0, texel.y), halfTexel, vec2(1.0) - halfTexel);
    vec2 uvU = clamp(uv + vec2(0.0, texel.y), halfTexel, vec2(1.0) - halfTexel);

    float dL = texture(uDepthTexture, uvL).r;
    float dR = texture(uDepthTexture, uvR).r;
    float dD = texture(uDepthTexture, uvD).r;
    float dU = texture(uDepthTexture, uvU).r;

    vec3 pL = dL < 0.999999 ? ReconstructViewPosition(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPosition(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPosition(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPosition(uvU, dU) : centerViewPos;

    bool validL = dL < 0.999999;
    bool validR = dR < 0.999999;
    bool validD = dD < 0.999999;
    bool validU = dU < 0.999999;

    vec3 dPdx;
    if (validR && validL) {
        dPdx = abs(pR.z - centerViewPos.z) < abs(centerViewPos.z - pL.z)
                   ? (pR - centerViewPos)
                   : (centerViewPos - pL);
    } else if (validR) {
        dPdx = pR - centerViewPos;
    } else if (validL) {
        dPdx = centerViewPos - pL;
    } else {
        dPdx = vec3(1.0, 0.0, 0.0);
    }

    vec3 dPdy;
    if (validU && validD) {
        dPdy = abs(pU.z - centerViewPos.z) < abs(centerViewPos.z - pD.z)
                   ? (pU - centerViewPos)
                   : (centerViewPos - pD);
    } else if (validU) {
        dPdy = pU - centerViewPos;
    } else if (validD) {
        dPdy = centerViewPos - pD;
    } else {
        dPdy = vec3(0.0, 1.0, 0.0);
    }

    vec3 n = cross(dPdx, dPdy);
    float nLenSq = dot(n, n);
    vec3 viewNormal = nLenSq > 1.0e-12 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    if (isnan(viewNormal.x) || isnan(viewNormal.y) || isnan(viewNormal.z) ||
        isinf(viewNormal.x) || isinf(viewNormal.y) || isinf(viewNormal.z)) {
        viewNormal = vec3(0.0, 0.0, 1.0);
    }
    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    if (dot(viewNormal, towardEye) < 0.0) {
        viewNormal = -viewNormal;
    }
    return viewNormal;
}

// High-frequency blue-noise-like screen-space rotation with zero Moire patterns.
float InterleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Orthonormal basis from a unit normal without any pole singularity.
void BuildOrthonormalBasis(vec3 n, out vec3 b1, out vec3 b2) {
    if (n.z < -0.999999999) {
        b1 = vec3(0.0, -1.0, 0.0);
        b2 = vec3(-1.0, 0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    b1 = vec3(1.0 - n.x * n.x * a, b, -n.x);
    b2 = vec3(b, 1.0 - n.y * n.y * a, -n.y);
}

void main() {
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // Background/far plane: never occluded.
        return;
    }
    vec3 centerViewPos = ReconstructViewPosition(vTexCoord, centerDepth);
    vec3 viewNormal = ReconstructViewNormal(vTexCoord, centerViewPos);

    vec3 basisT;
    vec3 basisB;
    BuildOrthonormalBasis(viewNormal, basisT, basisB);
    float rotationAngle = InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530718;
    float cosAngle = cos(rotationAngle);
    float sinAngle = sin(rotationAngle);
    vec3 tangent = basisT * cosAngle + basisB * sinAngle;
    vec3 bitangent = -basisT * sinAngle + basisB * cosAngle;
    mat3 tbn = mat3(tangent, bitangent, viewNormal);

    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    float eyeLen = max(length(towardEye), 1.0e-5);
    float normalViewDot = max(dot(viewNormal, towardEye / eyeLen), 0.15);
    float distanceScale = uOrthographic != 0 ? 1.0 : clamp(abs(centerViewPos.z) * 0.02, 0.25, 4.0);
    float angleBias = max(uBias, uBias / normalViewDot) * distanceScale;
    float safeRadius = max(uRadius, 0.01);

    float occlusion = 0.0;
    int sampleCount = clamp(uSampleCount, 1, kKernelSize);
    int validSamples = 0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        vec3 samplePos = centerViewPos + (tbn * kKernel[sampleIndex]) * safeRadius;

        vec4 sampleClip = uProjection * vec4(samplePos, 1.0);
        if (sampleClip.w <= 0.0001) {
            continue;
        }
        vec2 sampleUv = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
        if (isnan(sampleUv.x) || isnan(sampleUv.y) || isinf(sampleUv.x) || isinf(sampleUv.y) ||
            sampleUv.x < 0.001 || sampleUv.x > 0.999 || sampleUv.y < 0.001 || sampleUv.y > 0.999) {
            continue;
        }

        float sampledDepth = texture(uDepthTexture, sampleUv).r;
        if (sampledDepth >= 0.999999) {
            ++validSamples;
            continue;
        }
        vec3 sampledViewPos = ReconstructViewPosition(sampleUv, sampledDepth);

        float depthDelta = abs(centerViewPos.z - sampledViewPos.z);
        float rangeCheck = 1.0 - smoothstep(safeRadius * 0.65, safeRadius * 1.5, depthDelta);
        float occluded = sampledViewPos.z >= samplePos.z + angleBias ? 1.0 : 0.0;
        occlusion += occluded * rangeCheck;
        ++validSamples;
    }

    float rawOcc = occlusion / float(max(validSamples, 1));
    float visibility = clamp(1.0 - rawOcc * max(uIntensity, 0.0), 0.08, 1.0);
    visibility = pow(visibility, clamp(uPower, 0.1, 4.0));
    if (isnan(visibility) || isinf(visibility)) {
        visibility = 1.0;
    }

    vec2 edgeDist = min(vTexCoord, 1.0 - vTexCoord);
    float edgeFade = smoothstep(0.0, 0.025, min(edgeDist.x, edgeDist.y));
    visibility = mix(1.0, visibility, edgeFade);

    FragColor = vec4(vec3(visibility), 1.0);
}
#endif
