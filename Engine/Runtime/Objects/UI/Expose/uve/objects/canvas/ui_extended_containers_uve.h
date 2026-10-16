// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class UIExtendedContainerKindUVE : std::uint8_t {
    FlowWrap = 0,
    Panel = 1,
    Center = 2,
    Margin = 3,
    Split = 4,
    Tab = 5,
    AspectRatio = 6,
    SubViewport = 7,
};

/// Extended UI layout container (`FlowWrap`, `Panel`, `Center`, `Margin`, `Split`, `Tab`,
/// `AspectRatio`, `SubViewport`).
struct UIExtendedContainerComponentUVE final {
    UIExtendedContainerKindUVE kind = UIExtendedContainerKindUVE::Panel;
    Math::RectUVE bounds{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{300.0F, 200.0F}};
    float marginLeft = 8.0F;
    float marginTop = 8.0F;
    float marginRight = 8.0F;
    float marginBottom = 8.0F;
    float splitRatio = 0.5F;
    float aspectRatio = 16.0F / 9.0F;
    std::uint32_t activeTabIndex = 0U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIExtendedContainerComponentUVE&) const = default;
};

struct UIExtendedContainerObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIExtendedContainer";
    UIExtendedContainerComponentUVE container{};
};

void ApplyUIExtendedContainerObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const UIExtendedContainerObjectDefinitionUVE& value);

class UIExtendedContainerUVE final {
public:
    UIExtendedContainerUVE() = delete;

    /// Computes the inner content rectangle for `childSize` inside `container`.
    [[nodiscard]] static Math::RectUVE ResolveContentRectUVE(
        const UIExtendedContainerComponentUVE& container,
        const Math::Vector2UVE& childSize) noexcept;
};

struct UIGraphConnectionUVE final {
    std::uint32_t fromNodeId = 0U;
    std::uint32_t fromPort = 0U;
    std::uint32_t toNodeId = 0U;
    std::uint32_t toPort = 0U;

    [[nodiscard]] bool operator==(const UIGraphConnectionUVE&) const = default;
};

struct UIGraphNodeSlotUVE final {
    std::uint32_t nodeId = 0U;
    std::string title{"GraphNode"};
    Math::Vector2UVE positionOffset{0.0F, 0.0F};
    std::uint32_t inputPortCount = 1U;
    std::uint32_t outputPortCount = 1U;

    [[nodiscard]] bool operator==(const UIGraphNodeSlotUVE&) const = default;
};

/// Visual node-graph editing canvas (`panOffset`, `zoom`, `nodes`, `connections`).
struct UIGraphEditComponentUVE final {
    Math::Vector2UVE panOffset{0.0F, 0.0F};
    float zoom = 1.0F;
    std::vector<UIGraphNodeSlotUVE> nodes{};
    std::vector<UIGraphConnectionUVE> connections{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIGraphEditComponentUVE&) const = default;
};

struct UIGraphEditObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "UIGraphEdit";
    UIGraphEditComponentUVE graphEdit{};
};

void ApplyUIGraphEditObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const UIGraphEditObjectDefinitionUVE& value);

class UIGraphEditUVE final {
public:
    UIGraphEditUVE() = delete;

    /// Connects `(fromNodeId, fromPort)` to `(toNodeId, toPort)` when both nodes exist and ports
    /// are in range, avoiding self-loops and duplicate edges.
    static bool TryConnectPortsUVE(UIGraphEditComponentUVE& graph, std::uint32_t fromNodeId,
                                   std::uint32_t fromPort, std::uint32_t toNodeId,
                                   std::uint32_t toPort);
};

} // namespace UVE::Scene
