// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_vfx_graph.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using PT = prism::BlueprintPinType;
using PK = prism::BlueprintPinKind;

constexpr float kStageNodeX = 780.0F;
constexpr float kFirstModuleX = 60.0F;
constexpr float kModuleSpacingX = 230.0F;
constexpr float kDetachedRowY = 760.0F;

[[nodiscard]] float StageRowY(const Asset::ParticleStage stage) noexcept {
    switch (stage) {
        case Asset::ParticleStage::EmitterUpdate: return 40.0F;
        case Asset::ParticleStage::ParticleSpawn: return 220.0F;
        case Asset::ParticleStage::ParticleUpdate: return 400.0F;
        case Asset::ParticleStage::Render: return 580.0F;
    }
    return 40.0F;
}

[[nodiscard]] int StageNodeIdFor(const Asset::ParticleStage stage) noexcept {
    switch (stage) {
        case Asset::ParticleStage::EmitterUpdate: return kVfxStageNodeEmitterUpdate;
        case Asset::ParticleStage::ParticleSpawn: return kVfxStageNodeParticleSpawn;
        case Asset::ParticleStage::ParticleUpdate: return kVfxStageNodeParticleUpdate;
        case Asset::ParticleStage::Render: return kVfxStageNodeRender;
    }
    return kVfxStageNodeEmitterUpdate;
}

[[nodiscard]] std::string StageDisplayName(const Asset::ParticleStage stage) {
    switch (stage) {
        case Asset::ParticleStage::EmitterUpdate: return "Emitter Update";
        case Asset::ParticleStage::ParticleSpawn: return "Particle Spawn";
        case Asset::ParticleStage::ParticleUpdate: return "Particle Update";
        case Asset::ParticleStage::Render: return "Render";
    }
    return "Stage";
}

[[nodiscard]] std::string FormatFloat(const float value) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(value));
    return buf;
}

[[nodiscard]] std::string FormatVec3(const Math::Vector3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4g,%.4g,%.4g",
                  static_cast<double>(v.x),
                  static_cast<double>(v.y),
                  static_cast<double>(v.z));
    return buf;
}

[[nodiscard]] std::string FormatColor(const Math::Color& c, const float alpha) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4g,%.4g,%.4g,%.4g",
                  static_cast<double>(c.r),
                  static_cast<double>(c.g),
                  static_cast<double>(c.b),
                  static_cast<double>(alpha));
    return buf;
}

[[nodiscard]] bool TryParseFloat(const std::string& text, float& out) noexcept {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(v)) {
        return false;
    }
    while (*end != '\0') {
        if (std::isspace(static_cast<unsigned char>(*end)) == 0) {
            return false;
        }
        ++end;
    }
    out = v;
    return true;
}

[[nodiscard]] bool TryParseInt(const std::string& text, std::int32_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return false;
    }
    while (*end != '\0') {
        if (std::isspace(static_cast<unsigned char>(*end)) == 0) {
            return false;
        }
        ++end;
    }
    out = static_cast<std::int32_t>(v);
    return true;
}

[[nodiscard]] bool TryParseVec3(const std::string& text, Math::Vector3& out) noexcept {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    if (std::sscanf(text.c_str(), " %f , %f , %f", &x, &y, &z) == 3 &&
        std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
        out = Math::Vector3{x, y, z};
        return true;
    }
    return false;
}

[[nodiscard]] bool TryParseColor(const std::string& text, Math::Color& outColor, float& outAlpha) noexcept {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
    const int matched = std::sscanf(text.c_str(), " %f , %f , %f , %f", &r, &g, &b, &a);
    if (matched >= 3 && std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a)) {
        outColor = Math::Color{r, g, b};
        if (matched == 4) {
            outAlpha = std::clamp(a, 0.0F, 1.0F);
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::string FloatPinDisplayText(const Asset::ParticleFloatInput& input) {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant: return FormatFloat(input.constant);
        case Asset::ParticleValueKind::RandomRange:
            return FormatFloat(input.min) + ".." + FormatFloat(input.max);
        case Asset::ParticleValueKind::Curve: return "Curve x" + FormatFloat(input.scale);
        case Asset::ParticleValueKind::UserParameter: return "$" + input.parameter;
        case Asset::ParticleValueKind::Wire: return FormatFloat(input.constant);
        case Asset::ParticleValueKind::Gradient: break;
    }
    return FormatFloat(input.constant);
}

[[nodiscard]] std::string VectorPinDisplayText(const Asset::ParticleVectorInput& input) {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant: return FormatVec3(input.constant);
        case Asset::ParticleValueKind::RandomRange:
            return FormatVec3(input.min) + ".." + FormatVec3(input.max);
        case Asset::ParticleValueKind::UserParameter: return "$" + input.parameter;
        case Asset::ParticleValueKind::Wire: return FormatVec3(input.constant);
        default: break;
    }
    return FormatVec3(input.constant);
}

[[nodiscard]] std::string ColorPinDisplayText(const Asset::ParticleColorInput& input) {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant: return FormatColor(input.constant, input.alpha);
        case Asset::ParticleValueKind::Gradient: return "Gradient";
        case Asset::ParticleValueKind::RandomRange: return "Range";
        case Asset::ParticleValueKind::UserParameter: return "$" + input.parameter;
        default: break;
    }
    return FormatColor(input.constant, input.alpha);
}

