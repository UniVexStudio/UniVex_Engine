#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
#ifdef UVE_ALPHA_CUTOUT
layout(location = 2) in vec2 aTexCoord;
out vec2 vTexCoord;
#endif

#if defined(UVE_INSTANCED) || defined(UVE_INDIRECT_INSTANCED)
// The instanced shadow variant, mirroring lit_shadowed_3d.glsl's arrangement: same define name,
// same binding 0, same transposed upload convention, same uInstanceBaseIndex + gl_InstanceID
// indexing. One rule across both shaders rather than two.
//
// Only the model matrix is needed here - a depth-only pass has no normals to transform - so this
// binds one buffer where the lit shader binds three. The base-index buffer is still required
// because gl_InstanceID restarts at zero for every draw while the frame shares one upload.
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#ifdef UVE_INDIRECT_INSTANCED
layout(std430, binding = 3) readonly buffer VisibleIndexBlock {
    uint visibleIndices[];
};
#endif
#else
uniform mat4 uModel;
#endif
uniform mat4 uLightSpaceMatrix;

void main() {
#if defined(UVE_INDIRECT_INSTANCED)
    mat4 model = instanceModels[uInstanceBaseIndex + int(visibleIndices[gl_InstanceID])];
#elif defined(UVE_INSTANCED)
    mat4 model = instanceModels[uInstanceBaseIndex + gl_InstanceID];
#else
    mat4 model = uModel;
#endif
#ifdef UVE_ALPHA_CUTOUT
    vTexCoord = aTexCoord;
#endif
    gl_Position = uLightSpaceMatrix * model * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
#ifdef UVE_ALPHA_CUTOUT
in vec2 vTexCoord;
uniform sampler2D uAlbedoTexture;
uniform float uAlphaCutoff;
uniform vec3 uUvScale;
uniform vec3 uUvOffset;
#endif

void main() {
#ifdef UVE_ALPHA_CUTOUT
    vec2 scale = dot(uUvScale.xy, uUvScale.xy) > 0.0 ? uUvScale.xy : vec2(1.0);
    vec2 uv = vTexCoord * scale + uUvOffset.xy;
    if (uAlphaCutoff > 0.0 && texture(uAlbedoTexture, uv).a < uAlphaCutoff) {
        discard;
    }
#endif
}
#endif
