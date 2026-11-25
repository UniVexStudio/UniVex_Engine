// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "viewport_manager.h"

#include <algorithm>
#include <cmath>

#include "logging_macros.h"
#include "camera_component.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool IsPaneRectValid(const ViewportPane& pane) noexcept {
    return std::isfinite(pane.originX01) && std::isfinite(pane.originY01) && std::isfinite(pane.sizeX01) &&
           std::isfinite(pane.sizeY01) && pane.sizeX01 > 0.0F && pane.sizeY01 > 0.0F &&
           pane.originX01 >= 0.0F && pane.originY01 >= 0.0F && pane.originX01 + pane.sizeX01 <= 1.0F &&
           pane.originY01 + pane.sizeY01 <= 1.0F;
}

[[nodiscard]] bool HasLiveCamera(Scene::IEntityManager& entityManager,
                                    const Scene::Entity cameraEntity) {
    if (cameraEntity == Scene::kInvalidEntity || !entityManager.IsAlive(cameraEntity)) {
        return false;
    }
    if (!entityManager.HasComponent<Scene::CameraComponent>(cameraEntity)) {
        return false;
    }
    return Scene::IsCameraComponentValid(entityManager.GetComponent<Scene::CameraComponent>(cameraEntity));
}

} // namespace

void ViewportManager::AddPane(ViewportPane pane) {
    m_panes.push_back(pane);
}

void ViewportManager::RemovePane(const std::size_t index) {
    if (index >= m_panes.size()) {
        UVE_ERROR("ViewportManager: RemovePane index {} is out of range ({} panes)", index, m_panes.size());
        return;
    }
    m_panes.erase(m_panes.begin() + static_cast<std::ptrdiff_t>(index));
}

std::size_t ViewportManager::GetPaneCount() const noexcept {
    return m_panes.size();
}

const ViewportPane* ViewportManager::GetPane(const std::size_t index) const noexcept {
    if (index >= m_panes.size()) {
        return nullptr;
    }
    return &m_panes[index];
}

std::optional<std::size_t> ViewportManager::FindPaneAtNormalizedPosition(const float x01,
                                                                                const float y01) const noexcept {
    if (!std::isfinite(x01) || !std::isfinite(y01)) {
        return std::nullopt;
    }
    for (std::size_t reverseIndex = m_panes.size(); reverseIndex > 0U; --reverseIndex) {
        const std::size_t index = reverseIndex - 1U;
        const ViewportPane& pane = m_panes[index];
        if (x01 >= pane.originX01 && x01 <= pane.originX01 + pane.sizeX01 && y01 >= pane.originY01 &&
            y01 <= pane.originY01 + pane.sizeY01) {
            return index;
        }
    }
    return std::nullopt;
}

void ViewportManager::RenderAllPanes(IRenderer3D& renderer, Scene::IEntityManager& entityManager,
                                            const std::uint32_t windowWidth, const std::uint32_t windowHeight) const {
    if (windowWidth == 0U || windowHeight == 0U) {
        return;
    }
    for (const ViewportPane& pane : m_panes) {
        if (pane.uiOnly) {
            continue;
        }
        if (!IsPaneRectValid(pane)) {
            UVE_WARNING("ViewportManager: RenderAllPanes skipping a pane with an invalid normalized rect "
                        "({}, {}, {}x{})",
                        pane.originX01, pane.originY01, pane.sizeX01, pane.sizeY01);
            continue;
        }
        if (!HasLiveCamera(entityManager, pane.cameraEntity)) {
            UVE_WARNING("ViewportManager: RenderAllPanes skipping a pane with no live camera entity");
            continue;
        }

        const auto pixelX = static_cast<std::uint32_t>(pane.originX01 * static_cast<float>(windowWidth));
        const auto pixelYFromTop = static_cast<std::uint32_t>(pane.originY01 * static_cast<float>(windowHeight));
        const std::uint32_t pixelWidth =
            std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(pane.sizeX01 * static_cast<float>(windowWidth)));
        const std::uint32_t pixelHeight =
            std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(pane.sizeY01 * static_cast<float>(windowHeight)));
        // Clamp instead of letting a rounding-driven off-by-one overflow the window: e.g. a pane
        // at originX01=0.5, sizeX01=0.5 against an odd windowWidth can round pixelX + pixelWidth to
        // windowWidth + 1.
        const std::uint32_t clampedWidth = std::min(pixelWidth, windowWidth - std::min(pixelX, windowWidth));
        const std::uint32_t clampedHeight = std::min(pixelHeight, windowHeight - std::min(pixelYFromTop, windowHeight));
        if (clampedWidth == 0U || clampedHeight == 0U) {
            continue;
        }

        if (!renderer.ResizeTargets(clampedWidth, clampedHeight)) {
            UVE_WARNING("ViewportManager: RenderAllPanes could not resize the shared renderer target to "
                        "{}x{}; skipping this pane",
                        clampedWidth, clampedHeight);
            continue;
        }

        // ViewportRect/glViewport use OpenGL's bottom-left origin; ViewportPane's
        // originY01 is top-left (this engine's other screen-space UI convention) - flip here,
        // at the one boundary between the two conventions, rather than asking every pane author
        // to think in GL's coordinate space.
        const std::uint32_t pixelYFromBottom = windowHeight - pixelYFromTop - clampedHeight;
        renderer.RenderFrameToRegion(entityManager, pane.cameraEntity,
                                        ViewportRect{Math::Vector2i{static_cast<std::int32_t>(pixelX),
                                                                          static_cast<std::int32_t>(pixelYFromBottom)},
                                                        Math::Vector2i{static_cast<std::int32_t>(clampedWidth),
                                                                          static_cast<std::int32_t>(clampedHeight)}});
    }
}

} // namespace UVE::Render
