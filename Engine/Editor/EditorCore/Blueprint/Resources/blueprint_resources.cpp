// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_resources.h"

#include <array>

namespace UVE::Blueprint::Resources {
namespace {

constexpr std::array<BlueprintIconDescriptorUVE, 12> kIconsUVE{{
    {"bp_icon_event", "EVT", "Events", "Event entry point"},
    {"bp_icon_flow", "FLW", "Flow Control", "Control flow branching and loops"},
    {"bp_icon_math", "MTH", "Math", "Math and vector operations"},
    {"bp_icon_variable", "VAR", "Variables", "Blueprint member variable"},
    {"bp_icon_transform", "TRN", "Transform", "Spatial position and rotation"},
    {"bp_icon_physics", "PHY", "Physics", "Physics and collision queries"},
    {"bp_icon_audio", "AUD", "Audio", "Audio playback and mixing"},
    {"bp_icon_animation", "ANM", "Animation", "Animation and montage playback"},
    {"bp_icon_networking", "NET", "Networking", "RPC, Lobby, and Relay bindings"},
    {"bp_icon_ui", "UI", "User Interface", "Widget Blueprint UI controls"},
    {"bp_icon_gameplay", "GMP", "Gameplay", "Gameplay attributes, tags, and world"},
    {"bp_icon_utility", "UTL", "Utilities", "General utilities and diagnostics"},
}};

constexpr std::array<BlueprintTemplateDescriptorUVE, 5> kTemplatesUVE{{
    {BlueprintTemplateKindUVE::EmptyObject, "empty_object", "Empty Blueprint Class", "Object3D",
     "Minimal empty Blueprint graph for an Object3D."},
    {BlueprintTemplateKindUVE::ActorTickAndReady, "actor_tick_ready", "Event Ready + Event Tick", "Object3D",
     "Starter graph with Event Ready and Event Tick entry nodes."},
    {BlueprintTemplateKindUVE::InteractiveTrigger, "interactive_trigger", "Interactive Trigger", "Object3D",
     "Logs overlap enter and exit events with an exposed trigger counter variable."},
    {BlueprintTemplateKindUVE::WidgetHudCounter, "widget_hud_counter", "HUD Counter Widget", "Canvas",
     "Widget Blueprint starter with a Score variable updated on Event Ready."},
    {BlueprintTemplateKindUVE::NetworkRpcClient, "network_rpc_client", "Network RPC Sender", "Object3D",
     "Starter graph demonstrating an RPC call dispatched from Event Ready."},
}};

} // namespace

// ---------------------------------------------------------------------------
// Themes & Styles
// ---------------------------------------------------------------------------

BlueprintPinStyleUVE BlueprintThemeUVE::PinStyleForUVE(const Asset::BlueprintPinTypeUVE pinType) const noexcept {
    using T = Asset::BlueprintPinTypeUVE;
    const bool highContrast = (kind == BlueprintThemeKindUVE::HighContrast);
    switch (pinType) {
        case T::Exec:
            return {highContrast ? BlueprintColorRgbaUVE{255, 255, 255, 255}
                                 : BlueprintColorRgbaUVE{235, 240, 248, 255},
                    BlueprintPinShapeUVE::Arrow, 6.0F};
        case T::Bool: return {{215, 58, 73, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
        case T::Int: return {{45, 212, 191, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
        case T::Float: return {{120, 224, 96, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
        case T::Vector: return {{250, 196, 48, 255}, BlueprintPinShapeUVE::Diamond, 6.0F};
        case T::String: return {{236, 98, 192, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
        case T::Object: return {{70, 165, 250, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
        case T::ObjectArray: return {{70, 165, 250, 255}, BlueprintPinShapeUVE::Grid, 6.0F};
        case T::IntArray: return {{45, 212, 191, 255}, BlueprintPinShapeUVE::Grid, 6.0F};
        case T::FloatArray: return {{120, 224, 96, 255}, BlueprintPinShapeUVE::Grid, 6.0F};
        case T::StringArray: return {{236, 98, 192, 255}, BlueprintPinShapeUVE::Grid, 6.0F};
    }
    return {{200, 200, 200, 255}, BlueprintPinShapeUVE::Circle, 5.5F};
}

BlueprintNodeStyleUVE BlueprintThemeUVE::NodeStyleForUVE(const Asset::BlueprintNodeRoleUVE role) const noexcept {
    const BlueprintColorRgbaUVE body =
        kind == BlueprintThemeKindUVE::LightStudio
            ? BlueprintColorRgbaUVE{244, 246, 250, 242}
            : (kind == BlueprintThemeKindUVE::HighContrast ? BlueprintColorRgbaUVE{12, 14, 18, 255}
                                                           : BlueprintColorRgbaUVE{28, 32, 40, 240});
    const BlueprintColorRgbaUVE border =
        kind == BlueprintThemeKindUVE::HighContrast ? BlueprintColorRgbaUVE{255, 255, 255, 255}
                                                    : BlueprintColorRgbaUVE{64, 72, 86, 255};
    BlueprintColorRgbaUVE header{58, 114, 184, 255};
    switch (role) {
        case Asset::BlueprintNodeRoleUVE::Event: header = {178, 46, 58, 255}; break;
        case Asset::BlueprintNodeRoleUVE::Pure: header = {46, 134, 92, 255}; break;
        case Asset::BlueprintNodeRoleUVE::GetVariable:
        case Asset::BlueprintNodeRoleUVE::SetVariable: header = {140, 96, 184, 255}; break;
        case Asset::BlueprintNodeRoleUVE::Branch:
        case Asset::BlueprintNodeRoleUVE::Sequence:
        case Asset::BlueprintNodeRoleUVE::ForLoop:
        case Asset::BlueprintNodeRoleUVE::While:
        case Asset::BlueprintNodeRoleUVE::ForEach:
        case Asset::BlueprintNodeRoleUVE::Gate:
        case Asset::BlueprintNodeRoleUVE::MultiGate:
        case Asset::BlueprintNodeRoleUVE::DoOnce:
        case Asset::BlueprintNodeRoleUVE::DoN:
        case Asset::BlueprintNodeRoleUVE::FlipFlop:
        case Asset::BlueprintNodeRoleUVE::SwitchInt:
        case Asset::BlueprintNodeRoleUVE::SwitchString: header = {138, 146, 162, 255}; break;
        default: header = {52, 110, 180, 255}; break;
    }
    return BlueprintNodeStyleUVE{header, body, border, 6.0F, 26.0F, 170.0F};
}

BlueprintThemeUVE GetBlueprintThemeUVE(const BlueprintThemeKindUVE kind) noexcept {
    switch (kind) {
        case BlueprintThemeKindUVE::LightStudio:
            return {kind,
                    "Light Studio",
                    {235, 238, 244, 255},
                    {218, 223, 232, 255},
                    {196, 203, 216, 255},
                    {36, 124, 236, 255},
                    {48, 116, 198, 42}};
        case BlueprintThemeKindUVE::HighContrast:
            return {kind,
                    "High Contrast",
                    {8, 10, 14, 255},
                    {36, 40, 48, 255},
                    {76, 84, 98, 255},
                    {255, 214, 10, 255},
                    {255, 214, 10, 64}};
        case BlueprintThemeKindUVE::DarkStudio:
        default:
            return {BlueprintThemeKindUVE::DarkStudio,
                    "Dark Studio",
                    {20, 23, 29, 255},
                    {31, 35, 44, 255},
                    {45, 51, 64, 255},
                    {240, 168, 48, 255},
                    {30, 80, 130, 45}};
    }
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

std::span<const BlueprintIconDescriptorUVE> GetAllBlueprintIconsUVE() noexcept { return kIconsUVE; }

BlueprintIconDescriptorUVE GetBlueprintIconForCategoryUVE(const std::string_view category) noexcept {
    if (category.starts_with("Events")) return kIconsUVE[0];
    if (category.starts_with("Flow")) return kIconsUVE[1];
    if (category.starts_with("Math")) return kIconsUVE[2];
    if (category.starts_with("Variable")) return kIconsUVE[3];
    if (category.starts_with("Transform")) return kIconsUVE[4];
    if (category.starts_with("Physics") || category == "Character") return kIconsUVE[5];
    if (category.starts_with("Audio")) return kIconsUVE[6];
    if (category.starts_with("Animation")) return kIconsUVE[7];
    if (category.starts_with("Networking")) return kIconsUVE[8];
    if (category.starts_with("User Interface")) return kIconsUVE[9];
    if (category.starts_with("Gameplay") || category == "World" || category == "Game") return kIconsUVE[10];
    return kIconsUVE[11];
}

BlueprintIconDescriptorUVE GetBlueprintIconForPinTypeUVE(const Asset::BlueprintPinTypeUVE pinType) noexcept {
    using T = Asset::BlueprintPinTypeUVE;
    switch (pinType) {
        case T::Exec: return kIconsUVE[1];
        case T::Vector: return kIconsUVE[4];
        case T::Bool:
        case T::Int:
        case T::Float: return kIconsUVE[2];
        case T::Object:
        case T::ObjectArray: return kIconsUVE[10];
        default: return kIconsUVE[3];
    }
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

std::span<const BlueprintTemplateDescriptorUVE> GetBlueprintTemplatesUVE() noexcept { return kTemplatesUVE; }

Asset::BlueprintAssetUVE InstantiateBlueprintTemplateUVE(const BlueprintTemplateKindUVE kind,
                                                         const std::string_view parentKindOverride) {
    Asset::BlueprintAssetUVE bp;
    const auto applyParent = [&](const std::string_view fallback) {
        bp.parentKind = parentKindOverride.empty() ? std::string(fallback) : std::string(parentKindOverride);
    };

    switch (kind) {
        case BlueprintTemplateKindUVE::EmptyObject:
            applyParent("Object3D");
            break;

        case BlueprintTemplateKindUVE::ActorTickAndReady:
            applyParent("Object3D");
            bp.nodes.push_back(Asset::BlueprintNodeUVE{1U, "event_ready", {}, 80.0F, 80.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{2U, "event_tick", {}, 80.0F, 240.0F, {}});
            bp.comments.push_back(Asset::BlueprintCommentUVE{"Lifecycle Events", 40.0F, 30.0F, 320.0F, 310.0F,
                                                             GetBlueprintThemeUVE(BlueprintThemeKindUVE::DarkStudio)
                                                                 .commentDefaultTint.ToPackedRgbaUVE()});
            break;

        case BlueprintTemplateKindUVE::InteractiveTrigger:
            applyParent("Object3D");
            bp.variables.push_back(
                Asset::BlueprintVariableUVE{"trigger_count", Asset::BlueprintPinTypeUVE::Int, "0", true});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{1U, "event_overlap_enter", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{2U, "print", {}, 360.0F, 100.0F, {"", "Overlap entered"}});
            bp.wires.push_back(Asset::BlueprintWireUVE{1U, 0U, 2U, 0U});
            break;

        case BlueprintTemplateKindUVE::WidgetHudCounter:
            applyParent("Canvas");
            bp.variables.push_back(Asset::BlueprintVariableUVE{"score", Asset::BlueprintPinTypeUVE::Int, "0", true});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{1U, "event_ready", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{2U, "print", {}, 340.0F, 100.0F, {"", "HUD Ready"}});
            bp.wires.push_back(Asset::BlueprintWireUVE{1U, 0U, 2U, 0U});
            break;

        case BlueprintTemplateKindUVE::NetworkRpcClient:
            applyParent("Object3D");
            bp.nodes.push_back(Asset::BlueprintNodeUVE{1U, "event_ready", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{2U, "net_rpc_begin", {}, 260.0F, 220.0F, {"Ping"}});
            bp.nodes.push_back(Asset::BlueprintNodeUVE{3U, "net_rpc_send", {}, 540.0F, 100.0F, {"", "0"}});
            bp.wires.push_back(Asset::BlueprintWireUVE{1U, 0U, 3U, 0U});
            bp.wires.push_back(Asset::BlueprintWireUVE{2U, 0U, 3U, 1U});
            break;
    }

    return bp;
}

} // namespace UVE::Blueprint::Resources
