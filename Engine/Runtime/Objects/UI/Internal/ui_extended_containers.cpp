// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_extended_containers.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"

namespace UVE::Scene {

void ApplyUIExtendedContainerObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const UIExtendedContainerObjectDefinition& value) {
    UIExtendedContainerComponent out = value.container;
    out.splitRatio =
        std::isfinite(out.splitRatio) ? std::clamp(out.splitRatio, 0.05F, 0.95F) : 0.5F;
    if (!std::isfinite(out.aspectRatio) || out.aspectRatio <= 0.0F) {
        out.aspectRatio = 16.0F / 9.0F;
    }
    entityManager.AddComponent<UIExtendedContainerComponent>(entity, out);
}

Math::Rect UIExtendedContainer::ResolveContentRect(
    const UIExtendedContainerComponent& container,
    const Math::Vector2& childSize) noexcept {
    const Math::Rect& b = container.bounds;
    switch (container.kind) {
    case UIExtendedContainerKind::Margin: {
        const float w = std::max(0.0F, b.size.x - container.marginLeft - container.marginRight);
        const float h = std::max(0.0F, b.size.y - container.marginTop - container.marginBottom);
        return Math::Rect{
            Math::Vector2{b.position.x + container.marginLeft,
                             b.position.y + container.marginTop},
            Math::Vector2{w, h}};
    }
    case UIExtendedContainerKind::Center: {
        const float cx = b.position.x + 0.5F * (b.size.x - childSize.x);
        const float cy = b.position.y + 0.5F * (b.size.y - childSize.y);
        return Math::Rect{Math::Vector2{cx, cy}, childSize};
    }
    case UIExtendedContainerKind::Split: {
        const float ratio = std::clamp(container.splitRatio, 0.05F, 0.95F);
        return Math::Rect{b.position, Math::Vector2{b.size.x * ratio, b.size.y}};
    }
    case UIExtendedContainerKind::AspectRatio: {
        const float ratio = (container.aspectRatio > 0.0F) ? container.aspectRatio : 1.0F;
        float fitW = b.size.x;
        float fitH = fitW / ratio;
        if (fitH > b.size.y) {
            fitH = b.size.y;
            fitW = fitH * ratio;
        }
        return Math::Rect{
            Math::Vector2{b.position.x + 0.5F * (b.size.x - fitW),
                             b.position.y + 0.5F * (b.size.y - fitH)},
            Math::Vector2{fitW, fitH}};
    }
    case UIExtendedContainerKind::FlowWrap:
    case UIExtendedContainerKind::Panel:
    case UIExtendedContainerKind::Tab:
    case UIExtendedContainerKind::SubViewport:
        return b;
    }
    return b;
}

void ApplyUIGraphEditObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const UIGraphEditObjectDefinition& value) {
    UIGraphEditComponent out = value.graphEdit;
    if (!std::isfinite(out.zoom) || out.zoom <= 0.05F) {
        out.zoom = 1.0F;
    }
    entityManager.AddComponent<UIGraphEditComponent>(entity, std::move(out));
}

bool UIGraphEdit::TryConnectPorts(UIGraphEditComponent& graph,
                                        const std::uint32_t fromNodeId,
                                        const std::uint32_t fromPort,
                                        const std::uint32_t toNodeId,
                                        const std::uint32_t toPort) {
    if (fromNodeId == toNodeId) {
        return false;
    }
    const UIGraphNodeSlot* src = nullptr;
    const UIGraphNodeSlot* dst = nullptr;
    for (const UIGraphNodeSlot& n : graph.nodes) {
        if (n.nodeId == fromNodeId) {
            src = &n;
        }
        if (n.nodeId == toNodeId) {
            dst = &n;
        }
    }
    if (src == nullptr || dst == nullptr || fromPort >= src->outputPortCount ||
        toPort >= dst->inputPortCount) {
        return false;
    }
    const UIGraphConnection edge{fromNodeId, fromPort, toNodeId, toPort};
    if (std::find(graph.connections.begin(), graph.connections.end(), edge) !=
        graph.connections.end()) {
        return false;
    }
    graph.connections.push_back(edge);
    return true;
}

} // namespace UVE::Scene