[[nodiscard]] prism::BlueprintNode MakeStageOutputNode(const Asset::ParticleStage stage, const float posX, const float posY) {
    prism::BlueprintNode node;
    node.id = StageNodeIdFor(stage);
    node.title = StageDisplayName(stage);
    node.subtitle = "Stage Output";
    node.category = prism::BlueprintNodeCategory::Event;
    node.style = prism::BlueprintNodeStyle::Header;
    node.pos = prism::Vec2{posX, posY};
    node.icon = prism::Icon::VfxGraph;
    node.typeKey = "vfx_stage:" + StageDisplayName(stage);
    node.tooltip = "Fixed stage output node for " + StageDisplayName(stage) + ".";
    prism::BlueprintPin inPin;
    inPin.name = "Chain";
    inPin.type = PT::Link;
    inPin.kind = PK::Input;
    inPin.hideLabel = false;
    node.inputs.push_back(std::move(inPin));
    return node;
}

[[nodiscard]] prism::BlueprintNode MakeModuleGraphNode(const Asset::ParticleModuleDescriptor& desc,
                                                          const Asset::ParticleModule* const instance) {
    prism::BlueprintNode node;
    node.id = instance != nullptr ? static_cast<int>(instance->id) : 0;
    node.title = std::string(desc.displayName);
    node.subtitle = StageDisplayName(desc.stage) +
                    (instance != nullptr && !instance->enabled ? " (Disabled)" : "");
    node.category = prism::BlueprintNodeCategory::Effect;
    node.style = prism::BlueprintNodeStyle::Header;
    node.icon = prism::Icon::VfxGraph;
    node.typeKey = "vfx_module:" + std::string(desc.canonicalName);
    node.tooltip = std::string(desc.description);
    if (instance != nullptr) {
        node.pos = prism::Vec2{instance->graphX, instance->graphY};
    }

    // Input 0: structural chain link from the previous module in the same stage.
    {
        prism::BlueprintPin inChain;
        inChain.name = "In";
        inChain.type = PT::Link;
        inChain.kind = PK::Input;
        node.inputs.push_back(std::move(inChain));
    }

    // Float inputs (excluding "Size" when a Vector "Size" also exists on InitialSize to avoid duplicate pin names).
    for (const auto& [name, defVal] : desc.defaultFloats) {
        if (name == "Size" && desc.defaultVectors.contains("Size")) {
            // Keep float Size only when explicitly overridden or when Vector Size is not overridden.
            const bool hasVecOverride = instance != nullptr && instance->vectors.contains("Size");
            if (hasVecOverride) {
                continue;
            }
        }
        Asset::ParticleFloatInput val = defVal;
        if (instance != nullptr) {
            if (const auto it = instance->floats.find(name); it != instance->floats.end()) {
                val = it->second;
            }
        }
        prism::BlueprintPin pin;
        pin.name = name;
        pin.type = PT::Float;
        pin.kind = PK::Input;
        pin.defaultValue = FloatPinDisplayText(val);
        pin.editable = val.kind == Asset::ParticleValueKind::Constant;
        node.inputs.push_back(std::move(pin));
    }

    // Vector inputs.
    for (const auto& [name, defVal] : desc.defaultVectors) {
        if (name == "Size" && desc.defaultFloats.contains("Size")) {
            const bool hasVecOverride = instance != nullptr && instance->vectors.contains("Size");
            if (!hasVecOverride) {
                continue;
            }
        }
        Asset::ParticleVectorInput val = defVal;
        if (instance != nullptr) {
            if (const auto it = instance->vectors.find(name); it != instance->vectors.end()) {
                val = it->second;
            }
        }
        prism::BlueprintPin pin;
        pin.name = name;
        pin.type = PT::Vector;
        pin.kind = PK::Input;
        pin.defaultValue = VectorPinDisplayText(val);
        pin.editable = val.kind == Asset::ParticleValueKind::Constant;
        node.inputs.push_back(std::move(pin));
    }

    // Color inputs.
    for (const auto& [name, defVal] : desc.defaultColors) {
        Asset::ParticleColorInput val = defVal;
        if (instance != nullptr) {
            if (const auto it = instance->colors.find(name); it != instance->colors.end()) {
                val = it->second;
            }
        }
        prism::BlueprintPin pin;
        pin.name = name;
        pin.type = PT::Color;
        pin.kind = PK::Input;
        pin.defaultValue = ColorPinDisplayText(val);
        pin.editable = val.kind == Asset::ParticleValueKind::Constant;
        node.inputs.push_back(std::move(pin));
    }

    // Int inputs.
    for (const auto& [name, defVal] : desc.defaultInts) {
        std::int32_t val = defVal;
        if (instance != nullptr) {
            if (const auto it = instance->ints.find(name); it != instance->ints.end()) {
                val = it->second;
            }
        }
        prism::BlueprintPin pin;
        pin.name = name;
        pin.type = PT::Int;
        pin.kind = PK::Input;
        pin.defaultValue = std::to_string(val);
        pin.editable = true;
        node.inputs.push_back(std::move(pin));
    }

    // String inputs.
    for (const auto& [name, defVal] : desc.defaultStrings) {
        std::string val = defVal;
        if (instance != nullptr) {
            if (const auto it = instance->strings.find(name); it != instance->strings.end()) {
                val = it->second;
            }
        }
        prism::BlueprintPin pin;
        pin.name = name;
        pin.type = PT::String;
        pin.kind = PK::Input;
        pin.defaultValue = val;
        pin.editable = true;
        node.inputs.push_back(std::move(pin));
    }

    // Output 0: structural chain link to the next module or the stage output node.
    {
        prism::BlueprintPin outChain;
        outChain.name = "Out";
        outChain.type = PT::Link;
        outChain.kind = PK::Output;
        node.outputs.push_back(std::move(outChain));
    }
    return node;
}

struct MathOpSpec final {
    const char* op;
    const char* title;
    const char* description;
    std::size_t arity;
    PT outType;
};

