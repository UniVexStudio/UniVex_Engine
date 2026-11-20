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

uniform sampler2D uSourceTexture;
uniform sampler2D uHigherMipTexture;
// (1,0) for horizontal Gaussian pass, (0,1) for vertical Gaussian pass when uUpsampleMode == 0.
// When uUpsampleMode != 0, performs a 3x3 tent upsample of uSourceTexture (coarser mip) blended
// with uHigherMipTexture (finer mip) for smooth multi-scale bloom reconstruction.
uniform float uBlurDirectionX;
uniform float uBlurDirectionY;
uniform float uTexelSizeX;
uniform float uTexelSizeY;
uniform int uUpsampleMode;
uniform float uUpsampleScatter;

vec3 SampleSourceClampedUVE(vec2 uv, vec2 halfTexel) {
    return max(texture(uSourceTexture, clamp(uv, halfTexel, vec2(1.0) - halfTexel)).rgb, vec3(0.0));
}

void main() {
    if (uUpsampleMode != 0) {
        vec2 texel = vec2(uTexelSizeX, uTexelSizeY);
        if (texel.x <= 0.0 || texel.y <= 0.0) {
            texel = 1.0 / max(vec2(textureSize(uSourceTexture, 0)), vec2(1.0));
        }
        vec2 halfTexel = texel * 0.5;
        float x = texel.x;
        float y = texel.y;

        vec3 a = SampleSourceClampedUVE(vTexCoord + vec2(-x,  y), halfTexel);
        vec3 b = SampleSourceClampedUVE(vTexCoord + vec2( 0.0, y), halfTexel);
        vec3 c = SampleSourceClampedUVE(vTexCoord + vec2( x,  y), halfTexel);
        vec3 d = SampleSourceClampedUVE(vTexCoord + vec2(-x,  0.0), halfTexel);
        vec3 e = SampleSourceClampedUVE(vTexCoord, halfTexel);
        vec3 f = SampleSourceClampedUVE(vTexCoord + vec2( x,  0.0), halfTexel);
        vec3 g = SampleSourceClampedUVE(vTexCoord + vec2(-x, -y), halfTexel);
        vec3 h = SampleSourceClampedUVE(vTexCoord + vec2( 0.0, -y), halfTexel);
        vec3 i = SampleSourceClampedUVE(vTexCoord + vec2( x, -y), halfTexel);

        vec3 tent = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0);
        vec3 higher = max(texture(uHigherMipTexture, vTexCoord).rgb, vec3(0.0));
        float scatter = clamp(uUpsampleScatter > 0.0 ? uUpsampleScatter : 0.65, 0.0, 1.0);
        // Energy-conserving progressive blend preserves both tight inner glow and wide outer halo.
        FragColor = vec4(mix(higher, tent, scatter), 1.0);
        return;
    }

    // 9-tap separable Gaussian blur (sigma ~= 2 texels) for single-mip bloom.
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 direction = vec2(uBlurDirectionX, uBlurDirectionY);
    vec2 texelSize = vec2(uTexelSizeX, uTexelSizeY);
    vec2 halfTexel = texelSize * 0.5;
    vec3 result = SampleSourceClampedUVE(vTexCoord, halfTexel) * weights[0];
    for (int tap = 1; tap < 5; ++tap) {
        vec2 offset = direction * texelSize * float(tap);
        result += SampleSourceClampedUVE(vTexCoord + offset, halfTexel) * weights[tap];
        result += SampleSourceClampedUVE(vTexCoord - offset, halfTexel) * weights[tap];
    }
    FragColor = vec4(result, 1.0);
}
#endif
