// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_system_asset.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] ParticleFloatInput NonDefaultFloat(const float val) {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = val;
    return input;
}

[[nodiscard]] ParticleVectorInput NonDefaultVec(const Math::Vector3& v) {
    ParticleVectorInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = v;
    return input;
}

[[nodiscard]] ParticleModule MakeRichNonDefaultModule(const std::uint32_t id,
                                                            const ParticleModuleDescriptor& desc) {
    ParticleModule mod;
    mod.id = id;
    mod.type = desc.type;
    mod.enabled = (id % 2U) == 1U;
    mod.graphX = static_cast<float>(id) * 15.0F + 2.5F;
    mod.graphY = static_cast<float>(id) * -10.0F;

    for (const auto& [name, def] : desc.defaultFloats) {
        if (name == "Scale" && desc.type == ParticleModuleType::SizeOverLife) {
            ParticleFloatInput curveInput;
            curveInput.kind = ParticleValueKind::Curve;
            curveInput.scale = 1.5F;
            curveInput.curve.smooth = true;
            curveInput.curve.keys = {
                {0.0F, 0.5F, 0.0F, 1.0F},
                {0.5F, 1.25F, 0.25F, -0.25F},
                {1.0F, 0.1F, -0.5F, 0.0F},
            };
            mod.floats[name] = curveInput;
        } else if (def.kind == ParticleValueKind::Constant) {
            mod.floats[name] = NonDefaultFloat(def.constant + 3.25F);
        } else {
            mod.floats[name] = NonDefaultFloat(2.25F);
        }
    }
    for (const auto& [name, def] : desc.defaultVectors) {
        mod.vectors[name] = NonDefaultVec(def.constant + Math::Vector3{1.5F, -2.5F, 3.5F});
    }
    for (const auto& [name, def] : desc.defaultColors) {
        if (desc.type == ParticleModuleType::ColorOverLife) {
            ParticleColorInput grad;
            grad.kind = ParticleValueKind::Gradient;
            grad.gradient.stops = {
                {0.0F, {1.0F, 0.5F, 0.2F}, 1.0F},
                {1.0F, {0.1F, 0.2F, 0.9F}, 0.25F},
            };
            mod.colors[name] = grad;
        } else {
            ParticleColorInput col;
            col.kind = ParticleValueKind::RandomRange;
            col.minColor = {0.1F, 0.2F, 0.3F};
            col.maxColor = {0.8F, 0.9F, 1.0F};
            col.alpha = def.alpha * 0.5F;
            mod.colors[name] = col;
        }
    }
    for (const auto& [name, def] : desc.defaultInts) {
        mod.ints[name] = def + 2;
    }
    for (const auto& [name, def] : desc.defaultStrings) {
        mod.strings[name] = def + "VFX/Custom_" + std::string(desc.canonicalName) + "_" + name;
    }
    return mod;
}

