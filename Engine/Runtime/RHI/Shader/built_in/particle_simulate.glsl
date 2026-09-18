#version 430 core

// GPU twin of Scene::ParticleRuntimeUVE::SimulateDetailedUVE's per-particle integration
// (CS4). The CPU runtime remains the authority on WHICH particles exist - emission,
// budgets, lifetime culling and array compaction all stay on the CPU, where they are
// bounded and testable; this kernel does only the part that is pure arithmetic over an
// array, which is exactly the part worth moving to the GPU.
//
// The integration must match the CPU statement for statement, because the engine asserts
// the two agree bit-for-bit:
//
//     nextVelocity = velocity + acceleration * dt;
//     nextPosition = position + nextVelocity * dt;   // semi-implicit Euler: NEW velocity
//     nextLifetime = remainingLifetimeSeconds - dt;
//
// Two deliberate choices protect that equality. First, `precise` on the outputs forbids
// the compiler from contracting `a + b * c` into a fused multiply-add: an FMA keeps more
// intermediate precision, which sounds better but produces a DIFFERENT float than the
// CPU's separate multiply and add, and a result that is merely close is not a result the
// engine can compare. Second, nothing here is reordered or vectorised across particles -
// each invocation owns exactly one particle.
//
// Particles whose lifetime has run out are integrated anyway and left in place with a
// non-positive lifetime; the CPU's compaction pass is what removes them. Skipping them
// here would put a branch in the hot path to save nothing, and would make the readback
// disagree with the CPU on the dead entries' contents.

layout(local_size_x = 64) in;

// std430 packs this struct as 8 consecutive floats with no padding, which is what
// ParticleComputeSimulationUVE::ParticleGpuStateUVE mirrors on the host. vec3 would be
// 16-byte aligned and silently introduce padding, so positions and velocities are spelled
// out as scalars: the host layout assertion and this declaration must agree exactly.
struct ParticleGpuState {
    float positionX;
    float positionY;
    float positionZ;
    float velocityX;
    float velocityY;
    float velocityZ;
    float remainingLifetimeSeconds;
    float padding;
};

layout(std430, binding = 0) buffer ParticleBlock {
    ParticleGpuState particles[];
};

// The kernel's parameters travel in a storage buffer rather than as bare `uniform` scalars.
// That is a portability requirement, not a style choice: GLSL permits non-opaque uniforms at
// global scope and the GL backend resolves them by name, but SPIR-V has no such concept - glslang
// rejects this very file with "non-opaque uniform variables need a layout(location=L)" - so a
// kernel written that way can never run on Vulkan. A std430 block compiles unchanged for both.
layout(std430, binding = 1) readonly buffer ParticleSimulateParams {
    float deltaSeconds;
    float accelerationX;
    float accelerationY;
    float accelerationZ;
    int particleCount;
} params;

void main() {
    const uint index = gl_GlobalInvocationID.x;
    // The dispatch rounds up to whole workgroups, so the tail invocations of the last
    // group address particles that do not exist. Without this guard they would write past
    // the live range - the buffer is sized to the instance's capacity, so the write would
    // land inside allocated memory and silently corrupt state the CPU still owns.
    if (index >= uint(params.particleCount)) {
        return;
    }

    ParticleGpuState state = particles[index];

    precise float nextVelocityX = state.velocityX + params.accelerationX * params.deltaSeconds;
    precise float nextVelocityY = state.velocityY + params.accelerationY * params.deltaSeconds;
    precise float nextVelocityZ = state.velocityZ + params.accelerationZ * params.deltaSeconds;

    precise float nextPositionX = state.positionX + nextVelocityX * params.deltaSeconds;
    precise float nextPositionY = state.positionY + nextVelocityY * params.deltaSeconds;
    precise float nextPositionZ = state.positionZ + nextVelocityZ * params.deltaSeconds;

    precise float nextLifetime = state.remainingLifetimeSeconds - params.deltaSeconds;

    particles[index].positionX = nextPositionX;
    particles[index].positionY = nextPositionY;
    particles[index].positionZ = nextPositionZ;
    particles[index].velocityX = nextVelocityX;
    particles[index].velocityY = nextVelocityY;
    particles[index].velocityZ = nextVelocityZ;
    particles[index].remainingLifetimeSeconds = nextLifetime;
}
