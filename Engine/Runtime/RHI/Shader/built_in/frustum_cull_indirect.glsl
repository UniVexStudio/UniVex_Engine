#version 430 core

// CS8: the same frustum test as frustum_cull.glsl, but the result never comes back to the CPU.
//
// frustum_cull.glsl writes one uint per box and the host reads all of them, counts the visible
// ones, and issues draws accordingly. That readback is a full GPU->CPU round trip on the critical
// path, and it is precisely what a culling pass exists to avoid. This kernel instead writes, in
// device memory, the two things a draw actually needs:
//
//   1. the instanceCount field of a DrawIndexedIndirectCommand, and
//   2. a COMPACTED list of which boxes survived, in slot order,
//
// so DrawIndexedIndirectUVE can consume the command directly and the vertex shader can look up
// gl_InstanceID in the compacted list. Nothing on the CPU ever learns how many objects passed.
//
// The test itself is character-for-character the one in frustum_cull.glsl, including `precise`
// forbidding FMA contraction, because the two kernels must agree exactly - CS8's tests verify the
// compacted output against CS5's per-box output, and a divergence in the arithmetic would show up
// as a phantom disagreement that has nothing to do with the compaction being tested.

layout(local_size_x = 64) in;

// Mirrored by CullBoxGpuUVE on the host - shared with frustum_cull.glsl, same layout.
struct CullBox {
    float centerX;
    float centerY;
    float centerZ;
    float extentX;
    float extentY;
    float extentZ;
    float padding0;
    float padding1;
};

struct CullPlane {
    float normalX;
    float normalY;
    float normalZ;
    float distance;
};

layout(std430, binding = 0) readonly buffer BoxBlock {
    CullBox boxes[];
};

layout(std430, binding = 1) readonly buffer PlaneBlock {
    CullPlane planes[6];
};

// The draw parameters themselves, in exactly the five-word order both Vulkan and GL define for an
// indexed indirect draw and DrawIndexedIndirectCommandUVE mirrors on the host. Only instanceCount
// is touched here: the host seeds the other four (they describe the MESH, which no culling
// decision can change) and zeroes instanceCount before the dispatch.
//
// Not `writeonly`: atomicAdd both reads and writes, and a writeonly qualifier would make the
// buffer illegal to use that way.
layout(std430, binding = 2) buffer DrawCommandBlock {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
} drawCommand;

// Slot i holds the index of the i-th surviving box. Capacity equals the box count - the worst
// case is everything visible - so the atomic can never hand out a slot outside the buffer, which
// is why there is no bounds check on the store below and why there must never be one added
// without also changing the allocation.
layout(std430, binding = 3) writeonly buffer VisibleIndexBlock {
    uint visibleIndices[];
};

layout(std430, binding = 4) readonly buffer FrustumCullIndirectParams {
    int boxCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= uint(params.boxCount)) {
        return;
    }

    CullBox box = boxes[index];
    bool visible = true;

    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        CullPlane plane = planes[planeIndex];

        precise float radius = box.extentX * abs(plane.normalX) + box.extentY * abs(plane.normalY) +
                               box.extentZ * abs(plane.normalZ);
        precise float signedDistance = plane.normalX * box.centerX + plane.normalY * box.centerY +
                                       plane.normalZ * box.centerZ + plane.distance;

        if (signedDistance + radius < 0.0) {
            visible = false;
            break;
        }
    }

    if (visible) {
        // This single atomic is the whole point of the pass: it both counts the survivors into the
        // draw's instanceCount and hands this invocation a unique compaction slot, without any
        // ordering between invocations and without the CPU being told the answer.
        //
        // Consequence the host must live with: slot assignment is NOT deterministic, so the
        // compacted list is a SET, not a sequence. Anything verifying it has to sort first.
        const uint slot = atomicAdd(drawCommand.instanceCount, 1u);
        visibleIndices[slot] = index;
    }
}
