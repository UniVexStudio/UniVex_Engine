#version 450 core

// Temporal Anti-Aliasing (TAA) resolve pass. Reprojects the previous frame's HDR history via
// depth-reconstructed world position, clamps history inside the current frame's 3x3 YCoCg
// variance box (eliminating ghosting), and blends smoothly with disocclusion rejection.

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

uniform sampler2D uCurrentColorTexture;
uniform sampler2D uHistoryColorTexture;
uniform sampler2D uDepthTexture;

uniform mat4 uInverseViewProjection;
uniform mat4 uPreviousViewProjection;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uHistoryWeight;
uniform int uHistoryValid;
uniform vec2 uTexelSize;

vec3 RgbToYCoCg(vec3 rgb) {
    float co = rgb.r - rgb.b;
    float t = rgb.b + co * 0.5;
    float cg = rgb.g - t;
    float y = t + cg * 0.5;
    return vec3(y, co, cg);
}

vec3 YCoCgToRgb(vec3 ycocg) {
    float t = ycocg.x - ycocg.z * 0.5;
    float g = ycocg.z + t;
    float b = t - ycocg.y * 0.5;
    float r = b + ycocg.y;
    return max(vec3(r, g, b), vec3(0.0));
}

vec3 ReinhardCompress(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / (1.0 + max(luma, 0.0));
}

vec3 ReinhardDecompress(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c / max(1.0 - luma, 1.0e-4);
}

float DecodeNdcDepth(float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    return depthSample * scale + bias;
}

void main() {
    vec4 currentSample = texture(uCurrentColorTexture, vTexCoord);
    vec3 currentRgb = max(currentSample.rgb, vec3(0.0));
    if (uHistoryValid == 0) {
        FragColor = vec4(currentRgb, currentSample.a);
        return;
    }

    // Closest-depth 3x3 dilation improves silhouette edge reprojection.
    float bestDepth = texture(uDepthTexture, vTexCoord).r;
    vec2 bestOffset = vec2(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 off = vec2(float(dx), float(dy)) * uTexelSize;
            float d = texture(uDepthTexture, clamp(vTexCoord + off, vec2(0.0), vec2(1.0))).r;
            if (d < bestDepth) {
                bestDepth = d;
                bestOffset = off;
            }
        }
    }

    vec2 reprojectUv = vTexCoord + bestOffset;
    float ndcZ = DecodeNdcDepth(bestDepth);
    vec4 worldPos = uInverseViewProjection * vec4(reprojectUv * 2.0 - 1.0, ndcZ, 1.0);
    vec2 prevUv = vTexCoord;
    bool validReprojection = false;
    if (abs(worldPos.w) > 1.0e-5) {
        vec4 prevClip = uPreviousViewProjection * (worldPos / worldPos.w);
        if (abs(prevClip.w) > 1.0e-5) {
            vec2 velocity = (prevClip.xy / prevClip.w) * 0.5 + 0.5 - reprojectUv;
            prevUv = vTexCoord + velocity;
            validReprojection = prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0;
        }
    }

    if (!validReprojection) {
        FragColor = vec4(currentRgb, currentSample.a);
        return;
    }

    // 3x3 YCoCg variance box in Reinhard-compressed HDR space so bright specular highlights
    // cannot destabilize the neighborhood clamp.
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 sampleUv = clamp(vTexCoord + vec2(float(dx), float(dy)) * uTexelSize, vec2(0.0), vec2(1.0));
            vec3 c = RgbToYCoCg(ReinhardCompress(max(texture(uCurrentColorTexture, sampleUv).rgb, vec3(0.0))));
            m1 += c;
            m2 += c * c;
        }
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    const float kGamma = 1.25;
    vec3 boxMin = mean - kGamma * sigma;
    vec3 boxMax = mean + kGamma * sigma;

    vec3 historyRgb = max(texture(uHistoryColorTexture, prevUv).rgb, vec3(0.0));
    vec3 historyYcc = clamp(RgbToYCoCg(ReinhardCompress(historyRgb)), boxMin, boxMax);
    vec3 currentYcc = RgbToYCoCg(ReinhardCompress(currentRgb));

    float blendWeight = clamp(uHistoryWeight, 0.0, 0.96);
    vec3 resolvedYcc = mix(currentYcc, historyYcc, blendWeight);
    vec3 resolvedRgb = ReinhardDecompress(YCoCgToRgb(resolvedYcc));
    if (any(isnan(resolvedRgb)) || any(isinf(resolvedRgb))) {
        resolvedRgb = currentRgb;
    }
    FragColor = vec4(max(resolvedRgb, vec3(0.0)), currentSample.a);
}
#endif
