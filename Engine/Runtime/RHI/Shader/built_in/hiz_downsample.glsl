#version 430 core

// Conservative hierarchical depth (Hi-Z) max-depth downsample compute shader.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D uSourceDepthMip;
layout(r32f, binding = 1) uniform writeonly image2D uDestDepthMip;

void main() {
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dstSize = imageSize(uDestDepthMip);
    if (dstCoord.x >= dstSize.x || dstCoord.y >= dstSize.y) {
        return;
    }

    ivec2 srcSize = textureSize(uSourceDepthMip, 0);
    ivec2 srcBase = dstCoord * 2;
    ivec2 c00 = min(srcBase + ivec2(0, 0), srcSize - ivec2(1));
    ivec2 c10 = min(srcBase + ivec2(1, 0), srcSize - ivec2(1));
    ivec2 c01 = min(srcBase + ivec2(0, 1), srcSize - ivec2(1));
    ivec2 c11 = min(srcBase + ivec2(1, 1), srcSize - ivec2(1));

    float d = max(
        max(texelFetch(uSourceDepthMip, c00, 0).r, texelFetch(uSourceDepthMip, c10, 0).r),
        max(texelFetch(uSourceDepthMip, c01, 0).r, texelFetch(uSourceDepthMip, c11, 0).r));

    if ((srcSize.x & 1) != 0 && dstCoord.x == dstSize.x - 1) {
        ivec2 ex0 = min(srcBase + ivec2(2, 0), srcSize - ivec2(1));
        ivec2 ex1 = min(srcBase + ivec2(2, 1), srcSize - ivec2(1));
        d = max(d, max(texelFetch(uSourceDepthMip, ex0, 0).r, texelFetch(uSourceDepthMip, ex1, 0).r));
    }
    if ((srcSize.y & 1) != 0 && dstCoord.y == dstSize.y - 1) {
        ivec2 ey0 = min(srcBase + ivec2(0, 2), srcSize - ivec2(1));
        ivec2 ey1 = min(srcBase + ivec2(1, 2), srcSize - ivec2(1));
        d = max(d, max(texelFetch(uSourceDepthMip, ey0, 0).r, texelFetch(uSourceDepthMip, ey1, 0).r));
    }

    imageStore(uDestDepthMip, dstCoord, vec4(d, 0.0, 0.0, 0.0));
}
