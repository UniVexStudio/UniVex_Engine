// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/rhi_shader/built_in_compute_spirv_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"

namespace UVE::Render::Tests {
namespace {

// ---------------------------------------------------------------------------
// The built-in compute kernels have to be consumable by BOTH backends: GLSL text
// on OpenGL, SPIR-V bytes on Vulkan, because the RHI takes each backend's own
// shader language and runtime translation is still an open ROADMAP item. These
// tests guard the properties that make that true, and that a reader would
// otherwise have to take on trust.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kSpirvMagicUVE = 0x07230203U;
/// SPIR-V's header is five words: magic, version, generator, id bound, reserved schema.
constexpr std::size_t kSpirvHeaderWordsUVE = 5U;

[[nodiscard]] std::vector<std::uint32_t> ToWordsUVE(const char* const bytes, const std::size_t size) {
    std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes, words.size() * sizeof(std::uint32_t));
    return words;
}

struct KernelUVE {
    std::string_view name;
    std::string_view glsl;
    const char* spirvBytes;
    std::size_t spirvSize;
};

[[nodiscard]] std::array<KernelUVE, 4U> AllComputeKernelsUVE() {
    return {
        KernelUVE{"particle_simulate", Shader::BuiltIn::kParticleSimulateSource,
                  BuiltInSpirv::kParticleSimulateSpirvBytesUVE, BuiltInSpirv::kParticleSimulateSpirvSizeUVE},
        KernelUVE{"frustum_cull", Shader::BuiltIn::kFrustumCullSource,
                  BuiltInSpirv::kFrustumCullSpirvBytesUVE, BuiltInSpirv::kFrustumCullSpirvSizeUVE},
        KernelUVE{"frustum_cull_indirect", Shader::BuiltIn::kFrustumCullIndirectSource,
                  BuiltInSpirv::kFrustumCullIndirectSpirvBytesUVE,
                  BuiltInSpirv::kFrustumCullIndirectSpirvSizeUVE},
        KernelUVE{"mesh_skin", Shader::BuiltIn::kMeshSkinSource,
                  BuiltInSpirv::kMeshSkinSpirvBytesUVE, BuiltInSpirv::kMeshSkinSpirvSizeUVE},
    };
}

TEST(ComputeKernelPortabilityUVETest, BakedSpirv_IsWellFormedAndSurvivedItsNulBytes) {
    for (const KernelUVE& kernel : AllComputeKernelsUVE()) {
        SCOPED_TRACE(kernel.name);

        ASSERT_GT(kernel.spirvSize, kSpirvHeaderWordsUVE * sizeof(std::uint32_t));
        ASSERT_EQ(kernel.spirvSize % sizeof(std::uint32_t), 0U) << "SPIR-V must be word aligned";

        const std::vector<std::uint32_t> words = ToWordsUVE(kernel.spirvBytes, kernel.spirvSize);
        EXPECT_EQ(words[0], kSpirvMagicUVE)
            << "the Vulkan backend rejects any module whose first word is not the SPIR-V magic";
        EXPECT_GT(words[3], 0U) << "the id bound must be non-zero in a module that declares anything";

        // The bytes contain embedded NULs, which is precisely what a cstring construction would
        // truncate at - the M2a defect. Constructing the std::string the way the engine does must
        // preserve every byte.
        const std::string asEngineBuildsIt(kernel.spirvBytes, kernel.spirvSize);
        EXPECT_EQ(asEngineBuildsIt.size(), kernel.spirvSize);
        EXPECT_NE(std::string(kernel.spirvBytes).size(), kernel.spirvSize)
            << "this kernel's SPIR-V happens to contain no NUL bytes, so it cannot guard the "
               "truncation bug - pick a different assertion rather than deleting this one";
    }
}

TEST(ComputeKernelPortabilityUVETest, GlslKernels_AvoidTheConstructsSpirvCannotExpress) {
    // The reason this file exists. GLSL allows non-opaque uniforms at global scope and the GL
    // backend resolves them by name, but SPIR-V has no such concept: glslang rejects
    // `uniform float uDeltaSeconds` outright with "non-opaque uniform variables need a
    // layout(location=L)". A kernel written that way compiles happily for GL and can NEVER run on
    // Vulkan - a capability gap that looks like a working feature. Both kernels therefore pass
    // their parameters in std430 storage blocks, and this test fails the moment someone
    // reintroduces the convenient form.
    for (const KernelUVE& kernel : AllComputeKernelsUVE()) {
        SCOPED_TRACE(kernel.name);

        std::size_t searchPosition = kernel.glsl.find("uniform");
        while (searchPosition != std::string_view::npos) {
            // Walk back to the start of the line: `layout(...) uniform` on a block or an opaque
            // sampler/image is fine, a bare leading `uniform` is not.
            const std::size_t lineStart = kernel.glsl.rfind('\n', searchPosition);
            const std::string_view line = kernel.glsl.substr(
                lineStart == std::string_view::npos ? 0U : lineStart + 1U,
                kernel.glsl.find('\n', searchPosition) - (lineStart + 1U));

            const std::size_t firstNonSpace = line.find_first_not_of(" \t");
            const bool isCommentary = firstNonSpace != std::string_view::npos &&
                                      line.substr(firstNonSpace, 2U) == "//";
            if (!isCommentary) {
                EXPECT_TRUE(line.find("layout") != std::string_view::npos)
                    << "bare `uniform` in " << kernel.name
                    << " cannot compile to SPIR-V, so this kernel would never run on Vulkan: " << line;
            }
            searchPosition = kernel.glsl.find("uniform", searchPosition + 1U);
        }
    }
}

TEST(ComputeKernelPortabilityUVETest, GlslKernels_DeclareTheBindingsTheirHostsBind) {
    // A binding-index typo between kernel and host is invisible at compile time on both backends
    // and produces wrong numbers at runtime, so the declarations are asserted literally here
    // against what ParticleComputeSimulationUVE and FrustumCullComputeUVE bind.
    const std::string_view particle = Shader::BuiltIn::kParticleSimulateSource;
    EXPECT_NE(particle.find("binding = 0) buffer ParticleBlock"), std::string_view::npos);
    EXPECT_NE(particle.find("binding = 1) readonly buffer ParticleSimulateParams"),
              std::string_view::npos);

    const std::string_view cull = Shader::BuiltIn::kFrustumCullSource;
    EXPECT_NE(cull.find("binding = 0) readonly buffer BoxBlock"), std::string_view::npos);
    EXPECT_NE(cull.find("binding = 1) readonly buffer PlaneBlock"), std::string_view::npos);
    EXPECT_NE(cull.find("binding = 2) writeonly buffer VisibilityBlock"), std::string_view::npos);
    EXPECT_NE(cull.find("binding = 3) readonly buffer FrustumCullParams"), std::string_view::npos);

    const std::string_view indirect = Shader::BuiltIn::kFrustumCullIndirectSource;
    EXPECT_NE(indirect.find("binding = 0) readonly buffer BoxBlock"), std::string_view::npos);
    EXPECT_NE(indirect.find("binding = 1) readonly buffer PlaneBlock"), std::string_view::npos);
    EXPECT_NE(indirect.find("binding = 2) buffer DrawCommandBlock"), std::string_view::npos);
    EXPECT_NE(indirect.find("binding = 3) writeonly buffer VisibleIndexBlock"),
              std::string_view::npos);
    EXPECT_NE(indirect.find("binding = 4) readonly buffer FrustumCullIndirectParams"),
              std::string_view::npos);
    // Deliberately NOT writeonly on binding 2: atomicAdd reads as well as writes, and a writeonly
    // qualifier would make the kernel's one essential operation illegal. Asserted because the
    // qualifier is easy to "tidy up" into matching its siblings.
    EXPECT_EQ(indirect.find("binding = 2) writeonly"), std::string_view::npos);

    const std::string_view skin = Shader::BuiltIn::kMeshSkinSource;
    EXPECT_NE(skin.find("binding = 0) readonly buffer InputVertexBlock"), std::string_view::npos);
    EXPECT_NE(skin.find("binding = 1) readonly buffer InfluenceBlock"), std::string_view::npos);
    EXPECT_NE(skin.find("binding = 2) readonly buffer SkinningMatrixBlock"),
              std::string_view::npos);
    EXPECT_NE(skin.find("binding = 3) writeonly buffer OutputVertexBlock"), std::string_view::npos);
    EXPECT_NE(skin.find("binding = 4) readonly buffer MeshSkinParams"), std::string_view::npos);
    // The blend accumulator carries `precise` too, not just the final transform: each `acc += M*w`
    // is itself contractible into an FMA, and qualifying only the transform would leave the
    // subtler half of the hazard open.
    EXPECT_NE(skin.find("precise mat4 blended"), std::string_view::npos);
}

TEST(ComputeKernelPortabilityUVETest, GlslKernels_KeepTheirFmaBarrier) {
    // CS4 and CS5 both promise results that match the CPU bit for bit, and `precise` is what makes
    // that promise keepable - without it the compiler may contract a + b*c into a fused
    // multiply-add, which carries MORE intermediate precision and therefore produces a DIFFERENT
    // float than the CPU's separate operations. Losing these qualifiers would not fail to compile;
    // it would quietly turn an exact guarantee into an approximate one.
    for (const KernelUVE& kernel : AllComputeKernelsUVE()) {
        SCOPED_TRACE(kernel.name);
        EXPECT_NE(kernel.glsl.find("precise float"), std::string_view::npos)
            << "this kernel's bit-for-bit guarantee depends on `precise` forbidding FMA contraction";
    }
}

} // namespace
} // namespace UVE::Render::Tests
