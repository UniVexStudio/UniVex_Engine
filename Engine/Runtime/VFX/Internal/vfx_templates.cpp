// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_templates.h"

#include <utility>
#include <vector>

namespace UVE::VFX {
namespace {

using Asset::ParticleColorInput;
using Asset::ParticleEmitterAsset;
using Asset::ParticleFloatInput;
using Asset::ParticleGradientStop;
using Asset::ParticleModuleType;
using Asset::ParticleModule;
using Asset::ParticleSystemAsset;
using Asset::ParticleValueKind;
using Asset::ParticleVectorInput;

[[nodiscard]] ParticleFloatInput ConstFloat(const float value) noexcept {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleFloatInput RandomFloat(const float minVal, const float maxVal) noexcept {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::RandomRange;
    input.min = minVal;
    input.max = maxVal;
    return input;
}

[[nodiscard]] ParticleFloatInput CurveFloat(std::initializer_list<std::pair<float, float>> points,
                                                  const bool smooth = true) {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Curve;
    input.scale = 1.0F;
    input.curve.smooth = smooth;
    input.curve.keys.reserve(points.size());
    for (const auto& [t, v] : points) {
        input.curve.keys.push_back(Asset::ParticleCurveKey{t, v, 0.0F, 0.0F});
    }
    return input;
}

[[nodiscard]] ParticleVectorInput ConstVec(const Math::Vector3& v) noexcept {
    ParticleVectorInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = v;
    return input;
}

[[nodiscard]] ParticleColorInput ConstColor(const Math::Color& c, const float alpha = 1.0F) noexcept {
    ParticleColorInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = c;
    input.alpha = alpha;
    return input;
}

[[nodiscard]] ParticleColorInput GradientColor(
    std::initializer_list<ParticleGradientStop> stops) {
    ParticleColorInput input;
    input.kind = ParticleValueKind::Gradient;
    input.gradient.stops.assign(stops.begin(), stops.end());
    return input;
}

[[nodiscard]] ParticleSystemAsset BuildEmptyTemplate() {
    ParticleSystemAsset system;
    system.duration = 2.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};

    ParticleEmitterAsset emitter;
    emitter.name = "Emitter";
    emitter.maxParticles = 1000U;

    std::uint32_t nextId = 1U;
    ParticleModule rate = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
    rate.floats["Rate"] = ConstFloat(10.0F);
    emitter.emitterUpdate.push_back(std::move(rate));

    emitter.particleSpawn.push_back(
        Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapePoint));
    ParticleModule life =
        Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(1.0F);
    emitter.particleSpawn.push_back(std::move(life));

