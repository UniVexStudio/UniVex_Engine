// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_nodes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blueprint_ai_nodes.h"
#include "blueprint_animation_nodes.h"
#include "blueprint_audio_nodes.h"
#include "blueprint_character_nodes.h"
#include "blueprint_events_nodes.h"
#include "blueprint_flow_nodes.h"
#include "blueprint_game_nodes.h"
#include "blueprint_gameplay_nodes.h"
#include "blueprint_input_nodes.h"
#include "blueprint_math_nodes.h"
#include "blueprint_networking_nodes.h"
#include "blueprint_node_builder.h"
#include "blueprint_physics_nodes.h"
#include "blueprint_ragdoll_nodes.h"
#include "blueprint_rendering_nodes.h"
#include "blueprint_transform_nodes.h"
#include "blueprint_ui_nodes.h"
#include "blueprint_utilities_nodes.h"
#include "blueprint_variables_nodes.h"
#include "blueprint_world_nodes.h"

namespace UVE::Asset {
namespace {

/// Where each node belongs: an object's graph or a widget's, and the component it needs.
void AssignScopes(std::vector<BlueprintNodeType>& all) {
    constexpr auto object = static_cast<std::uint8_t>(BlueprintGraphKind::Object);
    constexpr auto widget = static_cast<std::uint8_t>(BlueprintGraphKind::Widget);
    for (BlueprintNodeType& n : all) {
        const std::string& c = n.category;
        if (n.event == "custom_event" || n.event == "timer" || c.starts_with("Utilities") || c == "Game" || c.starts_with("Game|")) {
            n.graphs = object | widget;
        } else if (c.starts_with("User Interface")) {
            // A widget's own controls; creating a widget or the mouse cursor work from any graph.
            const bool anywhere = n.key == "ui_create_widget" || n.key == "ui_add_to_viewport" || n.key == "ui_show_mouse_cursor";
            n.graphs = anywhere ? (object | widget) : widget;
        } else if (c == "Character" || c == "Physics" || c.starts_with("Physics|") || c == "World" || c == "Effects" || c == "AI" ||
                   c.starts_with("Animation|") || c.starts_with("Rendering|") || c == "Animation" || c == "Audio" || c == "Transform" || c == "Rendering" ||
                   c.starts_with("Gameplay") ||
                   (c == "Events" && n.event != "ready" && n.event != "tick")) {
            n.graphs = object;
        }
        if (c == "Character" || n.key == "is_grounded") {
            n.needs = "CharacterController";
        } else if (n.key.starts_with("ragdoll_")) {
            n.needs = "Ragdoll3D";
        } else if (n.key.starts_with("apply_")) {
            n.needs = "Rigid3D";
        } else if (c == "Audio") {
            n.needs = "AudioSource";
        } else if (c == "Effects") {
            n.needs = "ParticleEmitter";
        } else if (c == "Rendering|Material") {
            n.needs = "SurfaceInstance";
        } else if (c == "Animation|Sequencer") {
            n.needs = "AnimationSequencer";
        } else if (c == "AI") {
            n.needs = "NavSeeker3D";
        } else if (n.key.find("light") != std::string::npos) {
            n.needs = "Light";
        } else if (n.key.find("field_of_view") != std::string::npos) {
            n.needs = "Camera";
        } else if (n.key.starts_with("attr_")) {
            n.needs = "GameplayAttributes";
        } else if (n.key.starts_with("tag_")) {
            n.needs = "GameplayTag";
        }
    }
}

void RegisterNetworkingCatalogueNodes(std::vector<BlueprintNodeType>& out) {
    using T = BlueprintPinType;
    using Pin = BlueprintPinSpec;
    Blueprint::Nodes::BlueprintNodeBuilder b{out};
    const auto toPinType = [](const Blueprint::Nodes::Networking::NetworkingPinType t) {
        using NT = Blueprint::Nodes::Networking::NetworkingPinType;
        switch (t) {
            case NT::Bool: return T::Bool;
            case NT::Int: return T::Int;
            case NT::Float: return T::Float;
            case NT::String: return T::String;
            case NT::Vector: return T::Vector;
        }
        return T::Int;
    };
    for (const auto& spec : Blueprint::Nodes::Networking::GetNetworkingBlueprintNodes()) {
        std::vector<Pin> inPins;
        inPins.reserve(spec.inputs.size());
        for (const auto& p : spec.inputs) {
            inPins.push_back(Pin{std::string(p.name), toPinType(p.type), std::string(p.defaultText)});
        }
        if (spec.kind == Blueprint::Nodes::Networking::NetworkingNodeKind::Action) {
            b.Action(std::string(spec.key), std::string(spec.title), std::string(spec.category),
                     std::move(inPins), std::string(spec.expression), std::string(spec.keywords),
                     std::string(spec.tooltip));
        } else {
            std::vector<Pin> outPins;
            outPins.reserve(spec.outputs.size());
            for (const auto& p : spec.outputs) {
                outPins.push_back(Pin{std::string(p.name), toPinType(p.type), std::string(p.defaultText)});
            }
            b.Pure(std::string(spec.key), std::string(spec.title), std::string(spec.category),
                   std::move(inPins), std::move(outPins), {std::string(spec.expression)},
                   std::string(spec.keywords), std::string(spec.tooltip));
        }
    }
}

[[nodiscard]] std::vector<BlueprintNodeType> BuildCatalogue() {
    std::vector<BlueprintNodeType> all;
    all.reserve(380U);

    Blueprint::Nodes::RegisterEventsBlueprintNodes(all);
    Blueprint::Nodes::RegisterFlowBlueprintNodes(all);
    Blueprint::Nodes::RegisterUtilitiesBlueprintNodes(all);
    Blueprint::Nodes::RegisterTransformBlueprintNodes(all);
    Blueprint::Nodes::RegisterPhysicsBlueprintNodes(all);
    Blueprint::Nodes::RegisterRagdollBlueprintNodes(all);
    Blueprint::Nodes::RegisterInputBlueprintNodes(all);
    Blueprint::Nodes::RegisterCharacterBlueprintNodes(all);
    Blueprint::Nodes::RegisterRenderingBlueprintNodes(all);
    Blueprint::Nodes::RegisterAudioBlueprintNodes(all);
    Blueprint::Nodes::RegisterAnimationBlueprintNodes(all);
    Blueprint::Nodes::RegisterWorldBlueprintNodes(all);
    Blueprint::Nodes::RegisterGameBlueprintNodes(all);
    Blueprint::Nodes::RegisterAiBlueprintNodes(all);
    Blueprint::Nodes::RegisterUiBlueprintNodes(all);
    Blueprint::Nodes::RegisterGameplayBlueprintNodes(all);
    Blueprint::Nodes::RegisterMathBlueprintNodes(all);
    Blueprint::Nodes::RegisterVariablesBlueprintNodes(all);
    RegisterNetworkingCatalogueNodes(all);

    AssignScopes(all);
    return all;
}

} // namespace

const std::vector<BlueprintNodeType>& GetBlueprintNodeTypes() {
    static const std::vector<BlueprintNodeType> all = BuildCatalogue();
    return all;
}

const BlueprintNodeType* FindBlueprintNodeType(const std::string_view key) {
    static const std::unordered_map<std::string_view, const BlueprintNodeType*> byKey = [] {
        const auto& all = GetBlueprintNodeTypes();
        std::unordered_map<std::string_view, const BlueprintNodeType*> index;
        index.reserve(all.size());
        for (const BlueprintNodeType& type : all) {
            index.emplace(type.key, &type);
        }
        return index;
    }();
    const auto found = byKey.find(key);
    return found != byKey.end() ? found->second : nullptr;
}

} // namespace UVE::Asset