constexpr std::array<MathOpSpec, 21> kMathOpSpecs{{
    {"ValueFloat", "Value: Float", "Outputs a constant scalar value.", 0U, PT::Float},
    {"ParticleAge", "Particle Age", "Current particle age in seconds.", 0U, PT::Float},
    {"NormalizedAge", "Normalized Age", "Particle age divided by lifetime in [0, 1].", 0U, PT::Float},
    {"ParticleSpeed", "Particle Speed", "Magnitude of the particle's velocity vector.", 0U, PT::Float},
    {"EmitterAge", "Emitter Age", "Current emitter age in seconds.", 0U, PT::Float},
    {"RandomPerParticle", "Random (Per Particle)", "Deterministic [0, 1) random value per particle.", 0U, PT::Float},
    {"Add", "Add", "Adds two values (A + B).", 2U, PT::Float},
    {"Subtract", "Subtract", "Subtracts B from A (A - B).", 2U, PT::Float},
    {"Multiply", "Multiply", "Multiplies two values (A * B).", 2U, PT::Float},
    {"Divide", "Divide", "Safely divides A by B (returns 0 when |B| < 1e-6).", 2U, PT::Float},
    {"Lerp", "Lerp", "Linearly interpolates between A and B by T.", 3U, PT::Float},
    {"Clamp", "Clamp", "Clamps Value to [Min, Max].", 3U, PT::Float},
    {"Remap", "Remap", "Remaps Value from [In0, In1] to [Out0, Out1].", 5U, PT::Float},
    {"Sin", "Sin", "Computes the sine of an angle in radians.", 1U, PT::Float},
    {"Cos", "Cos", "Computes the cosine of an angle in radians.", 1U, PT::Float},
    {"Abs", "Abs", "Computes the absolute value |X|.", 1U, PT::Float},
    {"Min", "Min", "Returns the smaller of A and B.", 2U, PT::Float},
    {"Max", "Max", "Returns the larger of A and B.", 2U, PT::Float},
    {"Power", "Power", "Raises Base to Exponent.", 2U, PT::Float},
    {"OneMinus", "One Minus", "Computes 1 - X.", 1U, PT::Float},
    {"Step", "Step", "Returns 1 when X >= Edge, else 0.", 2U, PT::Float},
}};

