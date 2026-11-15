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

using Asset::ParticleColorInputUVE;
using Asset::ParticleFloatInputUVE;
using Asset::ParticleModuleDescriptorUVE;
using Asset::ParticleModuleTypeUVE;
using Asset::ParticleModuleUVE;
using Asset::ParticleSimTargetUVE;
using Asset::ParticleStageUVE;
using Asset::ParticleSystemAssetUVE;
using Asset::ParticleUserParameterTypeUVE;
using Asset::ParticleUserParameterUVE;
using Asset::ParticleValueKindUVE;
using Asset::ParticleVectorInputUVE;

constexpr std::size_t kMaximumShapeModulesWarningLimitUVE = 1U;

void PushErrorUVE(std::vector<ParticleCompileMessageUVE>& messages, const std::uint32_t moduleId, std::string text) {
    messages.push_back(ParticleCompileMessageUVE{
        ParticleCompileMessageUVE::Severity::Error,
        moduleId,
        std::move(text),
    });
}

void PushWarningUVE(std::vector<ParticleCompileMessageUVE>& messages, const std::uint32_t moduleId, std::string text) {
    messages.push_back(ParticleCompileMessageUVE{
        ParticleCompileMessageUVE::Severity::Warning,
        moduleId,
        std::move(text),
    });
}

