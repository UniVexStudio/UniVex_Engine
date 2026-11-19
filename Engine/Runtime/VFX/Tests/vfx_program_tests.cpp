// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_program.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

namespace UVE::VFX::Tests {
namespace {

using namespace Asset;

[[nodiscard]] ParticleSystemAssetUVE MakeCompilableBaseSystemUVE() {
    ParticleSystemAssetUVE sys;
    ParticleEmitterAssetUVE em;
    em.name = "Main";
    em.maxParticles = 500U;
    em.emitterUpdate.push_back(MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate));
    em.particleSpawn.push_back(MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::InitialLifetime));
    em.render.push_back(MakeDefaultParticleModuleUVE(3U, ParticleModuleTypeUVE::SpriteRenderer));
    sys.emitters.push_back(std::move(em));
    return sys;
}

[[nodiscard]] bool HasSeverityUVE(const std::vector<ParticleCompileMessageUVE>& msgs,
                                  const ParticleCompileMessageUVE::Severity sev,
                                  const std::uint32_t moduleId = 0U,
                                  const bool matchId = false) {
    return std::any_of(msgs.begin(), msgs.end(), [&](const ParticleCompileMessageUVE& msg) {
        return msg.severity == sev && (!matchId || msg.moduleId == moduleId);
    });
}

TEST(VfxProgramUVETest, ReportsAllRequiredCompileErrors) {
    ParticleProgramUVE program;
    std::vector<ParticleCompileMessageUVE> messages;

    // 1. Wrong stage
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(20U, ParticleModuleTypeUVE::Gravity));
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 20U, true));
    }

    // 2. Wire to missing node & cycle in Math nodes
    {
        auto sys = MakeCompilableBaseSystemUVE();
        auto cond = MakeDefaultParticleModuleUVE(21U, ParticleModuleTypeUVE::KillByCondition);
        cond.floats["Condition"].kind = ParticleValueKindUVE::Wire;
        cond.floats["Condition"].wireNode = 999U;
        sys.emitters[0].particleUpdate.push_back(std::move(cond));
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 21U, true));
    }
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].mathNodes = {
            {50U, "add", {51U}, 0.0F, 0.0F, 0.0F},
            {51U, "mul", {50U}, 0.0F, 0.0F, 0.0F},
        };
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error));
    }

    // 3. UserParameter wrong type
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.parameters.push_back({"MyColor", ParticleUserParameterTypeUVE::Color, {1.0F, 1.0F, 1.0F}, 1.0F});
        sys.emitters[0].emitterUpdate[0].floats["Rate"].kind = ParticleValueKindUVE::UserParameter;
        sys.emitters[0].emitterUpdate[0].floats["Rate"].parameter = "MyColor";
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 1U, true));
    }

    // 4. Render stage with no enabled renderer
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].render.clear();
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error));
    }
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].render[0].enabled = false;
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error));
    }

    // 5. SpriteRenderer / RibbonRenderer texture non-.uvtex or missing file
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].render[0].strings["Texture"] = "not_a_texture.png";
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 3U, true));
    }
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].render[0].strings["Texture"] = "missing_spark.uvtex";
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 3U, true));
    }

    // 6. MeshRenderer & ShapeMeshSurface missing mesh
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].render[0] = MakeDefaultParticleModuleUVE(30U, ParticleModuleTypeUVE::MeshRenderer);
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 30U, true));
    }
    {
        auto sys = MakeCompilableBaseSystemUVE();
        auto shape = MakeDefaultParticleModuleUVE(31U, ParticleModuleTypeUVE::ShapeMeshSurface);
        shape.strings["Mesh"] = "missing.uvmodel";
        sys.emitters[0].particleSpawn.push_back(std::move(shape));
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 31U, true));
    }

    // 7. EventOnDeath / EventOnCollision missing target or self-target
    {
        auto sys = MakeCompilableBaseSystemUVE();
        auto ev = MakeDefaultParticleModuleUVE(40U, ParticleModuleTypeUVE::EventOnDeath);
        ev.strings["TargetEmitter"] = "NonExistent";
        sys.emitters[0].particleUpdate.push_back(std::move(ev));
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 40U, true));
    }
    {
        auto sys = MakeCompilableBaseSystemUVE();
        auto ev = MakeDefaultParticleModuleUVE(41U, ParticleModuleTypeUVE::EventOnCollision);
        ev.strings["TargetEmitter"] = "Main";
        sys.emitters[0].particleUpdate.push_back(std::move(ev));
        EXPECT_FALSE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Error, 41U, true));
    }
}

TEST(VfxProgramUVETest, ReportsAllRequiredCompileWarnings) {
    ParticleProgramUVE program;
    std::vector<ParticleCompileMessageUVE> messages;

    // 1. CollisionDepth on CPU
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].target = ParticleSimTargetUVE::Cpu;
        sys.emitters[0].particleUpdate.push_back(MakeDefaultParticleModuleUVE(10U, ParticleModuleTypeUVE::CollisionDepth));
        EXPECT_TRUE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Warning, 10U, true));
    }

    // 2. CollisionPhysics with maxParticles > 2000
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].maxParticles = 2500U;
        sys.emitters[0].particleUpdate.push_back(
            MakeDefaultParticleModuleUVE(11U, ParticleModuleTypeUVE::CollisionPhysics));
        EXPECT_TRUE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Warning, 11U, true));
    }

    // 3. maxParticles > 100,000 on CPU
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].target = ParticleSimTargetUVE::Cpu;
        sys.emitters[0].maxParticles = 150'000U;
        EXPECT_TRUE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Warning));
    }

    // 4. Two shapes in Particle Spawn (only the last is kept)
    {
        auto sys = MakeCompilableBaseSystemUVE();
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(12U, ParticleModuleTypeUVE::ShapePoint));
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(13U, ParticleModuleTypeUVE::ShapeBox));
        EXPECT_TRUE(CompileParticleEmitterUVE(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverityUVE(messages, ParticleCompileMessageUVE::Severity::Warning, 12U, true));
        int shapeCount = 0;
        ParticleModuleTypeUVE keptShape = ParticleModuleTypeUVE::ShapePoint;
        for (const auto& op : program.particleSpawnOps) {
            if (op.type == ParticleModuleTypeUVE::ShapePoint || op.type == ParticleModuleTypeUVE::ShapeBox) {
                ++shapeCount;
                keptShape = op.type;
            }
        }
        EXPECT_EQ(shapeCount, 1);
        EXPECT_EQ(keptShape, ParticleModuleTypeUVE::ShapeBox);
    }
}

TEST(VfxProgramUVETest, AllocatesMinimalAttributeMaskAndValidatesExistingTexture) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectoryUVE("prog-tex");
    {
        std::ofstream texFile(dir / "spark.uvtex", std::ios::binary);
        texFile << "stub";
    }

    auto sys = MakeCompilableBaseSystemUVE();
    sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(5U, ParticleModuleTypeUVE::ShapePoint));
    sys.emitters[0].render[0].strings["Texture"] = "spark.uvtex";

    ParticleProgramUVE program;
    std::vector<ParticleCompileMessageUVE> messages;
    ASSERT_TRUE(CompileParticleEmitterUVE(sys, 0U, VfxCompileContextUVE{dir}, program, messages));
    EXPECT_TRUE(messages.empty());
    EXPECT_TRUE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Position));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Velocity));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Color));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Size));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Rotation));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Frame));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Mass));
    EXPECT_FALSE(HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::PrevPosition));

    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::VFX::Tests
