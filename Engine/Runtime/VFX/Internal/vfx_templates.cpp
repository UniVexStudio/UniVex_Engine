// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_templates.h"

#include <utility>
#include <vector>

namespace UVE::VFX {
namespace {

using Asset::ParticleColorInputUVE;
using Asset::ParticleEmitterAssetUVE;
using Asset::ParticleFloatInputUVE;
using Asset::ParticleGradientStopUVE;
using Asset::ParticleModuleTypeUVE;
using Asset::ParticleModuleUVE;
using Asset::ParticleSystemAssetUVE;
using Asset::ParticleValueKindUVE;
using Asset::ParticleVectorInputUVE;

[[nodiscard]] ParticleFloatInputUVE ConstFloatUVE(const float value) noexcept {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleFloatInputUVE RandomFloatUVE(const float minVal, const float maxVal) noexcept {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::RandomRange;
    input.min = minVal;
    input.max = maxVal;
    return input;
}

[[nodiscard]] ParticleFloatInputUVE CurveFloatUVE(std::initializer_list<std::pair<float, float>> points,
                                                  const bool smooth = true) {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Curve;
    input.scale = 1.0F;
    input.curve.smooth = smooth;
    input.curve.keys.reserve(points.size());
    for (const auto& [t, v] : points) {
        input.curve.keys.push_back(Asset::ParticleCurveKeyUVE{t, v, 0.0F, 0.0F});
    }
    return input;
}

[[nodiscard]] ParticleVectorInputUVE ConstVecUVE(const Math::Vector3UVE& v) noexcept {
    ParticleVectorInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = v;
    return input;
}

[[nodiscard]] ParticleColorInputUVE ConstColorUVE(const Math::ColorUVE& c, const float alpha = 1.0F) noexcept {
    ParticleColorInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = c;
    input.alpha = alpha;
    return input;
}

[[nodiscard]] ParticleColorInputUVE GradientColorUVE(
    std::initializer_list<ParticleGradientStopUVE> stops) {
    ParticleColorInputUVE input;
    input.kind = ParticleValueKindUVE::Gradient;
    input.gradient.stops.assign(stops.begin(), stops.end());
    return input;
}

[[nodiscard]] ParticleSystemAssetUVE BuildEmptyTemplateUVE() {
    ParticleSystemAssetUVE system;
    system.duration = 2.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};

    ParticleEmitterAssetUVE emitter;
    emitter.name = "Emitter";
    emitter.maxParticles = 1000U;

    std::uint32_t nextId = 1U;
    ParticleModuleUVE rate = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
    rate.floats["Rate"] = ConstFloatUVE(10.0F);
    emitter.emitterUpdate.push_back(std::move(rate));

    emitter.particleSpawn.push_back(
        Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapePoint));
    ParticleModuleUVE life =
        Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"] = ConstFloatUVE(1.0F);
    emitter.particleSpawn.push_back(std::move(life));

