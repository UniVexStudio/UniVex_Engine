#version 450 core

#ifdef VERTEX_SHADER
#ifdef UVE_VFX_VERTEX_FALLBACK
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aFrame;
#else
struct VfxSpriteInstanceData {
    vec4 positionAndRotation;
    vec4 velocityAndFrame;
    vec4 sizeAndColor;
};

layout(std430, binding = 0) readonly buffer VfxSpriteInstanceBuffer {
    VfxSpriteInstanceData instances[];
} bSpriteInstances;

uniform vec3 uCameraPosition;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraForward;
uniform vec3 uFacingAxis;
uniform int uFacingMode;
uniform int uInstanceBaseIndex;
uniform float uStretchScale;
#endif

uniform mat4 uViewProjection;

out vec2 vCornerUv;
out vec4 vColor;
out vec3 vWorldPosition;
out float vFrame;

void main() {
#ifdef UVE_VFX_VERTEX_FALLBACK
    vWorldPosition = aPosition;
    vCornerUv = aTexCoord;
    vColor = aColor;
    vFrame = aFrame.x;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
#else
    const vec2 kQuadOffsets[6] = vec2[6](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );
    const vec2 kQuadUvs[6] = vec2[6](
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(0.0, 0.0)
    );

    int cornerIndex = clamp(gl_VertexID % 6, 0, 5);
    int instanceIndex = uInstanceBaseIndex + gl_InstanceID;
    VfxSpriteInstanceData inst = bSpriteInstances.instances[instanceIndex];

    vec3 center = inst.positionAndRotation.xyz;
    float rotationRad = inst.positionAndRotation.w;
    vec3 velocity = inst.velocityAndFrame.xyz;
    float frame = inst.velocityAndFrame.w;
    vec2 size = max(inst.sizeAndColor.xy, vec2(0.0));
    uint packedColor = floatBitsToUint(inst.sizeAndColor.z);
    vec4 color = unpackUnorm4x8(packedColor) * vec4(vec3(max(inst.sizeAndColor.w, 0.0)), 1.0);

    vec3 basisRight = uCameraRight;
    vec3 basisUp = uCameraUp;

    if (uFacingMode == 1) {
        vec3 toCamera = uCameraPosition - center;
        float distSq = dot(toCamera, toCamera);
        vec3 forwardDir = distSq > 1.0e-8 ? toCamera * inversesqrt(distSq) : -uCameraForward;
        vec3 refUp = abs(forwardDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        basisRight = normalize(cross(refUp, forwardDir));
        basisUp = normalize(cross(forwardDir, basisRight));
    } else if (uFacingMode == 2) {
        float speedSq = dot(velocity, velocity);
        float speed = sqrt(speedSq);
        vec3 velDir = speed > 1.0e-5 ? (velocity / speed) : uCameraUp;
        vec3 toCamera = uCameraPosition - center;
        float toCamLenSq = dot(toCamera, toCamera);
        vec3 viewDir = toCamLenSq > 1.0e-8 ? (toCamera * inversesqrt(toCamLenSq)) : -uCameraForward;
        vec3 side = cross(viewDir, velDir);
        float sideLenSq = dot(side, side);
        basisRight = sideLenSq > 1.0e-8 ? (side * inversesqrt(sideLenSq)) : uCameraRight;
        basisUp = velDir;
        size.y += speed * max(uStretchScale, 0.0);
    } else if (uFacingMode == 3) {
        float axisLenSq = dot(uFacingAxis, uFacingAxis);
        vec3 normalAxis = axisLenSq > 1.0e-8 ? (uFacingAxis * inversesqrt(axisLenSq)) : vec3(0.0, 1.0, 0.0);
        vec3 refUp = abs(normalAxis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
        basisRight = normalize(cross(refUp, normalAxis));
        basisUp = normalize(cross(normalAxis, basisRight));
    }

    float cosAngle = cos(rotationRad);
    float sinAngle = sin(rotationRad);
    vec2 localCorner = kQuadOffsets[cornerIndex] * size;
    vec2 rotatedCorner = vec2(
        localCorner.x * cosAngle - localCorner.y * sinAngle,
        localCorner.x * sinAngle + localCorner.y * cosAngle
    );

    vec3 worldPos = center + basisRight * rotatedCorner.x + basisUp * rotatedCorner.y;
    vWorldPosition = worldPos;
    vCornerUv = kQuadUvs[cornerIndex];
    vColor = color;
    vFrame = frame;
    gl_Position = uViewProjection * vec4(worldPos, 1.0);
#endif
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vCornerUv;
in vec4 vColor;
in vec3 vWorldPosition;
in float vFrame;

uniform sampler2D uAlbedoTexture;
uniform vec4 uMaterialTint;
uniform vec3 uMaterialEmissive;
uniform vec3 uCameraPosition;
uniform int uFlipbookRows;
uniform int uFlipbookCols;
uniform int uFrameBlend;
uniform int uBlendMode;
uniform int uFacingMode;
uniform float uAlphaCutoff;
uniform float uCameraFadeNear;
uniform float uCameraFadeFar;
uniform float uEmissive;

#ifdef UVE_VFX_TRANSLUCENT
uniform sampler2D uSceneDepthTexture;
uniform mat4 uInverseProjection;
uniform vec2 uInvViewportSize;
uniform float uSoftDistance;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
#endif

out vec4 FragColor;

vec2 ComputeFlipbookUv(vec2 cornerUv, int frameIndex, int rows, int cols) {
    int safeCols = max(cols, 1);
    int safeRows = max(rows, 1);
    int totalFrames = safeCols * safeRows;
    int clampedFrame = clamp(frameIndex, 0, max(totalFrames - 1, 0));
    int col = clampedFrame % safeCols;
    int row = clampedFrame / safeCols;
    vec2 cellScale = vec2(1.0 / float(safeCols), 1.0 / float(safeRows));
    return (vec2(float(col), float(row)) + clamp(cornerUv, vec2(0.0), vec2(1.0))) * cellScale;
}

#ifdef UVE_VFX_TRANSLUCENT
float ReconstructLinearViewDepth(vec2 screenUv, float rawDepth) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    vec3 ndc = vec3(screenUv * 2.0 - 1.0, clamp(rawDepth, 0.0, 1.0) * scale + bias);
    vec4 viewPos = uInverseProjection * vec4(ndc, 1.0);
    float invW = abs(viewPos.w) > 1.0e-7 ? (1.0 / viewPos.w) : 1.0;
    return -(viewPos.z * invW);
}
#endif

void main() {
    int rows = max(uFlipbookRows, 1);
    int cols = max(uFlipbookCols, 1);
    int totalFrames = rows * cols;
    float clampedFrame = clamp(vFrame, 0.0, float(max(totalFrames - 1, 0)));
    int frame0 = int(floor(clampedFrame));
    vec2 uv0 = ComputeFlipbookUv(vCornerUv, frame0, rows, cols);
    vec4 texSample = texture(uAlbedoTexture, uv0);
    if (uFrameBlend != 0 && totalFrames > 1) {
        int frame1 = min(frame0 + 1, totalFrames - 1);
        float frac = fract(clampedFrame);
        vec2 uv1 = ComputeFlipbookUv(vCornerUv, frame1, rows, cols);
        texSample = mix(texSample, texture(uAlbedoTexture, uv1), frac);
    }

    // Procedural non-boxy particle shaping when no authored sprite texture is bound (1x1 fallback).
    ivec2 albedoDims = textureSize(uAlbedoTexture, 0);
    if (albedoDims.x <= 1 && albedoDims.y <= 1) {
        vec2 p = vCornerUv * 2.0 - 1.0;
        if (uFacingMode == 2) {
            // Velocity-aligned incandescent spark streak with tapered ends.
            float across = abs(p.x);
            float along = abs(p.y);
            float taper = max(1.0 - along * along, 0.0);
            float core = smoothstep(0.65 * taper + 0.06, 0.0, across) * smoothstep(1.0, 0.12, along);
            float hot = exp(-(across * across * 14.0 + along * along * 2.5));
            texSample.rgb *= mix(vec3(1.0), vec3(1.35, 1.25, 1.1), hot);
            texSample.a *= clamp(core + hot * 0.55, 0.0, 1.0) * smoothstep(1.0, 0.85, max(across, along));
        } else if (uBlendMode == 1) {
            // Soft radial glow with hot inner core for additive fire/embers/magic.
            float r = length(p);
            float radial = smoothstep(1.0, 0.0, r);
            float core = exp(-r * r * 4.2);
            texSample.rgb *= mix(vec3(1.0), vec3(1.25, 1.18, 1.08), core);
            texSample.a *= clamp(radial * radial * 0.60 + core * 0.85, 0.0, 1.0) * smoothstep(1.0, 0.82, r);
        } else {
            // Soft feathered organic disc for alpha-blended smoke/dust/snow/clouds.
            float r = length(p);
            float puff = smoothstep(1.0, 0.0, r);
            puff = puff * puff * (3.0 - 2.0 * puff);
            texSample.a *= puff;
        }
    }

    vec4 base = texSample * vColor * uMaterialTint;
    float alpha = clamp(base.a, 0.0, 1.0);

    if (uCameraFadeFar > uCameraFadeNear && uCameraFadeFar > 0.0) {
        float camDist = length(vWorldPosition - uCameraPosition);
        float camFade = clamp((camDist - uCameraFadeNear) / max(uCameraFadeFar - uCameraFadeNear, 1.0e-5), 0.0, 1.0);
        alpha *= camFade;
    }

#ifdef UVE_VFX_TRANSLUCENT
    vec2 screenUv = clamp(gl_FragCoord.xy * uInvViewportSize, vec2(0.0005), vec2(0.9995));
    float rawSceneDepth = texture(uSceneDepthTexture, screenUv).r;
    if (rawSceneDepth < 0.999999 && gl_FragCoord.z > rawSceneDepth) {
        discard;
    }
    if (uSoftDistance > 0.0 && rawSceneDepth < 0.999999) {
        float sceneLinearDepth = ReconstructLinearViewDepth(screenUv, rawSceneDepth);
        float particleLinearDepth = ReconstructLinearViewDepth(screenUv, gl_FragCoord.z);
        float softFade = clamp((sceneLinearDepth - particleLinearDepth) / max(uSoftDistance, 1.0e-5), 0.0, 1.0);
        alpha *= softFade;
    }
#endif

    if (uBlendMode == 2) {
        if (alpha < uAlphaCutoff) {
            discard;
        }
        vec3 rgbOut = base.rgb * max(uEmissive, 0.0) + uMaterialEmissive;
        FragColor = vec4(rgbOut, 1.0);
        return;
    }

    if (alpha <= 1.0e-5) {
        discard;
    }

    vec3 rgbLit = (base.rgb * max(uEmissive, 0.0) + uMaterialEmissive) * alpha;
    if (uBlendMode == 0) {
        FragColor = vec4(rgbLit, alpha);
    } else {
        FragColor = vec4(rgbLit, alpha);
    }
}
#endif