TEST(ParticleSystemAssetTest, RoundTripsEveryModuleType) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectory("every-module");
    const std::filesystem::path assetPath = dir / "VFX_EveryModule.uvfx";

    ParticleSystemAsset original;
    original.duration = 5.5F;
    original.looping = false;
    original.warmUpSeconds = 1.25F;
    original.fixedBounds = {{-5.0F, -2.0F, -5.0F}, {5.0F, 8.0F, 5.0F}};
    original.scalability[0] = {0.25F, 500U, 25.0F};
    original.scalability[1] = {0.5F, 1500U, 50.0F};
    original.scalability[2] = {1.0F, 0U, 0.0F};
    original.parameters = {
        {"RateScale", ParticleUserParameterType::Float, {2.5F, 0.0F, 0.0F}, 1.0F},
        {"WindVec", ParticleUserParameterType::Vector, {1.0F, 2.0F, 3.0F}, 1.0F},
        {"Tint", ParticleUserParameterType::Color, {0.9F, 0.5F, 0.1F}, 0.85F},
        {"ActiveFlag", ParticleUserParameterType::Bool, {1.0F, 0.0F, 0.0F}, 1.0F},
        {"BurstLoops", ParticleUserParameterType::Int, {4.0F, 0.0F, 0.0F}, 1.0F},
    };

    ParticleEmitterAsset emitter;
    emitter.name = "AllModules";
    emitter.enabled = true;
    emitter.space = ParticleSimSpace::Local;
    emitter.target = ParticleSimTarget::Auto;
    emitter.maxParticles = 4096U;
    emitter.seed = 4242U;
    emitter.mathNodes = {
        {101U, "const", {}, 3.0F, 10.0F, 20.0F},
        {102U, "add", {101U, 101U}, 0.0F, 40.0F, 20.0F},
    };
    emitter.comments = {{1U, 4U}};

    const auto& catalog = GetParticleModuleCatalog();
    ASSERT_EQ(catalog.size(), 46U);
    std::uint32_t nextId = 1U;
    for (const ParticleModuleDescriptor& desc : catalog) {
        ParticleModule mod = MakeRichNonDefaultModule(nextId++, desc);
        switch (desc.stage) {
            case ParticleStage::EmitterUpdate: emitter.emitterUpdate.push_back(std::move(mod)); break;
            case ParticleStage::ParticleSpawn: emitter.particleSpawn.push_back(std::move(mod)); break;
            case ParticleStage::ParticleUpdate: emitter.particleUpdate.push_back(std::move(mod)); break;
            case ParticleStage::Render: emitter.render.push_back(std::move(mod)); break;
        }
    }

    // Also attach parameter and wire inputs explicitly on a second emitter to exercise shorthands.
    ParticleEmitterAsset paramEmitter;
    paramEmitter.name = "BoundEmitter";
    paramEmitter.space = ParticleSimSpace::World;
    paramEmitter.target = ParticleSimTarget::Cpu;
    paramEmitter.maxParticles = 512U;
    {
        ParticleModule rate = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate);
        ParticleFloatInput rateInput;
        rateInput.kind = ParticleValueKind::UserParameter;
        rateInput.parameter = "RateScale";
        rateInput.scale = 2.0F;
        rate.floats["Rate"] = rateInput;
        paramEmitter.emitterUpdate.push_back(std::move(rate));
    }
    {
        ParticleModule wind = MakeDefaultParticleModule(2U, ParticleModuleType::Gravity);
        ParticleVectorInput accInput;
        accInput.kind = ParticleValueKind::UserParameter;
        accInput.parameter = "WindVec";
        wind.vectors["Acceleration"] = accInput;
        paramEmitter.particleUpdate.push_back(std::move(wind));

        ParticleModule cond = MakeDefaultParticleModule(3U, ParticleModuleType::KillByCondition);
        ParticleFloatInput condInput;
        condInput.kind = ParticleValueKind::Wire;
        condInput.wireNode = 102U;
        cond.floats["Condition"] = condInput;
        paramEmitter.particleUpdate.push_back(std::move(cond));
    }
    {
        ParticleModule col = MakeDefaultParticleModule(4U, ParticleModuleType::InitialColor);
        ParticleColorInput colInput;
        colInput.kind = ParticleValueKind::UserParameter;
        colInput.parameter = "Tint";
        col.colors["Color"] = colInput;
        paramEmitter.particleSpawn.push_back(std::move(col));
    }
    paramEmitter.render.push_back(MakeDefaultParticleModule(5U, ParticleModuleType::SpriteRenderer));

    original.emitters.push_back(std::move(emitter));
    original.emitters.push_back(std::move(paramEmitter));

    ASSERT_TRUE(IsParticleSystemAssetValid(original));
    ASSERT_TRUE(SaveParticleSystemAsset(original, assetPath));

    ParticleSystemAsset loaded;
    ASSERT_TRUE(LoadParticleSystemAsset(assetPath, loaded));
    EXPECT_EQ(loaded, original);

    std::filesystem::remove_all(dir);
}

