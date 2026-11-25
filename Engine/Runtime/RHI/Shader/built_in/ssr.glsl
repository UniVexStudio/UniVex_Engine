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
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uProjA;
uniform float uProjB;
uniform float uInvProjX;
uniform float uInvProjY;
uniform int uFastReconstruct;
uniform vec3 uCameraPosition;
uniform float uCameraNear;
uniform float uCameraFar;
uniform int uMaxSteps;
uniform float uDepthTolerance;
uniform float uFadeIn;
uniform float uFadeOut;

float DecodeNdcDepth(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

vec3 ReconstructViewPos(vec2 uv, float depth) {
    float ndcZ = DecodeNdcDepth(depth);
    vec2 ndcXY = uv * 2.0 - 1.0;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        float viewZ = uProjB / max(uProjA - ndcZ, 1.0e-6);
        float negZ = -viewZ;
        return vec3(ndcXY.x * negZ * uInvProjX, ndcXY.y * negZ * uInvProjY, viewZ);
    }
    vec4 view = uInverseProjection * vec4(ndcXY, ndcZ, 1.0);
    return view.xyz / max(abs(view.w), 1.0e-5);
}

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

    vec3 pL = dL < 0.999999 ? ReconstructViewPos(uvL, dL) : centerViewPos;
    vec3 pR = dR < 0.999999 ? ReconstructViewPos(uvR, dR) : centerViewPos;
    vec3 pD = dD < 0.999999 ? ReconstructViewPos(uvD, dD) : centerViewPos;
    vec3 pU = dU < 0.999999 ? ReconstructViewPos(uvU, dU) : centerViewPos;

    vec3 dPdx = (dR < 0.999999 && dL < 0.999999)
                    ? (abs(pR.z - centerViewPos.z) < abs(centerViewPos.z - pL.z)
                           ? (pR - centerViewPos)
                           : (centerViewPos - pL))
                    : (dR < 0.999999 ? (pR - centerViewPos)
                                   : (dL < 0.999999 ? (centerViewPos - pL) : vec3(1.0, 0.0, 0.0)));
    vec3 dPdy = (dU < 0.999999 && dD < 0.999999)
                    ? (abs(pU.z - centerViewPos.z) < abs(centerViewPos.z - pD.z)
                           ? (pU - centerViewPos)
                           : (centerViewPos - pD))
                    : (dU < 0.999999 ? (pU - centerViewPos)
                                   : (dD < 0.999999 ? (centerViewPos - pD) : vec3(0.0, 1.0, 0.0)));

    vec3 n = cross(dPdx, dPdy);
    float nLenSq = dot(n, n);
    vec3 normal = nLenSq > 1.0e-12 ? n * inversesqrt(nLenSq) : vec3(0.0, 0.0, 1.0);
    vec3 towardEye = uOrthographic != 0 ? vec3(0.0, 0.0, 1.0) : -centerViewPos;
    if (dot(normal, towardEye) < 0.0) {
        normal = -normal;
    }
    return normal;
}

float InterleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    float depth = texture(uDepthTexture, vTexCoord).r;
    if (depth >= 0.999999) {
        FragColor = vec4(0.0);
        return;
    }

    vec3 viewPos = ReconstructViewPos(vTexCoord, depth);
    vec3 normal = ReconstructViewNormal(vTexCoord, viewPos);
    vec3 viewDir = uOrthographic != 0 ? vec3(0.0, 0.0, -1.0) : normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, normal));

    if (reflectDir.z >= 0.0) {
        FragColor = vec4(0.0);
        return;
    }

    int steps = clamp(uMaxSteps, 8, 128);
    float stepSize = 0.25;
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy);
    float currentDist = 0.08 + stepSize * jitter;
    float prevDist = 0.0;
    float tolerance = max(uDepthTolerance, 0.05);
    vec3 hitColor = vec3(0.0);
    float hitFactor = 0.0;

    for (int i = 0; i < steps; ++i) {
        prevDist = currentDist;
        currentDist += stepSize * (1.0 + float(i) * 0.04);
        vec3 rayPos = viewPos + reflectDir * currentDist;
        if (rayPos.z > -uCameraNear || rayPos.z < -uCameraFar) {
            break;
        }

        vec4 proj = uProjection * vec4(rayPos, 1.0);
        if (proj.w <= 1.0e-4) {
            break;
        }
        vec2 screenUv = (proj.xy / proj.w) * 0.5 + 0.5;
        if (screenUv.x < 0.0 || screenUv.x > 1.0 || screenUv.y < 0.0 || screenUv.y > 1.0) {
            break;
        }

        float sampledDepth = texture(uDepthTexture, screenUv).r;
        if (sampledDepth >= 0.999999) {
            continue;
        }

        vec3 sampledViewPos = ReconstructViewPos(screenUv, sampledDepth);
        float depthDiff = sampledViewPos.z - rayPos.z;

        if (depthDiff >= 0.0 && depthDiff < tolerance) {
            // 3-step binary refinement between prevDist and currentDist for crisp reflection edges.
            float lo = prevDist;
            float hi = currentDist;
            vec2 bestUv = screenUv;
            for (int r = 0; r < 3; ++r) {
                float mid = 0.5 * (lo + hi);
                vec3 midPos = viewPos + reflectDir * mid;
                vec4 midProj = uProjection * vec4(midPos, 1.0);
                if (midProj.w <= 1.0e-4) {
                    break;
                }
                vec2 midUv = (midProj.xy / midProj.w) * 0.5 + 0.5;
                if (midUv.x < 0.0 || midUv.x > 1.0 || midUv.y < 0.0 || midUv.y > 1.0) {
                    break;
                }
                float midDepth = texture(uDepthTexture, midUv).r;
                if (midDepth < 0.999999) {
                    vec3 midSamplePos = ReconstructViewPos(midUv, midDepth);
                    float midDiff = midSamplePos.z - midPos.z;
                    if (midDiff >= 0.0 && midDiff < tolerance) {
                        hi = mid;
                        bestUv = midUv;
                    } else {
                        lo = mid;
                    }
                } else {
                    lo = mid;
                }
            }

            vec2 edgeDist = min(bestUv, 1.0 - bestUv);
            float edgeFade = smoothstep(0.0, 0.12, min(edgeDist.x, edgeDist.y));
            float travelFrac = float(i) / float(steps);
            float fadeInLimit = clamp(uFadeIn, 0.0, 0.95);
            float fadeOutLimit = clamp(uFadeOut, fadeInLimit + 0.01, 1.0);
            float nearFade = fadeInLimit > 1.0e-4 ? smoothstep(0.0, fadeInLimit, travelFrac) : 1.0;
            float farFade = 1.0 - smoothstep(fadeOutLimit, 1.0, travelFrac);
            float ndotv = clamp(-dot(viewDir, normal), 0.0, 1.0);
            float fresnel = clamp(0.08 + 0.92 * pow(max(1.0 - ndotv, 0.0), 3.0), 0.05, 0.95);

            hitColor = max(texture(uColorTexture, bestUv).rgb, vec3(0.0));
            hitFactor = edgeFade * nearFade * farFade * fresnel;
            break;
        }
    }

    // Store unpremultiplied reflection color + blend weight in alpha for energy-conserving lerp.
    FragColor = vec4(hitColor, clamp(hitFactor, 0.0, 1.0));
}
#endif
