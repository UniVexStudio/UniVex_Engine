#version 430 core

// GPU twin of Math::FrustumUVE::IntersectsUVE (CS5) - the conservative centre/extents AABB test
// against six inward-facing planes that Renderer3DUVE and MeshRenderEligibilityUVE already use on
// the CPU:
//
//     radius = extents.x*|n.x| + extents.y*|n.y| + extents.z*|n.z|;
//     if (dot(n, center) + d + radius < 0) -> rejected by this plane, box is invisible
//
// Culling is a BOOLEAN result, which makes it tempting to accept "nearly the same" answers. That
// would be the wrong standard. A box sitting exactly on a plane is where CPU and GPU are most
// likely to differ, and it is also exactly where a difference is visible as an object popping in
// or out depending on which path ran. So the arithmetic underneath the boolean is held to the
// same bit-for-bit rule as the particle kernel: `precise` forbids the compiler from contracting
// the multiply-adds into FMAs, which would keep more intermediate precision and therefore produce
// a DIFFERENT float than the CPU's separate operations - and a different float is what flips a
// borderline decision.
//
// The host computes each box's centre and extents and uploads those rather than min/max, so the
// halving in AabbUVE::GetCenterUVE (including its double-precision fallback for boxes whose
// min+max overflows) happens once, on the CPU, in the CPU's own arithmetic. Recomputing it here
// would introduce a second place for the two paths to disagree, for no benefit.
//
// Deliberately NOT done here: the plane extraction itself. Six planes per frustum is not work
// worth a dispatch, and keeping FrustumUVE::FromViewProjectionUVE as the single authority means
// there is exactly one plane-extraction implementation in the engine to be correct.

layout(local_size_x = 64) in;

// std430, 8 floats, no padding - mirrored by CullBoxGpuUVE on the host. Spelled out as scalars
// for the same reason the particle kernel does: a vec3 here would be 16-byte aligned and silently
// introduce padding the host struct does not have.
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

// A plane as normal + distance: exactly Math::PlaneUVE's layout, four floats.
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

// One uint per box: 1 visible, 0 culled. A uint rather than a packed bitfield because the host
// reads this back and compares it element-wise against the CPU's decision - a bitfield would make
// the readback denser and every mismatch report harder to read, and the buffer is already tiny
// next to the box data it describes.
layout(std430, binding = 2) writeonly buffer VisibilityBlock {
    uint visible[];
};

// Parameters travel in a storage buffer, not as a bare `uniform` scalar - SPIR-V has no
// non-opaque global uniforms, so the uniform form cannot compile for Vulkan at all. See
// particle_simulate.glsl for the same note.
layout(std430, binding = 3) readonly buffer FrustumCullParams {
    int boxCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // Dispatches round up to whole workgroups; the tail invocations own no box. Without this the
    // write would land inside the allocated visibility buffer and corrupt a neighbouring result.
    if (index >= uint(params.boxCount)) {
        return;
    }

    CullBox box = boxes[index];
    uint result = 1u;

    // Plane order is fixed by FrustumUVE (left, right, bottom, top, near, far) and the loop is
    // unrolled over exactly six - a dynamic count would be a different contract, and the CPU side
    // has no such thing.
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex) {
        CullPlane plane = planes[planeIndex];

        precise float radius = box.extentX * abs(plane.normalX) + box.extentY * abs(plane.normalY) +
                               box.extentZ * abs(plane.normalZ);
        precise float signedDistance = plane.normalX * box.centerX + plane.normalY * box.centerY +
                                       plane.normalZ * box.centerZ + plane.distance;

        // Matches the CPU's early return exactly, including the strict `< 0` comparison: a box
        // touching the plane exactly is INSIDE, and that boundary has to be the same on both sides.
        if (signedDistance + radius < 0.0) {
            result = 0u;
            break;
        }
    }

    visible[index] = result;
}