TEST(ParticleSystemAssetTest, RejectsEachInvalidCase) {
    auto makeMinimalValid = []() {
        ParticleSystemAsset sys;
        ParticleEmitterAsset em;
        em.name = "Base";
        em.emitterUpdate.push_back(MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate));
        em.render.push_back(MakeDefaultParticleModule(2U, ParticleModuleType::SpriteRenderer));
        sys.emitters.push_back(std::move(em));
        sys.parameters.push_back({"FloatParam", ParticleUserParameterType::Float, {1.0F, 0.0F, 0.0F}, 1.0F});
        sys.parameters.push_back({"VecParam", ParticleUserParameterType::Vector, {1.0F, 0.0F, 0.0F}, 1.0F});
        sys.parameters.push_back({"ColorParam", ParticleUserParameterType::Color, {1.0F, 1.0F, 1.0F}, 1.0F});
        return sys;
    };

    ASSERT_TRUE(IsParticleSystemAssetValid(makeMinimalValid()));

    // 1. Empty emitters / >32 emitters
    {
        auto s = makeMinimalValid();
        s.emitters.clear();
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        for (std::size_t i = 1U; i <= kMaximumParticleEmitters; ++i) {
            auto em = s.emitters[0];
            em.name = "Emitter_" + std::to_string(i);
            s.emitters.push_back(std::move(em));
        }
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 2. Empty, oversized, NUL or duplicate emitter names
    {
        auto s = makeMinimalValid();
        s.emitters[0].name.clear();
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].name.assign(kMaximumParticleNameBytes + 1U, 'A');
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters.push_back(s.emitters[0]);
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 3. maxParticles bounds
    {
        auto s = makeMinimalValid();
        s.emitters[0].maxParticles = 0U;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].maxParticles = kMaximumParticlesPerEmitter + 1U;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 4. >64 modules per stage & module in wrong stage
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate.resize(kMaximumParticleModulesPerStage + 1U, s.emitters[0].emitterUpdate[0]);
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(99U, ParticleModuleType::Gravity));
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 5. Non-finite float, RandomRange min > max, unsorted/out-of-range Curve & Gradient
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate[0].floats["Rate"].constant = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKind::RandomRange;
        rate.min = 10.0F;
        rate.max = 2.0F;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKind::Curve;
        rate.curve.keys = {{0.8F, 1.0F}, {0.2F, 1.0F}};
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKind::Curve;
        rate.curve.keys = {{1.2F, 1.0F}};
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto mod = MakeDefaultParticleModule(10U, ParticleModuleType::ColorOverLife);
        mod.colors["Color"].kind = ParticleValueKind::Gradient;
        mod.colors["Color"].gradient.stops = {{0.7F, {1, 1, 1}, 1.0F}, {0.3F, {0, 0, 0}, 1.0F}};
        s.emitters[0].particleUpdate.push_back(std::move(mod));
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto mod = MakeDefaultParticleModule(10U, ParticleModuleType::ColorOverLife);
        mod.colors["Color"].kind = ParticleValueKind::Gradient;
        mod.colors["Color"].gradient.stops = {{-0.1F, {1, 1, 1}, 1.0F}};
        s.emitters[0].particleUpdate.push_back(std::move(mod));
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 6. Duplicate UserParameters and mismatched/missing parameter reference
    {
        auto s = makeMinimalValid();
        s.parameters.push_back(s.parameters[0]);
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKind::UserParameter;
        rate.parameter = "MissingParam";
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKind::UserParameter;
        rate.parameter = "ColorParam";
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 7. Duration, warmUpSeconds, fixedBounds
    {
        auto s = makeMinimalValid();
        s.duration = 0.0F;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.warmUpSeconds = -0.1F;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.fixedBounds.min.x = 5.0F;
        s.fixedBounds.max.x = 1.0F;
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 8. Unknown module input name
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate[0].floats["DoesNotExist"] = NonDefaultFloat(1.0F);
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].render[0].strings["UnknownStringKey"] = "bad";
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }

    // 9. Detached links and stage node positions validation
    {
        auto s = makeMinimalValid();
        s.emitters[0].detachedLinks.push_back({0U, 2U});
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].detachedLinks.push_back({5U, 5U});
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].detachedLinks.assign(kMaximumParticleDetachedLinks + 1U, {1U, 2U});
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].stageNodePositions[0] = {true, std::numeric_limits<float>::quiet_NaN(), 10.0F};
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].stageNodePositions[2] = {true, 100.0F, std::numeric_limits<float>::infinity()};
        EXPECT_FALSE(IsParticleSystemAssetValid(s));
    }
}