    emitter.render.push_back(
        Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer));

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAsset BuildFountainTemplate() {
    ParticleSystemAsset system;
    system.duration = 4.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-4.0F, -1.0F, -4.0F}, {4.0F, 7.0F, 4.0F}};

    ParticleEmitterAsset emitter;
    emitter.name = "WaterJets";
    emitter.maxParticles = 2000U;

    std::uint32_t nextId = 1U;
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
        mod.floats["Rate"] = ConstFloat(120.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapeSphere);
        mod.floats["Radius"] = ConstFloat(0.08F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::VelocityInCone);
        mod.vectors["Axis"] = ConstVec({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloat(12.0F);
        mod.floats["Speed"] = RandomFloat(6.0F, 8.5F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloat(1.4F, 1.9F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSize);
        mod.floats["Size"] = RandomFloat(0.06F, 0.12F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialColor);
        mod.colors["Color"] = ConstColor({0.75F, 0.92F, 1.0F}, 0.85F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Gravity);
        mod.vectors["Acceleration"] = ConstVec({0.0F, -9.8F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Drag);
        mod.floats["Coefficient"] = ConstFloat(0.15F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ColorOverLife);
        mod.colors["Color"] = GradientColor({
            {0.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
            {0.7F, {0.8F, 0.95F, 1.0F}, 0.7F},
            {1.0F, {0.9F, 0.98F, 1.0F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SizeOverLife);
        mod.floats["Scale"] = CurveFloat({{0.0F, 0.8F}, {0.5F, 1.0F}, {1.0F, 0.4F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::CollisionPhysics);
        mod.floats["Bounce"] = ConstFloat(0.55F);
        mod.floats["Friction"] = ConstFloat(0.18F);
        mod.floats["LifeLoss"] = ConstFloat(0.05F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer);
        mod.ints["Facing"] = 1;
        mod.ints["Blend"] = 1;
        mod.floats["SoftDistance"] = ConstFloat(0.15F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAsset BuildSparksTemplate() {
    ParticleSystemAsset system;
    system.duration = 2.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-5.0F, -2.0F, -5.0F}, {5.0F, 5.0F, 5.0F}};

    ParticleEmitterAsset emitter;
    emitter.name = "Sparks";
    emitter.maxParticles = 2000U;

    std::uint32_t nextId = 1U;
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
        mod.floats["Rate"] = ConstFloat(180.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnBurst);
        mod.floats["Time"] = ConstFloat(0.0F);
        mod.ints["Count"] = 40;
        mod.ints["Cycles"] = 1;
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapeSphere);
        mod.floats["Radius"] = ConstFloat(0.1F);
        mod.ints["Surface"] = 1;
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::VelocityInCone);
        mod.vectors["Axis"] = ConstVec({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloat(55.0F);
        mod.floats["Speed"] = RandomFloat(4.0F, 9.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloat(0.4F, 0.95F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSize);
        mod.floats["Size"] = RandomFloat(0.04F, 0.08F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialColor);
        mod.colors["Color"] = ConstColor({1.0F, 0.65F, 0.25F}, 1.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Gravity);
        mod.vectors["Acceleration"] = ConstVec({0.0F, -9.8F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Drag);
        mod.floats["Coefficient"] = ConstFloat(0.8F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ColorOverLife);
        mod.colors["Color"] = GradientColor({
            {0.0F, {1.0F, 0.95F, 0.65F}, 1.0F},
            {0.5F, {1.0F, 0.45F, 0.1F}, 0.9F},
            {1.0F, {0.8F, 0.15F, 0.02F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SizeOverLife);
        mod.floats["Scale"] = CurveFloat({{0.0F, 1.0F}, {1.0F, 0.1F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::CollisionPhysics);
        mod.floats["Bounce"] = ConstFloat(0.65F);
        mod.floats["Friction"] = ConstFloat(0.12F);
        mod.floats["LifeLoss"] = ConstFloat(0.08F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer);
        mod.ints["Facing"] = 2;
        mod.floats["StretchScale"] = ConstFloat(0.25F);
        mod.ints["Blend"] = 0;
        mod.floats["Emissive"] = ConstFloat(3.0F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

[[nodiscard]] ParticleSystemAsset BuildFireTemplate() {
    ParticleSystemAsset system;
    system.duration = 3.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-2.0F, -0.5F, -2.0F}, {2.0F, 4.5F, 2.0F}};

    // Emitter 1: Flames
    {
        ParticleEmitterAsset flames;
        flames.name = "Flames";
        flames.maxParticles = 800U;
        std::uint32_t nextId = 1U;

        ParticleModule rate = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
        rate.floats["Rate"] = ConstFloat(65.0F);
        flames.emitterUpdate.push_back(std::move(rate));

        ParticleModule shape =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapeCylinder);
        shape.floats["Radius"] = ConstFloat(0.35F);
        shape.floats["Height"] = ConstFloat(0.15F);
        flames.particleSpawn.push_back(std::move(shape));

        ParticleModule vel =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::VelocityInCone);
        vel.vectors["Axis"] = ConstVec({0.0F, 1.0F, 0.0F});
        vel.floats["Angle"] = ConstFloat(14.0F);
        vel.floats["Speed"] = RandomFloat(1.2F, 2.4F);
        flames.particleSpawn.push_back(std::move(vel));

        ParticleModule life =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
        life.floats["Lifetime"] = RandomFloat(0.55F, 0.95F);
        flames.particleSpawn.push_back(std::move(life));

        ParticleModule size = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSize);
        size.floats["Size"] = RandomFloat(0.35F, 0.6F);
        flames.particleSpawn.push_back(std::move(size));

        ParticleModule rot =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialRotation);
        rot.floats["Rotation"] = RandomFloat(0.0F, 360.0F);
        flames.particleSpawn.push_back(std::move(rot));

        ParticleModule spin = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSpin);
        spin.floats["Spin"] = RandomFloat(-45.0F, 45.0F);
        flames.particleSpawn.push_back(std::move(spin));

        ParticleModule buoyancy = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Gravity);
        buoyancy.vectors["Acceleration"] = ConstVec({0.0F, 1.5F, 0.0F});
        flames.particleUpdate.push_back(std::move(buoyancy));

        ParticleModule drag = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Drag);
        drag.floats["Coefficient"] = ConstFloat(0.6F);
        flames.particleUpdate.push_back(std::move(drag));

        ParticleModule col =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ColorOverLife);
        col.colors["Color"] = GradientColor({
            {0.0F, {1.0F, 0.9F, 0.55F}, 0.0F},
            {0.15F, {1.0F, 0.65F, 0.2F}, 0.95F},
            {0.65F, {0.9F, 0.22F, 0.05F}, 0.7F},
            {1.0F, {0.35F, 0.04F, 0.01F}, 0.0F},
        });
        flames.particleUpdate.push_back(std::move(col));

        ParticleModule sizeCurve =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SizeOverLife);
        sizeCurve.floats["Scale"] = CurveFloat({{0.0F, 0.6F}, {0.35F, 1.15F}, {1.0F, 0.2F}});
        flames.particleUpdate.push_back(std::move(sizeCurve));

        ParticleModule renderer =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer);
        renderer.ints["Facing"] = 1;
        renderer.ints["Blend"] = 0;
        renderer.floats["Emissive"] = ConstFloat(2.2F);
        renderer.floats["SoftDistance"] = ConstFloat(0.3F);
        flames.render.push_back(std::move(renderer));

        system.emitters.push_back(std::move(flames));
    }

    // Emitter 2: Rising Embers
    {
        ParticleEmitterAsset embers;
        embers.name = "Embers";
        embers.maxParticles = 400U;
        std::uint32_t nextId = 1U;

        ParticleModule rate = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
        rate.floats["Rate"] = ConstFloat(30.0F);
        embers.emitterUpdate.push_back(std::move(rate));

        ParticleModule shape = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapeSphere);
        shape.floats["Radius"] = ConstFloat(0.25F);
        embers.particleSpawn.push_back(std::move(shape));

        ParticleModule vel =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::VelocityInCone);
        vel.vectors["Axis"] = ConstVec({0.0F, 1.0F, 0.0F});
        vel.floats["Angle"] = ConstFloat(30.0F);
        vel.floats["Speed"] = RandomFloat(1.8F, 3.6F);
        embers.particleSpawn.push_back(std::move(vel));

        ParticleModule life =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
        life.floats["Lifetime"] = RandomFloat(0.9F, 1.8F);
        embers.particleSpawn.push_back(std::move(life));

        ParticleModule size = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSize);
        size.floats["Size"] = RandomFloat(0.03F, 0.06F);
        embers.particleSpawn.push_back(std::move(size));

        ParticleModule buoyancy = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Gravity);
        buoyancy.vectors["Acceleration"] = ConstVec({0.0F, 0.6F, 0.0F});
        embers.particleUpdate.push_back(std::move(buoyancy));

        ParticleModule drag = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Drag);
        drag.floats["Coefficient"] = ConstFloat(0.5F);
        embers.particleUpdate.push_back(std::move(drag));

        ParticleModule col =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ColorOverLife);
        col.colors["Color"] = GradientColor({
            {0.0F, {1.0F, 0.7F, 0.25F}, 1.0F},
            {1.0F, {0.9F, 0.2F, 0.05F}, 0.0F},
        });
        embers.particleUpdate.push_back(std::move(col));

        ParticleModule renderer =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer);
        renderer.ints["Facing"] = 1;
        renderer.ints["Blend"] = 0;
        renderer.floats["Emissive"] = ConstFloat(3.5F);
        embers.render.push_back(std::move(renderer));

        system.emitters.push_back(std::move(embers));
    }

    return system;
}