[[nodiscard]] const MathOpSpec* FindMathOpSpec(const std::string_view op) noexcept {
    for (const MathOpSpec& spec : kMathOpSpecs) {
        if (op == spec.op) {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] prism::BlueprintNode MakeMathGraphNode(const Asset::ParticleMathNode& mathNode) {
    const MathOpSpec* const spec = FindMathOpSpec(mathNode.op);
    prism::BlueprintNode node;
    node.id = static_cast<int>(mathNode.id);
    node.title = spec != nullptr ? std::string(spec->title) : mathNode.op;
    node.subtitle = "Math / Value";
    node.category = prism::BlueprintNodeCategory::PureMath;
    node.style = prism::BlueprintNodeStyle::Header;
    node.pos = prism::Vec2{mathNode.graphX, mathNode.graphY};
    node.typeKey = "vfx_math:" + mathNode.op;
    node.tooltip = spec != nullptr ? std::string(spec->description) : "VFX math / value node.";

    if (mathNode.op == "ValueFloat" || mathNode.op.rfind("UserParam:", 0U) == 0U) {
        prism::BlueprintPin valPin;
        valPin.name = "Value";
        valPin.type = PT::Float;
        valPin.kind = PK::Input;
        valPin.defaultValue = FormatFloat(mathNode.constant);
        valPin.editable = mathNode.op == "ValueFloat";
        node.inputs.push_back(std::move(valPin));
    } else {
        const std::size_t arity = spec != nullptr ? spec->arity : mathNode.inputs.size();
        static constexpr std::array<const char*, 5> kPinNames{"A", "B", "C", "D", "E"};
        for (std::size_t i = 0U; i < arity; ++i) {
            prism::BlueprintPin inPin;
            inPin.name = i < kPinNames.size() ? kPinNames[i] : ("In" + std::to_string(i));
            inPin.type = PT::Float;
            inPin.kind = PK::Input;
            inPin.defaultValue = FormatFloat(i == 0U ? mathNode.constant : 0.0F);
            inPin.editable = true;
            node.inputs.push_back(std::move(inPin));
        }
    }

    prism::BlueprintPin outPin;
    outPin.name = "Result";
    outPin.type = spec != nullptr ? spec->outType : PT::Float;
    outPin.kind = PK::Output;
    node.outputs.push_back(std::move(outPin));
    return node;
}

} // namespace

void BuildEmitterGraph(const Asset::ParticleSystemAsset& system,
                          const std::size_t emitterIndex,
                          prism::BlueprintGraph& graph) {
    graph.clear();
    if (emitterIndex >= system.emitters.size()) {
        return;
    }
    const Asset::ParticleEmitterAsset& emitter = system.emitters[emitterIndex];

    const auto addStageChain = [&](const Asset::ParticleStage stage,
                                   const std::vector<Asset::ParticleModule>& modules) {
        const float rowY = StageRowY(stage);
        float maxModuleX = kFirstModuleX;
        std::vector<int> chainNodeIds;
        chainNodeIds.reserve(modules.size());

        for (std::size_t i = 0U; i < modules.size(); ++i) {
            const Asset::ParticleModule& mod = modules[i];
            const Asset::ParticleModuleDescriptor* const desc = Asset::FindParticleModuleDescriptor(mod.type);
            if (desc == nullptr) {
                continue;
            }
            prism::BlueprintNode node = MakeModuleGraphNode(*desc, &mod);
            const bool unplaced = !mod.graphPosAtOrigin &&
                                  std::abs(node.pos.x) < 1.0e-4F && std::abs(node.pos.y) < 1.0e-4F;
            if (unplaced) {
                node.pos = prism::Vec2{kFirstModuleX + static_cast<float>(i) * kModuleSpacingX, rowY};
            }
            maxModuleX = std::max(maxModuleX, node.pos.x + kModuleSpacingX);
            chainNodeIds.push_back(node.id);
            graph.addNode(std::move(node));
        }

        const std::size_t stageIdx = static_cast<std::size_t>(stage);
        const Asset::ParticleStageNodePos& savedStagePos = emitter.stageNodePositions[stageIdx];
        const float stageX = savedStagePos.placed ? savedStagePos.x : std::max(kStageNodeX, maxModuleX + 60.0F);
        const float stageY = savedStagePos.placed ? savedStagePos.y : rowY;
        graph.addNode(MakeStageOutputNode(stage, stageX, stageY));

        const int stageNodeId = StageNodeIdFor(stage);
        for (std::size_t i = 0U; i < chainNodeIds.size(); ++i) {
            const int fromId = chainNodeIds[i];
            const int toId = (i + 1U < chainNodeIds.size()) ? chainNodeIds[i + 1U] : stageNodeId;
            graph.connectPins(fromId, 0, toId, 0, PT::Link);
        }
    };

    addStageChain(Asset::ParticleStage::EmitterUpdate, emitter.emitterUpdate);
    addStageChain(Asset::ParticleStage::ParticleSpawn, emitter.particleSpawn);
    addStageChain(Asset::ParticleStage::ParticleUpdate, emitter.particleUpdate);
    addStageChain(Asset::ParticleStage::Render, emitter.render);

    // Detached modules are real nodes that simply are not wired into a stage output node.
    {
        std::size_t slot = 0U;
        std::set<std::uint32_t> detachedIds;
        for (const Asset::ParticleModule& mod : emitter.detached) {
            const Asset::ParticleModuleDescriptor* const desc = Asset::FindParticleModuleDescriptor(mod.type);
            if (desc == nullptr || graph.findNode(static_cast<int>(mod.id)) != nullptr) {
                continue;
            }
            prism::BlueprintNode node = MakeModuleGraphNode(*desc, &mod);
            const bool unplaced = !mod.graphPosAtOrigin &&
                                  std::abs(node.pos.x) < 1.0e-4F && std::abs(node.pos.y) < 1.0e-4F;
            if (unplaced) {
                node.pos = prism::Vec2{kFirstModuleX + static_cast<float>(slot) * kModuleSpacingX,
                                       kDetachedRowY};
            }
            ++slot;
            detachedIds.insert(mod.id);
            graph.addNode(std::move(node));
        }

        // Restore any preserved chain wires between detached modules so cutting one wire does not
        // wipe remaining upstream connections.
        for (const Asset::ParticleDetachedLink& link : emitter.detachedLinks) {
            if (link.fromModuleId == 0U || link.toModuleId == 0U || link.fromModuleId == link.toModuleId) {
                continue;
            }
            if (!detachedIds.contains(link.fromModuleId) || !detachedIds.contains(link.toModuleId)) {
                continue;
            }
            const int fromId = static_cast<int>(link.fromModuleId);
            const int toId = static_cast<int>(link.toModuleId);
            if (graph.findNode(fromId) != nullptr && graph.findNode(toId) != nullptr) {
                graph.connectPins(fromId, 0, toId, 0, PT::Link);
            }
        }
    }

    // Add any Math / Value nodes and connect their wires.
    for (const Asset::ParticleMathNode& mathNode : emitter.mathNodes) {
        graph.addNode(MakeMathGraphNode(mathNode));
    }
    for (const Asset::ParticleMathNode& mathNode : emitter.mathNodes) {
        for (std::size_t pinIdx = 0U; pinIdx < mathNode.inputs.size(); ++pinIdx) {
            const std::uint32_t srcId = mathNode.inputs[pinIdx];
            if (srcId != 0U && graph.findNode(static_cast<int>(srcId)) != nullptr) {
                graph.connectPins(static_cast<int>(srcId), 0, static_cast<int>(mathNode.id),
                                  static_cast<int>(pinIdx), PT::Float);
            }
        }
    }

    // Connect Math/Value node wires into module inputs where `kind == ParticleValueKind::Wire`.
    const auto connectModuleWires = [&](const std::vector<Asset::ParticleModule>& modules) {
        for (const Asset::ParticleModule& mod : modules) {
            const prism::BlueprintNode* const gnode = graph.findNode(static_cast<int>(mod.id));
            if (gnode == nullptr) {
                continue;
            }
            for (const auto& [inputName, floatIn] : mod.floats) {
                if (floatIn.kind != Asset::ParticleValueKind::Wire || floatIn.wireNode == 0U) {
                    continue;
                }
                for (std::size_t pinIdx = 1U; pinIdx < gnode->inputs.size(); ++pinIdx) {
                    if (gnode->inputs[pinIdx].name == inputName) {
                        graph.connectPins(static_cast<int>(floatIn.wireNode), 0, gnode->id,
                                          static_cast<int>(pinIdx), PT::Float);
                        break;
                    }
                }
            }
            for (const auto& [inputName, vecIn] : mod.vectors) {
                if (vecIn.kind != Asset::ParticleValueKind::Wire || vecIn.wireNode == 0U) {
                    continue;
                }
                for (std::size_t pinIdx = 1U; pinIdx < gnode->inputs.size(); ++pinIdx) {
                    if (gnode->inputs[pinIdx].name == inputName) {
                        graph.connectPins(static_cast<int>(vecIn.wireNode), 0, gnode->id,
                                          static_cast<int>(pinIdx), PT::Vector);
                        break;
                    }
                }
            }
        }
    };
    connectModuleWires(emitter.emitterUpdate);
    connectModuleWires(emitter.particleSpawn);
    connectModuleWires(emitter.particleUpdate);
    connectModuleWires(emitter.render);
    connectModuleWires(emitter.detached);
}

bool ExtractEmitterFromGraph(const prism::BlueprintGraph& graph,
                                const Asset::ParticleSystemAsset& currentSystem,
                                const std::size_t emitterIndex,
                                Asset::ParticleEmitterAsset& outEmitter) {
    if (emitterIndex >= currentSystem.emitters.size()) {
        return false;
    }
    const Asset::ParticleEmitterAsset& prevEmitter = currentSystem.emitters[emitterIndex];
    outEmitter = prevEmitter;
    outEmitter.emitterUpdate.clear();
    outEmitter.particleSpawn.clear();
    outEmitter.particleUpdate.clear();
    outEmitter.render.clear();
    outEmitter.detached.clear();
    outEmitter.detachedLinks.clear();
    outEmitter.stageNodePositions = {};
    outEmitter.mathNodes.clear();

    // Index existing modules by id so curves, gradients, random ranges, and enabled states are preserved.
    std::unordered_map<std::uint32_t, const Asset::ParticleModule*> prevModules;
    const auto indexStage = [&](const std::vector<Asset::ParticleModule>& list) {
        for (const Asset::ParticleModule& m : list) {
            prevModules[m.id] = &m;
        }
    };
    indexStage(prevEmitter.emitterUpdate);
    indexStage(prevEmitter.particleSpawn);
    indexStage(prevEmitter.particleUpdate);
    indexStage(prevEmitter.render);
    indexStage(prevEmitter.detached);

    std::unordered_map<std::uint32_t, const Asset::ParticleMathNode*> prevMathNodes;
    for (const Asset::ParticleMathNode& m : prevEmitter.mathNodes) {
        prevMathNodes[m.id] = &m;
    }

    // Build lookup of incoming wires: (toNodeId, toPinIndex) -> fromNodeId.
    std::map<std::pair<int, int>, int> incomingWire;
    for (const prism::BlueprintWire& wire : graph.wires()) {
        incomingWire[{wire.toNodeId, wire.toPinIndex}] = wire.fromNodeId;
    }

    // Build all module nodes present in `graph` and group them by stage.
    std::unordered_map<int, Asset::ParticleModule> builtModules;
    std::unordered_map<int, Asset::ParticleStage> moduleStage;

    for (const prism::BlueprintNode& gnode : graph.nodes()) {
        if (IsVfxStageNodeId(gnode.id)) {
            std::optional<Asset::ParticleStage> stageOpt;
            switch (gnode.id) {
                case kVfxStageNodeEmitterUpdate: stageOpt = Asset::ParticleStage::EmitterUpdate; break;
                case kVfxStageNodeParticleSpawn: stageOpt = Asset::ParticleStage::ParticleSpawn; break;
                case kVfxStageNodeParticleUpdate: stageOpt = Asset::ParticleStage::ParticleUpdate; break;
                case kVfxStageNodeRender: stageOpt = Asset::ParticleStage::Render; break;
                default: break;
            }
            if (stageOpt.has_value()) {
                const std::size_t stageIdx = static_cast<std::size_t>(*stageOpt);
                const Asset::ParticleStageNodePos& prevPos = prevEmitter.stageNodePositions[stageIdx];
                if (prevPos.placed &&
                    std::abs(prevPos.x - gnode.pos.x) < 1.0e-3F &&
                    std::abs(prevPos.y - gnode.pos.y) < 1.0e-3F) {
                    outEmitter.stageNodePositions[stageIdx] = prevPos;
                } else {
                    outEmitter.stageNodePositions[stageIdx] = Asset::ParticleStageNodePos{true, gnode.pos.x, gnode.pos.y};
                }
            }
            continue;
        }
        if (gnode.id <= 0) {
            continue;
        }
        if (gnode.typeKey.rfind("vfx_math:", 0U) == 0U) {
            Asset::ParticleMathNode mathNode;
            mathNode.id = static_cast<std::uint32_t>(gnode.id);
            mathNode.op = gnode.typeKey.substr(9U);
            mathNode.graphX = gnode.pos.x;
            mathNode.graphY = gnode.pos.y;
            if (const auto it = prevMathNodes.find(mathNode.id); it != prevMathNodes.end()) {
                mathNode.constant = it->second->constant;
            }
            if (!gnode.inputs.empty()) {
                float parsed = 0.0F;
                if (TryParseFloat(gnode.inputs[0].defaultValue, parsed)) {
                    mathNode.constant = parsed;
                }
            }
            const MathOpSpec* const spec = FindMathOpSpec(mathNode.op);
            const std::size_t arity = spec != nullptr ? spec->arity : gnode.inputs.size();
            mathNode.inputs.assign(arity, 0U);
            for (std::size_t pinIdx = 0U; pinIdx < arity; ++pinIdx) {
                if (const auto wireIt = incomingWire.find({gnode.id, static_cast<int>(pinIdx)});
                    wireIt != incomingWire.end() && wireIt->second > 0) {
                    mathNode.inputs[pinIdx] = static_cast<std::uint32_t>(wireIt->second);
                }
            }
            outEmitter.mathNodes.push_back(std::move(mathNode));
            continue;
        }

        const Asset::ParticleModuleDescriptor* desc = nullptr;
        if (gnode.typeKey.rfind("vfx_module:", 0U) == 0U) {
            desc = Asset::FindParticleModuleDescriptorByName(gnode.typeKey.substr(11U));
        }
        if (desc == nullptr) {
            for (const Asset::ParticleModuleDescriptor& candidate : Asset::GetParticleModuleCatalog()) {
                if (candidate.displayName == gnode.title || candidate.canonicalName == gnode.title) {
                    desc = &candidate;
                    break;
                }
            }
        }
        if (desc == nullptr) {
            continue;
        }

        const auto prevIt = prevModules.find(static_cast<std::uint32_t>(gnode.id));
        Asset::ParticleModule mod =
            prevIt != prevModules.end() && prevIt->second->type == desc->type
                ? *prevIt->second
                : Asset::MakeDefaultParticleModule(static_cast<std::uint32_t>(gnode.id), desc->type,
                                                     gnode.pos.x, gnode.pos.y);

        // Preserve exact coordinates if the node was not dragged from its initial integer/float placement.
        if (prevIt != prevModules.end() &&
            std::abs(prevIt->second->graphX - gnode.pos.x) < 1.0e-3F &&
            std::abs(prevIt->second->graphY - gnode.pos.y) < 1.0e-3F) {
            mod.graphX = prevIt->second->graphX;
            mod.graphY = prevIt->second->graphY;
        } else {
            mod.graphX = gnode.pos.x;
            mod.graphY = gnode.pos.y;
        }
        mod.graphPosAtOrigin = (std::abs(mod.graphX) < 1.0e-4F && std::abs(mod.graphY) < 1.0e-4F);

        // Update pins that were edited on the graph or wired to Math/Value nodes.
        for (std::size_t pinIdx = 1U; pinIdx < gnode.inputs.size(); ++pinIdx) {
            const prism::BlueprintPin& pin = gnode.inputs[pinIdx];
            const auto wireIt = incomingWire.find({gnode.id, static_cast<int>(pinIdx)});
            const std::uint32_t wiredNodeId =
                (wireIt != incomingWire.end() && wireIt->second > 0) ? static_cast<std::uint32_t>(wireIt->second) : 0U;

            if (desc->defaultFloats.contains(pin.name) && pin.type == PT::Float) {
                const Asset::ParticleFloatInput& defVal = desc->defaultFloats.at(pin.name);
                Asset::ParticleFloatInput currentVal = defVal;
                if (const auto existing = mod.floats.find(pin.name); existing != mod.floats.end()) {
                    currentVal = existing->second;
                }
                if (wiredNodeId != 0U) {
                    currentVal.kind = Asset::ParticleValueKind::Wire;
                    currentVal.wireNode = wiredNodeId;
                    mod.floats[pin.name] = currentVal;
                } else {
                    if (currentVal.kind == Asset::ParticleValueKind::Wire) {
                        // The wire was removed: the input is a constant again (its pin text), and
                        // the asset must stop naming the Math node.
                        currentVal.kind = Asset::ParticleValueKind::Constant;
                        currentVal.wireNode = 0U;
                        mod.floats[pin.name] = currentVal;
                    }
                    if (currentVal.kind == Asset::ParticleValueKind::Constant) {
                        float parsed = currentVal.constant;
                        if (TryParseFloat(pin.defaultValue, parsed) &&
                            std::abs(parsed - currentVal.constant) > 1.0e-5F) {
                            currentVal.constant = parsed;
                            mod.floats[pin.name] = currentVal;
                        }
                    }
                }
            } else if (desc->defaultVectors.contains(pin.name) && pin.type == PT::Vector) {
                const Asset::ParticleVectorInput& defVal = desc->defaultVectors.at(pin.name);
                Asset::ParticleVectorInput currentVal = defVal;
                if (const auto existing = mod.vectors.find(pin.name); existing != mod.vectors.end()) {
                    currentVal = existing->second;
                }
                if (wiredNodeId != 0U) {
                    currentVal.kind = Asset::ParticleValueKind::Wire;
                    currentVal.wireNode = wiredNodeId;
                    mod.vectors[pin.name] = currentVal;
                } else {
                    if (currentVal.kind == Asset::ParticleValueKind::Wire) {
                        // The wire was removed: the input is a constant again (its pin text), and
                        // the asset must stop naming the Math node.
                        currentVal.kind = Asset::ParticleValueKind::Constant;
                        currentVal.wireNode = 0U;
                        mod.vectors[pin.name] = currentVal;
                    }
                    if (currentVal.kind == Asset::ParticleValueKind::Constant) {
                        Math::Vector3 parsed = currentVal.constant;
                        if (TryParseVec3(pin.defaultValue, parsed) &&
                            (std::abs(parsed.x - currentVal.constant.x) > 1.0e-5F ||
                             std::abs(parsed.y - currentVal.constant.y) > 1.0e-5F ||
                             std::abs(parsed.z - currentVal.constant.z) > 1.0e-5F)) {
                            currentVal.constant = parsed;
                            mod.vectors[pin.name] = currentVal;
                        }
                    }
                }
            } else if (desc->defaultColors.contains(pin.name) && pin.type == PT::Color) {
                const Asset::ParticleColorInput& defVal = desc->defaultColors.at(pin.name);
                Asset::ParticleColorInput currentVal = defVal;
                if (const auto existing = mod.colors.find(pin.name); existing != mod.colors.end()) {
                    currentVal = existing->second;
                }
                if (currentVal.kind == Asset::ParticleValueKind::Constant) {
                    Math::Color parsedColor = currentVal.constant;
                    float parsedAlpha = currentVal.alpha;
                    if (TryParseColor(pin.defaultValue, parsedColor, parsedAlpha) &&
                        (std::abs(parsedColor.r - currentVal.constant.r) > 1.0e-5F ||
                         std::abs(parsedColor.g - currentVal.constant.g) > 1.0e-5F ||
                         std::abs(parsedColor.b - currentVal.constant.b) > 1.0e-5F ||
                         std::abs(parsedAlpha - currentVal.alpha) > 1.0e-5F)) {
                        currentVal.constant = parsedColor;
                        currentVal.alpha = parsedAlpha;
                        mod.colors[pin.name] = currentVal;
                    }
                }
            } else if (desc->defaultInts.contains(pin.name) && pin.type == PT::Int) {
                const std::int32_t defVal = desc->defaultInts.at(pin.name);
                std::int32_t currentVal = defVal;
                if (const auto existing = mod.ints.find(pin.name); existing != mod.ints.end()) {
                    currentVal = existing->second;
                }
                std::int32_t parsed = currentVal;
                if (TryParseInt(pin.defaultValue, parsed) && parsed != currentVal) {
                    mod.ints[pin.name] = parsed;
                }
            } else if (desc->defaultStrings.contains(pin.name) && pin.type == PT::String) {
                const std::string& defVal = desc->defaultStrings.at(pin.name);
                std::string currentVal = defVal;
                if (const auto existing = mod.strings.find(pin.name); existing != mod.strings.end()) {
                    currentVal = existing->second;
                }
                if (pin.defaultValue != currentVal) {
                    mod.strings[pin.name] = pin.defaultValue;
                }
            }
        }

        moduleStage[gnode.id] = desc->stage;
        builtModules.emplace(gnode.id, std::move(mod));
    }

    // The graph is the source of truth: a stage's module list is exactly the chain that reaches the
    // stage output node, walked backward through `PT::Link` wires (pin 0 of every node). A module that
    // is not on a chain contributes nothing; it is kept in `detached` (along with any chain wires
    // between detached modules in `detachedLinks`) so authoring work is never lost.
    std::set<int> chained;
    const auto extractStageChain = [&](const Asset::ParticleStage stage,
                                       std::vector<Asset::ParticleModule>& outStageList) {
        const int stageNodeId = StageNodeIdFor(stage);
        std::vector<int> chainRev;
        std::set<int> visited;
        int cursor = stageNodeId;
        while (true) {
            const auto wireIt = incomingWire.find({cursor, 0});
            if (wireIt == incomingWire.end()) {
                break;
            }
            const int prevNodeId = wireIt->second;
            if (visited.contains(prevNodeId)) {
                break;
            }
            const auto stageIt = moduleStage.find(prevNodeId);
            if (stageIt == moduleStage.end() || stageIt->second != stage) {
                break;
            }
            visited.insert(prevNodeId);
            chainRev.push_back(prevNodeId);
            cursor = prevNodeId;
        }
        std::reverse(chainRev.begin(), chainRev.end());
        for (const int id : chainRev) {
            outStageList.push_back(builtModules.at(id));
            chained.insert(id);
        }
    };

    extractStageChain(Asset::ParticleStage::EmitterUpdate, outEmitter.emitterUpdate);
    extractStageChain(Asset::ParticleStage::ParticleSpawn, outEmitter.particleSpawn);
    extractStageChain(Asset::ParticleStage::ParticleUpdate, outEmitter.particleUpdate);
    extractStageChain(Asset::ParticleStage::Render, outEmitter.render);

    std::vector<int> unchained;
    for (const auto& [id, mod] : builtModules) {
        static_cast<void>(mod);
        if (!chained.contains(id)) {
            unchained.push_back(id);
        }
    }
    std::sort(unchained.begin(), unchained.end());
    std::set<int> unchainedSet(unchained.begin(), unchained.end());
    for (const int id : unchained) {
        outEmitter.detached.push_back(builtModules.at(id));
    }

    // Preserve chain wires (`Out` pin 0 -> `In` pin 0) connecting detached modules of the same stage.
    std::set<std::pair<std::uint32_t, std::uint32_t>> seenDetachedLinks;
    for (const prism::BlueprintWire& wire : graph.wires()) {
        if (wire.fromPinIndex != 0 || wire.toPinIndex != 0 || wire.fromNodeId == wire.toNodeId) {
            continue;
        }
        if (!unchainedSet.contains(wire.fromNodeId) || !unchainedSet.contains(wire.toNodeId)) {
            continue;
        }
        const auto fromStageIt = moduleStage.find(wire.fromNodeId);
        const auto toStageIt = moduleStage.find(wire.toNodeId);
        if (fromStageIt == moduleStage.end() || toStageIt == moduleStage.end() ||
            fromStageIt->second != toStageIt->second) {
            continue;
        }
        const auto key = std::make_pair(static_cast<std::uint32_t>(wire.fromNodeId),
                                        static_cast<std::uint32_t>(wire.toNodeId));
        if (seenDetachedLinks.insert(key).second &&
            outEmitter.detachedLinks.size() < Asset::kMaximumParticleDetachedLinks) {
            outEmitter.detachedLinks.push_back(Asset::ParticleDetachedLink{key.first, key.second});
        }
    }
    std::sort(outEmitter.detachedLinks.begin(), outEmitter.detachedLinks.end(),
              [](const Asset::ParticleDetachedLink& a, const Asset::ParticleDetachedLink& b) {
                  return a.fromModuleId != b.fromModuleId ? a.fromModuleId < b.fromModuleId
                                                          : a.toModuleId < b.toModuleId;
              });
    return true;
}

std::vector<prism::BlueprintNodeSpec> BuildVfxGraphNodeLibrary(
    const Asset::ParticleSystemAsset& system,
    const bool includeMathNodes) {
    static_cast<void>(system);
    std::vector<prism::BlueprintNodeSpec> library;
    const std::vector<Asset::ParticleModuleDescriptor>& catalog = Asset::GetParticleModuleCatalog();
    library.reserve(catalog.size() + (includeMathNodes ? kMathOpSpecs.size() : 0U));

    for (const Asset::ParticleModuleDescriptor& desc : catalog) {
        prism::BlueprintNodeSpec spec;
        spec.category = StageDisplayName(desc.stage);
        spec.name = std::string(desc.displayName);
        spec.keywords = std::string(desc.canonicalName) + " " + std::string(desc.description);
        spec.make = [&desc] { return MakeModuleGraphNode(desc, nullptr); };
        library.push_back(std::move(spec));
    }

    if (includeMathNodes) {
        for (const MathOpSpec& mathSpec : kMathOpSpecs) {
            prism::BlueprintNodeSpec spec;
            spec.category = "Math / Value";
            spec.name = mathSpec.title;
            spec.keywords = std::string(mathSpec.op) + " " + mathSpec.description;
            spec.make = [mathSpec] {
                Asset::ParticleMathNode dummy;
                dummy.op = mathSpec.op;
                dummy.constant = 0.0F;
                return MakeMathGraphNode(dummy);
            };
            library.push_back(std::move(spec));
        }
    }

    return library;
}

int SpawnVfxGraphNode(prism::BlueprintGraph& graph, const prism::BlueprintNodeSpec& spec,
                         const prism::Vec2 world) {
    if (!spec.make) {
        return -1;
    }
    prism::BlueprintNode node = spec.make();
    int id = 1;
    for (const prism::BlueprintNode& existing : graph.nodes()) {
        if (!IsVfxStageNodeId(existing.id)) {
            id = std::max(id, existing.id + 1);
        }
    }
    while (IsVfxStageNodeId(id) || graph.findNode(id) != nullptr) {
        ++id;
    }
    node.id = id;
    node.pos = world;
    graph.addNode(std::move(node));
    return id;
}

bool CanConnectVfxPins(const prism::BlueprintGraph& graph, const int fromNode, const int fromPin,
                          const int toNode, const int toPin, std::string* const why) {
    const auto fail = [&](const char* message) {
        if (why != nullptr) {
            *why = message;
        }
        return false;
    };
    const prism::BlueprintNode* const from = graph.findNode(fromNode);
    const prism::BlueprintNode* const to = graph.findNode(toNode);
    if (from == nullptr || to == nullptr || fromPin < 0 || toPin < 0 ||
        static_cast<std::size_t>(fromPin) >= from->outputs.size() ||
        static_cast<std::size_t>(toPin) >= to->inputs.size()) {
        return true; // the graph's own checks report unknown nodes and pins
    }
    const auto stageOf = [](const prism::BlueprintNode& node) -> std::optional<Asset::ParticleStage> {
        switch (node.id) {
            case kVfxStageNodeEmitterUpdate: return Asset::ParticleStage::EmitterUpdate;
            case kVfxStageNodeParticleSpawn: return Asset::ParticleStage::ParticleSpawn;
            case kVfxStageNodeParticleUpdate: return Asset::ParticleStage::ParticleUpdate;
            case kVfxStageNodeRender: return Asset::ParticleStage::Render;
            default: break;
        }
        if (node.typeKey.rfind("vfx_module:", 0U) == 0U) {
            if (const Asset::ParticleModuleDescriptor* const desc =
                    Asset::FindParticleModuleDescriptorByName(node.typeKey.substr(11U))) {
                return desc->stage;
            }
        }
        return std::nullopt;
    };

    if (from->outputs[static_cast<std::size_t>(fromPin)].type == PT::Link) {
        if (IsVfxStageNodeId(fromNode)) {
            return fail("A stage output node has no outputs");
        }
        const auto fromStage = stageOf(*from);
        const auto toStage = stageOf(*to);
        if (!fromStage || !toStage) {
            return fail("Chain links only join VFX modules");
        }
        if (*fromStage != *toStage) {
            return fail("A module can only be chained inside its own stage");
        }
        if (graph.isPinConnected(fromNode, fromPin, false)) {
            for (const prism::BlueprintWire& wire : graph.wires()) {
                if (wire.fromNodeId == fromNode && wire.fromPinIndex == fromPin &&
                    !(wire.toNodeId == toNode && wire.toPinIndex == toPin)) {
                    return fail("This module already feeds another node; disconnect it first");
                }
            }
        }
        return true;
    }

    // Data wire: Math / Value result into a module or Math input.
    if (to->typeKey.rfind("vfx_module:", 0U) == 0U) {
        if (from->typeKey.rfind("vfx_math:", 0U) != 0U) {
            return fail("Only Math / Value nodes can feed module inputs");
        }
        if (to->inputs[static_cast<std::size_t>(toPin)].type != PT::Float) {
            return fail("Only Float inputs can be wired");
        }
    }
    return true;
}

void ApplyVfxCompileMessagesToGraph(prism::BlueprintGraph& graph,
                                       const std::vector<VFX::ParticleCompileMessage>& messages) {
    prism::BlueprintCompileReport report;
    for (const VFX::ParticleCompileMessage& msg : messages) {
        const bool isError = msg.severity == VFX::ParticleCompileMessage::Severity::Error;
        if (isError) {
            ++report.errors;
        } else {
            ++report.warnings;
        }
        report.messages.push_back((isError ? "[Error] " : "[Warning] ") + msg.text);

        int targetNodeId = static_cast<int>(msg.moduleId);
        if (targetNodeId <= 0 || graph.findNode(targetNodeId) == nullptr) {
            // Attach emitter-level renderer errors to the fixed Render stage output node so the canvas
            // always shows a visible badge when all renderers are removed or invalid.
            if (msg.text.find("renderer") != std::string::npos ||
                msg.text.find("Renderer") != std::string::npos) {
                targetNodeId = kVfxStageNodeRender;
            } else if (msg.text.find("Spawn") != std::string::npos ||
                       msg.text.find("spawn") != std::string::npos) {
                targetNodeId = kVfxStageNodeEmitterUpdate;
            } else {
                targetNodeId = kVfxStageNodeParticleUpdate;
            }
        }
        if (graph.findNode(targetNodeId) != nullptr) {
            auto& issue = report.nodeIssues[targetNodeId];
            if (!issue.empty()) {
                issue += "\n";
            }
            issue += msg.text;
        }
    }
    graph.setCompileResult(std::move(report));
}

} // namespace UVE::Editor
