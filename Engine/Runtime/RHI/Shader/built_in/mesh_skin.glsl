#version 430 core

// GPU twin of Asset::TrySkinMeshUVE (CS9) - linear blend skinning over many vertices at once.
//
// The CPU side was written first, deliberately: a GPU kernel with no CPU authority to compare
// against cannot be shown to be right. Everything below mirrors that implementation step for step,
// and the ordering of the arithmetic is part of the contract, not an implementation detail.
//
// Three things are load-bearing and must not be "tidied":
//
//   1. BLEND THE MATRICES, THEN TRANSFORM ONCE. Transforming by each joint and blending the
//      results is algebraically identical and numerically different. The CPU blends first; so
//      does this.
//
//   2. SKIP ZERO-WEIGHT SLOTS rather than multiplying by zero. An unused slot may hold any joint
//      index, and 0 * infinity is NaN - the CPU skips, so this skips, or a degenerate joint would
//      poison a vertex on one path only.
//
//   3. `precise` FORBIDS FMA CONTRACTION. A fused multiply-add keeps more intermediate precision
//      and therefore produces a DIFFERENT float than the CPU's separate operations. Same rule as
//      the particle and cull kernels.
//
// Note on precision: the CPU's own general-purpose Math::TransformPointUVE accumulates in double,
// which GLSL has no portable equivalent for (float64 is an optional Vulkan feature absent from
// whole classes of hardware). Rather than accept a permanent ~1 ULP disagreement on roughly one
// vertex in six, the CPU skinning path uses a float-accumulating transform of its own - see
// TransformPointFloatUVE in mesh_skinning_uve.cpp. That is what makes an exact comparison possible
// here at all.

layout(local_size_x = 64) in;

// Mirrored by MeshSkinVertexGpuUVE on the host: position, normal, tangent, handedness. Scalars
// rather than vec3s because a vec3 in std430 is 16-byte aligned and would silently introduce
// padding the host struct does not have.
struct SkinVertex {
    float positionX;
    float positionY;
    float positionZ;
    float normalX;
    float normalY;
    float normalZ;
    float tangentX;
    float tangentY;
    float tangentZ;
    float handedness;
};

// Four joint indices and four weights per vertex, matching MeshSkinningInfluenceUVE.
struct SkinInfluence {
    uint joints[4];
    float weights[4];
};

layout(std430, binding = 0) readonly buffer InputVertexBlock {
    SkinVertex inputVertices[];
};

layout(std430, binding = 1) readonly buffer InfluenceBlock {
    SkinInfluence influences[];
};

// The resolved skinning matrices, one per joint - already composed with each joint's inverse bind
// matrix on the CPU. Pose resolution stays there on purpose: it is a walk down a parent chain over
// a handful of joints, which is serial work a dispatch cannot help with, and keeping
// TryResolvePoseUVE the single authority means there is one implementation to be correct.
//
// mat4 in std430 is column-major with a 16-byte column stride, which is exactly a dense float[16];
// the host uploads Matrix4x4UVE::m transposed into that order. See MeshSkinComputeUVE for why the
// transpose happens on the host rather than here.
layout(std430, binding = 2) readonly buffer SkinningMatrixBlock {
    mat4 skinningMatrices[];
};

layout(std430, binding = 3) writeonly buffer OutputVertexBlock {
    SkinVertex outputVertices[];
};

layout(std430, binding = 4) readonly buffer MeshSkinParams {
    int vertexCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // Dispatches round up to whole workgroups; the tail invocations own no vertex.
    if (index >= uint(params.vertexCount)) {
        return;
    }

    SkinVertex source = inputVertices[index];
    SkinInfluence influence = influences[index];

    // The weighted sum of the influencing joints' matrices - point 1 above.
    //
    // `precise` on the ACCUMULATOR, not just on the final transform: each step here is itself a
    // multiply-add (acc += M * w) and is just as contractible into an FMA as the dot products
    // below. Qualifying only the transform would leave the blend free to diverge, which is the
    // subtler half of the same hazard.
    precise mat4 blended = mat4(0.0);
    for (int slot = 0; slot < 4; ++slot) {
        float weight = influence.weights[slot];
        if (weight == 0.0) {
            continue; // Point 2: skipped, never multiplied by zero.
        }
        blended += skinningMatrices[influence.joints[slot]] * weight;
    }

    // Spelled out element by element rather than as a matrix-vector product: the multiply order
    // and the addition order are what has to match the CPU, and `precise` can only forbid FMA
    // contraction on operations the shader actually names. A `blended * vec4(p, 1.0)` would leave
    // the accumulation order to the compiler.
    //
    // mat4 indexing in GLSL is [column][row], which is why these read transposed relative to the
    // host's row-major Matrix4x4UVE - the host uploads them transposed to make exactly this work.
    precise float positionX = blended[0][0] * source.positionX + blended[1][0] * source.positionY +
                              blended[2][0] * source.positionZ + blended[3][0];
    precise float positionY = blended[0][1] * source.positionX + blended[1][1] * source.positionY +
                              blended[2][1] * source.positionZ + blended[3][1];
    precise float positionZ = blended[0][2] * source.positionX + blended[1][2] * source.positionY +
                              blended[2][2] * source.positionZ + blended[3][2];

    // Directions: the translation column is not read at all. Running a normal through the point
    // transform would displace it by the joint's position.
    precise float normalX = blended[0][0] * source.normalX + blended[1][0] * source.normalY +
                            blended[2][0] * source.normalZ;
    precise float normalY = blended[0][1] * source.normalX + blended[1][1] * source.normalY +
                            blended[2][1] * source.normalZ;
    precise float normalZ = blended[0][2] * source.normalX + blended[1][2] * source.normalY +
                            blended[2][2] * source.normalZ;

    precise float tangentX = blended[0][0] * source.tangentX + blended[1][0] * source.tangentY +
                             blended[2][0] * source.tangentZ;
    precise float tangentY = blended[0][1] * source.tangentX + blended[1][1] * source.tangentY +
                             blended[2][1] * source.tangentZ;
    precise float tangentZ = blended[0][2] * source.tangentX + blended[1][2] * source.tangentY +
                             blended[2][2] * source.tangentZ;

    SkinVertex result;
    result.positionX = positionX;
    result.positionY = positionY;
    result.positionZ = positionZ;
    result.normalX = normalX;
    result.normalY = normalY;
    result.normalZ = normalZ;
    result.tangentX = tangentX;
    result.tangentY = tangentY;
    result.tangentZ = tangentZ;
    // Handedness is a sign carried through untouched, exactly as the CPU does.
    result.handedness = source.handedness;

    outputVertices[index] = result;
}
