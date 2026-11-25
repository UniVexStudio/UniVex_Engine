// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_program.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace UVE::VFX {
namespace {

using Asset::ParticleColorInput;
using Asset::ParticleFloatInput;
using Asset::ParticleModuleDescriptor;
using Asset::ParticleModuleType;
using Asset::ParticleModule;
using Asset::ParticleSimTarget;
using Asset::ParticleStage;
using Asset::ParticleSystemAsset;
using Asset::ParticleUserParameterType;
using Asset::ParticleUserParameter;
using Asset::ParticleValueKind;
using Asset::ParticleVectorInput;

constexpr std::size_t kMaximumShapeModulesWarningLimit = 1U;

void PushError(std::vector<ParticleCompileMessage>& messages, const std::uint32_t moduleId, std::string text) {
    messages.push_back(ParticleCompileMessage{
        ParticleCompileMessage::Severity::Error,
        moduleId,
        std::move(text),
    });
}

void PushWarning(std::vector<ParticleCompileMessage>& messages, const std::uint32_t moduleId, std::string text) {
    messages.push_back(ParticleCompileMessage{
        ParticleCompileMessage::Severity::Warning,
        moduleId,
        std::move(text),
    });
}

[[nodiscard]] std::string LowercaseExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

[[nodiscard]] bool DoesAssetExist(const std::string_view assetPath, const VfxCompileContext& context) {
    if (assetPath.empty()) {
        return false;
    }
    const std::filesystem::path rel(assetPath);
    std::error_code ec;
    if (rel.is_absolute()) {
        return std::filesystem::is_regular_file(rel, ec) && !ec;
    }

    auto checkCandidate = [](const std::filesystem::path& candidate) {
        std::error_code localEc;
        return std::filesystem::is_regular_file(candidate, localEc) && !localEc;
    };

    if (!context.projectRoot.empty()) {
        if (checkCandidate(context.projectRoot / rel) ||
            checkCandidate(context.projectRoot / "content" / rel) ||
            checkCandidate(context.projectRoot / "assets" / rel)) {
            return true;
        }
    }

    return checkCandidate(rel) || checkCandidate(std::filesystem::path("content") / rel) ||
           checkCandidate(std::filesystem::path("assets") / rel);
}

[[nodiscard]] bool IsShapeModule(const ParticleModuleType type) noexcept {
    switch (type) {
        case ParticleModuleType::ShapePoint:
        case ParticleModuleType::ShapeSphere:
        case ParticleModuleType::ShapeBox:
        case ParticleModuleType::ShapeCone:
        case ParticleModuleType::ShapeCylinder:
        case ParticleModuleType::ShapeTorus:
        case ParticleModuleType::ShapeMeshSurface:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool IsRendererModule(const ParticleModuleType type) noexcept {
    switch (type) {
        case ParticleModuleType::SpriteRenderer:
        case ParticleModuleType::RibbonRenderer:
        case ParticleModuleType::MeshRenderer:
        case ParticleModuleType::LightRenderer:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool ValidateMathGraphCycles(const std::vector<Asset::ParticleMathNode>& mathNodes,
                                              const std::unordered_map<std::uint32_t, std::size_t>& idToIndex,
                                              std::vector<ParticleCompileMessage>& messages) {
    // 0 = unvisited, 1 = visiting, 2 = done
    std::vector<std::uint8_t> state(mathNodes.size(), 0U);
    bool hasCycle = false;

    const auto visit = [&](auto& self, const std::size_t nodeIdx) -> void {
        state[nodeIdx] = 1U;
        for (const std::uint32_t inputId : mathNodes[nodeIdx].inputs) {
            const auto it = idToIndex.find(inputId);
            if (it == idToIndex.end()) {
                PushError(messages, mathNodes[nodeIdx].id, "Math node references a missing input node.");
                continue;
            }
            const std::size_t depIdx = it->second;
            if (state[depIdx] == 1U) {
                hasCycle = true;
                PushError(messages, mathNodes[nodeIdx].id, "Cycle detected in Math node graph.");
            } else if (state[depIdx] == 0U) {
                self(self, depIdx);
            }
        }
        state[nodeIdx] = 2U;
    };

    for (std::size_t i = 0U; i < mathNodes.size(); ++i) {
        if (state[i] == 0U) {
            visit(visit, i);
        }
    }
    return !hasCycle;
}

[[nodiscard]] ParticleFloatInput ResolveFloatInput(const ParticleModule& mod,
                                                         const ParticleModuleDescriptor& desc,
                                                         const std::string& name) {
    const auto it = mod.floats.find(name);
    if (it != mod.floats.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultFloats.find(name);
    if (defIt != desc.defaultFloats.end()) {
        return defIt->second;
    }
    return ParticleFloatInput{};
}

[[nodiscard]] ParticleVectorInput ResolveVectorInput(const ParticleModule& mod,
                                                           const ParticleModuleDescriptor& desc,
                                                           const std::string& name) {
    const auto it = mod.vectors.find(name);
    if (it != mod.vectors.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultVectors.find(name);
    if (defIt != desc.defaultVectors.end()) {
        return defIt->second;
    }
    return ParticleVectorInput{};
}

[[nodiscard]] ParticleColorInput ResolveColorInput(const ParticleModule& mod,
                                                         const ParticleModuleDescriptor& desc,
                                                         const std::string& name) {
    const auto it = mod.colors.find(name);
    if (it != mod.colors.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultColors.find(name);
    if (defIt != desc.defaultColors.end()) {
        return defIt->second;
    }
    return ParticleColorInput{};
}

[[nodiscard]] std::int32_t ResolveIntInput(const ParticleModule& mod,
                                              const ParticleModuleDescriptor& desc,
                                              const std::string& name) {
    const auto it = mod.ints.find(name);
    if (it != mod.ints.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultInts.find(name);
    if (defIt != desc.defaultInts.end()) {
        return defIt->second;
    }
    return 0;
}

[[nodiscard]] std::string ResolveStringInput(const ParticleModule& mod,
                                              const ParticleModuleDescriptor& desc,
                                              const std::string& name) {
    const auto it = mod.strings.find(name);
    if (it != mod.strings.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultStrings.find(name);
    if (defIt != desc.defaultStrings.end()) {
        return defIt->second;
    }
    return {};
}

struct CompileTables final {
    const std::unordered_map<std::string_view, std::pair<std::uint32_t, ParticleUserParameterType>>& paramMap;
    const std::unordered_map<std::uint32_t, std::size_t>& mathIdMap;
    std::vector<VfxCurveTable>& curveTables;
    std::vector<VfxGradientTable>& gradientTables;
    std::vector<ParticleCompileMessage>& messages;
    bool hasError = false;

    void Fail(const std::uint32_t moduleId, std::string text) {
        hasError = true;
        PushError(messages, moduleId, std::move(text));
    }

    [[nodiscard]] VfxCompiledFloatInput CompileFloat(const std::uint32_t moduleId,
                                                        const ParticleFloatInput& input) {
        VfxCompiledFloatInput out;
        out.kind = input.kind;
        out.constant = input.constant;
        out.min = input.min;
        out.max = input.max;
        out.scale = input.scale;
        out.wireNode = input.wireNode;

        if (!std::isfinite(input.constant) || !std::isfinite(input.min) || !std::isfinite(input.max) ||
            !std::isfinite(input.scale)) {
            Fail(moduleId, "Float input contains non-finite numeric values.");
            return out;
        }
        switch (input.kind) {
            case ParticleValueKind::Constant:
                break;
            case ParticleValueKind::RandomRange:
                if (input.min > input.max) {
                    Fail(moduleId, "Random range input requires minimum <= maximum.");
                }
                break;
            case ParticleValueKind::Curve: {
                if (input.curve.keys.size() > Asset::kMaximumCurveKeys) {
                    Fail(moduleId, "Curve exceeds the maximum supported key count.");
                    break;
                }
                float prevT = -1.0F;
                for (const Asset::ParticleCurveKey& key : input.curve.keys) {
                    if (!std::isfinite(key.time) || !std::isfinite(key.value) ||
                        !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent) ||
                        key.time < 0.0F || key.time > 1.0F || key.time < prevT) {
                        Fail(moduleId, "Curve keys must be finite, ordered, and within [0, 1].");
                        return out;
                    }
                    prevT = key.time;
                }
                out.curveTableIndex = static_cast<std::uint32_t>(curveTables.size());
                curveTables.push_back(BakeVfxCurveTable(input.curve, input.scale));
                break;
            }
            case ParticleValueKind::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced float user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterType::Float) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Float.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKind::Wire:
                if (!mathIdMap.contains(input.wireNode)) {
                    Fail(moduleId, "Wire input names a missing Math node.");
                }
                break;
            case ParticleValueKind::Gradient:
                Fail(moduleId, "Float input cannot evaluate a color gradient.");
                break;
        }
        return out;
    }

    [[nodiscard]] VfxCompiledVectorInput CompileVector(const std::uint32_t moduleId,
                                                          const ParticleVectorInput& input) {
        VfxCompiledVectorInput out;
        out.kind = input.kind;
        out.constant = input.constant;
        out.min = input.min;
        out.max = input.max;
        out.wireNode = input.wireNode;

        if (!Math::IsFinite(input.constant) || !Math::IsFinite(input.min) || !Math::IsFinite(input.max)) {
            Fail(moduleId, "Vector input contains non-finite components.");
            return out;
        }
        switch (input.kind) {
            case ParticleValueKind::Constant:
                break;
            case ParticleValueKind::RandomRange:
                if (input.min.x > input.max.x || input.min.y > input.max.y || input.min.z > input.max.z) {
                    Fail(moduleId, "Vector random range requires minimum <= maximum on each axis.");
                }
                break;
            case ParticleValueKind::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced vector user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterType::Vector) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Vector.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKind::Wire:
                if (!mathIdMap.contains(input.wireNode)) {
                    Fail(moduleId, "Wire input names a missing Math node.");
                }
                break;
            case ParticleValueKind::Curve:
            case ParticleValueKind::Gradient:
                Fail(moduleId, "Vector input kind is not supported.");
                break;
        }
        return out;
    }

    [[nodiscard]] VfxCompiledColorInput CompileColor(const std::uint32_t moduleId,
                                                        const ParticleColorInput& input) {
        VfxCompiledColorInput out;
        out.kind = input.kind;
        out.constant = input.constant;
        out.alpha = input.alpha;
        out.minColor = input.minColor;
        out.maxColor = input.maxColor;

        if (!std::isfinite(input.constant.r) || !std::isfinite(input.constant.g) || !std::isfinite(input.constant.b) ||
            !std::isfinite(input.alpha) || !std::isfinite(input.minColor.r) || !std::isfinite(input.minColor.g) ||
            !std::isfinite(input.minColor.b) || !std::isfinite(input.maxColor.r) || !std::isfinite(input.maxColor.g) ||
            !std::isfinite(input.maxColor.b)) {
            Fail(moduleId, "Color input contains non-finite values.");
            return out;
        }
        switch (input.kind) {
            case ParticleValueKind::Constant:
            case ParticleValueKind::RandomRange:
                break;
            case ParticleValueKind::Gradient: {
                if (input.gradient.stops.size() > Asset::kMaximumGradientStops) {
                    Fail(moduleId, "Gradient exceeds maximum stop count.");
                    break;
                }
                float prevT = -1.0F;
                for (const Asset::ParticleGradientStop& stop : input.gradient.stops) {
                    if (!std::isfinite(stop.time) || !std::isfinite(stop.color.r) ||
                        !std::isfinite(stop.color.g) || !std::isfinite(stop.color.b) ||
                        !std::isfinite(stop.alpha) || stop.time < 0.0F || stop.time > 1.0F || stop.time < prevT) {
                        Fail(moduleId, "Gradient stops must be finite, ordered, and within [0, 1].");
                        return out;
                    }
                    prevT = stop.time;
                }
                out.gradientTableIndex = static_cast<std::uint32_t>(gradientTables.size());
                gradientTables.push_back(BakeVfxGradientTable(input.gradient));
                break;
            }
            case ParticleValueKind::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced color user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterType::Color) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Color.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKind::Curve:
            case ParticleValueKind::Wire:
                Fail(moduleId, "Color input kind is not supported.");
                break;
        }
        return out;
    }
};

[[nodiscard]] bool ValidateModuleKeysAndStage(const ParticleModule& mod,
                                                 const ParticleStage expectedStage,
                                                 std::vector<ParticleCompileMessage>& messages,
                                                 const ParticleModuleDescriptor*& outDesc) {
    outDesc = Asset::FindParticleModuleDescriptor(mod.type);
    if (outDesc == nullptr) {
        PushError(messages, mod.id, "Unknown particle module type.");
        return false;
    }
    if (outDesc->stage != expectedStage) {
        PushError(messages, mod.id,
                     std::string("Module \"") + std::string(outDesc->canonicalName) +
                         "\" is placed in the wrong pipeline stage.");
        return false;
    }
    bool ok = true;
    for (const auto& [name, val] : mod.floats) {
        static_cast<void>(val);
        if (!outDesc->defaultFloats.contains(name)) {
            PushError(messages, mod.id, "Unknown float input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.vectors) {
        static_cast<void>(val);
        if (!outDesc->defaultVectors.contains(name)) {
            PushError(messages, mod.id, "Unknown vector input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.colors) {
        static_cast<void>(val);
        if (!outDesc->defaultColors.contains(name)) {
            PushError(messages, mod.id, "Unknown color input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.ints) {
        static_cast<void>(val);
        if (!outDesc->defaultInts.contains(name)) {
            PushError(messages, mod.id, "Unknown integer input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.strings) {
        static_cast<void>(val);
        if (!outDesc->defaultStrings.contains(name)) {
            PushError(messages, mod.id, "Unknown string input \"" + name + "\".");
            ok = false;
        }
    }
    return ok;
}

[[nodiscard]] bool HasNonZeroFloatPossibility(const ParticleFloatInput& input) noexcept {
    if (input.kind == ParticleValueKind::Constant) {
        return input.constant != 0.0F;
    }
    return true;
}

} // namespace

bool CompileParticleEmitter(const Asset::ParticleSystemAsset& system,
                               const std::size_t emitterIndex,
                               ParticleProgram& outProgram,
                               std::vector<ParticleCompileMessage>& messages) {
    return CompileParticleEmitter(system, emitterIndex, VfxCompileContext{}, outProgram, messages);
}

bool CompileParticleEmitter(const Asset::ParticleSystemAsset& system,
                               const std::size_t emitterIndex,
                               const VfxCompileContext& context,
                               ParticleProgram& outProgram,
                               std::vector<ParticleCompileMessage>& messages) {
    messages.clear();

    if (emitterIndex >= system.emitters.size()) {
        PushError(messages, 0U, "Emitter index is out of range.");
        return false;
    }

    const Asset::ParticleEmitterAsset& emitter = system.emitters[emitterIndex];
    bool hasError = false;

    if (!std::isfinite(system.duration) || system.duration <= 0.0F) {
        PushError(messages, 0U, "System duration must be finite and greater than zero.");
        hasError = true;
    }
    if (!std::isfinite(system.warmUpSeconds) || system.warmUpSeconds < 0.0F) {
        PushError(messages, 0U, "System warm-up duration must be non-negative.");
        hasError = true;
    }
    if (emitter.maxParticles == 0U || emitter.maxParticles > Asset::kMaximumParticlesPerEmitter) {
        PushError(messages, 0U, "Emitter maxParticles must be between 1 and 1,000,000.");
        hasError = true;
    }

    if (emitter.target != ParticleSimTarget::Gpu && emitter.maxParticles > 100'000U) {
        PushWarning(messages, 0U, "Emitter maxParticles exceeds 100,000 on CPU target.");
    }

    // Build user-parameter lookup map.
    std::unordered_map<std::string_view, std::pair<std::uint32_t, ParticleUserParameterType>> paramMap;
    paramMap.reserve(system.parameters.size());
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(system.parameters.size()); ++idx) {
        const ParticleUserParameter& param = system.parameters[idx];
        if (!paramMap.emplace(param.name, std::make_pair(idx, param.type)).second) {
            PushError(messages, 0U, "Duplicate user parameter name \"" + param.name + "\".");
            hasError = true;
        }
    }

    // Build Math node lookup and check for cycles.
    std::unordered_map<std::uint32_t, std::size_t> mathIdMap;
    mathIdMap.reserve(emitter.mathNodes.size());
    for (std::size_t idx = 0U; idx < emitter.mathNodes.size(); ++idx) {
        const auto& math = emitter.mathNodes[idx];
        if (math.id == 0U || !mathIdMap.emplace(math.id, idx).second) {
            PushError(messages, math.id, "Math nodes must have unique non-zero IDs.");
            hasError = true;
        }
    }
    if (!emitter.mathNodes.empty() && !ValidateMathGraphCycles(emitter.mathNodes, mathIdMap, messages)) {
        hasError = true;
    }

    // Build emitter name lookup for event targets.
    std::unordered_map<std::string_view, std::uint32_t> emitterNameMap;
    emitterNameMap.reserve(system.emitters.size());
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(system.emitters.size()); ++idx) {
        emitterNameMap.emplace(system.emitters[idx].name, idx);
    }

    ParticleProgram candidate;
    candidate.emitterName = emitter.name;
    candidate.enabled = emitter.enabled;
    candidate.space = emitter.space;
    candidate.target = emitter.target;
    candidate.maxParticles = emitter.maxParticles;
    candidate.seed = emitter.seed;

    // Base identity & aging attributes are required by every live emitter.
    VfxAttributeMask mask = ToMask(VfxAttributeBit::Id) | ToMask(VfxAttributeBit::Seed) |
                               ToMask(VfxAttributeBit::Age) | ToMask(VfxAttributeBit::Lifetime);

    CompileTables tables{
        .paramMap = paramMap,
        .mathIdMap = mathIdMap,
        .curveTables = candidate.curveTables,
        .gradientTables = candidate.gradientTables,
        .messages = messages,
    };

    // ---- Stage 1: Emitter Update ----
    for (const ParticleModule& mod : emitter.emitterUpdate) {
        const ParticleModuleDescriptor* desc = nullptr;
        if (!ValidateModuleKeysAndStage(mod, ParticleStage::EmitterUpdate, messages, desc)) {
            hasError = true;
            continue;
        }
        if (!mod.enabled) {
            continue;
        }
        VfxEmitterUpdateOp op;
        op.type = mod.type;
        op.moduleId = mod.id;
        switch (mod.type) {
            case ParticleModuleType::SpawnRate:
                op.rate = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Rate"));
                break;
            case ParticleModuleType::SpawnBurst:
                op.time = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Time"));
                op.interval = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Interval"));
                op.count = ResolveIntInput(mod, *desc, "Count");
                op.cycles = ResolveIntInput(mod, *desc, "Cycles");
                break;
            case ParticleModuleType::SpawnPerDistance:
                op.perUnit = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "PerUnit"));
                break;
            default:
                break;
        }
        candidate.emitterUpdateOps.push_back(std::move(op));
    }

    // ---- Stage 2: Particle Spawn ----
    std::size_t lastEnabledShapeModuleIndex = std::numeric_limits<std::size_t>::max();
    std::size_t enabledShapeCount = 0U;
    for (std::size_t idx = 0U; idx < emitter.particleSpawn.size(); ++idx) {
        if (emitter.particleSpawn[idx].enabled && IsShapeModule(emitter.particleSpawn[idx].type)) {
            lastEnabledShapeModuleIndex = idx;
            ++enabledShapeCount;
        }
    }
    if (enabledShapeCount > kMaximumShapeModulesWarningLimit) {
        for (std::size_t idx = 0U; idx < emitter.particleSpawn.size(); ++idx) {
            const ParticleModule& mod = emitter.particleSpawn[idx];
            if (mod.enabled && IsShapeModule(mod.type) && idx != lastEnabledShapeModuleIndex) {
                PushWarning(messages, mod.id,
                               "Multiple shapes enabled in Particle Spawn; only the last shape is used.");
            }
        }
    }

    bool hasInitialColor = false;
    bool hasColorOverLife = false;
    bool hasSizeOverLifeOrBySpeed = false;
    bool hasOrbit = false;
    bool needsVelocity = false;
    bool needsPosition = false;
    bool needsPrevPosition = false;

    for (std::size_t idx = 0U; idx < emitter.particleSpawn.size(); ++idx) {
        const ParticleModule& mod = emitter.particleSpawn[idx];
        const ParticleModuleDescriptor* desc = nullptr;
        if (!ValidateModuleKeysAndStage(mod, ParticleStage::ParticleSpawn, messages, desc)) {
            hasError = true;
            continue;
        }

        // Validate mesh reference even if disabled or shadowed.
        if (mod.type == ParticleModuleType::ShapeMeshSurface) {
            const std::string meshPath = ResolveStringInput(mod, *desc, "Mesh");
            if (meshPath.empty()) {
                tables.Fail(mod.id, "Shape: Mesh Surface requires a mesh asset path.");
            } else if (LowercaseExtension(meshPath) != ".uvmodel") {
                tables.Fail(mod.id, "Shape: Mesh Surface mesh must be a .uvmodel asset.");
            } else if (!DoesAssetExist(meshPath, context)) {
                tables.Fail(mod.id, "Shape: Mesh Surface mesh asset file is missing: " + meshPath);
            }
        }

        if (!mod.enabled) {
            continue;
        }
        if (IsShapeModule(mod.type) && idx != lastEnabledShapeModuleIndex) {
            continue;
        }

        VfxParticleSpawnOp op;
        op.type = mod.type;
        op.moduleId = mod.id;

        switch (mod.type) {
            case ParticleModuleType::ShapePoint:
            case ParticleModuleType::ShapeSphere:
            case ParticleModuleType::ShapeBox:
            case ParticleModuleType::ShapeCone:
            case ParticleModuleType::ShapeCylinder:
            case ParticleModuleType::ShapeTorus:
            case ParticleModuleType::ShapeMeshSurface: {
                needsPosition = true;
                const ParticleFloatInput ns = ResolveFloatInput(mod, *desc, "NormalSpeed");
                op.normalSpeed = tables.CompileFloat(mod.id, ns);
                op.surface = ResolveIntInput(mod, *desc, "Surface");
                if (HasNonZeroFloatPossibility(ns)) {
                    needsVelocity = true;
                }
                if (mod.type == ParticleModuleType::ShapeSphere) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                    op.hemisphere = ResolveIntInput(mod, *desc, "Hemisphere");
                } else if (mod.type == ParticleModuleType::ShapeBox) {
                    op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Size"));
                } else if (mod.type == ParticleModuleType::ShapeCone) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Angle"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Length"));
                    op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                } else if (mod.type == ParticleModuleType::ShapeCylinder) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Height"));
                } else if (mod.type == ParticleModuleType::ShapeTorus) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MajorRadius"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MinorRadius"));
                } else if (mod.type == ParticleModuleType::ShapeMeshSurface) {
                    op.assetPath = ResolveStringInput(mod, *desc, "Mesh");
                }
                break;
            }
            case ParticleModuleType::InitialVelocity:
                needsVelocity = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Velocity"));
                break;
            case ParticleModuleType::VelocityInCone:
                needsVelocity = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Axis"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Angle"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Speed"));
                break;
            case ParticleModuleType::InitialLifetime:
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Lifetime"));
                break;
            case ParticleModuleType::InitialColor:
                hasInitialColor = true;
                mask |= ToMask(VfxAttributeBit::Color);
                op.colorA = tables.CompileColor(mod.id, ResolveColorInput(mod, *desc, "Color"));
                break;
            case ParticleModuleType::InitialSize:
                mask |= ToMask(VfxAttributeBit::Size);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Size"));
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Size"));
                break;
            case ParticleModuleType::InitialRotation:
                mask |= ToMask(VfxAttributeBit::Rotation);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Rotation"));
                break;
            case ParticleModuleType::InitialSpin:
                mask |= ToMask(VfxAttributeBit::Rotation) | ToMask(VfxAttributeBit::Spin);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Spin"));
                break;
            case ParticleModuleType::InitialSpriteFrame:
                mask |= ToMask(VfxAttributeBit::Frame);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Frame"));
                break;
            case ParticleModuleType::InitialMass:
                mask |= ToMask(VfxAttributeBit::Mass);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Mass"));
                break;
            case ParticleModuleType::InheritOwnerVelocity:
                needsVelocity = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Scale"));
                break;
            default:
                break;
        }
        candidate.particleSpawnOps.push_back(std::move(op));
    }

    // ---- Stage 3: Particle Update ----
    for (const ParticleModule& mod : emitter.particleUpdate) {
        const ParticleModuleDescriptor* desc = nullptr;
        if (!ValidateModuleKeysAndStage(mod, ParticleStage::ParticleUpdate, messages, desc)) {
            hasError = true;
            continue;
        }
        if (!mod.enabled) {
            continue;
        }

        VfxParticleUpdateOp op;
        op.type = mod.type;
        op.moduleId = mod.id;

        switch (mod.type) {
            case ParticleModuleType::Gravity:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Acceleration"));
                break;
            case ParticleModuleType::Drag:
                needsVelocity = true;
                needsPosition = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Coefficient"));
                break;
            case ParticleModuleType::AddVelocity:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Velocity"));
                break;
            case ParticleModuleType::CurlNoise:
                needsVelocity = true;
                needsPosition = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Frequency"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Speed"));
                break;
            case ParticleModuleType::Vortex:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Axis"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Center"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "PullIn"));
                break;
            case ParticleModuleType::PointAttractor:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Position"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "KillRadius"));
                break;
            case ParticleModuleType::Wind:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Velocity"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Turbulence"));
                break;
            case ParticleModuleType::LimitSpeed:
                needsVelocity = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MaxSpeed"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Damping"));
                break;
            case ParticleModuleType::Orbit:
                needsPosition = true;
                hasOrbit = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Axis"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Speed"));
                break;
            case ParticleModuleType::ColorOverLife:
                hasColorOverLife = true;
                mask |= ToMask(VfxAttributeBit::Color) | ToMask(VfxAttributeBit::SpawnColor);
                op.colorA = tables.CompileColor(mod.id, ResolveColorInput(mod, *desc, "Color"));
                break;
            case ParticleModuleType::SizeOverLife:
                hasSizeOverLifeOrBySpeed = true;
                mask |= ToMask(VfxAttributeBit::Size) | ToMask(VfxAttributeBit::SpawnSize);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Scale"));
                break;
            case ParticleModuleType::RotationRate:
                mask |= ToMask(VfxAttributeBit::Rotation) | ToMask(VfxAttributeBit::Spin);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Rate"));
                break;
            case ParticleModuleType::SizeBySpeed:
                needsVelocity = true;
                hasSizeOverLifeOrBySpeed = true;
                mask |= ToMask(VfxAttributeBit::Size) | ToMask(VfxAttributeBit::SpawnSize);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MinSpeed"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MaxSpeed"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MinScale"));
                op.floatD = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "MaxScale"));
                break;
            case ParticleModuleType::SpriteFlipbook:
                mask |= ToMask(VfxAttributeBit::Frame);
                op.intA = std::max(1, ResolveIntInput(mod, *desc, "Rows"));
                op.intB = std::max(1, ResolveIntInput(mod, *desc, "Columns"));
                op.intC = ResolveIntInput(mod, *desc, "Mode");
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Fps"));
                break;
            case ParticleModuleType::CollisionPlane:
                needsPosition = true;
                needsVelocity = true;
                needsPrevPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInput(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleType::CollisionDepth:
                needsPosition = true;
                needsVelocity = true;
                if (emitter.target != ParticleSimTarget::Gpu) {
                    PushWarning(messages, mod.id,
                                   "Collision: Depth only runs on the GPU path and is skipped on CPU.");
                }
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInput(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleType::CollisionPhysics:
                needsPosition = true;
                needsVelocity = true;
                needsPrevPosition = true;
                if (emitter.maxParticles > 2000U) {
                    PushWarning(messages, mod.id,
                                   "Collision: Physics is ignored when live particles exceed 2,000.");
                }
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInput(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleType::KillByAge:
                break;
            case ParticleModuleType::KillByBounds:
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Min"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Max"));
                op.intA = ResolveIntInput(mod, *desc, "Inside");
                break;
            case ParticleModuleType::KillByCondition:
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Condition"));
                break;
            case ParticleModuleType::EventOnDeath:
            case ParticleModuleType::EventOnCollision: {
                needsPosition = true;
                needsVelocity = true;
                const std::string targetName = ResolveStringInput(mod, *desc, "TargetEmitter");
                const auto it = emitterNameMap.find(targetName);
                if (targetName.empty() || it == emitterNameMap.end()) {
                    tables.Fail(mod.id, "Event target emitter \"" + targetName + "\" does not exist.");
                } else if (it->second == emitterIndex) {
                    tables.Fail(mod.id, "Event cannot target its own emitter \"" + targetName + "\".");
                } else {
                    op.targetEmitterIndex = it->second;
                }
                op.intA = ResolveIntInput(mod, *desc, "Count");
                break;
            }
            default:
                break;
        }
        candidate.particleUpdateOps.push_back(std::move(op));
    }

    // ---- Stage 4: Render ----
    std::size_t enabledRendererCount = 0U;
    for (const ParticleModule& mod : emitter.render) {
        const ParticleModuleDescriptor* desc = nullptr;
        if (!ValidateModuleKeysAndStage(mod, ParticleStage::Render, messages, desc)) {
            hasError = true;
            continue;
        }

        // Validate texture/mesh/material references cleanly on every render module.
        if (mod.type == ParticleModuleType::SpriteRenderer ||
            mod.type == ParticleModuleType::RibbonRenderer) {
            const std::string tex = ResolveStringInput(mod, *desc, "Texture");
            if (!tex.empty()) {
                if (LowercaseExtension(tex) != ".uvtex") {
                    tables.Fail(mod.id, "Renderer texture must be a .uvtex asset: " + tex);
                } else if (!DoesAssetExist(tex, context)) {
                    tables.Fail(mod.id, "Renderer texture file is missing: " + tex);
                }
            }
            const std::string mat = ResolveStringInput(mod, *desc, "Material");
            if (!mat.empty()) {
                const std::string ext = LowercaseExtension(mat);
                if (ext != ".uvmat" && ext != ".uvmaterial") {
                    tables.Fail(mod.id, "Renderer material must be a .uvmat or .uvmaterial asset: " + mat);
                } else if (!DoesAssetExist(mat, context)) {
                    tables.Fail(mod.id, "Renderer material file is missing: " + mat);
                }
            }
        } else if (mod.type == ParticleModuleType::MeshRenderer) {
            const std::string mesh = ResolveStringInput(mod, *desc, "Mesh");
            if (mesh.empty()) {
                tables.Fail(mod.id, "Mesh Renderer requires a mesh asset path.");
            } else if (LowercaseExtension(mesh) != ".uvmodel") {
                tables.Fail(mod.id, "Mesh Renderer mesh must be a .uvmodel asset.");
            } else if (!DoesAssetExist(mesh, context)) {
                tables.Fail(mod.id, "Mesh Renderer mesh file is missing: " + mesh);
            }
            const std::string mat = ResolveStringInput(mod, *desc, "Material");
            if (!mat.empty()) {
                const std::string ext = LowercaseExtension(mat);
                if (ext != ".uvmat" && ext != ".uvmaterial") {
                    tables.Fail(mod.id, "Mesh Renderer material must be a .uvmat or .uvmaterial asset: " + mat);
                } else if (!DoesAssetExist(mat, context)) {
                    tables.Fail(mod.id, "Mesh Renderer material file is missing: " + mat);
                }
            }
        }

        if (!mod.enabled || !IsRendererModule(mod.type)) {
            continue;
        }
        ++enabledRendererCount;
        needsPosition = true;

        VfxRendererDesc rd;
        rd.type = mod.type;
        rd.moduleId = mod.id;
        switch (mod.type) {
            case ParticleModuleType::SpriteRenderer:
                rd.facing = ResolveIntInput(mod, *desc, "Facing");
                rd.blend = ResolveIntInput(mod, *desc, "Blend");
                rd.rows = std::max(1, ResolveIntInput(mod, *desc, "Rows"));
                rd.cols = std::max(1, ResolveIntInput(mod, *desc, "Columns"));
                rd.frameBlend = ResolveIntInput(mod, *desc, "FrameBlend");
                rd.stretchScale = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "StretchScale"));
                rd.alphaCutoff = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "AlphaCutoff"));
                rd.softDistance = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "SoftDistance"));
                rd.cameraFadeNear = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "CameraFadeNear"));
                rd.cameraFadeFar = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "CameraFadeFar"));
                rd.emissive = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Emissive"));
                rd.axis = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Axis"));
                rd.texturePath = ResolveStringInput(mod, *desc, "Texture");
                rd.materialPath = ResolveStringInput(mod, *desc, "Material");
                if (rd.facing == 2) {
                    needsVelocity = true;
                }
                break;
            case ParticleModuleType::RibbonRenderer:
                needsPrevPosition = true;
                rd.perParticle = ResolveIntInput(mod, *desc, "PerParticle");
                rd.trailLength = std::max(2, ResolveIntInput(mod, *desc, "TrailLength"));
                rd.ribbonMode = ResolveIntInput(mod, *desc, "Mode");
                rd.blend = ResolveIntInput(mod, *desc, "Blend");
                rd.width = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Width"));
                rd.tileLength = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "TileLength"));
                rd.emissive = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Emissive"));
                rd.texturePath = ResolveStringInput(mod, *desc, "Texture");
                rd.materialPath = ResolveStringInput(mod, *desc, "Material");
                break;
            case ParticleModuleType::MeshRenderer:
                rd.orient = ResolveIntInput(mod, *desc, "Orient");
                rd.axis = tables.CompileVector(mod.id, ResolveVectorInput(mod, *desc, "Axis"));
                rd.meshPath = ResolveStringInput(mod, *desc, "Mesh");
                rd.materialPath = ResolveStringInput(mod, *desc, "Material");
                if (rd.orient == 1) {
                    needsVelocity = true;
                }
                break;
            case ParticleModuleType::LightRenderer:
                rd.radius = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Radius"));
                rd.intensity = tables.CompileFloat(mod.id, ResolveFloatInput(mod, *desc, "Intensity"));
                break;
            default:
                break;
        }
        candidate.renderers.push_back(std::move(rd));
    }

    if (enabledRendererCount == 0U) {
        PushError(messages, 0U, "Render stage must contain at least one enabled renderer.");
        hasError = true;
    }

    if (needsPosition) {
        mask |= ToMask(VfxAttributeBit::Position);
    }
    if (needsVelocity) {
        mask |= ToMask(VfxAttributeBit::Velocity) | ToMask(VfxAttributeBit::Position);
    }
    if (needsPrevPosition) {
        mask |= ToMask(VfxAttributeBit::PrevPosition);
    }
    if (hasColorOverLife && !hasInitialColor) {
        mask |= ToMask(VfxAttributeBit::Color) | ToMask(VfxAttributeBit::SpawnColor);
    }
    if (hasColorOverLife) {
        mask |= ToMask(VfxAttributeBit::SpawnColor);
    }
    if (hasSizeOverLifeOrBySpeed) {
        mask |= ToMask(VfxAttributeBit::Size) | ToMask(VfxAttributeBit::SpawnSize);
    }
    if (hasOrbit) {
        mask |= ToMask(VfxAttributeBit::SpawnOrigin);
    }

    candidate.attributeMask = mask;

    if (hasError || tables.hasError) {
        return false;
    }

    outProgram = std::move(candidate);
    return true;
}

} // namespace UVE::VFX
