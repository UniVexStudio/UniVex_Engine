// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "rect.h"
#include "vector2.h"

namespace UVE::Scene {

class IEntityManager;

enum class UIExtendedContainerKind : std::uint8_t {
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
struct UIExtendedContainerComponent final {
    UIExtendedContainerKind kind = UIExtendedContainerKind::Panel;
    Math::Rect bounds{Math::Vector2{0.0F, 0.0F}, Math::Vector2{300.0F, 200.0F}};
    float marginLeft = 8.0F;
    float marginTop = 8.0F;
    float marginRight = 8.0F;
    float marginBottom = 8.0F;
    float splitRatio = 0.5F;
    float aspectRatio = 16.0F / 9.0F;
    std::uint32_t activeTabIndex = 0U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIExtendedContainerComponent&) const = default;
};

struct UIExtendedContainerObjectDefinition final {
    static constexpr std::string_view defaultName = "UIExtendedContainer";
    UIExtendedContainerComponent container{};
};

void ApplyUIExtendedContainerObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const UIExtendedContainerObjectDefinition& value);

class UIExtendedContainer final {
public:
    UIExtendedContainer() = delete;

    /// Computes the inner content rectangle for `childSize` inside `container`.
    [[nodiscard]] static Math::Rect ResolveContentRect(
        const UIExtendedContainerComponent& container,
        const Math::Vector2& childSize) noexcept;
};

struct UIGraphConnection final {
    std::uint32_t fromNodeId = 0U;
    std::uint32_t fromPort = 0U;
    std::uint32_t toNodeId = 0U;
    std::uint32_t toPort = 0U;

    [[nodiscard]] bool operator==(const UIGraphConnection&) const = default;
};

struct UIGraphNodeSlot final {
    std::uint32_t nodeId = 0U;
    std::string title{"GraphNode"};
    Math::Vector2 positionOffset{0.0F, 0.0F};
    std::uint32_t inputPortCount = 1U;
    std::uint32_t outputPortCount = 1U;

    [[nodiscard]] bool operator==(const UIGraphNodeSlot&) const = default;
};

/// Visual node-graph editing canvas (`panOffset`, `zoom`, `nodes`, `connections`).
struct UIGraphEditComponent final {
    Math::Vector2 panOffset{0.0F, 0.0F};
    float zoom = 1.0F;
    std::vector<UIGraphNodeSlot> nodes{};
    std::vector<UIGraphConnection> connections{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const UIGraphEditComponent&) const = default;
};

struct UIGraphEditObjectDefinition final {
    static constexpr std::string_view defaultName = "UIGraphEdit";
    UIGraphEditComponent graphEdit{};
};

void ApplyUIGraphEditObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const UIGraphEditObjectDefinition& value);

class UIGraphEdit final {
public:
    UIGraphEdit() = delete;

    /// Connects `(fromNodeId, fromPort)` to `(toNodeId, toPort)` when both nodes exist and ports
    /// are in range, avoiding self-loops and duplicate edges.
    static bool TryConnectPorts(UIGraphEditComponent& graph, std::uint32_t fromNodeId,
                                   std::uint32_t fromPort, std::uint32_t toNodeId,
                                   std::uint32_t toPort);
};

} // namespace UVE::Scene
