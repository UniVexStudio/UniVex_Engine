// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class UIExtendedButtonKindUVE : std::uint8_t {
    ToggleSwitch = 0,
    MenuButton = 1,
    HyperlinkButton = 2,
    TextureButton = 3,
    ColorPickerButton = 4,
};

struct UIExtendedButtonComponentUVE final {
    UIExtendedButtonKindUVE kind = UIExtendedButtonKindUVE::ToggleSwitch;
    std::string label{"Button"};
    std::string uri{};
    Math::ColorUVE pickedColor{1.0F, 1.0F, 1.0F};
    bool toggledOn = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIExtendedButtonComponentUVE&) const = default;
};

struct UIExtendedButtonObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIExtendedButton";
    UIExtendedButtonComponentUVE button{};
};

void ApplyUIExtendedButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const UIExtendedButtonObjectDefinitionUVE& value);

enum class UIDisplayWidgetKindUVE : std::uint8_t {
    RichText = 0,
    MultilineTextEdit = 1,
    CodeEdit = 2,
    ColorRect = 3,
    PlainPanel = 4,
    ReferenceRect = 5,
    VideoStreamPlayer = 6,
};

struct UIDisplayWidgetComponentUVE final {
    UIDisplayWidgetKindUVE kind = UIDisplayWidgetKindUVE::RichText;
    std::string content{};
    Math::ColorUVE fillColor{0.2F, 0.2F, 0.25F};
    float playbackPositionSeconds = 0.0F;
    bool playing = false;
    bool showLineNumbers = true;

    [[nodiscard]] bool operator==(const UIDisplayWidgetComponentUVE&) const = default;
};

struct UIDisplayWidgetObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIDisplayWidget";
    UIDisplayWidgetComponentUVE display{};
};

void ApplyUIDisplayWidgetObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const UIDisplayWidgetObjectDefinitionUVE& value);

enum class UIRangeControlKindUVE : std::uint8_t {
    TextureProgressBar = 0,
    SpinBox = 1,
};

struct UIRangeControlComponentUVE final {
    UIRangeControlKindUVE kind = UIRangeControlKindUVE::SpinBox;
    float minValue = 0.0F;
    float maxValue = 100.0F;
    float stepValue = 1.0F;
    float value = 0.0F;

    [[nodiscard]] bool operator==(const UIRangeControlComponentUVE&) const = default;
};

struct UIRangeControlObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIRangeControl";
    UIRangeControlComponentUVE range{};
};

void ApplyUIRangeControlObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const UIRangeControlObjectDefinitionUVE& value);

enum class UISelectionWidgetKindUVE : std::uint8_t {
    ItemList = 0,
    TreeView = 1,
    TabBar = 2,
};

struct UISelectionWidgetComponentUVE final {
    UISelectionWidgetKindUVE kind = UISelectionWidgetKindUVE::ItemList;
    std::vector<std::string> items{};
    std::int32_t selectedIndex = -1;

    [[nodiscard]] bool operator==(const UISelectionWidgetComponentUVE&) const = default;
};

struct UISelectionWidgetObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UISelectionWidget";
    UISelectionWidgetComponentUVE selection{};
};

void ApplyUISelectionWidgetObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const UISelectionWidgetObjectDefinitionUVE& value);

enum class UIDialogWindowKindUVE : std::uint8_t {
    SubWindow = 0,
    PopupPanel = 1,
    PopupMenu = 2,
    MenuBar = 3,
    AcceptDialog = 4,
    ConfirmationDialog = 5,
    FileDialog = 6,
};

struct UIDialogWindowComponentUVE final {
    UIDialogWindowKindUVE kind = UIDialogWindowKindUVE::AcceptDialog;
    std::string title{"Dialog"};
    std::string selectedPath{};
    bool visible = false;
    bool confirmed = false;

    [[nodiscard]] bool operator==(const UIDialogWindowComponentUVE&) const = default;
};

struct UIDialogWindowObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIDialogWindow";
    UIDialogWindowComponentUVE dialog{};
};

void ApplyUIDialogWindowObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const UIDialogWindowObjectDefinitionUVE& value);

enum class UISpecialInputKindUVE : std::uint8_t {
    ColorPicker = 0,
    VirtualJoystick = 1,
};

struct UISpecialInputComponentUVE final {
    UISpecialInputKindUVE kind = UISpecialInputKindUVE::VirtualJoystick;
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    Math::Vector2UVE joystickAxis{0.0F, 0.0F};
    float deadzone = 0.1F;

    [[nodiscard]] bool operator==(const UISpecialInputComponentUVE&) const = default;
};

struct UISpecialInputObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UISpecialInput";
    UISpecialInputComponentUVE input{};
};

void ApplyUISpecialInputObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const UISpecialInputObjectDefinitionUVE& value);

/// Sanitizes and steps all extended UI controls (clamping `UIRangeControlComponentUVE::value` to
/// `[minValue, maxValue]` on `stepValue` increments, advancing playing `VideoStreamPlayer`
/// widgets, and applying radial deadzones to `VirtualJoystick` axes).
std::size_t UpdateUIExtendedObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
