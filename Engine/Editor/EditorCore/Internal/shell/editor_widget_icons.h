// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_icons.h"
#include "entity.h"

#include <cstdint>
#include <optional>

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::Editor {

/// What a widget of a Widget Blueprint is, as the Palette, the Hierarchy and the Designer name it.
enum class WidgetKindUVE : std::uint8_t {
    Canvas,
    Text,
    Button,
    Image,
    ProgressBar,
    Slider,
    CheckBox,
    TextBox,
    ComboBox,
    VerticalBox,
    HorizontalBox,
    WrapBox,
    ScrollBox,
    Border,
    Spacer,
};

/// Its name ("Combo Box", "Vertical Box" ...).
[[nodiscard]] const char* WidgetKindLabelUVE(WidgetKindUVE kind) noexcept;

/// The widget an object is, read from its components: a Combo Box is a dropdown, a Border an image
/// drawn nine-sliced, a Spacer an image that draws nothing, a Wrap Box a layout that wraps.
[[nodiscard]] std::optional<WidgetKindUVE> ClassifyWidgetUVE(const Scene::IEntityManagerUVE& em, Scene::EntityUVE entity);

/// The engine's own icon of each widget, drawn in code at `size` (0xAARRGGBB, straight alpha) and kept.
[[nodiscard]] EditorIconsUVE::ImageUVE WidgetIconUVE(WidgetKindUVE kind, int size);

} // namespace UVE::Editor