[[nodiscard]] ParticleSystemAsset BuildSmokeTemplate() {
    ParticleSystemAsset system;
    system.duration = 4.0F;
    system.looping = true;
    system.warmUpSeconds = 0.0F;
    system.fixedBounds = {{-3.0F, -0.5F, -3.0F}, {3.0F, 7.0F, 3.0F}};

    ParticleEmitterAsset emitter;
    emitter.name = "Smoke";
    emitter.maxParticles = 600U;

    std::uint32_t nextId = 1U;
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpawnRate);
        mod.floats["Rate"] = ConstFloat(28.0F);
        emitter.emitterUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ShapeCone);
        mod.floats["Angle"] = ConstFloat(15.0F);
        mod.floats["Length"] = ConstFloat(0.3F);
        mod.floats["Radius"] = ConstFloat(0.25F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::VelocityInCone);
        mod.vectors["Axis"] = ConstVec({0.0F, 1.0F, 0.0F});
        mod.floats["Angle"] = ConstFloat(18.0F);
        mod.floats["Speed"] = RandomFloat(1.0F, 1.8F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialLifetime);
        mod.floats["Lifetime"] = RandomFloat(2.0F, 3.2F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSize);
        mod.floats["Size"] = RandomFloat(0.45F, 0.75F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialRotation);
        mod.floats["Rotation"] = RandomFloat(0.0F, 360.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::InitialSpin);
        mod.floats["Spin"] = RandomFloat(-25.0F, 25.0F);
        emitter.particleSpawn.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Gravity);
        mod.vectors["Acceleration"] = ConstVec({0.0F, 0.4F, 0.0F});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::Drag);
        mod.floats["Coefficient"] = ConstFloat(0.7F);
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::ColorOverLife);
        mod.colors["Color"] = GradientColor({
            {0.0F, {0.45F, 0.45F, 0.48F}, 0.0F},
            {0.15F, {0.4F, 0.4F, 0.44F}, 0.55F},
            {0.7F, {0.28F, 0.28F, 0.3F}, 0.35F},
            {1.0F, {0.2F, 0.2F, 0.22F}, 0.0F},
        });
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod = Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SizeOverLife);
        mod.floats["Scale"] = CurveFloat({{0.0F, 0.6F}, {0.5F, 1.4F}, {1.0F, 2.4F}});
        emitter.particleUpdate.push_back(std::move(mod));
    }
    {
        ParticleModule mod =
            Asset::MakeDefaultParticleModule(nextId++, ParticleModuleType::SpriteRenderer);
        mod.ints["Facing"] = 1;
        mod.ints["Blend"] = 1;
        mod.floats["SoftDistance"] = ConstFloat(0.4F);
        emitter.render.push_back(std::move(mod));
    }

    system.emitters.push_back(std::move(emitter));
    return system;
}

} // namespace

