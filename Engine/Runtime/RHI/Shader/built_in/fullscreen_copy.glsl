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

// Default is a passthrough copy. When uSsaoBlurEnabled != 0, performs a depth-aware bilateral blur
// (either 7-tap separable along (uBilateralAxisX, uBilateralAxisY) or a 3x3 fallback) so half-res
// AO noise smooths cleanly without bleeding across geometric silhouettes or onto the sky.
uniform sampler2D uSourceTexture;
uniform sampler2D uDepthTexture;
uniform int uSsaoBlurEnabled;
uniform float uBilateralAxisX;
uniform float uBilateralAxisY;
uniform float uBilateralDepthSharpness;
uniform float uDepthNdcScale;
uniform float uDepthNdcBias;
uniform float uProjA;
uniform float uProjB;
uniform int uOrthographic;
uniform int uFastReconstruct;
uniform mat4 uInverseProjection;

float LinearDepth(vec2 uv, float depthSample) {
    float scale = uDepthNdcScale != 0.0 ? uDepthNdcScale : 2.0;
    float bias = uDepthNdcScale != 0.0 ? uDepthNdcBias : -1.0;
    float ndcZ = depthSample * scale + bias;
    if (uFastReconstruct != 0 && uOrthographic == 0) {
        return abs(uProjB / max(uProjA - ndcZ, 1.0e-6));
    }
    vec4 viewPos = uInverseProjection * vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    return abs(viewPos.z / max(abs(viewPos.w), 1.0e-6));
}

vec4 SampleSourceClamped(vec2 uv, vec2 halfTexel) {
    return texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel));
}

void main() {
    if (uSsaoBlurEnabled == 0) {
        FragColor = texture(uSourceTexture, vTexCoord);
        return;
    }

    vec2 texelSize = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
    vec2 halfTexel = texelSize * 0.5;
    float centerDepth = texture(uDepthTexture, vTexCoord).r;
    if (centerDepth >= 0.999999) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec4 centerValue = SampleSourceClamped(vTexCoord, halfTexel);
    float centerLinDepth = LinearDepth(vTexCoord, centerDepth);
    float depthSharpness = uBilateralDepthSharpness > 0.0 ? uBilateralDepthSharpness : 12.0;
    float depthScale = max(centerLinDepth * 0.05, 0.02);

    vec2 axis = vec2(uBilateralAxisX, uBilateralAxisY);
    if (dot(axis, axis) > 1.0e-5) {
        // 7-tap separable Gaussian-bilateral kernel (sigma ~= 1.6 texels).
        const float kSpatialWeights[4] = float[](0.25, 0.1875, 0.125, 0.0625);
        vec4 sum = centerValue * kSpatialWeights[0];
        float weightSum = kSpatialWeights[0];
        vec2 stepUv = axis * texelSize;
        for (int tap = 1; tap <= 3; ++tap) {
            float spatialW = kSpatialWeights[tap];
            vec2 offset = stepUv * float(tap);

            vec2 uvPos = clamp(vTexCoord + offset, halfTexel, vec2(1.0) - halfTexel);
            float dPos = texture(uDepthTexture, uvPos).r;
            if (dPos < 0.999999) {
                float deltaPos = abs(LinearDepth(uvPos, dPos) - centerLinDepth) / depthScale;
                float wPos = spatialW * exp(-deltaPos * depthSharpness * 0.15);
                sum += texture(uSourceTexture, uvPos) * wPos;
                weightSum += wPos;
            }

            vec2 uvNeg = clamp(vTexCoord - offset, halfTexel, vec2(1.0) - halfTexel);
            float dNeg = texture(uDepthTexture, uvNeg).r;
            if (dNeg < 0.999999) {
                float deltaNeg = abs(LinearDepth(uvNeg, dNeg) - centerLinDepth) / depthScale;
                float wNeg = spatialW * exp(-deltaNeg * depthSharpness * 0.15);
                sum += texture(uSourceTexture, uvNeg) * wNeg;
                weightSum += wNeg;
            }
        }
        FragColor = sum / max(weightSum, 1.0e-5);
        return;
    }

    // Fallback 3x3 depth-aware bilateral blur when invoked without a separable axis.
    vec4 sum = centerValue * 4.0;
    float weightSum = 4.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            float spatialW = (dx == 0 || dy == 0) ? 2.0 : 1.0;
            vec2 sampleUv = clamp(vTexCoord + vec2(float(dx), float(dy)) * texelSize, halfTexel, vec2(1.0) - halfTexel);
            float sampleDepth = texture(uDepthTexture, sampleUv).r;
            if (sampleDepth >= 0.999999) {
                continue;
            }
            float delta = abs(LinearDepth(sampleUv, sampleDepth) - centerLinDepth) / depthScale;
            float w = spatialW * exp(-delta * depthSharpness * 0.15);
            sum += texture(uSourceTexture, sampleUv) * w;
            weightSum += w;
        }
    }
    FragColor = sum / max(weightSum, 1.0e-5);
}
#endif
