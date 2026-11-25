// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_viewport_host.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "camera_preview_viewport.h"
#include "entity_viewport.h"
#include "level_viewport.h"
#include "montage_viewport.h"
#include "retarget_viewport.h"
#include "vector2.h"
#include "vfx_viewport.h"
#include "widget_preview_viewport.h"

namespace UVE::App::Viewports {

struct EditorViewportHost::Impl final {
    Impl(Editor::Editor& editorRef, Core::EngineCore& engineRef)
        : editor(editorRef), engine(engineRef) {}

    Editor::Editor& editor;
    Core::EngineCore& engine;
    std::unique_ptr<LevelViewport> level;
    std::unique_ptr<CameraPreviewViewport> cameraPreview;
    std::unique_ptr<EntityViewport> entity;
    std::unique_ptr<WidgetPreviewViewport> widgetPreview;
    std::unique_ptr<RetargetViewport> retarget;
    std::unique_ptr<MontageViewport> montage;
    std::unique_ptr<VfxViewport> vfx;
};

EditorViewportHost::EditorViewportHost(Editor::Editor& editor, Core::EngineCore& engine)
    : m_impl(std::make_unique<Impl>(editor, engine)) {}

EditorViewportHost::~EditorViewportHost() {
    if (m_impl != nullptr) {
        m_impl->editor.ClearEditorViewports();
    }
}

void EditorViewportHost::BindToEditor(Editor::Editor& editor) {
    Impl* const impl = m_impl.get();
    Editor::Editor::EditorViewports viewports{};

    viewports.level = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                             const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->level == nullptr || impl->level->Services() != &services) {
            impl->level.reset();
            impl->level = std::make_unique<LevelViewport>(impl->editor, impl->engine, services);
        }
        return impl->level->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.cameraPreview = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                                     const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->cameraPreview == nullptr || impl->cameraPreview->Services() != &services) {
            impl->cameraPreview.reset();
            impl->cameraPreview = std::make_unique<CameraPreviewViewport>(impl->editor, services);
        }
        return impl->cameraPreview->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.entity = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                              const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->entity == nullptr || impl->entity->Services() != &services) {
            impl->entity.reset();
            impl->entity = std::make_unique<EntityViewport>(impl->editor, services);
        }
        return impl->entity->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.widgetPreview = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                                     const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->widgetPreview == nullptr || impl->widgetPreview->Services() != &services) {
            impl->widgetPreview.reset();
            impl->widgetPreview = std::make_unique<WidgetPreviewViewport>(impl->editor, services);
        }
        return impl->widgetPreview->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.retarget = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                                const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->retarget == nullptr || impl->retarget->Services() != &services) {
            impl->retarget.reset();
            impl->retarget = std::make_unique<RetargetViewport>(impl->editor, services);
        }
        return impl->retarget->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.montage = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                               const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->montage == nullptr || impl->montage->Services() != &services) {
            impl->montage.reset();
            impl->montage = std::make_unique<MontageViewport>(impl->editor, services);
        }
        return impl->montage->Render(availableSize, outUsedSize, overlayState);
    };

    viewports.vfx = [impl](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                           const Editor::Editor::ViewportOverlayState& overlayState) -> std::uint64_t {
        Core::EngineServices& services = impl->editor.GetActiveServices();
        if (impl->vfx == nullptr || impl->vfx->Services() != &services) {
            impl->vfx.reset();
            impl->vfx = std::make_unique<VfxViewport>(impl->editor, services);
        }
        return impl->vfx->Render(availableSize, outUsedSize, overlayState);
    };

    editor.SetEditorViewports(std::move(viewports));
}

void EditorViewportHost::UnbindFromEditor(Editor::Editor& editor) {
    editor.ClearEditorViewports();
}

void EditorViewportHost::ForgetServices(const Core::EngineServices& services) {
    const auto forget = [&services](auto& viewport) {
        if (viewport != nullptr && viewport->Services() == &services) {
            viewport.reset();
        }
    };
    forget(m_impl->level);
    forget(m_impl->cameraPreview);
    forget(m_impl->entity);
    forget(m_impl->widgetPreview);
    forget(m_impl->retarget);
    forget(m_impl->montage);
    forget(m_impl->vfx);
}

} // namespace UVE::App::Viewports