    emitter.render.push_back(
        Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer));

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAssetUVE BuildFountainTemplateUVE() {
    ParticleSystemAssetUVE system;
    system.duration = 4.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-4.0F, -1.0F, -4.0F}, {4.0F, 7.0F, 4.0F}};

    ParticleEmitterAssetUVE emitter;
    emitter.name = "WaterJets";
    emitter.maxParticles = 2000U;

    std::uint32_t nextId = 1U;
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
        mod.floats["Rate"] = ConstFloatUVE(120.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapeSphere);
        mod.floats["Radius"] = ConstFloatUVE(0.08F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::VelocityInCone);
        mod.vectors["Axis"] = ConstVecUVE({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloatUVE(12.0F);
        mod.floats["Speed"] = RandomFloatUVE(6.0F, 8.5F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloatUVE(1.4F, 1.9F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSize);
        mod.floats["Size"] = RandomFloatUVE(0.06F, 0.12F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialColor);
        mod.colors["Color"] = ConstColorUVE({0.75F, 0.92F, 1.0F}, 0.85F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Gravity);
        mod.vectors["Acceleration"] = ConstVecUVE({0.0F, -9.8F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Drag);
        mod.floats["Coefficient"] = ConstFloatUVE(0.15F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ColorOverLife);
        mod.colors["Color"] = GradientColorUVE({
            {0.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
            {0.7F, {0.8F, 0.95F, 1.0F}, 0.7F},
            {1.0F, {0.9F, 0.98F, 1.0F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SizeOverLife);
        mod.floats["Scale"] = CurveFloatUVE({{0.0F, 0.8F}, {0.5F, 1.0F}, {1.0F, 0.4F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::CollisionPhysics);
        mod.floats["Bounce"] = ConstFloatUVE(0.55F);
        mod.floats["Friction"] = ConstFloatUVE(0.18F);
        mod.floats["LifeLoss"] = ConstFloatUVE(0.05F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer);
        mod.ints["Facing"] = 1;
        mod.ints["Blend"] = 1;
        mod.floats["SoftDistance"] = ConstFloatUVE(0.15F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAssetUVE BuildSparksTemplateUVE() {
    ParticleSystemAssetUVE system;
    system.duration = 2.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-5.0F, -2.0F, -5.0F}, {5.0F, 5.0F, 5.0F}};

    ParticleEmitterAssetUVE emitter;
    emitter.name = "Sparks";
    emitter.maxParticles = 2000U;

    std::uint32_t nextId = 1U;
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
        mod.floats["Rate"] = ConstFloatUVE(180.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnBurst);
        mod.floats["Time"] = ConstFloatUVE(0.0F);
        mod.ints["Count"] = 40;
        mod.ints["Cycles"] = 1;
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapeSphere);
        mod.floats["Radius"] = ConstFloatUVE(0.1F);
        mod.ints["Surface"] = 1;
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::VelocityInCone);
        mod.vectors["Axis"] = ConstVecUVE({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloatUVE(55.0F);
        mod.floats["Speed"] = RandomFloatUVE(4.0F, 9.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloatUVE(0.4F, 0.95F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSize);
        mod.floats["Size"] = RandomFloatUVE(0.04F, 0.08F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialColor);
        mod.colors["Color"] = ConstColorUVE({1.0F, 0.65F, 0.25F}, 1.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Gravity);
        mod.vectors["Acceleration"] = ConstVecUVE({0.0F, -9.8F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Drag);
        mod.floats["Coefficient"] = ConstFloatUVE(0.8F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ColorOverLife);
        mod.colors["Color"] = GradientColorUVE({
            {0.0F, {1.0F, 0.95F, 0.65F}, 1.0F},
            {0.5F, {1.0F, 0.45F, 0.1F}, 0.9F},
            {1.0F, {0.8F, 0.15F, 0.02F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SizeOverLife);
        mod.floats["Scale"] = CurveFloatUVE({{0.0F, 1.0F}, {1.0F, 0.1F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::CollisionPhysics);
        mod.floats["Bounce"] = ConstFloatUVE(0.65F);
        mod.floats["Friction"] = ConstFloatUVE(0.12F);
        mod.floats["LifeLoss"] = ConstFloatUVE(0.08F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer);
        mod.ints["Facing"] = 2;
        mod.floats["StretchScale"] = ConstFloatUVE(0.25F);
        mod.ints["Blend"] = 0;
        mod.floats["Emissive"] = ConstFloatUVE(3.0F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAssetUVE BuildFireTemplateUVE() {
    ParticleSystemAssetUVE system;
    system.duration = 3.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-2.0F, -0.5F, -2.0F}, {2.0F, 4.5F, 2.0F}};

    // Emitter 1: Flames
    {
        ParticleEmitterAssetUVE flames;
        flames.name = "Flames";
        flames.maxParticles = 800U;
        std::uint32_t nextId = 1U;

        ParticleModuleUVE rate = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
        rate.floats["Rate"] = ConstFloatUVE(65.0F);
        flames.emitterUpdate.push_back(std::move(rate));

        ParticleModuleUVE shape =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapeCylinder);
        shape.floats["Radius"] = ConstFloatUVE(0.35F);
        shape.floats["Height"] = ConstFloatUVE(0.15F);
        flames.particleSpawn.push_back(std::move(shape));

        ParticleModuleUVE vel =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::VelocityInCone);
        vel.vectors["Axis"] = ConstVecUVE({0.0F, 1.0F, 0.0F});
        vel.floats["Angle"] = ConstFloatUVE(14.0F);
        vel.floats["Speed"] = RandomFloatUVE(1.2F, 2.4F);
        flames.particleSpawn.push_back(std::move(vel));

        ParticleModuleUVE life =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
        life.floats["Lifetime"] = RandomFloatUVE(0.55F, 0.95F);
        flames.particleSpawn.push_back(std::move(life));

        ParticleModuleUVE size = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSize);
        size.floats["Size"] = RandomFloatUVE(0.35F, 0.6F);
        flames.particleSpawn.push_back(std::move(size));

        ParticleModuleUVE rot =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialRotation);
        rot.floats["Rotation"] = RandomFloatUVE(0.0F, 360.0F);
        flames.particleSpawn.push_back(std::move(rot));

        ParticleModuleUVE spin = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSpin);
        spin.floats["Spin"] = RandomFloatUVE(-45.0F, 45.0F);
        flames.particleSpawn.push_back(std::move(spin));

        ParticleModuleUVE buoyancy = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Gravity);
        buoyancy.vectors["Acceleration"] = ConstVecUVE({0.0F, 1.5F, 0.0F});
        flames.particleUpdate.push_back(std::move(buoyancy));

        ParticleModuleUVE drag = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Drag);
        drag.floats["Coefficient"] = ConstFloatUVE(0.6F);
        flames.particleUpdate.push_back(std::move(drag));

        ParticleModuleUVE col =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ColorOverLife);
        col.colors["Color"] = GradientColorUVE({
            {0.0F, {1.0F, 0.9F, 0.55F}, 0.0F},
            {0.15F, {1.0F, 0.65F, 0.2F}, 0.95F},
            {0.65F, {0.9F, 0.22F, 0.05F}, 0.7F},
            {1.0F, {0.35F, 0.04F, 0.01F}, 0.0F},
        });
        flames.particleUpdate.push_back(std::move(col));

        ParticleModuleUVE sizeCurve =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SizeOverLife);
        sizeCurve.floats["Scale"] = CurveFloatUVE({{0.0F, 0.6F}, {0.35F, 1.15F}, {1.0F, 0.2F}});
        flames.particleUpdate.push_back(std::move(sizeCurve));

        ParticleModuleUVE renderer =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer);
        renderer.ints["Facing"] = 1;
        renderer.ints["Blend"] = 0;
        renderer.floats["Emissive"] = ConstFloatUVE(2.2F);
        renderer.floats["SoftDistance"] = ConstFloatUVE(0.3F);
        flames.render.push_back(std::move(renderer));

        system.emitters.push_back(std::move(flames));
    }

    // Emitter 2: Rising Embers
    {
        ParticleEmitterAssetUVE embers;
        embers.name = "Embers";
        embers.maxParticles = 400U;
        std::uint32_t nextId = 1U;

        ParticleModuleUVE rate = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
        rate.floats["Rate"] = ConstFloatUVE(30.0F);
        embers.emitterUpdate.push_back(std::move(rate));

        ParticleModuleUVE shape = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapeSphere);
        shape.floats["Radius"] = ConstFloatUVE(0.25F);
        embers.particleSpawn.push_back(std::move(shape));

        ParticleModuleUVE vel =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::VelocityInCone);
        vel.vectors["Axis"] = ConstVecUVE({0.0F, 1.0F, 0.0F});
        vel.floats["Angle"] = ConstFloatUVE(30.0F);
        vel.floats["Speed"] = RandomFloatUVE(1.8F, 3.6F);
        embers.particleSpawn.push_back(std::move(vel));

        ParticleModuleUVE life =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
        life.floats["Lifetime"] = RandomFloatUVE(0.9F, 1.8F);
        embers.particleSpawn.push_back(std::move(life));

        ParticleModuleUVE size = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSize);
        size.floats["Size"] = RandomFloatUVE(0.03F, 0.06F);
        embers.particleSpawn.push_back(std::move(size));

        ParticleModuleUVE buoyancy = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Gravity);
        buoyancy.vectors["Acceleration"] = ConstVecUVE({0.0F, 0.6F, 0.0F});
        embers.particleUpdate.push_back(std::move(buoyancy));

        ParticleModuleUVE drag = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Drag);
        drag.floats["Coefficient"] = ConstFloatUVE(0.5F);
        embers.particleUpdate.push_back(std::move(drag));

        ParticleModuleUVE col =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ColorOverLife);
        col.colors["Color"] = GradientColorUVE({
            {0.0F, {1.0F, 0.7F, 0.25F}, 1.0F},
            {1.0F, {0.9F, 0.2F, 0.05F}, 0.0F},
        });
        embers.particleUpdate.push_back(std::move(col));

        ParticleModuleUVE renderer =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer);
        renderer.ints["Facing"] = 1;
        renderer.ints["Blend"] = 0;
        renderer.floats["Emissive"] = ConstFloatUVE(3.5F);
        embers.render.push_back(std::move(renderer));

        system.emitters.push_back(std::move(embers));
    }

    return system;
}

