// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "color.h"
#include "rect.h"
#include "vector2.h"

namespace UVE::Scene {

class IEntityManager;

enum class UIExtendedButtonKind : std::uint8_t {
    ToggleSwitch = 0,
    MenuButton = 1,
    HyperlinkButton = 2,
    TextureButton = 3,
    ColorPickerButton = 4,
};

struct UIExtendedButtonComponent final {
    UIExtendedButtonKind kind = UIExtendedButtonKind::ToggleSwitch;
    std::string label{"Button"};
    std::string uri{};
    Math::Color pickedColor{1.0F, 1.0F, 1.0F};
    bool toggledOn = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIExtendedButtonComponent&) const = default;
};

struct UIExtendedButtonObjectDefinition final {
    static constexpr std::string_view defaultName = "UIExtendedButton";
    UIExtendedButtonComponent button{};
};

void ApplyUIExtendedButtonObjectDefinition(IEntityManager& entityManager, Entity entity,
                                              const UIExtendedButtonObjectDefinition& value);

enum class UIDisplayWidgetKind : std::uint8_t {
    RichText = 0,
    MultilineTextEdit = 1,
    CodeEdit = 2,
    ColorRect = 3,
    PlainPanel = 4,
    ReferenceRect = 5,
    VideoStreamPlayer = 6,
};

struct UIDisplayWidgetComponent final {
    UIDisplayWidgetKind kind = UIDisplayWidgetKind::RichText;
    std::string content{};
    Math::Color fillColor{0.2F, 0.2F, 0.25F};
    float playbackPositionSeconds = 0.0F;
    bool playing = false;
    bool showLineNumbers = true;

    [[nodiscard]] bool operator==(const UIDisplayWidgetComponent&) const = default;
};

struct UIDisplayWidgetObjectDefinition final {
    static constexpr std::string_view defaultName = "UIDisplayWidget";
    UIDisplayWidgetComponent display{};
};

void ApplyUIDisplayWidgetObjectDefinition(IEntityManager& entityManager, Entity entity,
                                             const UIDisplayWidgetObjectDefinition& value);

enum class UIRangeControlKind : std::uint8_t {
    TextureProgressBar = 0,
    SpinBox = 1,
};

struct UIRangeControlComponent final {
    UIRangeControlKind kind = UIRangeControlKind::SpinBox;
    float minValue = 0.0F;
    float maxValue = 100.0F;
    float stepValue = 1.0F;
    float value = 0.0F;

    [[nodiscard]] bool operator==(const UIRangeControlComponent&) const = default;
};

struct UIRangeControlObjectDefinition final {
    static constexpr std::string_view defaultName = "UIRangeControl";
    UIRangeControlComponent range{};
};

void ApplyUIRangeControlObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const UIRangeControlObjectDefinition& value);

enum class UISelectionWidgetKind : std::uint8_t {
    ItemList = 0,
    TreeView = 1,
    TabBar = 2,
};

struct UISelectionWidgetComponent final {
    UISelectionWidgetKind kind = UISelectionWidgetKind::ItemList;
    std::vector<std::string> items{};
    std::int32_t selectedIndex = -1;

    [[nodiscard]] bool operator==(const UISelectionWidgetComponent&) const = default;
};

struct UISelectionWidgetObjectDefinition final {
    static constexpr std::string_view defaultName = "UISelectionWidget";
    UISelectionWidgetComponent selection{};
};

void ApplyUISelectionWidgetObjectDefinition(IEntityManager& entityManager, Entity entity,
                                               const UISelectionWidgetObjectDefinition& value);

enum class UIDialogWindowKind : std::uint8_t {
    SubWindow = 0,
    PopupPanel = 1,
    PopupMenu = 2,
    MenuBar = 3,
    AcceptDialog = 4,
    ConfirmationDialog = 5,
    FileDialog = 6,
};

struct UIDialogWindowComponent final {
    UIDialogWindowKind kind = UIDialogWindowKind::AcceptDialog;
    std::string title{"Dialog"};
    std::string selectedPath{};
    bool visible = false;
    bool confirmed = false;

    [[nodiscard]] bool operator==(const UIDialogWindowComponent&) const = default;
};

struct UIDialogWindowObjectDefinition final {
    static constexpr std::string_view defaultName = "UIDialogWindow";
    UIDialogWindowComponent dialog{};
};

void ApplyUIDialogWindowObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const UIDialogWindowObjectDefinition& value);

enum class UISpecialInputKind : std::uint8_t {
    ColorPicker = 0,
    VirtualJoystick = 1,
};

struct UISpecialInputComponent final {
    UISpecialInputKind kind = UISpecialInputKind::VirtualJoystick;
    Math::Color color{1.0F, 1.0F, 1.0F};
    Math::Vector2 joystickAxis{0.0F, 0.0F};
    float deadzone = 0.1F;

    [[nodiscard]] bool operator==(const UISpecialInputComponent&) const = default;
};

struct UISpecialInputObjectDefinition final {
    static constexpr std::string_view defaultName = "UISpecialInput";
    UISpecialInputComponent input{};
};

void ApplyUISpecialInputObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const UISpecialInputObjectDefinition& value);

/// Sanitizes and steps all extended UI controls (clamping `UIRangeControlComponent::value` to
/// `[minValue, maxValue]` on `stepValue` increments, advancing playing `VideoStreamPlayer`
/// widgets, and applying radial deadzones to `VirtualJoystick` axes).
std::size_t UpdateUIExtendedObjects(IEntityManager& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
