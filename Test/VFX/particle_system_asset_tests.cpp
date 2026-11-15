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

[[nodiscard]] ParticleFloatInputUVE NonDefaultFloatUVE(const float val) {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = val;
    return input;
}

[[nodiscard]] ParticleVectorInputUVE NonDefaultVecUVE(const Math::Vector3UVE& v) {
    ParticleVectorInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = v;
    return input;
}

[[nodiscard]] ParticleModuleUVE MakeRichNonDefaultModuleUVE(const std::uint32_t id,
                                                            const ParticleModuleDescriptorUVE& desc) {
    ParticleModuleUVE mod;
    mod.id = id;
    mod.type = desc.type;
    mod.enabled = (id % 2U) == 1U;
    mod.graphX = static_cast<float>(id) * 15.0F + 2.5F;
    mod.graphY = static_cast<float>(id) * -10.0F;

    for (const auto& [name, def] : desc.defaultFloats) {
        if (name == "Scale" && desc.type == ParticleModuleTypeUVE::SizeOverLife) {
            ParticleFloatInputUVE curveInput;
            curveInput.kind = ParticleValueKindUVE::Curve;
            curveInput.scale = 1.5F;
            curveInput.curve.smooth = true;
            curveInput.curve.keys = {
                {0.0F, 0.5F, 0.0F, 1.0F},
                {0.5F, 1.25F, 0.25F, -0.25F},
                {1.0F, 0.1F, -0.5F, 0.0F},
            };
            mod.floats[name] = curveInput;
        } else if (def.kind == ParticleValueKindUVE::Constant) {
            mod.floats[name] = NonDefaultFloatUVE(def.constant + 3.25F);
        } else {
            mod.floats[name] = NonDefaultFloatUVE(2.25F);
        }
    }
    for (const auto& [name, def] : desc.defaultVectors) {
        mod.vectors[name] = NonDefaultVecUVE(def.constant + Math::Vector3UVE{1.5F, -2.5F, 3.5F});
    }
    for (const auto& [name, def] : desc.defaultColors) {
        if (desc.type == ParticleModuleTypeUVE::ColorOverLife) {
            ParticleColorInputUVE grad;
            grad.kind = ParticleValueKindUVE::Gradient;
            grad.gradient.stops = {
                {0.0F, {1.0F, 0.5F, 0.2F}, 1.0F},
                {1.0F, {0.1F, 0.2F, 0.9F}, 0.25F},
            };
            mod.colors[name] = grad;
        } else {
            ParticleColorInputUVE col;
            col.kind = ParticleValueKindUVE::RandomRange;
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

TEST(ParticleSystemAssetUVETest, RoundTripsEveryModuleType) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectoryUVE("every-module");
    const std::filesystem::path assetPath = dir / "VFX_EveryModule.uvfx";

    ParticleSystemAssetUVE original;
    original.duration = 5.5F;
    original.looping = false;
    original.warmUpSeconds = 1.25F;
    original.fixedBounds = {{-5.0F, -2.0F, -5.0F}, {5.0F, 8.0F, 5.0F}};
    original.scalability[0] = {0.25F, 500U, 25.0F};
    original.scalability[1] = {0.5F, 1500U, 50.0F};
    original.scalability[2] = {1.0F, 0U, 0.0F};
    original.parameters = {
        {"RateScale", ParticleUserParameterTypeUVE::Float, {2.5F, 0.0F, 0.0F}, 1.0F},
        {"WindVec", ParticleUserParameterTypeUVE::Vector, {1.0F, 2.0F, 3.0F}, 1.0F},
        {"Tint", ParticleUserParameterTypeUVE::Color, {0.9F, 0.5F, 0.1F}, 0.85F},
        {"ActiveFlag", ParticleUserParameterTypeUVE::Bool, {1.0F, 0.0F, 0.0F}, 1.0F},
        {"BurstLoops", ParticleUserParameterTypeUVE::Int, {4.0F, 0.0F, 0.0F}, 1.0F},
    };

    ParticleEmitterAssetUVE emitter;
    emitter.name = "AllModules";
    emitter.enabled = true;
    emitter.space = ParticleSimSpaceUVE::Local;
    emitter.target = ParticleSimTargetUVE::Auto;
    emitter.maxParticles = 4096U;
    emitter.seed = 4242U;
    emitter.mathNodes = {
        {101U, "const", {}, 3.0F, 10.0F, 20.0F},
        {102U, "add", {101U, 101U}, 0.0F, 40.0F, 20.0F},
    };
    emitter.comments = {{1U, 4U}};

    const auto& catalog = GetParticleModuleCatalogUVE();
    ASSERT_EQ(catalog.size(), 46U);
    std::uint32_t nextId = 1U;
    for (const ParticleModuleDescriptorUVE& desc : catalog) {
        ParticleModuleUVE mod = MakeRichNonDefaultModuleUVE(nextId++, desc);
        switch (desc.stage) {
            case ParticleStageUVE::EmitterUpdate: emitter.emitterUpdate.push_back(std::move(mod)); break;
            case ParticleStageUVE::ParticleSpawn: emitter.particleSpawn.push_back(std::move(mod)); break;
            case ParticleStageUVE::ParticleUpdate: emitter.particleUpdate.push_back(std::move(mod)); break;
            case ParticleStageUVE::Render: emitter.render.push_back(std::move(mod)); break;
        }
    }

    // Also attach parameter and wire inputs explicitly on a second emitter to exercise shorthands.
    ParticleEmitterAssetUVE paramEmitter;
    paramEmitter.name = "BoundEmitter";
    paramEmitter.space = ParticleSimSpaceUVE::World;
    paramEmitter.target = ParticleSimTargetUVE::Cpu;
    paramEmitter.maxParticles = 512U;
    {
        ParticleModuleUVE rate = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate);
        ParticleFloatInputUVE rateInput;
        rateInput.kind = ParticleValueKindUVE::UserParameter;
        rateInput.parameter = "RateScale";
        rateInput.scale = 2.0F;
        rate.floats["Rate"] = rateInput;
        paramEmitter.emitterUpdate.push_back(std::move(rate));
    }
    {
        ParticleModuleUVE wind = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::Gravity);
        ParticleVectorInputUVE accInput;
        accInput.kind = ParticleValueKindUVE::UserParameter;
        accInput.parameter = "WindVec";
        wind.vectors["Acceleration"] = accInput;
        paramEmitter.particleUpdate.push_back(std::move(wind));

        ParticleModuleUVE cond = MakeDefaultParticleModuleUVE(3U, ParticleModuleTypeUVE::KillByCondition);
        ParticleFloatInputUVE condInput;
        condInput.kind = ParticleValueKindUVE::Wire;
        condInput.wireNode = 102U;
        cond.floats["Condition"] = condInput;
        paramEmitter.particleUpdate.push_back(std::move(cond));
    }
    {
        ParticleModuleUVE col = MakeDefaultParticleModuleUVE(4U, ParticleModuleTypeUVE::InitialColor);
        ParticleColorInputUVE colInput;
        colInput.kind = ParticleValueKindUVE::UserParameter;
        colInput.parameter = "Tint";
        col.colors["Color"] = colInput;
        paramEmitter.particleSpawn.push_back(std::move(col));
    }
    paramEmitter.render.push_back(MakeDefaultParticleModuleUVE(5U, ParticleModuleTypeUVE::SpriteRenderer));

    original.emitters.push_back(std::move(emitter));
    original.emitters.push_back(std::move(paramEmitter));

    ASSERT_TRUE(IsParticleSystemAssetValidUVE(original));
    ASSERT_TRUE(SaveParticleSystemAssetUVE(original, assetPath));

    ParticleSystemAssetUVE loaded;
    ASSERT_TRUE(LoadParticleSystemAssetUVE(assetPath, loaded));
    EXPECT_EQ(loaded, original);

    std::filesystem::remove_all(dir);
}