TEST(ParticleSystemAssetTest, RoundTripsStageNodePositionsDetachedLinksAndOriginPlacement) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectory("stage-detached-links");
    const std::filesystem::path assetPath = dir / "VFX_GraphLayout.uvfx";

    ParticleSystemAsset original;
    ParticleEmitterAsset em;
    em.name = "LayoutEmitter";
    ParticleModule rate = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate);
    rate.graphX = 0.0F;
    rate.graphY = 0.0F;
    rate.graphPosAtOrigin = true;
    em.emitterUpdate.push_back(std::move(rate));
    em.render.push_back(MakeDefaultParticleModule(2U, ParticleModuleType::SpriteRenderer, 400.0F, 600.0F));
    em.detached.push_back(MakeDefaultParticleModule(10U, ParticleModuleType::ShapeSphere, 100.0F, 200.0F));
    em.detached.push_back(MakeDefaultParticleModule(11U, ParticleModuleType::InitialVelocity, 360.0F, 200.0F));
    em.detachedLinks = {{10U, 11U}};
    em.stageNodePositions[0] = {true, 920.0F, 20.0F};
    em.stageNodePositions[1] = {false, 0.0F, 0.0F};
    em.stageNodePositions[2] = {true, 1040.0F, 440.0F};
    em.stageNodePositions[3] = {true, 1100.0F, 680.0F};
    original.emitters.push_back(std::move(em));

    ASSERT_TRUE(IsParticleSystemAssetValid(original));
    ASSERT_TRUE(SaveParticleSystemAsset(original, assetPath));

    ParticleSystemAsset loaded;
    ASSERT_TRUE(LoadParticleSystemAsset(assetPath, loaded));
    EXPECT_EQ(loaded, original);
    EXPECT_TRUE(loaded.emitters[0].emitterUpdate[0].graphPosAtOrigin);
    EXPECT_EQ(loaded.emitters[0].detachedLinks.size(), 1U);
    EXPECT_TRUE(loaded.emitters[0].stageNodePositions[0].placed);
    EXPECT_FALSE(loaded.emitters[0].stageNodePositions[1].placed);
    EXPECT_TRUE(loaded.emitters[0].stageNodePositions[2].placed);
    EXPECT_TRUE(loaded.emitters[0].stageNodePositions[3].placed);

    // Legacy JSON without stageNodes / detachedLinks / at still loads cleanly with defaults.
    const std::filesystem::path legacyPath = dir / "VFX_Legacy.uvfx";
    const std::string legacyJson =
        R"({"schema":"uve-particle-system-v1","emitters":[{"name":"Legacy","emitterUpdate":[{"id":1,"type":"SpawnRate"}],"render":[{"id":2,"type":"SpriteRenderer"}]}]})";
    const auto* bytes = reinterpret_cast<const std::byte*>(legacyJson.data());
    ASSERT_TRUE(WriteUveFile(legacyPath, AssetKind::ParticleSystem, {bytes, bytes + legacyJson.size()}));
    ParticleSystemAsset legacyLoaded;
    ASSERT_TRUE(LoadParticleSystemAsset(legacyPath, legacyLoaded));
    EXPECT_FALSE(legacyLoaded.emitters[0].emitterUpdate[0].graphPosAtOrigin);
    EXPECT_TRUE(legacyLoaded.emitters[0].detachedLinks.empty());
    for (const ParticleStageNodePos& p : legacyLoaded.emitters[0].stageNodePositions) {
        EXPECT_FALSE(p.placed);
    }

    std::filesystem::remove_all(dir);
}

TEST(ParticleSystemAssetTest, RejectsWrongSchemaAndKind) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectory("bad-schema-kind");

    {
        const std::filesystem::path wrongKindPath = dir / "wrong_kind.uvfx";
        const std::string json = R"({"schema":"uve-particle-system-v1","emitters":[{"name":"A"}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(WriteUveFile(wrongKindPath, AssetKind::Blueprint, {bytes, bytes + json.size()}));
        ParticleSystemAsset out;
        EXPECT_FALSE(LoadParticleSystemAsset(wrongKindPath, out));
    }

    {
        const std::filesystem::path wrongSchemaPath = dir / "wrong_schema.uvfx";
        const std::string json = R"({"schema":"unknown-v99","emitters":[{"name":"A"}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(
            WriteUveFile(wrongSchemaPath, AssetKind::ParticleSystem, {bytes, bytes + json.size()}));
        ParticleSystemAsset out;
        EXPECT_FALSE(LoadParticleSystemAsset(wrongSchemaPath, out));
    }

    {
        const std::filesystem::path unknownInputPath = dir / "unknown_input.uvfx";
        const std::string json =
            R"({"schema":"uve-particle-system-v1","emitters":[{"name":"A","emitterUpdate":[{"id":1,"type":"SpawnRate","floats":{"Bogus":{"constant":5}}}],"render":[{"id":2,"type":"SpriteRenderer"}]}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(
            WriteUveFile(unknownInputPath, AssetKind::ParticleSystem, {bytes, bytes + json.size()}));
        ParticleSystemAsset out;
        EXPECT_FALSE(LoadParticleSystemAsset(unknownInputPath, out));
    }

    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::Asset::Tests