[[nodiscard]] std::string LowercaseExtensionUVE(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

[[nodiscard]] bool DoesAssetExistUVE(const std::string_view assetPath, const VfxCompileContextUVE& context) {
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

[[nodiscard]] bool IsShapeModuleUVE(const ParticleModuleTypeUVE type) noexcept {
    switch (type) {
        case ParticleModuleTypeUVE::ShapePoint:
        case ParticleModuleTypeUVE::ShapeSphere:
        case ParticleModuleTypeUVE::ShapeBox:
        case ParticleModuleTypeUVE::ShapeCone:
        case ParticleModuleTypeUVE::ShapeCylinder:
        case ParticleModuleTypeUVE::ShapeTorus:
        case ParticleModuleTypeUVE::ShapeMeshSurface:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool IsRendererModuleUVE(const ParticleModuleTypeUVE type) noexcept {
    switch (type) {
        case ParticleModuleTypeUVE::SpriteRenderer:
        case ParticleModuleTypeUVE::RibbonRenderer:
        case ParticleModuleTypeUVE::MeshRenderer:
        case ParticleModuleTypeUVE::LightRenderer:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool ValidateMathGraphCyclesUVE(const std::vector<Asset::ParticleMathNodeUVE>& mathNodes,
                                              const std::unordered_map<std::uint32_t, std::size_t>& idToIndex,
                                              std::vector<ParticleCompileMessageUVE>& messages) {
    // 0 = unvisited, 1 = visiting, 2 = done
    std::vector<std::uint8_t> state(mathNodes.size(), 0U);
    bool hasCycle = false;

    const auto visit = [&](auto& self, const std::size_t nodeIdx) -> void {
        state[nodeIdx] = 1U;
        for (const std::uint32_t inputId : mathNodes[nodeIdx].inputs) {
            const auto it = idToIndex.find(inputId);
            if (it == idToIndex.end()) {
                PushErrorUVE(messages, mathNodes[nodeIdx].id, "Math node references a missing input node.");
                continue;
            }
            const std::size_t depIdx = it->second;
            if (state[depIdx] == 1U) {
                hasCycle = true;
                PushErrorUVE(messages, mathNodes[nodeIdx].id, "Cycle detected in Math node graph.");
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

[[nodiscard]] ParticleFloatInputUVE ResolveFloatInputUVE(const ParticleModuleUVE& mod,
                                                         const ParticleModuleDescriptorUVE& desc,
                                                         const std::string& name) {
    const auto it = mod.floats.find(name);
    if (it != mod.floats.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultFloats.find(name);
    if (defIt != desc.defaultFloats.end()) {
        return defIt->second;
    }
    return ParticleFloatInputUVE{};
}

[[nodiscard]] ParticleVectorInputUVE ResolveVectorInputUVE(const ParticleModuleUVE& mod,
                                                           const ParticleModuleDescriptorUVE& desc,
                                                           const std::string& name) {
    const auto it = mod.vectors.find(name);
    if (it != mod.vectors.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultVectors.find(name);
    if (defIt != desc.defaultVectors.end()) {
        return defIt->second;
    }
    return ParticleVectorInputUVE{};
}

[[nodiscard]] ParticleColorInputUVE ResolveColorInputUVE(const ParticleModuleUVE& mod,
                                                         const ParticleModuleDescriptorUVE& desc,
                                                         const std::string& name) {
    const auto it = mod.colors.find(name);
    if (it != mod.colors.end()) {
        return it->second;
    }
    const auto defIt = desc.defaultColors.find(name);
    if (defIt != desc.defaultColors.end()) {
        return defIt->second;
    }
    return ParticleColorInputUVE{};
}

[[nodiscard]] std::int32_t ResolveIntInputUVE(const ParticleModuleUVE& mod,
                                              const ParticleModuleDescriptorUVE& desc,
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

[[nodiscard]] std::string ResolveStringInputUVE(const ParticleModuleUVE& mod,
                                              const ParticleModuleDescriptorUVE& desc,
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

struct CompileTablesUVE final {
    const std::unordered_map<std::string_view, std::pair<std::uint32_t, ParticleUserParameterTypeUVE>>& paramMap;
    const std::unordered_map<std::uint32_t, std::size_t>& mathIdMap;
    std::vector<VfxCurveTableUVE>& curveTables;
    std::vector<VfxGradientTableUVE>& gradientTables;
    std::vector<ParticleCompileMessageUVE>& messages;
    bool hasError = false;

    void Fail(const std::uint32_t moduleId, std::string text) {
        hasError = true;
        PushErrorUVE(messages, moduleId, std::move(text));
    }

    [[nodiscard]] VfxCompiledFloatInputUVE CompileFloat(const std::uint32_t moduleId,
                                                        const ParticleFloatInputUVE& input) {
        VfxCompiledFloatInputUVE out;
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
            case ParticleValueKindUVE::Constant:
                break;
            case ParticleValueKindUVE::RandomRange:
                if (input.min > input.max) {
                    Fail(moduleId, "Random range input requires minimum <= maximum.");
                }
                break;
            case ParticleValueKindUVE::Curve: {
                if (input.curve.keys.size() > Asset::kMaximumCurveKeysUVE) {
                    Fail(moduleId, "Curve exceeds the maximum supported key count.");
                    break;
                }
                float prevT = -1.0F;
                for (const Asset::ParticleCurveKeyUVE& key : input.curve.keys) {
                    if (!std::isfinite(key.time) || !std::isfinite(key.value) ||
                        !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent) ||
                        key.time < 0.0F || key.time > 1.0F || key.time < prevT) {
                        Fail(moduleId, "Curve keys must be finite, ordered, and within [0, 1].");
                        return out;
                    }
                    prevT = key.time;
                }
                out.curveTableIndex = static_cast<std::uint32_t>(curveTables.size());
                curveTables.push_back(BakeVfxCurveTableUVE(input.curve, input.scale));
                break;
            }
            case ParticleValueKindUVE::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced float user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterTypeUVE::Float) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Float.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKindUVE::Wire:
                if (!mathIdMap.contains(input.wireNode)) {
                    Fail(moduleId, "Wire input names a missing Math node.");
                }
                break;
            case ParticleValueKindUVE::Gradient:
                Fail(moduleId, "Float input cannot evaluate a color gradient.");
                break;
        }
        return out;
    }

    [[nodiscard]] VfxCompiledVectorInputUVE CompileVector(const std::uint32_t moduleId,
                                                          const ParticleVectorInputUVE& input) {
        VfxCompiledVectorInputUVE out;
        out.kind = input.kind;
        out.constant = input.constant;
        out.min = input.min;
        out.max = input.max;
        out.wireNode = input.wireNode;

        if (!Math::IsFiniteUVE(input.constant) || !Math::IsFiniteUVE(input.min) || !Math::IsFiniteUVE(input.max)) {
            Fail(moduleId, "Vector input contains non-finite components.");
            return out;
        }
        switch (input.kind) {
            case ParticleValueKindUVE::Constant:
                break;
            case ParticleValueKindUVE::RandomRange:
                if (input.min.x > input.max.x || input.min.y > input.max.y || input.min.z > input.max.z) {
                    Fail(moduleId, "Vector random range requires minimum <= maximum on each axis.");
                }
                break;
            case ParticleValueKindUVE::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced vector user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterTypeUVE::Vector) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Vector.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKindUVE::Wire:
                if (!mathIdMap.contains(input.wireNode)) {
                    Fail(moduleId, "Wire input names a missing Math node.");
                }
                break;
            case ParticleValueKindUVE::Curve:
            case ParticleValueKindUVE::Gradient:
                Fail(moduleId, "Vector input kind is not supported.");
                break;
        }
        return out;
    }

    [[nodiscard]] VfxCompiledColorInputUVE CompileColor(const std::uint32_t moduleId,
                                                        const ParticleColorInputUVE& input) {
        VfxCompiledColorInputUVE out;
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
            case ParticleValueKindUVE::Constant:
            case ParticleValueKindUVE::RandomRange:
                break;
            case ParticleValueKindUVE::Gradient: {
                if (input.gradient.stops.size() > Asset::kMaximumGradientStopsUVE) {
                    Fail(moduleId, "Gradient exceeds maximum stop count.");
                    break;
                }
                float prevT = -1.0F;
                for (const Asset::ParticleGradientStopUVE& stop : input.gradient.stops) {
                    if (!std::isfinite(stop.time) || !std::isfinite(stop.color.r) ||
                        !std::isfinite(stop.color.g) || !std::isfinite(stop.color.b) ||
                        !std::isfinite(stop.alpha) || stop.time < 0.0F || stop.time > 1.0F || stop.time < prevT) {
                        Fail(moduleId, "Gradient stops must be finite, ordered, and within [0, 1].");
                        return out;
                    }
                    prevT = stop.time;
                }
                out.gradientTableIndex = static_cast<std::uint32_t>(gradientTables.size());
                gradientTables.push_back(BakeVfxGradientTableUVE(input.gradient));
                break;
            }
            case ParticleValueKindUVE::UserParameter: {
                const auto it = paramMap.find(input.parameter);
                if (it == paramMap.end()) {
                    Fail(moduleId, "Referenced color user parameter \"" + input.parameter + "\" does not exist.");
                } else if (it->second.second != ParticleUserParameterTypeUVE::Color) {
                    Fail(moduleId, "User parameter \"" + input.parameter + "\" is not of type Color.");
                } else {
                    out.parameterIndex = it->second.first;
                }
                break;
            }
            case ParticleValueKindUVE::Curve:
            case ParticleValueKindUVE::Wire:
                Fail(moduleId, "Color input kind is not supported.");
                break;
        }
        return out;
    }
};

[[nodiscard]] bool ValidateModuleKeysAndStageUVE(const ParticleModuleUVE& mod,
                                                 const ParticleStageUVE expectedStage,
                                                 std::vector<ParticleCompileMessageUVE>& messages,
                                                 const ParticleModuleDescriptorUVE*& outDesc) {
    outDesc = Asset::FindParticleModuleDescriptorUVE(mod.type);
    if (outDesc == nullptr) {
        PushErrorUVE(messages, mod.id, "Unknown particle module type.");
        return false;
    }
    if (outDesc->stage != expectedStage) {
        PushErrorUVE(messages, mod.id,
                     std::string("Module \"") + std::string(outDesc->canonicalName) +
                         "\" is placed in the wrong pipeline stage.");
        return false;
    }
    bool ok = true;
    for (const auto& [name, val] : mod.floats) {
        static_cast<void>(val);
        if (!outDesc->defaultFloats.contains(name)) {
            PushErrorUVE(messages, mod.id, "Unknown float input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.vectors) {
        static_cast<void>(val);
        if (!outDesc->defaultVectors.contains(name)) {
            PushErrorUVE(messages, mod.id, "Unknown vector input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.colors) {
        static_cast<void>(val);
        if (!outDesc->defaultColors.contains(name)) {
            PushErrorUVE(messages, mod.id, "Unknown color input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.ints) {
        static_cast<void>(val);
        if (!outDesc->defaultInts.contains(name)) {
            PushErrorUVE(messages, mod.id, "Unknown integer input \"" + name + "\".");
            ok = false;
        }
    }
    for (const auto& [name, val] : mod.strings) {
        static_cast<void>(val);
        if (!outDesc->defaultStrings.contains(name)) {
            PushErrorUVE(messages, mod.id, "Unknown string input \"" + name + "\".");
            ok = false;
        }
    }
    return ok;
}

[[nodiscard]] bool HasNonZeroFloatPossibilityUVE(const ParticleFloatInputUVE& input) noexcept {
    if (input.kind == ParticleValueKindUVE::Constant) {
        return input.constant != 0.0F;
    }
    return true;
}

} // namespace

bool CompileParticleEmitterUVE(const Asset::ParticleSystemAssetUVE& system,
                               const std::size_t emitterIndex,
                               ParticleProgramUVE& outProgram,
                               std::vector<ParticleCompileMessageUVE>& messages) {
    return CompileParticleEmitterUVE(system, emitterIndex, VfxCompileContextUVE{}, outProgram, messages);
}

bool CompileParticleEmitterUVE(const Asset::ParticleSystemAssetUVE& system,
                               const std::size_t emitterIndex,
                               const VfxCompileContextUVE& context,
                               ParticleProgramUVE& outProgram,
                               std::vector<ParticleCompileMessageUVE>& messages) {
    messages.clear();

    if (emitterIndex >= system.emitters.size()) {
        PushErrorUVE(messages, 0U, "Emitter index is out of range.");
        return false;
    }

    const Asset::ParticleEmitterAssetUVE& emitter = system.emitters[emitterIndex];
    bool hasError = false;

    if (!std::isfinite(system.duration) || system.duration <= 0.0F) {
        PushErrorUVE(messages, 0U, "System duration must be finite and greater than zero.");
        hasError = true;
    }
    if (!std::isfinite(system.warmUpSeconds) || system.warmUpSeconds < 0.0F) {
        PushErrorUVE(messages, 0U, "System warm-up duration must be non-negative.");
        hasError = true;
    }
    if (emitter.maxParticles == 0U || emitter.maxParticles > Asset::kMaximumParticlesPerEmitterUVE) {
        PushErrorUVE(messages, 0U, "Emitter maxParticles must be between 1 and 1,000,000.");
        hasError = true;
    }

    if (emitter.target != ParticleSimTargetUVE::Gpu && emitter.maxParticles > 100'000U) {
        PushWarningUVE(messages, 0U, "Emitter maxParticles exceeds 100,000 on CPU target.");
    }

    // Build user-parameter lookup map.
    std::unordered_map<std::string_view, std::pair<std::uint32_t, ParticleUserParameterTypeUVE>> paramMap;
    paramMap.reserve(system.parameters.size());
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(system.parameters.size()); ++idx) {
        const ParticleUserParameterUVE& param = system.parameters[idx];
        if (!paramMap.emplace(param.name, std::make_pair(idx, param.type)).second) {
            PushErrorUVE(messages, 0U, "Duplicate user parameter name \"" + param.name + "\".");
            hasError = true;
        }
    }

    // Build Math node lookup and check for cycles.
    std::unordered_map<std::uint32_t, std::size_t> mathIdMap;
    mathIdMap.reserve(emitter.mathNodes.size());
    for (std::size_t idx = 0U; idx < emitter.mathNodes.size(); ++idx) {
        const auto& math = emitter.mathNodes[idx];
        if (math.id == 0U || !mathIdMap.emplace(math.id, idx).second) {
            PushErrorUVE(messages, math.id, "Math nodes must have unique non-zero IDs.");
            hasError = true;
        }
    }
    if (!emitter.mathNodes.empty() && !ValidateMathGraphCyclesUVE(emitter.mathNodes, mathIdMap, messages)) {
        hasError = true;
    }

    // Build emitter name lookup for event targets.
    std::unordered_map<std::string_view, std::uint32_t> emitterNameMap;
    emitterNameMap.reserve(system.emitters.size());
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(system.emitters.size()); ++idx) {
        emitterNameMap.emplace(system.emitters[idx].name, idx);
    }

    ParticleProgramUVE candidate;
    candidate.emitterName = emitter.name;
    candidate.enabled = emitter.enabled;
    candidate.space = emitter.space;
    candidate.target = emitter.target;
    candidate.maxParticles = emitter.maxParticles;
    candidate.seed = emitter.seed;

    // Base identity & aging attributes are required by every live emitter.
    VfxAttributeMaskUVE mask = ToMaskUVE(VfxAttributeBitUVE::Id) | ToMaskUVE(VfxAttributeBitUVE::Seed) |
                               ToMaskUVE(VfxAttributeBitUVE::Age) | ToMaskUVE(VfxAttributeBitUVE::Lifetime);

    CompileTablesUVE tables{
        .paramMap = paramMap,
        .mathIdMap = mathIdMap,
        .curveTables = candidate.curveTables,
        .gradientTables = candidate.gradientTables,
        .messages = messages,
    };

    // ---- Stage 1: Emitter Update ----
    for (const ParticleModuleUVE& mod : emitter.emitterUpdate) {
        const ParticleModuleDescriptorUVE* desc = nullptr;
        if (!ValidateModuleKeysAndStageUVE(mod, ParticleStageUVE::EmitterUpdate, messages, desc)) {
            hasError = true;
            continue;
        }
        if (!mod.enabled) {
            continue;
        }
        VfxEmitterUpdateOpUVE op;
        op.type = mod.type;
        op.moduleId = mod.id;
        switch (mod.type) {
            case ParticleModuleTypeUVE::SpawnRate:
                op.rate = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Rate"));
                break;
            case ParticleModuleTypeUVE::SpawnBurst:
                op.time = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Time"));
                op.interval = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Interval"));
                op.count = ResolveIntInputUVE(mod, *desc, "Count");
                op.cycles = ResolveIntInputUVE(mod, *desc, "Cycles");
                break;
            case ParticleModuleTypeUVE::SpawnPerDistance:
                op.perUnit = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "PerUnit"));
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
        if (emitter.particleSpawn[idx].enabled && IsShapeModuleUVE(emitter.particleSpawn[idx].type)) {
            lastEnabledShapeModuleIndex = idx;
            ++enabledShapeCount;
        }
    }
    if (enabledShapeCount > kMaximumShapeModulesWarningLimitUVE) {
        for (std::size_t idx = 0U; idx < emitter.particleSpawn.size(); ++idx) {
            const ParticleModuleUVE& mod = emitter.particleSpawn[idx];
            if (mod.enabled && IsShapeModuleUVE(mod.type) && idx != lastEnabledShapeModuleIndex) {
                PushWarningUVE(messages, mod.id,
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
        const ParticleModuleUVE& mod = emitter.particleSpawn[idx];
        const ParticleModuleDescriptorUVE* desc = nullptr;
        if (!ValidateModuleKeysAndStageUVE(mod, ParticleStageUVE::ParticleSpawn, messages, desc)) {
            hasError = true;
            continue;
        }

        // Validate mesh reference even if disabled or shadowed.
        if (mod.type == ParticleModuleTypeUVE::ShapeMeshSurface) {
            const std::string meshPath = ResolveStringInputUVE(mod, *desc, "Mesh");
            if (meshPath.empty()) {
                tables.Fail(mod.id, "Shape: Mesh Surface requires a mesh asset path.");
            } else if (LowercaseExtensionUVE(meshPath) != ".uvmodel") {
                tables.Fail(mod.id, "Shape: Mesh Surface mesh must be a .uvmodel asset.");
            } else if (!DoesAssetExistUVE(meshPath, context)) {
                tables.Fail(mod.id, "Shape: Mesh Surface mesh asset file is missing: " + meshPath);
            }
        }

        if (!mod.enabled) {
            continue;
        }
        if (IsShapeModuleUVE(mod.type) && idx != lastEnabledShapeModuleIndex) {
            continue;
        }

        VfxParticleSpawnOpUVE op;
        op.type = mod.type;
        op.moduleId = mod.id;

        switch (mod.type) {
            case ParticleModuleTypeUVE::ShapePoint:
            case ParticleModuleTypeUVE::ShapeSphere:
            case ParticleModuleTypeUVE::ShapeBox:
            case ParticleModuleTypeUVE::ShapeCone:
            case ParticleModuleTypeUVE::ShapeCylinder:
            case ParticleModuleTypeUVE::ShapeTorus:
            case ParticleModuleTypeUVE::ShapeMeshSurface: {
                needsPosition = true;
                const ParticleFloatInputUVE ns = ResolveFloatInputUVE(mod, *desc, "NormalSpeed");
                op.normalSpeed = tables.CompileFloat(mod.id, ns);
                op.surface = ResolveIntInputUVE(mod, *desc, "Surface");
                if (HasNonZeroFloatPossibilityUVE(ns)) {
                    needsVelocity = true;
                }
                if (mod.type == ParticleModuleTypeUVE::ShapeSphere) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                    op.hemisphere = ResolveIntInputUVE(mod, *desc, "Hemisphere");
                } else if (mod.type == ParticleModuleTypeUVE::ShapeBox) {
                    op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Size"));
                } else if (mod.type == ParticleModuleTypeUVE::ShapeCone) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Angle"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Length"));
                    op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                } else if (mod.type == ParticleModuleTypeUVE::ShapeCylinder) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Height"));
                } else if (mod.type == ParticleModuleTypeUVE::ShapeTorus) {
                    op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MajorRadius"));
                    op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MinorRadius"));
                } else if (mod.type == ParticleModuleTypeUVE::ShapeMeshSurface) {
                    op.assetPath = ResolveStringInputUVE(mod, *desc, "Mesh");
                }
                break;
            }
            case ParticleModuleTypeUVE::InitialVelocity:
                needsVelocity = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Velocity"));
                break;
            case ParticleModuleTypeUVE::VelocityInCone:
                needsVelocity = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Axis"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Angle"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Speed"));
                break;
            case ParticleModuleTypeUVE::InitialLifetime:
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Lifetime"));
                break;
            case ParticleModuleTypeUVE::InitialColor:
                hasInitialColor = true;
                mask |= ToMaskUVE(VfxAttributeBitUVE::Color);
                op.colorA = tables.CompileColor(mod.id, ResolveColorInputUVE(mod, *desc, "Color"));
                break;
            case ParticleModuleTypeUVE::InitialSize:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Size);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Size"));
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Size"));
                break;
            case ParticleModuleTypeUVE::InitialRotation:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Rotation);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Rotation"));
                break;
            case ParticleModuleTypeUVE::InitialSpin:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Rotation) | ToMaskUVE(VfxAttributeBitUVE::Spin);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Spin"));
                break;
            case ParticleModuleTypeUVE::InitialSpriteFrame:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Frame);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Frame"));
                break;
            case ParticleModuleTypeUVE::InitialMass:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Mass);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Mass"));
                break;
            case ParticleModuleTypeUVE::InheritOwnerVelocity:
                needsVelocity = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Scale"));
                break;
            default:
                break;
        }
        candidate.particleSpawnOps.push_back(std::move(op));
    }

    // ---- Stage 3: Particle Update ----
    for (const ParticleModuleUVE& mod : emitter.particleUpdate) {
        const ParticleModuleDescriptorUVE* desc = nullptr;
        if (!ValidateModuleKeysAndStageUVE(mod, ParticleStageUVE::ParticleUpdate, messages, desc)) {
            hasError = true;
            continue;
        }
        if (!mod.enabled) {
            continue;
        }

        VfxParticleUpdateOpUVE op;
        op.type = mod.type;
        op.moduleId = mod.id;

        switch (mod.type) {
            case ParticleModuleTypeUVE::Gravity:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Acceleration"));
                break;
            case ParticleModuleTypeUVE::Drag:
                needsVelocity = true;
                needsPosition = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Coefficient"));
                break;
            case ParticleModuleTypeUVE::AddVelocity:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Velocity"));
                break;
            case ParticleModuleTypeUVE::CurlNoise:
                needsVelocity = true;
                needsPosition = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Frequency"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Speed"));
                break;
            case ParticleModuleTypeUVE::Vortex:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Axis"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Center"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "PullIn"));
                break;
            case ParticleModuleTypeUVE::PointAttractor:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Position"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Strength"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "KillRadius"));
                break;
            case ParticleModuleTypeUVE::Wind:
                needsVelocity = true;
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Velocity"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Turbulence"));
                break;
            case ParticleModuleTypeUVE::LimitSpeed:
                needsVelocity = true;
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MaxSpeed"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Damping"));
                break;
            case ParticleModuleTypeUVE::Orbit:
                needsPosition = true;
                hasOrbit = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Axis"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Speed"));
                break;
            case ParticleModuleTypeUVE::ColorOverLife:
                hasColorOverLife = true;
                mask |= ToMaskUVE(VfxAttributeBitUVE::Color) | ToMaskUVE(VfxAttributeBitUVE::SpawnColor);
                op.colorA = tables.CompileColor(mod.id, ResolveColorInputUVE(mod, *desc, "Color"));
                break;
            case ParticleModuleTypeUVE::SizeOverLife:
                hasSizeOverLifeOrBySpeed = true;
                mask |= ToMaskUVE(VfxAttributeBitUVE::Size) | ToMaskUVE(VfxAttributeBitUVE::SpawnSize);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Scale"));
                break;
            case ParticleModuleTypeUVE::RotationRate:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Rotation) | ToMaskUVE(VfxAttributeBitUVE::Spin);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Rate"));
                break;
            case ParticleModuleTypeUVE::SizeBySpeed:
                needsVelocity = true;
                hasSizeOverLifeOrBySpeed = true;
                mask |= ToMaskUVE(VfxAttributeBitUVE::Size) | ToMaskUVE(VfxAttributeBitUVE::SpawnSize);
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MinSpeed"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MaxSpeed"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MinScale"));
                op.floatD = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "MaxScale"));
                break;
            case ParticleModuleTypeUVE::SpriteFlipbook:
                mask |= ToMaskUVE(VfxAttributeBitUVE::Frame);
                op.intA = std::max(1, ResolveIntInputUVE(mod, *desc, "Rows"));
                op.intB = std::max(1, ResolveIntInputUVE(mod, *desc, "Columns"));
                op.intC = ResolveIntInputUVE(mod, *desc, "Mode");
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Fps"));
                break;
            case ParticleModuleTypeUVE::CollisionPlane:
                needsPosition = true;
                needsVelocity = true;
                needsPrevPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInputUVE(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleTypeUVE::CollisionDepth:
                needsPosition = true;
                needsVelocity = true;
                if (emitter.target != ParticleSimTargetUVE::Gpu) {
                    PushWarningUVE(messages, mod.id,
                                   "Collision: Depth only runs on the GPU path and is skipped on CPU.");
                }
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInputUVE(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleTypeUVE::CollisionPhysics:
                needsPosition = true;
                needsVelocity = true;
                needsPrevPosition = true;
                if (emitter.maxParticles > 2000U) {
                    PushWarningUVE(messages, mod.id,
                                   "Collision: Physics is ignored when live particles exceed 2,000.");
                }
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Point"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Normal"));
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Bounce"));
                op.floatB = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Friction"));
                op.floatC = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "LifeLoss"));
                op.intA = ResolveIntInputUVE(mod, *desc, "KillOnHit");
                break;
            case ParticleModuleTypeUVE::KillByAge:
                break;
            case ParticleModuleTypeUVE::KillByBounds:
                needsPosition = true;
                op.vecA = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Min"));
                op.vecB = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Max"));
                op.intA = ResolveIntInputUVE(mod, *desc, "Inside");
                break;
            case ParticleModuleTypeUVE::KillByCondition:
                op.floatA = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Condition"));
                break;
            case ParticleModuleTypeUVE::EventOnDeath:
            case ParticleModuleTypeUVE::EventOnCollision: {
                needsPosition = true;
                needsVelocity = true;
                const std::string targetName = ResolveStringInputUVE(mod, *desc, "TargetEmitter");
                const auto it = emitterNameMap.find(targetName);
                if (targetName.empty() || it == emitterNameMap.end()) {
                    tables.Fail(mod.id, "Event target emitter \"" + targetName + "\" does not exist.");
                } else if (it->second == emitterIndex) {
                    tables.Fail(mod.id, "Event cannot target its own emitter \"" + targetName + "\".");
                } else {
                    op.targetEmitterIndex = it->second;
                }
                op.intA = ResolveIntInputUVE(mod, *desc, "Count");
                break;
            }
            default:
                break;
        }
        candidate.particleUpdateOps.push_back(std::move(op));
    }

    // ---- Stage 4: Render ----
    std::size_t enabledRendererCount = 0U;
    for (const ParticleModuleUVE& mod : emitter.render) {
        const ParticleModuleDescriptorUVE* desc = nullptr;
        if (!ValidateModuleKeysAndStageUVE(mod, ParticleStageUVE::Render, messages, desc)) {
            hasError = true;
            continue;
        }

        // Validate texture/mesh/material references cleanly on every render module.
        if (mod.type == ParticleModuleTypeUVE::SpriteRenderer ||
            mod.type == ParticleModuleTypeUVE::RibbonRenderer) {
            const std::string tex = ResolveStringInputUVE(mod, *desc, "Texture");
            if (!tex.empty()) {
                if (LowercaseExtensionUVE(tex) != ".uvtex") {
                    tables.Fail(mod.id, "Renderer texture must be a .uvtex asset: " + tex);
                } else if (!DoesAssetExistUVE(tex, context)) {
                    tables.Fail(mod.id, "Renderer texture file is missing: " + tex);
                }
            }
            const std::string mat = ResolveStringInputUVE(mod, *desc, "Material");
            if (!mat.empty()) {
                const std::string ext = LowercaseExtensionUVE(mat);
                if (ext != ".uvmat" && ext != ".uvmaterial") {
                    tables.Fail(mod.id, "Renderer material must be a .uvmat or .uvmaterial asset: " + mat);
                } else if (!DoesAssetExistUVE(mat, context)) {
                    tables.Fail(mod.id, "Renderer material file is missing: " + mat);
                }
            }
        } else if (mod.type == ParticleModuleTypeUVE::MeshRenderer) {
            const std::string mesh = ResolveStringInputUVE(mod, *desc, "Mesh");
            if (mesh.empty()) {
                tables.Fail(mod.id, "Mesh Renderer requires a mesh asset path.");
            } else if (LowercaseExtensionUVE(mesh) != ".uvmodel") {
                tables.Fail(mod.id, "Mesh Renderer mesh must be a .uvmodel asset.");
            } else if (!DoesAssetExistUVE(mesh, context)) {
                tables.Fail(mod.id, "Mesh Renderer mesh file is missing: " + mesh);
            }
            const std::string mat = ResolveStringInputUVE(mod, *desc, "Material");
            if (!mat.empty()) {
                const std::string ext = LowercaseExtensionUVE(mat);
                if (ext != ".uvmat" && ext != ".uvmaterial") {
                    tables.Fail(mod.id, "Mesh Renderer material must be a .uvmat or .uvmaterial asset: " + mat);
                } else if (!DoesAssetExistUVE(mat, context)) {
                    tables.Fail(mod.id, "Mesh Renderer material file is missing: " + mat);
                }
            }
        }

        if (!mod.enabled || !IsRendererModuleUVE(mod.type)) {
            continue;
        }
        ++enabledRendererCount;
        needsPosition = true;

        VfxRendererDescUVE rd;
        rd.type = mod.type;
        rd.moduleId = mod.id;
        switch (mod.type) {
            case ParticleModuleTypeUVE::SpriteRenderer:
                rd.facing = ResolveIntInputUVE(mod, *desc, "Facing");
                rd.blend = ResolveIntInputUVE(mod, *desc, "Blend");
                rd.rows = std::max(1, ResolveIntInputUVE(mod, *desc, "Rows"));
                rd.cols = std::max(1, ResolveIntInputUVE(mod, *desc, "Columns"));
                rd.frameBlend = ResolveIntInputUVE(mod, *desc, "FrameBlend");
                rd.stretchScale = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "StretchScale"));
                rd.alphaCutoff = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "AlphaCutoff"));
                rd.softDistance = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "SoftDistance"));
                rd.cameraFadeNear = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "CameraFadeNear"));
                rd.cameraFadeFar = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "CameraFadeFar"));
                rd.emissive = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Emissive"));
                rd.axis = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Axis"));
                rd.texturePath = ResolveStringInputUVE(mod, *desc, "Texture");
                rd.materialPath = ResolveStringInputUVE(mod, *desc, "Material");
                if (rd.facing == 2) {
                    needsVelocity = true;
                }
                break;
            case ParticleModuleTypeUVE::RibbonRenderer:
                needsPrevPosition = true;
                rd.perParticle = ResolveIntInputUVE(mod, *desc, "PerParticle");
                rd.trailLength = std::max(2, ResolveIntInputUVE(mod, *desc, "TrailLength"));
                rd.ribbonMode = ResolveIntInputUVE(mod, *desc, "Mode");
                rd.blend = ResolveIntInputUVE(mod, *desc, "Blend");
                rd.width = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Width"));
                rd.tileLength = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "TileLength"));
                rd.emissive = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Emissive"));
                rd.texturePath = ResolveStringInputUVE(mod, *desc, "Texture");
                rd.materialPath = ResolveStringInputUVE(mod, *desc, "Material");
                break;
            case ParticleModuleTypeUVE::MeshRenderer:
                rd.orient = ResolveIntInputUVE(mod, *desc, "Orient");
                rd.axis = tables.CompileVector(mod.id, ResolveVectorInputUVE(mod, *desc, "Axis"));
                rd.meshPath = ResolveStringInputUVE(mod, *desc, "Mesh");
                rd.materialPath = ResolveStringInputUVE(mod, *desc, "Material");
                if (rd.orient == 1) {
                    needsVelocity = true;
                }
                break;
            case ParticleModuleTypeUVE::LightRenderer:
                rd.radius = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Radius"));
                rd.intensity = tables.CompileFloat(mod.id, ResolveFloatInputUVE(mod, *desc, "Intensity"));
                break;
            default:
                break;
        }
        candidate.renderers.push_back(std::move(rd));
    }

    if (enabledRendererCount == 0U) {
        PushErrorUVE(messages, 0U, "Render stage must contain at least one enabled renderer.");
        hasError = true;
    }

    if (needsPosition) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::Position);
    }
    if (needsVelocity) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::Velocity) | ToMaskUVE(VfxAttributeBitUVE::Position);
    }
    if (needsPrevPosition) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::PrevPosition);
    }
    if (hasColorOverLife && !hasInitialColor) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::Color) | ToMaskUVE(VfxAttributeBitUVE::SpawnColor);
    }
    if (hasColorOverLife) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::SpawnColor);
    }
    if (hasSizeOverLifeOrBySpeed) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::Size) | ToMaskUVE(VfxAttributeBitUVE::SpawnSize);
    }
    if (hasOrbit) {
        mask |= ToMaskUVE(VfxAttributeBitUVE::SpawnOrigin);
    }

    candidate.attributeMask = mask;

    if (hasError || tables.hasError) {
        return false;
    }

    outProgram = std::move(candidate);
    return true;
}

} // namespace UVE::VFX