TEST(ParticleSystemAssetUVETest, RejectsEachInvalidCase) {
    auto makeMinimalValid = []() {
        ParticleSystemAssetUVE sys;
        ParticleEmitterAssetUVE em;
        em.name = "Base";
        em.emitterUpdate.push_back(MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate));
        em.render.push_back(MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::SpriteRenderer));
        sys.emitters.push_back(std::move(em));
        sys.parameters.push_back({"FloatParam", ParticleUserParameterTypeUVE::Float, {1.0F, 0.0F, 0.0F}, 1.0F});
        sys.parameters.push_back({"VecParam", ParticleUserParameterTypeUVE::Vector, {1.0F, 0.0F, 0.0F}, 1.0F});
        sys.parameters.push_back({"ColorParam", ParticleUserParameterTypeUVE::Color, {1.0F, 1.0F, 1.0F}, 1.0F});
        return sys;
    };

    ASSERT_TRUE(IsParticleSystemAssetValidUVE(makeMinimalValid()));

    // 1. Empty emitters / >32 emitters
    {
        auto s = makeMinimalValid();
        s.emitters.clear();
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        for (std::size_t i = 1U; i <= kMaximumParticleEmittersUVE; ++i) {
            auto em = s.emitters[0];
            em.name = "Emitter_" + std::to_string(i);
            s.emitters.push_back(std::move(em));
        }
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 2. Empty, oversized, NUL or duplicate emitter names
    {
        auto s = makeMinimalValid();
        s.emitters[0].name.clear();
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].name.assign(kMaximumParticleNameBytesUVE + 1U, 'A');
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters.push_back(s.emitters[0]);
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 3. maxParticles bounds
    {
        auto s = makeMinimalValid();
        s.emitters[0].maxParticles = 0U;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].maxParticles = kMaximumParticlesPerEmitterUVE + 1U;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 4. >64 modules per stage & module in wrong stage
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate.resize(kMaximumParticleModulesPerStageUVE + 1U, s.emitters[0].emitterUpdate[0]);
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(99U, ParticleModuleTypeUVE::Gravity));
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 5. Non-finite float, RandomRange min > max, unsorted/out-of-range Curve & Gradient
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate[0].floats["Rate"].constant = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKindUVE::RandomRange;
        rate.min = 10.0F;
        rate.max = 2.0F;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKindUVE::Curve;
        rate.curve.keys = {{0.8F, 1.0F}, {0.2F, 1.0F}};
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKindUVE::Curve;
        rate.curve.keys = {{1.2F, 1.0F}};
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto mod = MakeDefaultParticleModuleUVE(10U, ParticleModuleTypeUVE::ColorOverLife);
        mod.colors["Color"].kind = ParticleValueKindUVE::Gradient;
        mod.colors["Color"].gradient.stops = {{0.7F, {1, 1, 1}, 1.0F}, {0.3F, {0, 0, 0}, 1.0F}};
        s.emitters[0].particleUpdate.push_back(std::move(mod));
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto mod = MakeDefaultParticleModuleUVE(10U, ParticleModuleTypeUVE::ColorOverLife);
        mod.colors["Color"].kind = ParticleValueKindUVE::Gradient;
        mod.colors["Color"].gradient.stops = {{-0.1F, {1, 1, 1}, 1.0F}};
        s.emitters[0].particleUpdate.push_back(std::move(mod));
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 6. Duplicate UserParameters and mismatched/missing parameter reference
    {
        auto s = makeMinimalValid();
        s.parameters.push_back(s.parameters[0]);
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKindUVE::UserParameter;
        rate.parameter = "MissingParam";
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        auto& rate = s.emitters[0].emitterUpdate[0].floats["Rate"];
        rate.kind = ParticleValueKindUVE::UserParameter;
        rate.parameter = "ColorParam";
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 7. Duration, warmUpSeconds, fixedBounds
    {
        auto s = makeMinimalValid();
        s.duration = 0.0F;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.warmUpSeconds = -0.1F;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.fixedBounds.min.x = 5.0F;
        s.fixedBounds.max.x = 1.0F;
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }

    // 8. Unknown module input name
    {
        auto s = makeMinimalValid();
        s.emitters[0].emitterUpdate[0].floats["DoesNotExist"] = NonDefaultFloatUVE(1.0F);
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
    {
        auto s = makeMinimalValid();
        s.emitters[0].render[0].strings["UnknownStringKey"] = "bad";
        EXPECT_FALSE(IsParticleSystemAssetValidUVE(s));
    }
}

TEST(ParticleSystemAssetUVETest, RejectsWrongSchemaAndKind) {
    const auto dir = ::UVE::Tests::MakeTestCaseDirectoryUVE("bad-schema-kind");

    {
        const std::filesystem::path wrongKindPath = dir / "wrong_kind.uvfx";
        const std::string json = R"({"schema":"uve-particle-system-v1","emitters":[{"name":"A"}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(WriteUveFileUVE(wrongKindPath, AssetKindUVE::Blueprint, {bytes, bytes + json.size()}));
        ParticleSystemAssetUVE out;
        EXPECT_FALSE(LoadParticleSystemAssetUVE(wrongKindPath, out));
    }

    {
        const std::filesystem::path wrongSchemaPath = dir / "wrong_schema.uvfx";
        const std::string json = R"({"schema":"unknown-v99","emitters":[{"name":"A"}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(
            WriteUveFileUVE(wrongSchemaPath, AssetKindUVE::ParticleSystem, {bytes, bytes + json.size()}));
        ParticleSystemAssetUVE out;
        EXPECT_FALSE(LoadParticleSystemAssetUVE(wrongSchemaPath, out));
    }

    {
        const std::filesystem::path unknownInputPath = dir / "unknown_input.uvfx";
        const std::string json =
            R"({"schema":"uve-particle-system-v1","emitters":[{"name":"A","emitterUpdate":[{"id":1,"type":"SpawnRate","floats":{"Bogus":{"constant":5}}}],"render":[{"id":2,"type":"SpriteRenderer"}]}]})";
        const auto* bytes = reinterpret_cast<const std::byte*>(json.data());
        ASSERT_TRUE(
            WriteUveFileUVE(unknownInputPath, AssetKindUVE::ParticleSystem, {bytes, bytes + json.size()}));
        ParticleSystemAssetUVE out;
        EXPECT_FALSE(LoadParticleSystemAssetUVE(unknownInputPath, out));
    }

    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::Asset::Tests