const std::vector<ParticleTemplateInfo>& GetParticleTemplates() noexcept {
    static const std::vector<ParticleTemplateInfo> templates{
        {ParticleTemplateKind::Empty, "Empty", "Minimal single-emitter particle system with sprite output."},
        {ParticleTemplateKind::Fountain, "Fountain", "Arcing fountain stream with gravity and alpha fade."},
        {ParticleTemplateKind::Sparks, "Sparks", "Velocity-aligned glowing spark bursts and spray."},
        {ParticleTemplateKind::Fire, "Fire", "Dual-emitter campfire flames and rising embers."},
        {ParticleTemplateKind::Smoke, "Smoke", "Rising expanding soft-faded smoke plume."},
    };
    return templates;
}

ParticleSystemAsset CreateParticleTemplate(const ParticleTemplateKind kind) {
    switch (kind) {
        case ParticleTemplateKind::Empty: return BuildEmptyTemplate();
        case ParticleTemplateKind::Fountain: return BuildFountainTemplate();
        case ParticleTemplateKind::Sparks: return BuildSparksTemplate();
        case ParticleTemplateKind::Fire: return BuildFireTemplate();
        case ParticleTemplateKind::Smoke: return BuildSmokeTemplate();
    }
    return BuildEmptyTemplate();
}

} // namespace UVE::VFX
