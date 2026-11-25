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

[[nodiscard]] ParticleSystemAsset MakeCompilableBaseSystem() {
    ParticleSystemAsset sys;
    ParticleEmitterAsset em;
    em.name = "Main";
    em.maxParticles = 500U;
    em.emitterUpdate.push_back(MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate));
    em.particleSpawn.push_back(MakeDefaultParticleModule(2U, ParticleModuleType::InitialLifetime));
    em.render.push_back(MakeDefaultParticleModule(3U, ParticleModuleType::SpriteRenderer));
    sys.emitters.push_back(std::move(em));
    return sys;
}

[[nodiscard]] bool HasSeverity(const std::vector<ParticleCompileMessage>& msgs,
                                  const ParticleCompileMessage::Severity sev,
                                  const std::uint32_t moduleId = 0U,
                                  const bool matchId = false) {
    return std::any_of(msgs.begin(), msgs.end(), [&](const ParticleCompileMessage& msg) {
        return msg.severity == sev && (!matchId || msg.moduleId == moduleId);
    });
}

TEST(VfxProgramTest, ReportsAllRequiredCompileErrors) {
    ParticleProgram program;
    std::vector<ParticleCompileMessage> messages;

    // 1. Wrong stage
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(20U, ParticleModuleType::Gravity));
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 20U, true));
    }

    // 2. Wire to missing node & cycle in Math nodes
    {
        auto sys = MakeCompilableBaseSystem();
        auto cond = MakeDefaultParticleModule(21U, ParticleModuleType::KillByCondition);
        cond.floats["Condition"].kind = ParticleValueKind::Wire;
        cond.floats["Condition"].wireNode = 999U;
        sys.emitters[0].particleUpdate.push_back(std::move(cond));
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 21U, true));
    }
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].mathNodes = {
            {50U, "add", {51U}, 0.0F, 0.0F, 0.0F},
            {51U, "mul", {50U}, 0.0F, 0.0F, 0.0F},
        };
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error));
    }

    // 3. UserParameter wrong type
    {
        auto sys = MakeCompilableBaseSystem();
        sys.parameters.push_back({"MyColor", ParticleUserParameterType::Color, {1.0F, 1.0F, 1.0F}, 1.0F});
        sys.emitters[0].emitterUpdate[0].floats["Rate"].kind = ParticleValueKind::UserParameter;
        sys.emitters[0].emitterUpdate[0].floats["Rate"].parameter = "MyColor";
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 1U, true));
    }

    // 4. Render stage with no enabled renderer
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].render.clear();
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error));
    }
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].render[0].enabled = false;
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error));
    }

    // 5. SpriteRenderer / RibbonRenderer texture non-.uvtex or missing file
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].render[0].strings["Texture"] = "not_a_texture.png";
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 3U, true));
    }
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].render[0].strings["Texture"] = "missing_spark.uvtex";
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 3U, true));
    }

    // 6. MeshRenderer & ShapeMeshSurface missing mesh
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].render[0] = MakeDefaultParticleModule(30U, ParticleModuleType::MeshRenderer);
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 30U, true));
    }
    {
        auto sys = MakeCompilableBaseSystem();
        auto shape = MakeDefaultParticleModule(31U, ParticleModuleType::ShapeMeshSurface);
        shape.strings["Mesh"] = "missing.uvmodel";
        sys.emitters[0].particleSpawn.push_back(std::move(shape));
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 31U, true));
    }

    // 7. EventOnDeath / EventOnCollision missing target or self-target
    {
        auto sys = MakeCompilableBaseSystem();
        auto ev = MakeDefaultParticleModule(40U, ParticleModuleType::EventOnDeath);
        ev.strings["TargetEmitter"] = "NonExistent";
        sys.emitters[0].particleUpdate.push_back(std::move(ev));
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 40U, true));
    }
    {
        auto sys = MakeCompilableBaseSystem();
        auto ev = MakeDefaultParticleModule(41U, ParticleModuleType::EventOnCollision);
        ev.strings["TargetEmitter"] = "Main";
        sys.emitters[0].particleUpdate.push_back(std::move(ev));
        EXPECT_FALSE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Error, 41U, true));
    }
}

TEST(VfxProgramTest, ReportsAllRequiredCompileWarnings) {
    ParticleProgram program;
    std::vector<ParticleCompileMessage> messages;

    // 1. CollisionDepth on CPU
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].target = ParticleSimTarget::Cpu;
        sys.emitters[0].particleUpdate.push_back(MakeDefaultParticleModule(10U, ParticleModuleType::CollisionDepth));
        EXPECT_TRUE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Warning, 10U, true));
    }

    // 2. CollisionPhysics with maxParticles > 2000
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].maxParticles = 2500U;
        sys.emitters[0].particleUpdate.push_back(
            MakeDefaultParticleModule(11U, ParticleModuleType::CollisionPhysics));
        EXPECT_TRUE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Warning, 11U, true));
    }

    // 3. maxParticles > 100,000 on CPU
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].target = ParticleSimTarget::Cpu;
        sys.emitters[0].maxParticles = 150'000U;
        EXPECT_TRUE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Warning));
    }

    // 4. Two shapes in Particle Spawn (only the last is kept)
    {
        auto sys = MakeCompilableBaseSystem();
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(12U, ParticleModuleType::ShapePoint));
        sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(13U, ParticleModuleType::ShapeBox));
        EXPECT_TRUE(CompileParticleEmitter(sys, 0U, program, messages));
        EXPECT_TRUE(HasSeverity(messages, ParticleCompileMessage::Severity::Warning, 12U, true));
        int shapeCount = 0;
        ParticleModuleType keptShape = ParticleModuleType::ShapePoint;
        for (const auto& op : program.particleSpawnOps) {
            if (op.type == ParticleModuleType::ShapePoint || op.type == ParticleModuleType::ShapeBox) {
                ++shapeCount;
                keptShape = op.type;
            }
        }
        EXPECT_EQ(shapeCount, 1);
        EXPECT_EQ(keptShape, ParticleModuleType::ShapeBox);
    }
}

TEST(VfxProgramTest, AllocatesMinimalAttributeMaskAndValidatesExistingTexture) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectory("prog-tex");
    {
        std::ofstream texFile(dir / "spark.uvtex", std::ios::binary);
        texFile << "stub";
    }

    auto sys = MakeCompilableBaseSystem();
    sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(5U, ParticleModuleType::ShapePoint));
    sys.emitters[0].render[0].strings["Texture"] = "spark.uvtex";

    ParticleProgram program;
    std::vector<ParticleCompileMessage> messages;
    ASSERT_TRUE(CompileParticleEmitter(sys, 0U, VfxCompileContext{dir}, program, messages));
    EXPECT_TRUE(messages.empty());
    EXPECT_TRUE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Position));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Velocity));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Color));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Size));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Rotation));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Frame));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::Mass));
    EXPECT_FALSE(HasVfxAttribute(program.attributeMask, VfxAttributeBit::PrevPosition));

    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::VFX::Tests
