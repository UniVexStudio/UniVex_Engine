// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_extended_containers_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ApplyUIExtendedContainerObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const UIExtendedContainerObjectDefinitionUVE& value) {
    UIExtendedContainerComponentUVE out = value.container;
    out.splitRatio =
        std::isfinite(out.splitRatio) ? std::clamp(out.splitRatio, 0.05F, 0.95F) : 0.5F;
    if (!std::isfinite(out.aspectRatio) || out.aspectRatio <= 0.0F) {
        out.aspectRatio = 16.0F / 9.0F;
    }
    entityManager.AddComponentUVE<UIExtendedContainerComponentUVE>(entity, out);
}

Math::RectUVE UIExtendedContainerUVE::ResolveContentRectUVE(
    const UIExtendedContainerComponentUVE& container,
    const Math::Vector2UVE& childSize) noexcept {
    const Math::RectUVE& b = container.bounds;
    switch (container.kind) {
    case UIExtendedContainerKindUVE::Margin: {
        const float w = std::max(0.0F, b.size.x - container.marginLeft - container.marginRight);
        const float h = std::max(0.0F, b.size.y - container.marginTop - container.marginBottom);
        return Math::RectUVE{
            Math::Vector2UVE{b.position.x + container.marginLeft,
                             b.position.y + container.marginTop},
            Math::Vector2UVE{w, h}};
    }
    case UIExtendedContainerKindUVE::Center: {
        const float cx = b.position.x + 0.5F * (b.size.x - childSize.x);
        const float cy = b.position.y + 0.5F * (b.size.y - childSize.y);
        return Math::RectUVE{Math::Vector2UVE{cx, cy}, childSize};
    }
    case UIExtendedContainerKindUVE::Split: {
        const float ratio = std::clamp(container.splitRatio, 0.05F, 0.95F);
        return Math::RectUVE{b.position, Math::Vector2UVE{b.size.x * ratio, b.size.y}};
    }
    case UIExtendedContainerKindUVE::AspectRatio: {
        const float ratio = (container.aspectRatio > 0.0F) ? container.aspectRatio : 1.0F;
        float fitW = b.size.x;
        float fitH = fitW / ratio;
        if (fitH > b.size.y) {
            fitH = b.size.y;
            fitW = fitH * ratio;
        }
        return Math::RectUVE{
            Math::Vector2UVE{b.position.x + 0.5F * (b.size.x - fitW),
                             b.position.y + 0.5F * (b.size.y - fitH)},
            Math::Vector2UVE{fitW, fitH}};
    }
    case UIExtendedContainerKindUVE::FlowWrap:
    case UIExtendedContainerKindUVE::Panel:
    case UIExtendedContainerKindUVE::Tab:
    case UIExtendedContainerKindUVE::SubViewport:
        return b;
    }
    return b;
}

void ApplyUIGraphEditObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const UIGraphEditObjectDefinitionUVE& value) {
    UIGraphEditComponentUVE out = value.graphEdit;
    if (!std::isfinite(out.zoom) || out.zoom <= 0.05F) {
        out.zoom = 1.0F;
    }
    entityManager.AddComponentUVE<UIGraphEditComponentUVE>(entity, std::move(out));
}

bool UIGraphEditUVE::TryConnectPortsUVE(UIGraphEditComponentUVE& graph,
                                        const std::uint32_t fromNodeId,
                                        const std::uint32_t fromPort,
                                        const std::uint32_t toNodeId,
                                        const std::uint32_t toPort) {
    if (fromNodeId == toNodeId) {
        return false;
    }
    const UIGraphNodeSlotUVE* src = nullptr;
    const UIGraphNodeSlotUVE* dst = nullptr;
    for (const UIGraphNodeSlotUVE& n : graph.nodes) {
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
    const UIGraphConnectionUVE edge{fromNodeId, fromPort, toNodeId, toPort};
    if (std::find(graph.connections.begin(), graph.connections.end(), edge) !=
        graph.connections.end()) {
        return false;
    }
    graph.connections.push_back(edge);
    return true;
}

} // namespace UVE::Scene
