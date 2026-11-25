// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"
#include "blueprint_nodes.h"

namespace UVE::Blueprint::Resources {

// ---------------------------------------------------------------------------
// Colors, Styles & Themes
// ---------------------------------------------------------------------------

struct BlueprintColorRgba final {
    std::uint8_t r = 0U;
    std::uint8_t g = 0U;
    std::uint8_t b = 0U;
    std::uint8_t a = 255U;

    [[nodiscard]] constexpr std::uint32_t ToPackedRgba() const noexcept {
        return (static_cast<std::uint32_t>(r) << 24U) | (static_cast<std::uint32_t>(g) << 16U) |
               (static_cast<std::uint32_t>(b) << 8U) | static_cast<std::uint32_t>(a);
    }

    [[nodiscard]] bool operator==(const BlueprintColorRgba&) const = default;
};

enum class BlueprintPinShape : std::uint8_t {
    Arrow = 0,   ///< Execution pins
    Circle,      ///< Scalar / Object / String pins
    Diamond,     ///< Vector / Math struct pins
    Grid,        ///< Array / Collection pins
};

struct BlueprintPinStyle final {
    BlueprintColorRgba color;
    BlueprintPinShape shape = BlueprintPinShape::Circle;
    float radius = 5.5F;
};

struct BlueprintNodeStyle final {
    BlueprintColorRgba headerColor;
    BlueprintColorRgba bodyColor;
    BlueprintColorRgba borderColor;
    float cornerRadius = 6.0F;
    float headerHeight = 26.0F;
    float minWidth = 170.0F;
};

enum class BlueprintThemeKind : std::uint8_t {
    DarkStudio = 0,
    LightStudio,
    HighContrast,
};

struct BlueprintTheme final {
    BlueprintThemeKind kind = BlueprintThemeKind::DarkStudio;
    std::string_view name;
    BlueprintColorRgba canvasBackground;
    BlueprintColorRgba gridMinor;
    BlueprintColorRgba gridMajor;
    BlueprintColorRgba selectionBorder;
    BlueprintColorRgba commentDefaultTint;

    [[nodiscard]] BlueprintPinStyle PinStyleFor(Asset::BlueprintPinType pinType) const noexcept;
    [[nodiscard]] BlueprintNodeStyle NodeStyleFor(Asset::BlueprintNodeRole role) const noexcept;
};

[[nodiscard]] BlueprintTheme GetBlueprintTheme(BlueprintThemeKind kind) noexcept;

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

struct BlueprintIconDescriptor final {
    std::string_view id;
    std::string_view glyphTag;
    std::string_view category;
    std::string_view tooltip;
};

[[nodiscard]] std::span<const BlueprintIconDescriptor> GetAllBlueprintIcons() noexcept;
[[nodiscard]] BlueprintIconDescriptor GetBlueprintIconForCategory(std::string_view category) noexcept;
[[nodiscard]] BlueprintIconDescriptor GetBlueprintIconForPinType(Asset::BlueprintPinType pinType) noexcept;

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

enum class BlueprintTemplateKind : std::uint8_t {
    EmptyObject = 0,
    ActorTickAndReady,
    InteractiveTrigger,
    WidgetHudCounter,
    NetworkRpcClient,
};

struct BlueprintTemplateDescriptor final {
    BlueprintTemplateKind kind = BlueprintTemplateKind::EmptyObject;
    std::string_view id;
    std::string_view title;
    std::string_view defaultParentKind;
    std::string_view description;
};

[[nodiscard]] std::span<const BlueprintTemplateDescriptor> GetBlueprintTemplates() noexcept;

/// Instantiates a valid, ready-to-compile starter BlueprintAsset for `kind`.
[[nodiscard]] Asset::BlueprintAsset InstantiateBlueprintTemplate(BlueprintTemplateKind kind,
                                                                       std::string_view parentKindOverride = {});

} // namespace UVE::Blueprint::Resources
