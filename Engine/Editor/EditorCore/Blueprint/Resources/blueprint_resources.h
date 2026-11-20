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

struct BlueprintColorRgbaUVE final {
    std::uint8_t r = 0U;
    std::uint8_t g = 0U;
    std::uint8_t b = 0U;
    std::uint8_t a = 255U;

    [[nodiscard]] constexpr std::uint32_t ToPackedRgbaUVE() const noexcept {
        return (static_cast<std::uint32_t>(r) << 24U) | (static_cast<std::uint32_t>(g) << 16U) |
               (static_cast<std::uint32_t>(b) << 8U) | static_cast<std::uint32_t>(a);
    }

    [[nodiscard]] bool operator==(const BlueprintColorRgbaUVE&) const = default;
};

enum class BlueprintPinShapeUVE : std::uint8_t {
    Arrow = 0,   ///< Execution pins
    Circle,      ///< Scalar / Object / String pins
    Diamond,     ///< Vector / Math struct pins
    Grid,        ///< Array / Collection pins
};

struct BlueprintPinStyleUVE final {
    BlueprintColorRgbaUVE color;
    BlueprintPinShapeUVE shape = BlueprintPinShapeUVE::Circle;
    float radius = 5.5F;
};

struct BlueprintNodeStyleUVE final {
    BlueprintColorRgbaUVE headerColor;
    BlueprintColorRgbaUVE bodyColor;
    BlueprintColorRgbaUVE borderColor;
    float cornerRadius = 6.0F;
    float headerHeight = 26.0F;
    float minWidth = 170.0F;
};

enum class BlueprintThemeKindUVE : std::uint8_t {
    DarkStudio = 0,
    LightStudio,
    HighContrast,
};

struct BlueprintThemeUVE final {
    BlueprintThemeKindUVE kind = BlueprintThemeKindUVE::DarkStudio;
    std::string_view name;
    BlueprintColorRgbaUVE canvasBackground;
    BlueprintColorRgbaUVE gridMinor;
    BlueprintColorRgbaUVE gridMajor;
    BlueprintColorRgbaUVE selectionBorder;
    BlueprintColorRgbaUVE commentDefaultTint;

    [[nodiscard]] BlueprintPinStyleUVE PinStyleForUVE(Asset::BlueprintPinTypeUVE pinType) const noexcept;
    [[nodiscard]] BlueprintNodeStyleUVE NodeStyleForUVE(Asset::BlueprintNodeRoleUVE role) const noexcept;
};

[[nodiscard]] BlueprintThemeUVE GetBlueprintThemeUVE(BlueprintThemeKindUVE kind) noexcept;

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

struct BlueprintIconDescriptorUVE final {
    std::string_view id;
    std::string_view glyphTag;
    std::string_view category;
    std::string_view tooltip;
};

[[nodiscard]] std::span<const BlueprintIconDescriptorUVE> GetAllBlueprintIconsUVE() noexcept;
[[nodiscard]] BlueprintIconDescriptorUVE GetBlueprintIconForCategoryUVE(std::string_view category) noexcept;
[[nodiscard]] BlueprintIconDescriptorUVE GetBlueprintIconForPinTypeUVE(Asset::BlueprintPinTypeUVE pinType) noexcept;

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

enum class BlueprintTemplateKindUVE : std::uint8_t {
    EmptyObject = 0,
    ActorTickAndReady,
    InteractiveTrigger,
    WidgetHudCounter,
    NetworkRpcClient,
};

struct BlueprintTemplateDescriptorUVE final {
    BlueprintTemplateKindUVE kind = BlueprintTemplateKindUVE::EmptyObject;
    std::string_view id;
    std::string_view title;
    std::string_view defaultParentKind;
    std::string_view description;
};

[[nodiscard]] std::span<const BlueprintTemplateDescriptorUVE> GetBlueprintTemplatesUVE() noexcept;

/// Instantiates a valid, ready-to-compile starter BlueprintAssetUVE for `kind`.
[[nodiscard]] Asset::BlueprintAssetUVE InstantiateBlueprintTemplateUVE(BlueprintTemplateKindUVE kind,
                                                                       std::string_view parentKindOverride = {});

} // namespace UVE::Blueprint::Resources