[[nodiscard]] ParticleSystemAssetUVE BuildSmokeTemplateUVE() {
    ParticleSystemAssetUVE system;
    system.duration = 4.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-3.0F, -0.5F, -3.0F}, {3.0F, 7.0F, 3.0F}};

    ParticleEmitterAssetUVE emitter;
    emitter.name = "Smoke";
    emitter.maxParticles = 600U;

    std::uint32_t nextId = 1U;
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpawnRate);
        mod.floats["Rate"] = ConstFloatUVE(28.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ShapeCone);
        mod.floats["Angle"] = ConstFloatUVE(15.0F);
        mod.floats["Length"] = ConstFloatUVE(0.3F);
        mod.floats["Radius"] = ConstFloatUVE(0.25F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::VelocityInCone);
        mod.vectors["Axis"] = ConstVecUVE({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloatUVE(18.0F);
        mod.floats["Speed"] = RandomFloatUVE(1.0F, 1.8F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloatUVE(2.0F, 3.2F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSize);
        mod.floats["Size"] = RandomFloatUVE(0.45F, 0.75F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialRotation);
        mod.floats["Rotation"] = RandomFloatUVE(0.0F, 360.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::InitialSpin);
        mod.floats["Spin"] = RandomFloatUVE(-25.0F, 25.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Gravity);
        mod.vectors["Acceleration"] = ConstVecUVE({0.0F, 0.4F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::Drag);
        mod.floats["Coefficient"] = ConstFloatUVE(0.7F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::ColorOverLife);
        mod.colors["Color"] = GradientColorUVE({
            {0.0F, {0.45F, 0.45F, 0.48F}, 0.0F},
            {0.15F, {0.4F, 0.4F, 0.44F}, 0.55F},
            {0.7F, {0.28F, 0.28F, 0.3F}, 0.35F},
            {1.0F, {0.2F, 0.2F, 0.22F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod = Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SizeOverLife);
        mod.floats["Scale"] = CurveFloatUVE({{0.0F, 0.6F}, {0.5F, 1.4F}, {1.0F, 2.4F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModuleUVE mod =
            Asset::MakeDefaultParticleModuleUVE(nextId++, ParticleModuleTypeUVE::SpriteRenderer);
        mod.ints["Facing"] = 1;
        mod.ints["Blend"] = 1;
        mod.floats["SoftDistance"] = ConstFloatUVE(0.4F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

} // namespace

const std::vector<ParticleTemplateInfoUVE>& GetParticleTemplatesUVE() noexcept {
    static const std::vector<ParticleTemplateInfoUVE> templates{
        {ParticleTemplateKindUVE::Empty, "Empty", "Minimal single-emitter particle system with sprite output."},
        {ParticleTemplateKindUVE::Fountain, "Fountain", "Arcing fountain stream with gravity and alpha fade."},
        {ParticleTemplateKindUVE::Sparks, "Sparks", "Velocity-aligned glowing spark bursts and spray."},
        {ParticleTemplateKindUVE::Fire, "Fire", "Dual-emitter campfire flames and rising embers."},
        {ParticleTemplateKindUVE::Smoke, "Smoke", "Rising expanding soft-faded smoke plume."},
    };
    return templates;
}

ParticleSystemAssetUVE CreateParticleTemplateUVE(const ParticleTemplateKindUVE kind) {
    switch (kind) {
        case ParticleTemplateKindUVE::Empty: return BuildEmptyTemplateUVE();
        case ParticleTemplateKindUVE::Fountain: return BuildFountainTemplateUVE();
        case ParticleTemplateKindUVE::Sparks: return BuildSparksTemplateUVE();
        case ParticleTemplateKindUVE::Fire: return BuildFireTemplateUVE();
        case ParticleTemplateKindUVE::Smoke: return BuildSmokeTemplateUVE();
    }
    return BuildEmptyTemplateUVE();
}

} // namespace UVE::VFX
