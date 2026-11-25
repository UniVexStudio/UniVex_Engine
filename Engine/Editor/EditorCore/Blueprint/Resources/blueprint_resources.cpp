// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_resources.h"

#include <array>

namespace UVE::Blueprint::Resources {
namespace {

constexpr std::array<BlueprintIconDescriptor, 12> kIcons{{
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

constexpr std::array<BlueprintTemplateDescriptor, 5> kTemplates{{
    {BlueprintTemplateKind::EmptyObject, "empty_object", "Empty Blueprint Class", "Object3D",
     "Minimal empty Blueprint graph for an Object3D."},
    {BlueprintTemplateKind::ActorTickAndReady, "actor_tick_ready", "Event Ready + Event Tick", "Object3D",
     "Starter graph with Event Ready and Event Tick entry nodes."},
    {BlueprintTemplateKind::InteractiveTrigger, "interactive_trigger", "Interactive Trigger", "Object3D",
     "Logs overlap enter and exit events with an exposed trigger counter variable."},
    {BlueprintTemplateKind::WidgetHudCounter, "widget_hud_counter", "HUD Counter Widget", "Canvas",
     "Widget Blueprint starter with a Score variable updated on Event Ready."},
    {BlueprintTemplateKind::NetworkRpcClient, "network_rpc_client", "Network RPC Sender", "Object3D",
     "Starter graph demonstrating an RPC call dispatched from Event Ready."},
}};

} // namespace

// ---------------------------------------------------------------------------
// Themes & Styles
// ---------------------------------------------------------------------------

BlueprintPinStyle BlueprintTheme::PinStyleFor(const Asset::BlueprintPinType pinType) const noexcept {
    using T = Asset::BlueprintPinType;
    const bool highContrast = (kind == BlueprintThemeKind::HighContrast);
    switch (pinType) {
        case T::Exec:
            return {highContrast ? BlueprintColorRgba{255, 255, 255, 255}
                                 : BlueprintColorRgba{235, 240, 248, 255},
                    BlueprintPinShape::Arrow, 6.0F};
        case T::Bool: return {{215, 58, 73, 255}, BlueprintPinShape::Circle, 5.5F};
        case T::Int: return {{45, 212, 191, 255}, BlueprintPinShape::Circle, 5.5F};
        case T::Float: return {{120, 224, 96, 255}, BlueprintPinShape::Circle, 5.5F};
        case T::Vector: return {{250, 196, 48, 255}, BlueprintPinShape::Diamond, 6.0F};
        case T::String: return {{236, 98, 192, 255}, BlueprintPinShape::Circle, 5.5F};
        case T::Object: return {{70, 165, 250, 255}, BlueprintPinShape::Circle, 5.5F};
        case T::ObjectArray: return {{70, 165, 250, 255}, BlueprintPinShape::Grid, 6.0F};
        case T::IntArray: return {{45, 212, 191, 255}, BlueprintPinShape::Grid, 6.0F};
        case T::FloatArray: return {{120, 224, 96, 255}, BlueprintPinShape::Grid, 6.0F};
        case T::StringArray: return {{236, 98, 192, 255}, BlueprintPinShape::Grid, 6.0F};
    }
    return {{200, 200, 200, 255}, BlueprintPinShape::Circle, 5.5F};
}

BlueprintNodeStyle BlueprintTheme::NodeStyleFor(const Asset::BlueprintNodeRole role) const noexcept {
    const BlueprintColorRgba body =
        kind == BlueprintThemeKind::LightStudio
            ? BlueprintColorRgba{244, 246, 250, 242}
            : (kind == BlueprintThemeKind::HighContrast ? BlueprintColorRgba{12, 14, 18, 255}
                                                           : BlueprintColorRgba{28, 32, 40, 240});
    const BlueprintColorRgba border =
        kind == BlueprintThemeKind::HighContrast ? BlueprintColorRgba{255, 255, 255, 255}
                                                    : BlueprintColorRgba{64, 72, 86, 255};
    BlueprintColorRgba header{58, 114, 184, 255};
    switch (role) {
        case Asset::BlueprintNodeRole::Event: header = {178, 46, 58, 255}; break;
        case Asset::BlueprintNodeRole::Pure: header = {46, 134, 92, 255}; break;
        case Asset::BlueprintNodeRole::GetVariable:
        case Asset::BlueprintNodeRole::SetVariable: header = {140, 96, 184, 255}; break;
        case Asset::BlueprintNodeRole::Branch:
        case Asset::BlueprintNodeRole::Sequence:
        case Asset::BlueprintNodeRole::ForLoop:
        case Asset::BlueprintNodeRole::While:
        case Asset::BlueprintNodeRole::ForEach:
        case Asset::BlueprintNodeRole::Gate:
        case Asset::BlueprintNodeRole::MultiGate:
        case Asset::BlueprintNodeRole::DoOnce:
        case Asset::BlueprintNodeRole::DoN:
        case Asset::BlueprintNodeRole::FlipFlop:
        case Asset::BlueprintNodeRole::SwitchInt:
        case Asset::BlueprintNodeRole::SwitchString: header = {138, 146, 162, 255}; break;
        default: header = {52, 110, 180, 255}; break;
    }
    return BlueprintNodeStyle{header, body, border, 6.0F, 26.0F, 170.0F};
}

BlueprintTheme GetBlueprintTheme(const BlueprintThemeKind kind) noexcept {
    switch (kind) {
        case BlueprintThemeKind::LightStudio:
            return {kind,
                    "Light Studio",
                    {235, 238, 244, 255},
                    {218, 223, 232, 255},
                    {196, 203, 216, 255},
                    {36, 124, 236, 255},
                    {48, 116, 198, 42}};
        case BlueprintThemeKind::HighContrast:
            return {kind,
                    "High Contrast",
                    {8, 10, 14, 255},
                    {36, 40, 48, 255},
                    {76, 84, 98, 255},
                    {255, 214, 10, 255},
                    {255, 214, 10, 64}};
        case BlueprintThemeKind::DarkStudio:
        default:
            return {BlueprintThemeKind::DarkStudio,
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

std::span<const BlueprintIconDescriptor> GetAllBlueprintIcons() noexcept { return kIcons; }

BlueprintIconDescriptor GetBlueprintIconForCategory(const std::string_view category) noexcept {
    if (category.starts_with("Events")) return kIcons[0];
    if (category.starts_with("Flow")) return kIcons[1];
    if (category.starts_with("Math")) return kIcons[2];
    if (category.starts_with("Variable")) return kIcons[3];
    if (category.starts_with("Transform")) return kIcons[4];
    if (category.starts_with("Physics") || category == "Character") return kIcons[5];
    if (category.starts_with("Audio")) return kIcons[6];
    if (category.starts_with("Animation")) return kIcons[7];
    if (category.starts_with("Networking")) return kIcons[8];
    if (category.starts_with("User Interface")) return kIcons[9];
    if (category.starts_with("Gameplay") || category == "World" || category == "Game") return kIcons[10];
    return kIcons[11];
}

BlueprintIconDescriptor GetBlueprintIconForPinType(const Asset::BlueprintPinType pinType) noexcept {
    using T = Asset::BlueprintPinType;
    switch (pinType) {
        case T::Exec: return kIcons[1];
        case T::Vector: return kIcons[4];
        case T::Bool:
        case T::Int:
        case T::Float: return kIcons[2];
        case T::Object:
        case T::ObjectArray: return kIcons[10];
        default: return kIcons[3];
    }
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

std::span<const BlueprintTemplateDescriptor> GetBlueprintTemplates() noexcept { return kTemplates; }

Asset::BlueprintAsset InstantiateBlueprintTemplate(const BlueprintTemplateKind kind,
                                                         const std::string_view parentKindOverride) {
    Asset::BlueprintAsset bp;
    const auto applyParent = [&](const std::string_view fallback) {
        bp.parentKind = parentKindOverride.empty() ? std::string(fallback) : std::string(parentKindOverride);
    };

    switch (kind) {
        case BlueprintTemplateKind::EmptyObject:
            applyParent("Object3D");
            break;

        case BlueprintTemplateKind::ActorTickAndReady:
            applyParent("Object3D");
            bp.nodes.push_back(Asset::BlueprintNode{1U, "event_ready", {}, 80.0F, 80.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNode{2U, "event_tick", {}, 80.0F, 240.0F, {}});
            bp.comments.push_back(Asset::BlueprintComment{"Lifecycle Events", 40.0F, 30.0F, 320.0F, 310.0F,
                                                             GetBlueprintTheme(BlueprintThemeKind::DarkStudio)
                                                                 .commentDefaultTint.ToPackedRgba()});
            break;

        case BlueprintTemplateKind::InteractiveTrigger:
            applyParent("Object3D");
            bp.variables.push_back(
                Asset::BlueprintVariable{"trigger_count", Asset::BlueprintPinType::Int, "0", true});
            bp.nodes.push_back(Asset::BlueprintNode{1U, "event_overlap_enter", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNode{2U, "print", {}, 360.0F, 100.0F, {"", "Overlap entered"}});
            bp.wires.push_back(Asset::BlueprintWire{1U, 0U, 2U, 0U});
            break;

        case BlueprintTemplateKind::WidgetHudCounter:
            applyParent("Canvas");
            bp.variables.push_back(Asset::BlueprintVariable{"score", Asset::BlueprintPinType::Int, "0", true});
            bp.nodes.push_back(Asset::BlueprintNode{1U, "event_ready", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNode{2U, "print", {}, 340.0F, 100.0F, {"", "HUD Ready"}});
            bp.wires.push_back(Asset::BlueprintWire{1U, 0U, 2U, 0U});
            break;

        case BlueprintTemplateKind::NetworkRpcClient:
            applyParent("Object3D");
            bp.nodes.push_back(Asset::BlueprintNode{1U, "event_ready", {}, 60.0F, 100.0F, {}});
            bp.nodes.push_back(Asset::BlueprintNode{2U, "net_rpc_begin", {}, 260.0F, 220.0F, {"Ping"}});
            bp.nodes.push_back(Asset::BlueprintNode{3U, "net_rpc_send", {}, 540.0F, 100.0F, {"", "0"}});
            bp.wires.push_back(Asset::BlueprintWire{1U, 0U, 3U, 0U});
            bp.wires.push_back(Asset::BlueprintWire{2U, 0U, 3U, 1U});
            break;
    }

    return bp;
}

} // namespace UVE::Blueprint::Resources
